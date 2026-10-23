#include "app.h"

#include "../../th/motif.h"
#include "../../tw/grid_widget.h"
#include "../../tw/surface.h"
#include "core/settings.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <Xm/CascadeB.h>
#include <Xm/FileSB.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/MessageB.h>
#include <Xm/MwmUtil.h>
#include <Xm/Protocols.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/ScrollBar.h>
#include <Xm/SelectioB.h>
#include <Xm/Separator.h>
#include <Xm/TextF.h>
#include <Xm/ToggleB.h>

namespace ttrcode {

namespace {

App* g_app;

/* Build output: directories named build or build-something, which the
 * tree leaves out (with names starting with '.'). */
bool build_dir(const char* name)
{
    return strncmp(name, "build", 5) == 0 && (name[5] == 0 || name[5] == '-');
}

char* dup_str(const char* s)
{
    char* d = (char*)malloc(strlen(s) + 1);
    if (d)
        strcpy(d, s);
    return d;
}

const char* base_name(const char* p)
{
    const char* s = strrchr(p, '/');
    return s && s[1] ? s + 1 : p;
}

/* Text from a key: UTF-8, or Latin-1 in a locale without UTF-8. */
int key_text(const char* in, int n, char* out, int cap)
{
    bool ascii_or_utf8 = true;
    for (int i = 0; i < n && ascii_or_utf8;) {
        unsigned char c = (unsigned char)in[i];
        int len = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3
                : (c & 0xf8) == 0xf0 ? 4 : 0;
        if (!len || i + len > n)
            ascii_or_utf8 = false;
        for (int k = 1; ascii_or_utf8 && k < len; k++)
            if (((unsigned char)in[i + k] & 0xc0) != 0x80)
                ascii_or_utf8 = false;
        i += len ? len : 1;
    }
    int o = 0;
    for (int i = 0; i < n && o + 2 < cap; i++) {
        unsigned char c = (unsigned char)in[i];
        if (ascii_or_utf8 || c < 0x80) {
            out[o++] = (char)c;
        } else {
            out[o++] = (char)(0xc0 | (c >> 6));
            out[o++] = (char)(0x80 | (c & 0x3f));
        }
    }
    return o;
}

/* ---- trampolines ---- */

void menu_cb(Widget, XtPointer data, XtPointer)
{
    g_app->command((int)(long)data);
}

void input_cb(Widget, XtPointer, XtPointer call)
{
    g_app->editor_key((TwGridCallbackStruct*)call);
}

void pointer_cb(Widget, XtPointer, XtPointer call)
{
    g_app->editor_pointer((TwGridCallbackStruct*)call);
}

void resize_cb(Widget, XtPointer, XtPointer call)
{
    g_app->editor_resize((TwGridCallbackStruct*)call);
}

void focus_cb(Widget, XtPointer, XtPointer call)
{
    g_app->editor_focus(((TwGridCallbackStruct*)call)->reason == TW_CR_FOCUS_IN);
}

void scroll_cb(Widget, XtPointer, XtPointer call)
{
    g_app->scrolled(((XmScrollBarCallbackStruct*)call)->value);
}

void tab_cb(Widget w, XtPointer, XtPointer call)
{
    g_app->tab_chosen(w, ((XmToggleButtonCallbackStruct*)call)->set != 0);
}

void tab_close_cb(Widget w, XtPointer, XtPointer)
{
    g_app->tab_close(w);
}

/* The middle button on a tab closes it. */
void tab_button(Widget w, XtPointer, XEvent* ev, Boolean*)
{
    if (ev->type == ButtonRelease && ev->xbutton.button == 2)
        g_app->tab_close(w);
}

/* A small x for the tabs' close buttons, 8 x 7. */
const unsigned char k_close_bits[] = { 0xc3, 0x66, 0x3c, 0x18, 0x3c, 0x66, 0xc3 };

void tree_cb(Widget, XtPointer, XtPointer call)
{
    g_app->tree_activate(((XmListCallbackStruct*)call)->item_position - 1);
}

void tree_key(Widget, XtPointer, XEvent* ev, Boolean* go_on)
{
    if (ev->type != KeyPress)
        return;
    KeySym ks = XLookupKeysym(&ev->xkey, 0);
    if (g_app->global_key(ks, ev->xkey.state))
        *go_on = False;
}

void scheme_cb(Widget, XtPointer data, XtPointer call)
{
    if (((XmToggleButtonCallbackStruct*)call)->set)
        g_app->scheme_chosen((int)(long)data);
}

void theme_cb(Widget, XtPointer data, XtPointer call)
{
    if (((XmToggleButtonCallbackStruct*)call)->set)
        g_app->theme_chosen((int)(long)data);
}

void restart_terminal_cb(XtPointer, XtIntervalId*)
{
    g_app->restart_terminal();
}

void sash_handler(Widget w, XtPointer, XEvent* ev, Boolean*)
{
    g_app->sash_event(w, ev);
}

void autoscroll_timeout(XtPointer, XtIntervalId*)
{
    g_app->autoscroll_step();
}

void close_window_cb(Widget, XtPointer, XtPointer)
{
    g_app->quit();
}

void shell_focus(Widget, XtPointer, XEvent* ev, Boolean*)
{
    if (ev->type == FocusIn)
        g_app->focus_changed_window();
}

Boolean convert_sel(Widget, Atom* sel, Atom* target, Atom* type, XtPointer* value,
                    unsigned long* length, int* format)
{
    return g_app->convert(*sel, *target, type, value, length, format);
}

void lose_sel(Widget, Atom* sel)
{
    g_app->lost(*sel);
}

void got_paste_cb(Widget w, XtPointer, Atom* sel, Atom* type, XtPointer value,
                  unsigned long* length, int* format)
{
    g_app->got_paste(w, *sel, *type, value, *length, *format, false);
}

void got_paste_string_cb(Widget w, XtPointer, Atom* sel, Atom* type, XtPointer value,
                         unsigned long* length, int* format)
{
    g_app->got_paste(w, *sel, *type, value, *length, *format, true);
}

/* Modal dialogs set the answer through these. */
int* g_answer;
void answer_cb(Widget, XtPointer data, XtPointer)
{
    *g_answer = (int)(long)data;
}

void destroy_dialog_cb(Widget w, XtPointer, XtPointer)
{
    XtDestroyWidget(XtParent(w));
}

void find_button_cb(Widget, XtPointer data, XtPointer)
{
    g_app->find_dialog_button((int)(long)data);
}

int compare_rows(const void* a, const void* b)
{
    const TreeRow* x = *(const TreeRow* const*)a;
    const TreeRow* y = *(const TreeRow* const*)b;
    if (x->dir != y->dir)
        return x->dir ? -1 : 1;
    return strcmp(x->name, y->name);
}

/* A dialog's standard child by its name: XmMessageBoxGetChild and its
 * kind are deprecated in Motif 2.3, the names are the same everywhere. */
Widget child(Widget dlg, const char* name)
{
    return XtNameToWidget(dlg, name);
}

Widget menu_item(Widget menu, const char* label, char mnemonic, const char* accel, int cmd)
{
    Widget w;
    if (accel) {
        XmString acc = XmStringCreateLocalized((char*)accel);
        w = XtVaCreateManagedWidget(label, xmPushButtonWidgetClass, menu, XmNmnemonic, mnemonic,
                                    XmNacceleratorText, acc, (char*)0);
        XmStringFree(acc);
    } else {
        w = XtVaCreateManagedWidget(label, xmPushButtonWidgetClass, menu, XmNmnemonic, mnemonic,
                                    (char*)0);
    }
    XtAddCallback(w, XmNactivateCallback, menu_cb, (XtPointer)(long)cmd);
    return w;
}

void separator(Widget menu)
{
    XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, menu, (char*)0);
}

} /* namespace */

/* ---- the window ---- */

App::App(Widget top, const Options& o)
    : top_(top), form_(0), bar_(0), body_(0), right_(0), tree_box_(0), tree_sash_(0),
      bottom_sash_(0), tree_(0), edit_box_(0),
      tabs_(0), grid_(0), scroll_(0), status_(0), bottom_slot_(0), ncascades_(0), term_(0),
      opt_(o), cur_(-1), untitled_(0), fullscreen_(false), workspace_on_(true), bottom_on_(true),
      was_workspace_(true), was_bottom_(true), tree_w_(o.tree_width), bottom_h_(o.bottom_height),
      drag_start_(-1), drag_size_(0), close_pixmap_(0), save_w_(0), save_h_(0), save_x_(0), save_y_(0),
      in_scroll_update_(false), selecting_(false), last_click_(0), click_count_(0),
      last_col_(-1), last_row_(-1), autoscroll_(0), autoscroll_id_(0), match_case_(false),
      find_dlg_(0), find_what_(0), replace_with_(0), case_toggle_(0), replace_row_(0),
      primary_(0), primary_len_(0), clipboard_(0), clipboard_len_(0), answer_(-1),
      scheme_items_(0), theme_items_(0)
{
    g_app = this;
    g_answer = &answer_;
    root_ = o.workspace;
    Display* dpy = XtDisplay(top);
    a_clipboard_ = XInternAtom(dpy, "CLIPBOARD", False);
    a_utf8_ = XInternAtom(dpy, "UTF8_STRING", False);
    a_targets_ = XInternAtom(dpy, "TARGETS", False);
    a_text_ = XInternAtom(dpy, "TEXT", False);
    src_.set_line_numbers(o.line_numbers);
    theme_name_ = o.ui_theme ? o.ui_theme : "";
    th::find_theme(theme_name_.empty() ? ttr_theme() : theme_name_.c_str(), &theme_);
    set_scheme(o.scheme);

    form_ = XtVaCreateManagedWidget("main", xmFormWidgetClass, top, (char*)0);
    build_menus();
    build_layout();
    tree_read(-1, root_.c_str(), 0);
    tree_fill();
    make_terminal();

    Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XmAddWMProtocolCallback(top, wm_delete, close_window_cb, 0);
    XtVaSetValues(top, XmNdeleteResponse, XmDO_NOTHING, (char*)0);
    XtAddEventHandler(top, FocusChangeMask, False, shell_focus, 0);
}

App::~App()
{
    delete term_;
    for (int i = 0; i < docs_.size(); i++)
        delete docs_[i];
    for (int i = 0; i < rows_.size(); i++) {
        free(rows_[i]->path);
        free(rows_[i]->name);
        delete rows_[i];
    }
    free(primary_);
    free(clipboard_);
}

void App::build_menus()
{
    bar_ = XmCreateMenuBar(form_, (char*)"menuBar", 0, 0);
    XtVaSetValues(bar_, XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, (char*)0);
    struct Title {
        const char* name;
        const char* label;
        char mnemonic;
    };
    static const Title titles[] = {
        { "fileMenu", "File", 'F' }, { "editMenu", "Edit", 'E' },
        { "searchMenu", "Search", 'S' }, { "viewMenu", "View", 'V' },
        { "windowMenu", "Window", 'W' }, { "helpMenu", "Help", 'H' }
    };
    Widget menus[6];
    for (int i = 0; i < 6; i++) {
        menus[i] = XmCreatePulldownMenu(bar_, (char*)titles[i].name, 0, 0);
        cascades_[i] = XtVaCreateManagedWidget(titles[i].label, xmCascadeButtonWidgetClass, bar_,
                                               XmNsubMenuId, menus[i], XmNmnemonic,
                                               titles[i].mnemonic, (char*)0);
        mnemonics_[i] = titles[i].mnemonic;
    }
    ncascades_ = 6;
    XtVaSetValues(bar_, XmNmenuHelpWidget, cascades_[5], (char*)0);

    Widget m = menus[0];
    menu_item(m, "New", 'N', "Ctrl+N", CMD_NEW);
    menu_item(m, "Open...", 'O', "Ctrl+O", CMD_OPEN);
    menu_item(m, "Open Workspace...", 'W', 0, CMD_OPEN_WORKSPACE);
    menu_item(m, "Close", 'C', "Ctrl+F4", CMD_CLOSE);
    separator(m);
    menu_item(m, "Save", 'S', "Ctrl+S", CMD_SAVE);
    menu_item(m, "Save As...", 'A', 0, CMD_SAVE_AS);
    menu_item(m, "Save All", 'l', 0, CMD_SAVE_ALL);
    separator(m);
    menu_item(m, "Exit", 'x', 0, CMD_EXIT);

    m = menus[1];
    menu_item(m, "Undo", 'U', "Ctrl+Z", CMD_UNDO);
    menu_item(m, "Redo", 'R', "Ctrl+Y", CMD_REDO);
    separator(m);
    menu_item(m, "Cut", 't', "Ctrl+X", CMD_CUT);
    menu_item(m, "Copy", 'C', "Ctrl+C", CMD_COPY);
    menu_item(m, "Paste", 'P', "Ctrl+V", CMD_PASTE);
    menu_item(m, "Delete", 'D', "Del", CMD_DELETE);
    separator(m);
    menu_item(m, "Select All", 'l', "Ctrl+A", CMD_SELECT_ALL);

    m = menus[2];
    menu_item(m, "Find...", 'F', "Ctrl+F", CMD_FIND);
    menu_item(m, "Find Next", 'N', "F3", CMD_FIND_NEXT);
    menu_item(m, "Find Previous", 'P', "Shift+F3", CMD_FIND_PREV);
    menu_item(m, "Replace...", 'R', "Ctrl+H", CMD_REPLACE);
    separator(m);
    menu_item(m, "Go To Line...", 'G', "Ctrl+G", CMD_GOTO);

    m = menus[3];
    menu_item(m, "Workspace", 'W', "Alt+0", CMD_VIEW_WORKSPACE);
    menu_item(m, "Terminal Pane", 'T', "Alt+2", CMD_VIEW_BOTTOM);
    menu_item(m, "Line Numbers", 'L', 0, CMD_VIEW_LINE_NUMBERS);
    separator(m);
    build_color_menus(m);
    separator(m);
    menu_item(m, "Full Screen", 'F', "F11", CMD_VIEW_FULLSCREEN);

    m = menus[4];
    menu_item(m, "Next Document", 'N', "Ctrl+Tab", CMD_NEXT_DOC);
    menu_item(m, "Previous Document", 'P', "Ctrl+Shift+Tab", CMD_PREV_DOC);
    menu_item(m, "Terminal", 'T', "Ctrl+`", CMD_TERMINAL);
    separator(m);
    menu_item(m, "Close", 'C', "Ctrl+F4", CMD_CLOSE);

    m = menus[5];
    menu_item(m, "About ttrcode", 'A', 0, CMD_ABOUT);
    XtManageChild(bar_);
}

/* The window is forms all the way down, so the editor takes whatever
 * the side panes leave: the workspace keeps its width and the bottom
 * pane its height when the window grows, as in VS6. The sashes between
 * them are separators that can be dragged. */
void App::build_layout()
{
    status_ = XtVaCreateManagedWidget("status", xmLabelWidgetClass, form_,
                                      XmNalignment, XmALIGNMENT_BEGINNING,
                                      XmNleftAttachment, XmATTACH_FORM,
                                      XmNrightAttachment, XmATTACH_FORM,
                                      XmNbottomAttachment, XmATTACH_FORM,
                                      XmNmarginWidth, 6, (char*)0);
    body_ = XtVaCreateWidget("body", xmFormWidgetClass, form_, (char*)0);

    /* The workspace. */
    tree_box_ = XtVaCreateWidget("workspace", xmFormWidgetClass, body_,
                                 XmNwidth, (Dimension)tree_w_,
                                 XmNresizePolicy, XmRESIZE_NONE, (char*)0);
    Widget title = XtVaCreateManagedWidget("Workspace", xmLabelWidgetClass, tree_box_,
                                           XmNalignment, XmALIGNMENT_BEGINNING,
                                           XmNtopAttachment, XmATTACH_FORM,
                                           XmNleftAttachment, XmATTACH_FORM,
                                           XmNrightAttachment, XmATTACH_FORM, (char*)0);
    Arg args[12];
    Cardinal n = 0;
    XtSetArg(args[n], XmNselectionPolicy, XmBROWSE_SELECT); n++;
    XtSetArg(args[n], XmNscrollBarDisplayPolicy, XmAS_NEEDED); n++;
    XtSetArg(args[n], XmNlistSizePolicy, XmCONSTANT); n++;
    tree_ = XmCreateScrolledList(tree_box_, (char*)"tree", args, n);
    XtVaSetValues(XtParent(tree_), XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, title,
                  XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
                  XmNbottomAttachment, XmATTACH_FORM, (char*)0);
    XtAddCallback(tree_, XmNdefaultActionCallback, tree_cb, 0);
    XtAddEventHandler(tree_, KeyPressMask, False, tree_key, 0);
    XtManageChild(tree_);
    XtManageChild(tree_box_);
    tree_sash_ = XtVaCreateManagedWidget("treeSash", xmSeparatorWidgetClass, body_,
                                         XmNorientation, XmVERTICAL,
                                         XmNseparatorType, XmSHADOW_ETCHED_OUT,
                                         XmNwidth, 7, XmNmargin, 0, (char*)0);
    XtAddEventHandler(tree_sash_, ButtonPressMask | ButtonReleaseMask | Button1MotionMask,
                      False, sash_handler, 0);

    /* Right of it: the editor over the bottom pane. */
    right_ = XtVaCreateManagedWidget("right", xmFormWidgetClass, body_, (char*)0);
    /* The slots' sizes are ours (the sashes'), not their children's: a
     * terminal pane being replaced must not shrink its slot to nothing. */
    bottom_slot_ = XtVaCreateManagedWidget("bottom", xmFormWidgetClass, right_,
                                           XmNheight, (Dimension)bottom_h_,
                                           XmNresizePolicy, XmRESIZE_NONE, (char*)0);
    bottom_sash_ = XtVaCreateManagedWidget("bottomSash", xmSeparatorWidgetClass, right_,
                                           XmNorientation, XmHORIZONTAL,
                                           XmNseparatorType, XmSHADOW_ETCHED_OUT,
                                           XmNheight, 7, XmNmargin, 0, (char*)0);
    XtAddEventHandler(bottom_sash_, ButtonPressMask | ButtonReleaseMask | Button1MotionMask,
                      False, sash_handler, 0);

    /* The editor: the tabs over the grid and its scroll bar. */
    edit_box_ = XtVaCreateWidget("editor", xmFormWidgetClass, right_, (char*)0);
    tabs_ = XtVaCreateManagedWidget("tabs", xmRowColumnWidgetClass, edit_box_,
                                    XmNorientation, XmHORIZONTAL,
                                    XmNpacking, XmPACK_TIGHT,
                                    XmNspacing, 0, XmNmarginHeight, 0, XmNmarginWidth, 0,
                                    XmNtopAttachment, XmATTACH_FORM,
                                    XmNleftAttachment, XmATTACH_FORM,
                                    XmNrightAttachment, XmATTACH_FORM, (char*)0);
    scroll_ = XtVaCreateManagedWidget("scrollBar", xmScrollBarWidgetClass, edit_box_,
                                      XmNorientation, XmVERTICAL,
                                      XmNtraversalOn, False,
                                      XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, tabs_,
                                      XmNrightAttachment, XmATTACH_FORM,
                                      XmNbottomAttachment, XmATTACH_FORM,
                                      XmNminimum, 0, XmNmaximum, 1, XmNsliderSize, 1, (char*)0);
    XtAddCallback(scroll_, XmNvalueChangedCallback, scroll_cb, 0);
    XtAddCallback(scroll_, XmNdragCallback, scroll_cb, 0);
    n = 0;
    XtSetArg(args[n], TwNfont, opt_.font); n++;
    if (opt_.fallback_fonts) {
        XtSetArg(args[n], TwNfallbackFonts, opt_.fallback_fonts); n++;
    }
    XtSetArg(args[n], TwNsource, (tw::Source*)&src_); n++;
    if (opt_.renderer && *opt_.renderer) {
        XtSetArg(args[n], TwNrenderer, opt_.renderer); n++;
    }
    XtSetArg(args[n], TwNcolumns, 100); n++;
    XtSetArg(args[n], TwNrows, 36); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget, tabs_); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNrightWidget, scroll_); n++;
    grid_ = TwCreateGrid(edit_box_, "grid", args, n);
    XtAddCallback(grid_, TwNinputCallback, input_cb, 0);
    XtAddCallback(grid_, TwNpointerCallback, pointer_cb, 0);
    XtAddCallback(grid_, TwNresizeCallback, resize_cb, 0);
    XtAddCallback(grid_, TwNfocusCallback, focus_cb, 0);
    XtManageChild(grid_);
    XtManageChild(edit_box_);
    layout();
    XtManageChild(body_);
    XtVaSetValues(form_, XmNinitialFocus, body_, (char*)0);
    XtVaSetValues(body_, XmNinitialFocus, right_, (char*)0);
    XtVaSetValues(right_, XmNinitialFocus, edit_box_, (char*)0);
    XtVaSetValues(edit_box_, XmNinitialFocus, grid_, (char*)0);
}

/* Attachments for what is shown: panes, sashes, and in full screen only
 * the editor's grid. */
void App::layout()
{
    bool bars = !fullscreen_;
    bool tree = workspace_on_ && !fullscreen_;
    bool bottom = bottom_on_ && !fullscreen_;
    if (bars) {
        XtManageChild(bar_);
        XtManageChild(status_);
        XtManageChild(tabs_);
    } else {
        XtUnmanageChild(bar_);
        XtUnmanageChild(status_);
        XtUnmanageChild(tabs_);
    }
    XtVaSetValues(body_, XmNtopAttachment, bars ? XmATTACH_WIDGET : XmATTACH_FORM,
                  XmNtopWidget, bar_,
                  XmNbottomAttachment, bars ? XmATTACH_WIDGET : XmATTACH_FORM,
                  XmNbottomWidget, status_,
                  XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, (char*)0);
    XtVaSetValues(scroll_, XmNtopAttachment, bars ? XmATTACH_WIDGET : XmATTACH_FORM,
                  XmNtopWidget, tabs_, (char*)0);
    XtVaSetValues(grid_, XmNtopAttachment, bars ? XmATTACH_WIDGET : XmATTACH_FORM,
                  XmNtopWidget, tabs_, (char*)0);

    if (tree) {
        XtVaSetValues(tree_box_, XmNwidth, (Dimension)tree_w_,
                      XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM,
                      XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_NONE,
                      (char*)0);
        XtVaSetValues(tree_sash_, XmNtopAttachment, XmATTACH_FORM,
                      XmNbottomAttachment, XmATTACH_FORM,
                      XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, tree_box_, (char*)0);
        XtManageChild(tree_box_);
        XtManageChild(tree_sash_);
    } else {
        XtUnmanageChild(tree_box_);
        XtUnmanageChild(tree_sash_);
    }
    XtVaSetValues(right_, XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM,
                  XmNleftAttachment, tree ? XmATTACH_WIDGET : XmATTACH_FORM,
                  XmNleftWidget, tree_sash_, XmNrightAttachment, XmATTACH_FORM, (char*)0);

    if (bottom) {
        XtVaSetValues(bottom_slot_, XmNheight, (Dimension)bottom_h_,
                      XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
                      XmNbottomAttachment, XmATTACH_FORM, XmNtopAttachment, XmATTACH_NONE,
                      (char*)0);
        XtVaSetValues(bottom_sash_, XmNleftAttachment, XmATTACH_FORM,
                      XmNrightAttachment, XmATTACH_FORM,
                      XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, bottom_slot_,
                      (char*)0);
        XtManageChild(bottom_slot_);
        XtManageChild(bottom_sash_);
    } else {
        XtUnmanageChild(bottom_slot_);
        XtUnmanageChild(bottom_sash_);
    }
    XtVaSetValues(edit_box_, XmNtopAttachment, XmATTACH_FORM,
                  XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
                  XmNbottomAttachment, bottom ? XmATTACH_WIDGET : XmATTACH_FORM,
                  XmNbottomWidget, bottom_sash_, (char*)0);
}

/* Dragging a sash: the workspace's width or the bottom pane's height
 * follows the pointer, within reason. */
void App::sash_event(Widget w, XEvent* ev)
{
    bool vertical = w == tree_sash_;
    int pos = vertical ? (ev->type == MotionNotify ? ev->xmotion.x_root : ev->xbutton.x_root)
                       : (ev->type == MotionNotify ? ev->xmotion.y_root : ev->xbutton.y_root);
    if (ev->type == ButtonPress && ev->xbutton.button == 1) {
        drag_start_ = pos;
        drag_size_ = vertical ? tree_w_ : bottom_h_;
        return;
    }
    if (ev->type != MotionNotify && ev->type != ButtonRelease)
        return;
    if (drag_start_ < 0)
        return;
    Dimension pw = 0, ph = 0;
    XtVaGetValues(vertical ? body_ : right_, XmNwidth, &pw, XmNheight, &ph, (char*)0);
    int limit = (vertical ? (int)pw : (int)ph) - 80;
    int size = vertical ? drag_size_ + (pos - drag_start_) : drag_size_ - (pos - drag_start_);
    if (size > limit)
        size = limit;
    if (size < 40)
        size = 40;
    if (vertical) {
        tree_w_ = size;
        XtVaSetValues(tree_box_, XmNwidth, (Dimension)size, (char*)0);
    } else {
        bottom_h_ = size;
        XtVaSetValues(bottom_slot_, XmNheight, (Dimension)size, (char*)0);
    }
    if (ev->type == ButtonRelease) {
        drag_start_ = -1;
        save_settings();
    }
}

void App::make_terminal()
{
    term_opt_.font = opt_.font;
    term_opt_.fallback_fonts = opt_.fallback_fonts;
    term_opt_.scrollback = 5000;
    term_opt_.cwd = root_.c_str();
    term_opt_.foreground = (uint32_t)scheme_.get(th::SC_FOREGROUND);
    term_opt_.background = (uint32_t)scheme_.get(th::SC_BACKGROUND);
    term_opt_.have_cursor_color = true;
    term_opt_.cursor_color = (uint32_t)scheme_.get(th::SC_CURSOR);
    term_opt_.palette = palette_;
    term_opt_.renderer = opt_.renderer;
    term_opt_.dynamic_background = opt_.terminal_background;
    term_ = new tt::TermPane(bottom_slot_, "terminal", term_opt_, this);
    XtVaSetValues(term_->widget(), XmNtopAttachment, XmATTACH_FORM,
                  XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
                  XmNbottomAttachment, XmATTACH_FORM, (char*)0);
    if (XtIsRealized(bottom_slot_))
        term_->start();
}

void App::realized()
{
    Display* dpy = XtDisplay(top_);
    XDefineCursor(dpy, XtWindow(grid_), XCreateFontCursor(dpy, XC_xterm));
    XDefineCursor(dpy, XtWindow(tree_sash_), XCreateFontCursor(dpy, XC_sb_h_double_arrow));
    XDefineCursor(dpy, XtWindow(bottom_sash_), XCreateFontCursor(dpy, XC_sb_v_double_arrow));
    if (term_)
        term_->start();
    XmProcessTraversal(grid_, XmTRAVERSE_CURRENT);
    update_ui();
}

Time App::now() const
{
    return XtLastTimestampProcessed(XtDisplay(top_));
}

/* ---- documents ---- */

Doc* App::new_doc()
{
    Doc* d = new Doc();
    d->view.tab_width = opt_.tab_width;
    d->view.indent_width = opt_.indent_width;
    d->view.insert_spaces = opt_.insert_spaces;
    d->tab = XtVaCreateManagedWidget("tab", xmToggleButtonWidgetClass, tabs_,
                                     XmNindicatorOn, False,
                                     XmNshadowThickness, 2,
                                     XmNfillOnSelect, True,
                                     XmNselectColor, tab_fill(),
                                     XmNtraversalOn, False,
                                     XmNmarginWidth, 8, (char*)0);
    XtAddCallback(d->tab, XmNvalueChangedCallback, tab_cb, 0);
    XtAddEventHandler(d->tab, ButtonReleaseMask, False, tab_button, 0);
    if (!close_pixmap_) {
        Pixel fg, bg;
        int depth;
        XtVaGetValues(tabs_, XmNforeground, &fg, XmNbackground, &bg, XmNdepth, &depth,
                      (char*)0);
        close_pixmap_ = XCreatePixmapFromBitmapData(
            XtDisplay(tabs_), RootWindowOfScreen(XtScreen(tabs_)), (char*)k_close_bits, 8, 7,
            fg, bg, (unsigned)depth);
    }
    d->close = XtVaCreateManagedWidget("close", xmPushButtonWidgetClass, tabs_,
                                       XmNlabelType, XmPIXMAP,
                                       XmNlabelPixmap, close_pixmap_,
                                       XmNshadowThickness, 1,
                                       XmNhighlightThickness, 0,
                                       XmNtraversalOn, False,
                                       XmNmarginWidth, 3, XmNmarginHeight, 3, (char*)0);
    XtAddCallback(d->close, XmNactivateCallback, tab_close_cb, 0);
    docs_.push_back(d);
    return d;
}

void App::show_doc(int i)
{
    if (i < 0 || i >= docs_.size()) {
        cur_ = -1;
        src_.set_view(0);
        TwGridDamage(grid_);
        update_ui();
        return;
    }
    cur_ = i;
    for (int k = 0; k < docs_.size(); k++)
        XmToggleButtonSetState(docs_[k]->tab, k == i, False);
    src_.set_view(&docs_[i]->view);
    docs_[i]->view.follow();
    TwGridDamage(grid_);
    update_ui();
}

void App::open_file(const char* path)
{
    char full[2048];
    if (path[0] != '/' && strlen(root_.c_str()) + strlen(path) < sizeof(full) - 2)
        sprintf(full, "%s/%s", root_.c_str(), path);
    else
        snprintf(full, sizeof(full), "%s", path);
    for (int i = 0; i < docs_.size(); i++)
        if (strcmp(docs_[i]->path.c_str(), full) == 0) {
            show_doc(i);
            return;
        }
    /* An untouched new document gives way to the file. */
    Doc* d = doc();
    if (!(d && d->path.empty() && !d->text.modified() && d->text.lines() == 1 &&
          d->text.length(0) == 0))
        d = new_doc();
    int err = load(d, full);
    if (err && err != ENOENT)
        error("Cannot read %s", full, err);
    for (int i = 0; i < docs_.size(); i++)
        if (docs_[i] == d)
            show_doc(i);
}

void App::open_workspace(const char* dir)
{
    root_ = dir;
    for (int i = 0; i < rows_.size(); i++) {
        free(rows_[i]->path);
        free(rows_[i]->name);
        delete rows_[i];
    }
    rows_.clear();
    tree_read(-1, root_.c_str(), 0);
    XtVaSetValues(tree_, XmNtopItemPosition, 1, (char*)0);
    tree_fill();
    /* A shell at its prompt follows; a running command is not disturbed
     * (a new shell starts in the workspace anyway). */
    if (term_ && term_->at_prompt()) {
        char cmd[2200];
        size_t n = 0;
        cmd[n++] = 0x15;            /* Ctrl+U: whatever was typed goes */
        n += (size_t)sprintf(cmd + n, "cd '");
        for (const char* p = dir; *p && n < sizeof(cmd) - 8; p++) {
            if (*p == '\'') {
                memcpy(cmd + n, "'\\''", 4);
                n += 4;
            } else {
                cmd[n++] = *p;
            }
        }
        cmd[n++] = '\'';
        cmd[n++] = '\r';
        term_->send(cmd, n);
    }
    update_ui();
}

int App::load(Doc* d, const char* path)
{
    d->path = path;
    d->name = base_name(path);
    FILE* f = fopen(path, "rb");
    if (!f) {
        int e = errno;
        d->text.set_text("", 0);
        update_tab(d);
        return e;
    }
    size_t cap = 65536, len = 0;
    char* buf = (char*)malloc(cap);
    int err = 0;
    while (buf) {
        if (len == cap) {
            char* nb = (char*)realloc(buf, cap * 2);
            if (!nb) {
                err = ENOMEM;
                break;
            }
            buf = nb;
            cap *= 2;
        }
        size_t r = fread(buf + len, 1, cap - len, f);
        if (r == 0) {
            if (ferror(f))
                err = errno ? errno : EIO;
            break;
        }
        len += r;
    }
    fclose(f);
    d->text.set_text(buf ? buf : "", buf ? len : 0);
    free(buf);
    struct stat st;
    d->mtime = stat(path, &st) == 0 ? st.st_mtime : 0;
    d->view.set_cursor(te::Pos(0, 0), false);
    d->detect_language();
    update_tab(d);
    return err;
}

/* Into a new file next to the old one, then renamed over it, so a full
 * disk never leaves half a file. A symbolic link is written through. */
int App::write_file(Doc* d, const char* path)
{
    size_t n;
    char* t = d->text.text(&n);
    if (!t)
        return ENOMEM;
    struct stat st, lst;
    bool exists = stat(path, &st) == 0;
    bool link = lstat(path, &lst) == 0 && S_ISLNK(lst.st_mode);
    char tmp[2100];
    bool direct = link || strlen(path) > 2000;
    if (!direct)
        sprintf(tmp, "%s.ttrcode-%ld", path, (long)getpid());
    FILE* f = fopen(direct ? path : tmp, "wb");
    if (!f && !direct) {
        /* No right to make files in the directory: write in place. */
        direct = true;
        f = fopen(path, "wb");
    }
    if (!f) {
        int e = errno;
        free(t);
        return e;
    }
    int err = 0;
    if (n && fwrite(t, 1, n, f) != n)
        err = errno ? errno : EIO;
    if (fflush(f) != 0 && !err)
        err = errno ? errno : EIO;
    if (fclose(f) != 0 && !err)
        err = errno ? errno : EIO;
    free(t);
    if (!direct) {
        if (!err && exists)
            chmod(tmp, st.st_mode & 07777);
        if (!err && rename(tmp, path) != 0)
            err = errno;
        if (err)
            unlink(tmp);
    }
    if (!err) {
        d->mtime = stat(path, &st) == 0 ? st.st_mtime : 0;
        d->text.mark_saved();
    }
    return err;
}

bool App::save_doc(Doc* d, bool ask_name)
{
    if (!d)
        return false;
    if (ask_name || d->path.empty()) {
        ttr::Str p;
        if (!ask_file("Save As", d->path.empty() ? root_.c_str() : d->path.c_str(), &p))
            return false;
        d->path = p;
        d->name = base_name(p.c_str());
        d->detect_language();
    }
    int err = write_file(d, d->path.c_str());
    if (err) {
        error("Cannot save %s", d->path.c_str(), err);
        return false;
    }
    update_tab(d);
    update_ui();
    return true;
}

void App::close_doc(int i, bool* cancelled)
{
    *cancelled = false;
    Doc* d = docs_[i];
    if (d->text.modified()) {
        show_doc(i);
        int a = ask_save(d);
        if (a == 2 || (a == 0 && !save_doc(d, false))) {
            *cancelled = true;
            return;
        }
    }
    XtDestroyWidget(d->tab);
    XtDestroyWidget(d->close);
    docs_.remove_at(i);
    if (src_.view() == &d->view)
        src_.set_view(0);
    delete d;
    int next = cur_ >= docs_.size() ? docs_.size() - 1 : cur_;
    if (i < cur_)
        next = cur_ - 1;
    show_doc(next);
}

void App::quit()
{
    for (int i = docs_.size() - 1; i >= 0; i--) {
        if (!docs_[i]->text.modified())
            continue;
        bool cancelled;
        close_doc(i, &cancelled);
        if (cancelled)
            return;
    }
    if (term_)
        term_->hangup(500);
    exit(0);
}

/* The window came back: files changed on disk? */
void App::focus_changed_window()
{
    for (int i = 0; i < docs_.size(); i++) {
        Doc* d = docs_[i];
        struct stat st;
        if (d->path.empty() || !d->mtime || stat(d->path.c_str(), &st) != 0 ||
            st.st_mtime == d->mtime)
            continue;
        d->mtime = st.st_mtime;
        if (!d->text.modified()) {
            te::Pos keep = d->view.cursor();
            load(d, d->path.c_str());
            d->view.set_cursor(keep, false);
        } else {
            error("%s changed on disk; saving will overwrite it.", d->path.c_str(), 0);
        }
    }
    if (doc())
        show_doc(cur_);
}

/* ---- updating the window ---- */

void App::update_tab(Doc* d)
{
    if (!d->tab)
        return;
    char label[600];
    bool mod = d->text.modified();
    snprintf(label, sizeof(label), "%s%s", d->name.c_str(), mod ? " *" : "");
    XmString s = XmStringCreateLocalized(label);
    XtVaSetValues(d->tab, XmNlabelString, s, (char*)0);
    XmStringFree(s);
    d->shown_modified = mod;
}

void App::update_title()
{
    Doc* d = doc();
    char t[700];
    if (d)
        snprintf(t, sizeof(t), "%s%s - ttrcode", d->name.c_str(), d->text.modified() ? " *" : "");
    else
        snprintf(t, sizeof(t), "%s - ttrcode", base_name(root_.c_str()));
    XtVaSetValues(top_, XmNtitle, t, XmNiconName, d ? d->name.c_str() : "ttrcode", (char*)0);
}

void App::update_scrollbar()
{
    Doc* d = doc();
    int total = d ? (int)d->text.lines() : 1;
    int rows = src_.rows() > 0 ? src_.rows() : 1;
    int value = d ? (int)d->view.top() : 0;
    /* The last line can scroll to the top, as in VS6. */
    int max = total + rows - 1;
    if (value > max - rows)
        value = max - rows;
    if (value < 0)
        value = 0;
    in_scroll_update_ = true;
    XtVaSetValues(scroll_, XmNmaximum, max, XmNsliderSize, rows, XmNvalue, value,
                  XmNpageIncrement, rows > 1 ? rows - 1 : 1, (char*)0);
    in_scroll_update_ = false;
}

void App::update_ui()
{
    Doc* d = doc();
    char st[800];
    if (d) {
        te::Pos c = d->view.cursor();
        snprintf(st, sizeof(st), "Ln %ld, Col %d     %s     %s     %s", c.line + 1,
                 d->view.column_of(c) + 1, te::language_name(d->syntax.language()),
                 d->text.crlf() ? "CRLF" : "LF",
                 d->path.empty() ? d->name.c_str() : d->path.c_str());
        if (d->shown_modified != d->text.modified())
            update_tab(d);
    } else {
        snprintf(st, sizeof(st), "%s", root_.c_str());
    }
    XmString s = XmStringCreateLocalized(st);
    XtVaSetValues(status_, XmNlabelString, s, (char*)0);
    XmStringFree(s);
    update_scrollbar();
    update_title();
}

void App::after_edit(bool follow)
{
    Doc* d = doc();
    if (d && follow)
        d->view.follow();
    TwGridDamage(grid_);
    update_ui();
}

void App::error(const char* fmt, const char* arg, int err)
{
    char m[1200];
    snprintf(m, sizeof(m), fmt, arg);
    if (err) {
        size_t l = strlen(m);
        snprintf(m + l, sizeof(m) - l, ": %s", strerror(err));
    }
    Widget dlg = XmCreateErrorDialog(top_, (char*)"error", 0, 0);
    XmString s = XmStringCreateLocalized(m);
    XtVaSetValues(dlg, XmNmessageString, s, (char*)0);
    XmStringFree(s);
    XtUnmanageChild(child(dlg, "Cancel"));
    XtUnmanageChild(child(dlg, "Help"));
    XtVaSetValues(XtParent(dlg), XmNtitle, "ttrcode", (char*)0);
    XtAddCallback(dlg, XmNokCallback, destroy_dialog_cb, 0);
    XtManageChild(dlg);
}

/* ---- modal questions ---- */

int App::ask_save(Doc* d)
{
    Widget dlg = XmCreateQuestionDialog(top_, (char*)"saveChanges", 0, 0);
    char m[700];
    snprintf(m, sizeof(m), "Save the changes to %s?", d->name.c_str());
    XmString s = XmStringCreateLocalized(m);
    XmString save = XmStringCreateLocalized((char*)"Save");
    XmString discard = XmStringCreateLocalized((char*)"Discard");
    XmString cancel = XmStringCreateLocalized((char*)"Cancel");
    XtVaSetValues(dlg, XmNmessageString, s, XmNokLabelString, save, XmNhelpLabelString, discard,
                  XmNcancelLabelString, cancel, XmNdialogStyle, XmDIALOG_FULL_APPLICATION_MODAL,
                  (char*)0);
    XmStringFree(s);
    XmStringFree(save);
    XmStringFree(discard);
    XmStringFree(cancel);
    XtVaSetValues(XtParent(dlg), XmNtitle, "ttrcode", (char*)0);
    XtAddCallback(dlg, XmNokCallback, answer_cb, (XtPointer)0);
    XtAddCallback(dlg, XmNhelpCallback, answer_cb, (XtPointer)1);
    XtAddCallback(dlg, XmNcancelCallback, answer_cb, (XtPointer)2);
    answer_ = -1;
    XtManageChild(dlg);
    XtAppContext ctx = XtWidgetToApplicationContext(top_);
    while (answer_ < 0 && XtIsManaged(dlg))
        XtAppProcessEvent(ctx, XtIMAll);
    int a = answer_ < 0 ? 2 : answer_;
    XtDestroyWidget(XtParent(dlg));
    return a;
}

bool App::ask_file(const char* title, const char* start, ttr::Str* out)
{
    Widget dlg = XmCreateFileSelectionDialog(top_, (char*)"fileDialog", 0, 0);
    XtVaSetValues(XtParent(dlg), XmNtitle, title, (char*)0);
    XtVaSetValues(dlg, XmNdialogStyle, XmDIALOG_FULL_APPLICATION_MODAL, (char*)0);
    XtUnmanageChild(child(dlg, "Help"));
    /* Start in the file's directory, with its name filled in. */
    struct stat st;
    char dir[2048];
    snprintf(dir, sizeof(dir), "%s", start);
    bool is_dir = stat(dir, &st) == 0 && S_ISDIR(st.st_mode);
    const char* name = "";
    if (!is_dir) {
        char* slash = strrchr(dir, '/');
        if (slash) {
            *slash = 0;
            name = start + (slash - dir) + 1;
        }
    }
    char mask[2100];
    snprintf(mask, sizeof(mask), "%s/*", dir[0] ? dir : "/");
    XmString m = XmStringCreateLocalized(mask);
    XtVaSetValues(dlg, XmNdirMask, m, (char*)0);
    XmStringFree(m);
    XmFileSelectionDoSearch(dlg, 0);
    if (name[0]) {
        char sel[2100];
        snprintf(sel, sizeof(sel), "%s/%s", dir, name);
        XmString s = XmStringCreateLocalized(sel);
        XtVaSetValues(dlg, XmNtextString, s, (char*)0);
        XmStringFree(s);
    }
    XtAddCallback(dlg, XmNokCallback, answer_cb, (XtPointer)0);
    XtAddCallback(dlg, XmNcancelCallback, answer_cb, (XtPointer)2);
    answer_ = -1;
    XtManageChild(dlg);
    XtAppContext ctx = XtWidgetToApplicationContext(top_);
    while (answer_ < 0 && XtIsManaged(dlg))
        XtAppProcessEvent(ctx, XtIMAll);
    bool ok = answer_ == 0;
    if (ok) {
        Widget text = child(dlg, "Text");
        char* v = XmTextFieldGetString(text);
        ok = v && v[0] && v[strlen(v) - 1] != '/';
        if (ok)
            *out = v;
        XtFree(v);
    }
    XtDestroyWidget(XtParent(dlg));
    return ok;
}

/* An absolute path without ".", ".." and doubled or trailing slashes. */
static void normalize_path(char* path)
{
    char out[2048];
    size_t o = 0;
    const char* p = path;
    while (*p) {
        while (*p == '/')
            p++;
        const char* e = p;
        while (*e && *e != '/')
            e++;
        size_t n = (size_t)(e - p);
        if (n == 0 || (n == 1 && p[0] == '.')) {
            /* nothing */
        } else if (n == 2 && p[0] == '.' && p[1] == '.') {
            while (o > 0 && out[o - 1] != '/')
                o--;
            if (o > 0)
                o--;
        } else if (o + n + 2 < sizeof(out)) {
            out[o++] = '/';
            memcpy(out + o, p, n);
            o += n;
        }
        p = e;
    }
    if (o == 0)
        out[o++] = '/';
    out[o] = 0;
    strcpy(path, out);
}

/* A directory: the file selection box showing directories only. */
bool App::ask_dir(const char* title, const char* start, ttr::Str* out)
{
    Arg a[1];
    XtSetArg(a[0], XmNfileTypeMask, XmFILE_DIRECTORY);
    Widget dlg = XmCreateFileSelectionDialog(top_, (char*)"dirDialog", a, 1);
    XtVaSetValues(XtParent(dlg), XmNtitle, title, (char*)0);
    XtVaSetValues(dlg, XmNdialogStyle, XmDIALOG_FULL_APPLICATION_MODAL, (char*)0);
    XtUnmanageChild(child(dlg, "Help"));
    /* In the current workspace: its "." entry, selected as the box
     * opens, is the workspace itself; a subdirectory is a click away. */
    char mask[2100];
    snprintf(mask, sizeof(mask), "%s/*", strcmp(start, "/") == 0 ? "" : start);
    XmString m = XmStringCreateLocalized(mask);
    XtVaSetValues(dlg, XmNdirMask, m, (char*)0);
    XmStringFree(m);
    XmFileSelectionDoSearch(dlg, 0);
    XtAddCallback(dlg, XmNokCallback, answer_cb, (XtPointer)0);
    XtAddCallback(dlg, XmNcancelCallback, answer_cb, (XtPointer)2);
    answer_ = -1;
    XtManageChild(dlg);
    XtAppContext ctx = XtWidgetToApplicationContext(top_);
    while (answer_ < 0 && XtIsManaged(dlg))
        XtAppProcessEvent(ctx, XtIMAll);
    bool ok = false;
    if (answer_ == 0) {
        char* v = XmTextFieldGetString(child(dlg, "Text"));
        char path[2048];
        snprintf(path, sizeof(path), "%s", v ? v : "");
        XtFree(v);
        normalize_path(path);
        struct stat st;
        ok = path[0] == '/' && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
        if (ok)
            *out = path;
        else
            error("%s is not a directory.", path, 0);
    }
    XtDestroyWidget(XtParent(dlg));
    return ok;
}

bool App::ask_text(const char* title, const char* label, ttr::Str* out)
{
    Widget dlg = XmCreatePromptDialog(top_, (char*)"prompt", 0, 0);
    XtVaSetValues(XtParent(dlg), XmNtitle, title, (char*)0);
    XmString l = XmStringCreateLocalized((char*)label);
    XtVaSetValues(dlg, XmNselectionLabelString, l,
                  XmNdialogStyle, XmDIALOG_FULL_APPLICATION_MODAL, (char*)0);
    XmStringFree(l);
    XtUnmanageChild(child(dlg, "Help"));
    XtAddCallback(dlg, XmNokCallback, answer_cb, (XtPointer)0);
    XtAddCallback(dlg, XmNcancelCallback, answer_cb, (XtPointer)2);
    answer_ = -1;
    XtManageChild(dlg);
    XtAppContext ctx = XtWidgetToApplicationContext(top_);
    while (answer_ < 0 && XtIsManaged(dlg))
        XtAppProcessEvent(ctx, XtIMAll);
    bool ok = answer_ == 0;
    if (ok) {
        char* v = XmTextFieldGetString(child(dlg, "Text"));
        *out = v ? v : "";
        XtFree(v);
    }
    XtDestroyWidget(XtParent(dlg));
    return ok;
}

/* ---- search ---- */

void App::find(bool forward)
{
    Doc* d = doc();
    if (!d || find_text_.empty())
        return;
    bool wrapped = false;
    if (!d->view.find(find_text_.c_str(), forward, match_case_, &wrapped)) {
        XBell(XtDisplay(top_), 0);
        char m[600];
        snprintf(m, sizeof(m), "Not found: %.500s", find_text_.c_str());
        XmString s = XmStringCreateLocalized(m);
        XtVaSetValues(status_, XmNlabelString, s, (char*)0);
        XmStringFree(s);
        return;
    }
    after_edit(true);
    if (wrapped) {
        XmString s = XmStringCreateLocalized((char*)"Search wrapped around.");
        XtVaSetValues(status_, XmNlabelString, s, (char*)0);
        XmStringFree(s);
    }
}

enum { FIND_NEXT, FIND_REPLACE, FIND_REPLACE_ALL, FIND_CLOSE };

void App::find_dialog_button(int which)
{
    char* v = XmTextFieldGetString(find_what_);
    find_text_ = v ? v : "";
    XtFree(v);
    v = XmTextFieldGetString(replace_with_);
    replace_text_ = v ? v : "";
    XtFree(v);
    match_case_ = XmToggleButtonGetState(case_toggle_) != False;
    Doc* d = doc();
    switch (which) {
    case FIND_NEXT:
        find(true);
        break;
    case FIND_REPLACE:
        /* Replace the hit if it is selected, then find the next one. */
        if (d && d->view.has_selection()) {
            size_t n;
            char* sel = d->view.copy(&n);
            bool hit = sel && n == strlen(find_text_.c_str()) &&
                       (match_case_ ? strncmp(sel, find_text_.c_str(), n) == 0
                                    : strncasecmp(sel, find_text_.c_str(), n) == 0);
            free(sel);
            if (hit)
                d->view.paste(replace_text_.c_str(), strlen(replace_text_.c_str()));
        }
        find(true);
        break;
    case FIND_REPLACE_ALL:
        if (d && !find_text_.empty()) {
            int count = d->view.replace_all(find_text_.c_str(), replace_text_.c_str(),
                                            match_case_);
            after_edit(true);
            char m[100];
            sprintf(m, "%d replaced.", count);
            XmString s = XmStringCreateLocalized(m);
            XtVaSetValues(status_, XmNlabelString, s, (char*)0);
            XmStringFree(s);
        }
        break;
    default:
        XtUnmanageChild(find_dlg_);
        XmProcessTraversal(grid_, XmTRAVERSE_CURRENT);
        break;
    }
}

/* One dialog for Find and Replace; Find hides the replace parts. */
void App::show_find(bool replace)
{
    if (!find_dlg_) {
        Arg a[2];
        XtSetArg(a[0], XmNautoUnmanage, False);
        find_dlg_ = XmCreateTemplateDialog(top_, (char*)"findDialog", a, 1);
        Widget work = XtVaCreateManagedWidget("work", xmFormWidgetClass, find_dlg_, (char*)0);
        Widget l1 = XtVaCreateManagedWidget("Find what:", xmLabelWidgetClass, work,
                                            XmNtopAttachment, XmATTACH_FORM, XmNtopOffset, 6,
                                            XmNleftAttachment, XmATTACH_FORM, (char*)0);
        find_what_ = XtVaCreateManagedWidget("what", xmTextFieldWidgetClass, work,
                                             XmNcolumns, 40,
                                             XmNtopAttachment, XmATTACH_FORM,
                                             XmNleftAttachment, XmATTACH_FORM,
                                             XmNleftOffset, 90,
                                             XmNrightAttachment, XmATTACH_FORM, (char*)0);
        replace_row_ = XtVaCreateManagedWidget("Replace with:", xmLabelWidgetClass, work,
                                               XmNtopAttachment, XmATTACH_WIDGET,
                                               XmNtopWidget, find_what_, XmNtopOffset, 10,
                                               XmNleftAttachment, XmATTACH_FORM, (char*)0);
        replace_with_ = XtVaCreateManagedWidget("with", xmTextFieldWidgetClass, work,
                                                XmNcolumns, 40,
                                                XmNtopAttachment, XmATTACH_WIDGET,
                                                XmNtopWidget, find_what_, XmNtopOffset, 4,
                                                XmNleftAttachment, XmATTACH_FORM,
                                                XmNleftOffset, 90,
                                                XmNrightAttachment, XmATTACH_FORM, (char*)0);
        case_toggle_ = XtVaCreateManagedWidget("Match case", xmToggleButtonWidgetClass, work,
                                               XmNtopAttachment, XmATTACH_WIDGET,
                                               XmNtopWidget, replace_with_, XmNtopOffset, 6,
                                               XmNleftAttachment, XmATTACH_FORM,
                                               XmNleftOffset, 86, (char*)0);
        (void)l1;
        static const char* const labels[] = { "Find Next", "Replace", "Replace All", "Close" };
        for (int i = 0; i < 4; i++) {
            Widget b = XtVaCreateManagedWidget(labels[i], xmPushButtonWidgetClass, find_dlg_,
                                               (char*)0);
            XtAddCallback(b, XmNactivateCallback, find_button_cb, (XtPointer)(long)i);
            if (i == 0)
                XtVaSetValues(find_dlg_, XmNdefaultButton, b, (char*)0);
        }
        /* Enter is the default button, Find Next. */
    }
    XtVaSetValues(XtParent(find_dlg_), XmNtitle, replace ? "Replace" : "Find", (char*)0);
    /* The selection (one line of it) is what to find. */
    Doc* d = doc();
    if (d && d->view.has_selection()) {
        size_t n;
        char* sel = d->view.copy(&n);
        if (sel && !memchr(sel, '\n', n))
            find_text_ = sel;
        free(sel);
    }
    XmTextFieldSetString(find_what_, (char*)find_text_.c_str());
    XmTextFieldSetString(replace_with_, (char*)replace_text_.c_str());
    XmToggleButtonSetState(case_toggle_, match_case_, False);
    WidgetList kids;
    Cardinal nk;
    XtVaGetValues(find_dlg_, XmNchildren, &kids, XmNnumChildren, &nk, (char*)0);
    for (Cardinal i = 0; i < nk; i++) {
        const char* nm = XtName(kids[i]);
        if (strcmp(nm, "Replace") == 0 || strcmp(nm, "Replace All") == 0) {
            if (replace)
                XtManageChild(kids[i]);
            else
                XtUnmanageChild(kids[i]);
        }
    }
    if (replace) {
        XtManageChild(replace_row_);
        XtManageChild(replace_with_);
    } else {
        XtUnmanageChild(replace_row_);
        XtUnmanageChild(replace_with_);
    }
    XtVaSetValues(case_toggle_, XmNtopWidget, replace ? replace_with_ : find_what_, (char*)0);
    XtManageChild(find_dlg_);
    XmProcessTraversal(find_what_, XmTRAVERSE_CURRENT);
    XmTextFieldSetSelection(find_what_, 0, XmTextFieldGetLastPosition(find_what_), now());
}

/* ---- panes and full screen ---- */

void App::show_workspace(bool on)
{
    workspace_on_ = on;
    layout();
}

void App::show_bottom(bool on)
{
    bottom_on_ = on;
    layout();
}

/* Does the window manager do _NET_WM_STATE_FULLSCREEN (ttr does)? */
static bool wm_has_fullscreen(Display* dpy)
{
    Atom net_supported = XInternAtom(dpy, "_NET_SUPPORTED", False);
    Atom full = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    bool found = false;
    Atom type;
    int format;
    unsigned long count, after;
    unsigned char* data = 0;
    if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), net_supported, 0, 4096, False, XA_ATOM,
                           &type, &format, &count, &after, &data) == Success && data) {
        Atom* atoms = (Atom*)data;
        for (unsigned long i = 0; i < count; i++)
            if (atoms[i] == full)
                found = true;
        XFree(data);
    }
    return found;
}

/* F11: only the text. Window managers with EWMH get asked for full
 * screen; mwm and 4Dwm read _MOTIF_WM_HINTS when a window is mapped, so
 * there the window goes away for a moment and comes back without
 * decorations, covering the screen. */
void App::set_fullscreen(bool on)
{
    if (on == fullscreen_)
        return;
    fullscreen_ = on;
    layout();
    Display* dpy = XtDisplay(top_);
    if (wm_has_fullscreen(dpy)) {
        XEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.xclient.type = ClientMessage;
        ev.xclient.window = XtWindow(top_);
        ev.xclient.message_type = XInternAtom(dpy, "_NET_WM_STATE", False);
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = on ? 1 : 0;
        ev.xclient.data.l[1] = (long)XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
        ev.xclient.data.l[3] = 1;
        XSendEvent(dpy, DefaultRootWindow(dpy), False,
                   SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    } else {
        Window win = XtWindow(top_);
        if (on) {
            /* Where the window is on the screen, frame aside. */
            Window child;
            int x = 0, y = 0;
            XTranslateCoordinates(dpy, win, DefaultRootWindow(dpy), 0, 0, &x, &y, &child);
            save_x_ = (Position)x;
            save_y_ = (Position)y;
            XtVaGetValues(top_, XmNwidth, &save_w_, XmNheight, &save_h_, (char*)0);
        }
        XWithdrawWindow(dpy, win, DefaultScreen(dpy));
        XSync(dpy, False);
        Screen* scr = XtScreen(top_);
        if (on)
            XtVaSetValues(top_, XmNmwmDecorations, 0, XmNx, 0, XmNy, 0,
                          XmNwidth, (Dimension)WidthOfScreen(scr),
                          XmNheight, (Dimension)HeightOfScreen(scr), (char*)0);
        else
            XtVaSetValues(top_, XmNmwmDecorations, MWM_DECOR_ALL, XmNx, save_x_, XmNy, save_y_,
                          XmNwidth, save_w_, XmNheight, save_h_, (char*)0);
        XMapWindow(dpy, win);
    }
    XmProcessTraversal(grid_, XmTRAVERSE_CURRENT);
}

/* In the terminal, Alt+letters belong to the shell, not to the menus. */
void App::set_mnemonics(bool on)
{
    for (int i = 0; i < ncascades_; i++)
        XtVaSetValues(cascades_[i], XmNmnemonic, on ? (KeySym)mnemonics_[i] : (KeySym)0,
                      (char*)0);
}

/* ---- commands ---- */

void App::command(int cmd)
{
    Doc* d = doc();
    switch (cmd) {
    case CMD_NEW: {
        Doc* nd = new_doc();
        char name[32];
        sprintf(name, "Untitled%d", ++untitled_);
        nd->name = name;
        nd->text.set_text("", 0);
        update_tab(nd);
        show_doc(docs_.size() - 1);
        break;
    }
    case CMD_OPEN: {
        ttr::Str p;
        if (ask_file("Open", d && !d->path.empty() ? d->path.c_str() : root_.c_str(), &p))
            open_file(p.c_str());
        break;
    }
    case CMD_OPEN_WORKSPACE: {
        ttr::Str p;
        if (ask_dir("Open Workspace", root_.c_str(), &p))
            open_workspace(p.c_str());
        break;
    }
    case CMD_SAVE:
        save_doc(d, false);
        break;
    case CMD_SAVE_AS:
        save_doc(d, true);
        break;
    case CMD_SAVE_ALL:
        for (int i = 0; i < docs_.size(); i++)
            if (docs_[i]->text.modified())
                save_doc(docs_[i], false);
        break;
    case CMD_CLOSE:
        if (d) {
            bool cancelled;
            close_doc(cur_, &cancelled);
        }
        break;
    case CMD_EXIT:
        quit();
        break;
    case CMD_UNDO:
        if (d && d->view.undo())
            after_edit(true);
        break;
    case CMD_REDO:
        if (d && d->view.redo())
            after_edit(true);
        break;
    case CMD_CUT:
    case CMD_COPY:
        if (d && d->view.has_selection()) {
            free(clipboard_);
            clipboard_ = cmd == CMD_CUT ? d->view.cut(&clipboard_len_)
                                        : d->view.copy(&clipboard_len_);
            own(a_clipboard_, now());
            after_edit(true);
        }
        break;
    case CMD_PASTE:
        request_paste(a_clipboard_, now());
        break;
    case CMD_DELETE:
        if (d) {
            d->view.del();
            after_edit(true);
        }
        break;
    case CMD_SELECT_ALL:
        if (d) {
            d->view.select_all();
            after_edit(false);
        }
        break;
    case CMD_FIND:
        show_find(false);
        break;
    case CMD_REPLACE:
        show_find(true);
        break;
    case CMD_FIND_NEXT:
    case CMD_FIND_PREV:
        if (find_text_.empty())
            show_find(false);
        else
            find(cmd == CMD_FIND_NEXT);
        break;
    case CMD_GOTO:
        if (d) {
            ttr::Str v;
            char label[64];
            sprintf(label, "Line number (1-%ld):", d->text.lines());
            if (ask_text("Go To Line", label, &v) && atol(v.c_str()) > 0) {
                d->view.goto_line(atol(v.c_str()));
                after_edit(true);
            }
        }
        break;
    case CMD_VIEW_WORKSPACE:
        if (fullscreen_)
            set_fullscreen(false);
        show_workspace(!workspace_on_);
        if (workspace_on_)
            XmProcessTraversal(tree_, XmTRAVERSE_CURRENT);
        else
            XmProcessTraversal(grid_, XmTRAVERSE_CURRENT);
        break;
    case CMD_VIEW_BOTTOM:
        if (fullscreen_)
            set_fullscreen(false);
        show_bottom(!bottom_on_);
        if (!bottom_on_)
            XmProcessTraversal(grid_, XmTRAVERSE_CURRENT);
        break;
    case CMD_VIEW_LINE_NUMBERS:
        opt_.line_numbers = !opt_.line_numbers;
        src_.set_line_numbers(opt_.line_numbers);
        after_edit(true);
        break;
    case CMD_VIEW_FULLSCREEN:
        set_fullscreen(!fullscreen_);
        break;
    case CMD_TERMINAL:
        /* To the terminal and back. */
        if (term_ && XmGetFocusWidget(top_) == term_->grid()) {
            XmProcessTraversal(grid_, XmTRAVERSE_CURRENT);
        } else if (term_) {
            if (fullscreen_)
                set_fullscreen(false);
            if (!bottom_on_)
                show_bottom(true);
            term_->focus();
        }
        break;
    case CMD_NEXT_DOC:
    case CMD_PREV_DOC:
        if (docs_.size() > 1)
            show_doc((cur_ + (cmd == CMD_NEXT_DOC ? 1 : docs_.size() - 1)) % docs_.size());
        break;
    case CMD_ABOUT: {
        Widget dlg = XmCreateInformationDialog(top_, (char*)"about", 0, 0);
        XmString s = XmStringCreateLocalized(
            (char*)"ttrcode, the To The Roots code editor: Motif, te and tw.");
        XtVaSetValues(dlg, XmNmessageString, s, (char*)0);
        XmStringFree(s);
        XtUnmanageChild(child(dlg, "Cancel"));
        XtUnmanageChild(child(dlg, "Help"));
        XtVaSetValues(XtParent(dlg), XmNtitle, "About ttrcode", (char*)0);
        XtAddCallback(dlg, XmNokCallback, destroy_dialog_cb, 0);
        XtManageChild(dlg);
        break;
    }
    default:
        break;
    }
}

/* Keys that work everywhere in the window, also in the terminal. */
bool App::global_key(KeySym ks, unsigned state)
{
    bool ctrl = (state & ControlMask) != 0, shift = (state & ShiftMask) != 0;
    bool alt = (state & Mod1Mask) != 0;
    if (ks == XK_F11 && !ctrl && !alt) {
        command(CMD_VIEW_FULLSCREEN);
        return true;
    }
    if (alt && !ctrl && ks == XK_0) {
        command(CMD_VIEW_WORKSPACE);
        return true;
    }
    if (alt && !ctrl && ks == XK_2) {
        command(CMD_VIEW_BOTTOM);
        return true;
    }
    if (ctrl && !alt && (ks == XK_grave || ks == XK_dead_grave)) {
        command(CMD_TERMINAL);
        return true;
    }
    if (ctrl && !alt && (ks == XK_Tab || ks == XK_ISO_Left_Tab)) {
        command(shift || ks == XK_ISO_Left_Tab ? CMD_PREV_DOC : CMD_NEXT_DOC);
        return true;
    }
    return false;
}

/* ---- the editor's keys (VS6) ---- */

void App::editor_key(TwGridCallbackStruct* cb)
{
    KeySym ks = cb->keysym;
    unsigned state = cb->state;
    if (ks == XK_Shift_L || ks == XK_Shift_R || ks == XK_Control_L || ks == XK_Control_R ||
        ks == XK_Alt_L || ks == XK_Alt_R || ks == XK_Meta_L || ks == XK_Meta_R ||
        ks == XK_Caps_Lock || ks == XK_Num_Lock || ks == XK_Super_L || ks == XK_Super_R)
        return;
    if (global_key(ks, state))
        return;
    bool ctrl = (state & ControlMask) != 0, shift = (state & ShiftMask) != 0;
    bool alt = (state & Mod1Mask) != 0;
    /* Commands with or without a document. */
    if (ctrl && !alt) {
        switch (ks) {
        case XK_n: case XK_N: command(CMD_NEW); return;
        case XK_o: case XK_O: command(CMD_OPEN); return;
        case XK_s: case XK_S: command(CMD_SAVE); return;
        case XK_F4: command(CMD_CLOSE); return;
        default: break;
        }
    }
    Doc* d = doc();
    if (!d)
        return;
    te::View& v = d->view;
    bool follow = true;
    if (ks == XK_Escape && fullscreen_) {
        set_fullscreen(false);
        return;
    }
    if (ctrl && !alt) {
        switch (ks) {
        case XK_z: case XK_Z:
            command(shift ? CMD_REDO : CMD_UNDO);
            return;
        case XK_y: case XK_Y: command(CMD_REDO); return;
        case XK_x: case XK_X: command(CMD_CUT); return;
        case XK_c: case XK_C: command(CMD_COPY); return;
        case XK_v: case XK_V: command(CMD_PASTE); return;
        case XK_a: case XK_A: command(CMD_SELECT_ALL); return;
        case XK_f: case XK_F: command(CMD_FIND); return;
        case XK_h: case XK_H: command(CMD_REPLACE); return;
        case XK_g: case XK_G: command(CMD_GOTO); return;
        case XK_Insert: case XK_KP_Insert: command(CMD_COPY); return;
        case XK_l: case XK_L:
            /* Cut the line (Ctrl+Shift+L: delete it). */
            if (shift) {
                v.delete_line();
            } else {
                te::Pos c = v.cursor();
                long last = d->text.lines() - 1;
                v.set_cursor(te::Pos(c.line, 0), false);
                v.set_cursor(c.line < last ? te::Pos(c.line + 1, 0)
                                           : te::Pos(c.line, d->text.length(c.line)), true);
                command(CMD_CUT);
                return;
            }
            after_edit(true);
            return;
        default:
            break;
        }
    }
    switch (ks) {
    case XK_Left: case XK_KP_Left:
        v.move(ctrl ? te::MOVE_WORD_LEFT : te::MOVE_LEFT, shift);
        break;
    case XK_Right: case XK_KP_Right:
        v.move(ctrl ? te::MOVE_WORD_RIGHT : te::MOVE_RIGHT, shift);
        break;
    case XK_Up: case XK_KP_Up:
        if (ctrl) {
            v.scroll(-1);
            follow = false;
        } else {
            v.move(te::MOVE_UP, shift);
        }
        break;
    case XK_Down: case XK_KP_Down:
        if (ctrl) {
            v.scroll(1);
            follow = false;
        } else {
            v.move(te::MOVE_DOWN, shift);
        }
        break;
    case XK_Home: case XK_KP_Home:
        v.move(ctrl ? te::MOVE_DOC_START : te::MOVE_HOME, shift);
        break;
    case XK_End: case XK_KP_End:
        v.move(ctrl ? te::MOVE_DOC_END : te::MOVE_END, shift);
        break;
    case XK_Prior: case XK_KP_Prior:
        v.move(te::MOVE_PAGE_UP, shift);
        break;
    case XK_Next: case XK_KP_Next:
        v.move(te::MOVE_PAGE_DOWN, shift);
        break;
    case XK_Return: case XK_KP_Enter:
        v.newline();
        break;
    case XK_Tab: case XK_ISO_Left_Tab:
        v.tab(shift || ks == XK_ISO_Left_Tab);
        break;
    case XK_BackSpace:
        if (alt) {
            command(CMD_UNDO);
            return;
        }
        if (ctrl && !v.has_selection())
            v.move(te::MOVE_WORD_LEFT, true);
        v.backspace();
        break;
    case XK_Delete: case XK_KP_Delete:
        if (shift) {
            command(CMD_CUT);
            return;
        }
        if (ctrl && !v.has_selection())
            v.move(te::MOVE_WORD_RIGHT, true);
        v.del();
        break;
    case XK_Insert: case XK_KP_Insert:
        if (shift)
            command(CMD_PASTE);
        return;
    case XK_F3:
        command(shift ? CMD_FIND_PREV : CMD_FIND_NEXT);
        return;
    case XK_Escape:
        v.clear_selection();
        break;
    default:
        if (ctrl || alt || cb->text_len <= 0)
            return;
        {
            char t[256];
            int n = key_text(cb->text, cb->text_len, t, sizeof(t));
            /* Only printable text: control characters come as keys. */
            if (n <= 0 || (unsigned char)t[0] < 0x20 || t[0] == 0x7f)
                return;
            v.type(t, (size_t)n);
        }
        break;
    }
    after_edit(follow);
}

/* ---- the mouse ---- */

te::Pos App::pos_at(int col, int row)
{
    Doc* d = doc();
    int x = col - src_.gutter() + d->view.left();
    if (x < 0)
        x = 0;
    return d->view.pos_at(d->view.top() + row, x);
}

void App::autoscroll_step()
{
    autoscroll_id_ = 0;
    Doc* d = doc();
    if (!d || !selecting_ || !autoscroll_)
        return;
    d->view.scroll(autoscroll_);
    int row = autoscroll_ < 0 ? 0 : src_.rows() - 1;
    te::Pos p = d->view.pos_at(d->view.top() + row, autoscroll_ < 0 ? 0 : 1 << 20);
    d->view.set_cursor(p, true);
    after_edit(false);
    autoscroll_id_ = XtAppAddTimeOut(XtWidgetToApplicationContext(grid_), 50,
                                     autoscroll_timeout, 0);
}

void App::editor_pointer(TwGridCallbackStruct* cb)
{
    Doc* d = doc();
    if (!d)
        return;
    te::View& v = d->view;
    Time t = cb->event ? cb->event->xbutton.time : CurrentTime;
    if (cb->reason == TW_CR_BUTTON_PRESS) {
        switch (cb->button) {
        case 1: {
            XmProcessTraversal(grid_, XmTRAVERSE_CURRENT);
            bool same = cb->col == last_col_ && cb->row == last_row_;
            Time multi = (Time)XtGetMultiClickTime(XtDisplay(grid_));
            click_count_ = same && t - last_click_ < multi ? click_count_ % 3 + 1 : 1;
            last_click_ = t;
            last_col_ = cb->col;
            last_row_ = cb->row;
            te::Pos p = pos_at(cb->col, cb->row);
            if (cb->col < src_.gutter() || click_count_ == 3) {
                /* The gutter (or a triple click) selects the line. */
                long last = d->text.lines() - 1;
                v.set_cursor(te::Pos(p.line, 0), false);
                v.set_cursor(p.line < last ? te::Pos(p.line + 1, 0)
                                           : te::Pos(p.line, d->text.length(p.line)), true);
            } else if (click_count_ == 2) {
                v.select_word(p);
            } else {
                v.set_cursor(p, (cb->state & ShiftMask) != 0);
            }
            selecting_ = true;
            after_edit(false);
            break;
        }
        case 2: {
            te::Pos p = pos_at(cb->col, cb->row);
            v.set_cursor(p, false);
            request_paste(XA_PRIMARY, t);
            break;
        }
        case 4: case 5:
            v.scroll(cb->button == 4 ? -3 : 3);
            after_edit(false);
            break;
        default:
            break;
        }
        return;
    }
    if (cb->reason == TW_CR_MOTION) {
        if (!selecting_ || !(cb->state & Button1Mask))
            return;
        int cw, ch;
        TwGridGetCellSize(grid_, &cw, &ch);
        int h = src_.rows() * ch + 4;
        autoscroll_ = cb->y < 0 ? -1 : cb->y >= h ? 1 : 0;
        if (autoscroll_ && !autoscroll_id_)
            autoscroll_step();
        if (!autoscroll_) {
            v.set_cursor(pos_at(cb->col, cb->row), true);
            after_edit(false);
        }
        return;
    }
    if (cb->reason == TW_CR_BUTTON_RELEASE && cb->button == 1 && selecting_) {
        selecting_ = false;
        autoscroll_ = 0;
        if (autoscroll_id_) {
            XtRemoveTimeOut(autoscroll_id_);
            autoscroll_id_ = 0;
        }
        if (v.has_selection()) {
            free(primary_);
            primary_ = v.copy(&primary_len_);
            own(XA_PRIMARY, t);
        }
    }
}

void App::editor_resize(TwGridCallbackStruct* cb)
{
    src_.set_size(cb->cols, cb->rows);
    if (doc())
        doc()->view.follow();
    TwGridDamage(grid_);
    update_scrollbar();
}

void App::editor_focus(bool in)
{
    if (in)
        set_mnemonics(true);
}

void App::scrolled(int value)
{
    if (in_scroll_update_ || !doc())
        return;
    te::View& v = doc()->view;
    v.scroll(value - v.top());
    TwGridDamage(grid_);
}

/* The tabs work as radio buttons, kept by hand: choosing the current
 * tab again leaves it chosen. */
void App::tab_chosen(Widget tab, bool set)
{
    for (int i = 0; i < docs_.size(); i++) {
        if (docs_[i]->tab != tab)
            continue;
        if (i != cur_)
            show_doc(i);
        else if (!set)
            XmToggleButtonSetState(tab, True, False);
    }
    XmProcessTraversal(grid_, XmTRAVERSE_CURRENT);
}

void App::tab_close(Widget w)
{
    for (int i = 0; i < docs_.size(); i++)
        if (docs_[i]->tab == w || docs_[i]->close == w) {
            bool cancelled;
            close_doc(i, &cancelled);
            break;
        }
    XmProcessTraversal(grid_, XmTRAVERSE_CURRENT);
}

/* ---- the clipboard ---- */

void App::own(Atom sel, Time t)
{
    if (!XtOwnSelection(grid_, sel, t, convert_sel, lose_sel, 0)) {
        if (sel == XA_PRIMARY) {
            free(primary_);
            primary_ = 0;
        }
    }
}

Boolean App::convert(Atom sel, Atom target, Atom* type, XtPointer* value, unsigned long* length,
                     int* format)
{
    const char* src = sel == XA_PRIMARY ? primary_ : clipboard_;
    size_t len = sel == XA_PRIMARY ? primary_len_ : clipboard_len_;
    if (!src)
        return False;
    if (target == a_targets_) {
        Atom* t = (Atom*)XtMalloc(4 * sizeof(Atom));
        t[0] = a_targets_;
        t[1] = a_utf8_;
        t[2] = XA_STRING;
        t[3] = a_text_;
        *value = (XtPointer)t;
        *length = 4;
        *type = XA_ATOM;
        *format = 32;
        return True;
    }
    char* v = XtMalloc((Cardinal)(len + 1));
    size_t o = 0;
    if (target == a_utf8_) {
        memcpy(v, src, len);
        o = len;
        *type = a_utf8_;
    } else if (target == XA_STRING || target == a_text_) {
        /* Latin-1; other characters become '?'. */
        for (size_t i = 0; i < len;) {
            unsigned char c = (unsigned char)src[i];
            if (c < 0x80) {
                v[o++] = (char)c;
                i++;
            } else if ((c & 0xe0) == 0xc0 && i + 1 < len) {
                unsigned cp = ((c & 0x1fu) << 6) | ((unsigned char)src[i + 1] & 0x3f);
                v[o++] = (char)(cp < 256 ? cp : '?');
                i += 2;
            } else {
                v[o++] = '?';
                i += (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 1;
            }
        }
        *type = XA_STRING;
    } else {
        XtFree(v);
        return False;
    }
    v[o] = 0;
    *value = (XtPointer)v;
    *length = o;
    *format = 8;
    return True;
}

void App::lost(Atom sel)
{
    if (sel == XA_PRIMARY) {
        free(primary_);
        primary_ = 0;
        primary_len_ = 0;
    }
}

void App::request_paste(Atom sel, Time t)
{
    XtGetSelectionValue(grid_, sel, a_utf8_, got_paste_cb, 0, t);
}

void App::got_paste(Widget w, Atom sel, Atom type, XtPointer value, unsigned long length,
                    int format, bool second_try)
{
    Doc* d = doc();
    if (value && format == 8 && type == a_utf8_ && !second_try) {
        if (d)
            d->view.paste((const char*)value, (size_t)length);
    } else if (value && format == 8 && (type == XA_STRING || type == a_text_)) {
        char* u = (char*)malloc(length * 2 + 1);
        size_t o = 0;
        const unsigned char* p = (const unsigned char*)value;
        for (unsigned long i = 0; u && i < length; i++) {
            if (p[i] < 0x80) {
                u[o++] = (char)p[i];
            } else {
                u[o++] = (char)(0xc0 | (p[i] >> 6));
                u[o++] = (char)(0x80 | (p[i] & 0x3f));
            }
        }
        if (d && u)
            d->view.paste(u, o);
        free(u);
    } else if (!second_try) {
        XtGetSelectionValue(w, sel, XA_STRING, got_paste_string_cb, 0,
                            XtLastTimestampProcessed(XtDisplay(w)));
    }
    if (value)
        XtFree((char*)value);
    after_edit(true);
}

/* ---- the terminal ---- */

void App::pane_exited(tt::TermPane* p, int)
{
    const char* m = "\r\n[The shell has ended. Press Enter for a new one.]";
    p->terminal().feed((const uint8_t*)m, strlen(m));
    TwGridDamage(p->grid());
}

bool App::pane_key(tt::TermPane* p, TwGridCallbackStruct* cb)
{
    if (global_key(cb->keysym, cb->state))
        return true;
    if (!p->running() && (cb->keysym == XK_Return || cb->keysym == XK_KP_Enter)) {
        /* Not from inside the pane's own key callback: it would be
         * deleted under it. */
        XtAppAddTimeOut(XtWidgetToApplicationContext(top_), 0, restart_terminal_cb, 0);
        return true;
    }
    return false;
}

void App::restart_terminal()
{
    if (!term_ || term_->running())
        return;
    delete term_;
    term_ = 0;
    make_terminal();
    layout();
    term_->focus();
}

void App::pane_focus(tt::TermPane*, bool in)
{
    set_mnemonics(!in);
}

/* ---- colours and settings ---- */

ttr::Str settings_path()
{
    const char* xdg = getenv("XDG_CONFIG_HOME");
    const char* home = getenv("HOME");
    char path[1100];
    if (xdg && *xdg && strlen(xdg) < 1000)
        sprintf(path, "%s/ttrcode/settings", xdg);
    else
        sprintf(path, "%.1000s/.config/ttrcode/settings", home ? home : ".");
    return ttr::Str(path);
}

/* The theme ttr itself uses, from its settings file. */
const char* ttr_theme()
{
    static char name[128];
    ttr::SettingsFile f;
    name[0] = 0;
    if (f.load(ttr::SettingsFile::default_path().c_str()) && f.get("theme"))
        snprintf(name, sizeof(name), "%s", f.get("theme"));
    return name;
}

void App::save_settings()
{
    ttr::SettingsFile f;
    ttr::Str path = settings_path();
    static const char* const header[] = {
        "# ttrcode's settings: X resource names (TTRcode*name) and values.",
        "# View > Color Scheme and View > UI Theme change the colours here.", 0
    };
    f.set_header(header);
    f.load(path.c_str());
    char v[32];
    f.set("colorScheme", scheme_.name);
    if (theme_name_.empty())
        f.remove("uiTheme");
    else
        f.set("uiTheme", theme_name_.c_str());
    sprintf(v, "%d", tree_w_);
    f.set("treeWidth", v);
    sprintf(v, "%d", bottom_h_);
    f.set("bottomHeight", v);
    ttr::make_parent_dirs(path.c_str());
    f.save(path.c_str());
}

/* The editor and the terminal pane take the scheme at once. */
void App::set_scheme(const char* name)
{
    th::find_scheme(name, &scheme_);
    EditColors c;
    c.text = scheme_.get(th::SC_FOREGROUND);
    c.background = scheme_.get(th::SC_BACKGROUND);
    c.selected_text = scheme_.get(th::SC_SELECTION_FG);
    c.selected_background = scheme_.get(th::SC_SELECTION_BG);
    c.gutter_text = scheme_.get(th::SC_GUTTER_FG);
    c.gutter_background = scheme_.get(th::SC_GUTTER_BG);
    c.control = scheme_.get(th::SC_CONTROL);
    c.found_background = scheme_.get(th::SC_FIND_BG);
    c.cursor = scheme_.get(th::SC_CURSOR);
    c.syntax[te::SYN_NONE] = c.text;
    c.syntax[te::SYN_COMMENT] = scheme_.get(th::SC_COMMENT);
    c.syntax[te::SYN_KEYWORD] = scheme_.get(th::SC_KEYWORD);
    c.syntax[te::SYN_TYPE] = scheme_.get(th::SC_KEYWORD);
    c.syntax[te::SYN_STRING] = scheme_.get(th::SC_STRING);
    c.syntax[te::SYN_NUMBER] = scheme_.get(th::SC_NUMBER);
    c.syntax[te::SYN_PREPROC] = scheme_.get(th::SC_PREPROCESSOR);
    src_.set_colors(c);
    for (int i = 0; i < 16; i++)
        palette_[i] = (uint32_t)scheme_.ansi(i);
    if (term_) {
        term_->set_colors((uint32_t)c.text, (uint32_t)c.background);
        term_->set_cursor_color(true, (uint32_t)c.cursor);
        term_->set_palette(palette_);
    }
    if (grid_)
        TwGridDamage(grid_);
    for (int i = 0; scheme_items_ && i < schemes_.count(); i++)
        XmToggleButtonSetState(scheme_items_[i],
                               th::compare_names(schemes_[i].name, scheme_.name) == 0, False);
}

/* Widgets change colour in place (XmChangeColor); dialogs made later get
 * the theme through the resource database. */
void App::set_theme(const char* name)
{
    theme_name_ = name ? name : "";
    th::Theme& t = theme_;
    const char* effective = theme_name_.empty() ? ttr_theme() : theme_name_.c_str();
    th::find_theme(effective, &t);
    XrmDatabase db = XtDatabase(XtDisplay(top_));
    th::apply_theme(&db, t);
    th::recolor(top_, t, twGridWidgetClass);
    for (int i = 0; i < docs_.size(); i++)
        XtVaSetValues(docs_[i]->tab, XmNselectColor, tab_fill(), (char*)0);
    /* The tabs' close buttons have the old colours in their pixmap. */
    if (close_pixmap_) {
        Pixel fg, bg;
        int depth;
        XtVaGetValues(tabs_, XmNforeground, &fg, XmNbackground, &bg, XmNdepth, &depth,
                      (char*)0);
        Pixmap old = close_pixmap_;
        close_pixmap_ = XCreatePixmapFromBitmapData(
            XtDisplay(tabs_), RootWindowOfScreen(XtScreen(tabs_)), (char*)k_close_bits, 8, 7,
            fg, bg, (unsigned)depth);
        for (int i = 0; i < docs_.size(); i++)
            XtVaSetValues(docs_[i]->close, XmNlabelPixmap, close_pixmap_, (char*)0);
        XFreePixmap(XtDisplay(tabs_), old);
    }
    for (int i = 0; theme_items_ && i < themes_.count() + 1; i++) {
        bool on = i == 0 ? theme_name_.empty()
                         : !theme_name_.empty() &&
                               th::compare_names(themes_[i - 1].name, theme_name_.c_str()) == 0;
        XmToggleButtonSetState(theme_items_[i], on, False);
    }
}

/* The chosen tab is filled with the background's select shade, which
 * the theme's text stays readable on (a theme's toggle fill may not). */
Pixel App::tab_fill()
{
    return th::pixel(tabs_, th::motif_colors(theme_.get(th::ROLE_BACKGROUND)).select);
}

void App::scheme_chosen(int i)
{
    if (i < 0 || i >= schemes_.count())
        return;
    set_scheme(schemes_[i].name);
    save_settings();
}

void App::theme_chosen(int i)
{
    set_theme(i == 0 ? "" : themes_[i - 1].name);
    save_settings();
}

/* View > Color Scheme and View > UI Theme: one radio item per scheme
 * and theme found (th/scheme.h, th/theme.h). */
void App::build_color_menus(Widget view)
{
    th::list_schemes(&schemes_);
    th::list_themes(&themes_);
    Arg a[2];
    XtSetArg(a[0], XmNradioBehavior, True);
    Widget sm = XmCreatePulldownMenu(view, (char*)"schemeMenu", a, 1);
    XtVaCreateManagedWidget("Color Scheme", xmCascadeButtonWidgetClass, view,
                            XmNsubMenuId, sm, XmNmnemonic, 'C', (char*)0);
    scheme_items_ = (Widget*)XtMalloc((Cardinal)(sizeof(Widget) * (size_t)(schemes_.count() + 1)));
    for (int i = 0; i < schemes_.count(); i++) {
        scheme_items_[i] = XtVaCreateManagedWidget(
            schemes_[i].name, xmToggleButtonWidgetClass, sm, XmNindicatorType, XmONE_OF_MANY,
            XmNvisibleWhenOff, True,
            XmNset, th::compare_names(schemes_[i].name, scheme_.name) == 0, (char*)0);
        XtAddCallback(scheme_items_[i], XmNvalueChangedCallback, scheme_cb, (XtPointer)(long)i);
    }
    /* Many themes: in columns. */
    XtSetArg(a[1], XmNpacking, XmPACK_COLUMN);
    Widget tm = XmCreatePulldownMenu(view, (char*)"themeMenu", a, 2);
    XtVaSetValues(tm, XmNnumColumns, (short)(themes_.count() > 18 ? 2 : 1), (char*)0);
    XtVaCreateManagedWidget("UI Theme", xmCascadeButtonWidgetClass, view,
                            XmNsubMenuId, tm, XmNmnemonic, 'U', (char*)0);
    theme_items_ = (Widget*)XtMalloc((Cardinal)(sizeof(Widget) * (size_t)(themes_.count() + 1)));
    for (int i = 0; i <= themes_.count(); i++) {
        const char* label = i == 0 ? "Same as ttr" : themes_[i - 1].name;
        bool on = i == 0 ? theme_name_.empty()
                         : !theme_name_.empty() &&
                               th::compare_names(themes_[i - 1].name, theme_name_.c_str()) == 0;
        theme_items_[i] = XtVaCreateManagedWidget(
            label, xmToggleButtonWidgetClass, tm, XmNindicatorType, XmONE_OF_MANY,
            XmNvisibleWhenOff, True, XmNset, on, (char*)0);
        XtAddCallback(theme_items_[i], XmNvalueChangedCallback, theme_cb, (XtPointer)(long)i);
    }
}

/* ---- the workspace tree ---- */

void App::tree_read(int at, const char* dir, int depth)
{
    DIR* dp = opendir(dir);
    if (!dp)
        return;
    ttr::Vector<TreeRow*> found;
    struct dirent* e;
    while ((e = readdir(dp)) != 0) {
        const char* nm = e->d_name;
        if (nm[0] == '.')
            continue;
        char path[2400];
        if (strlen(dir) + strlen(nm) + 2 > 2048)
            continue;
        snprintf(path, sizeof(path), "%s/%s", strcmp(dir, "/") == 0 ? "" : dir, nm);
        struct stat st;
        bool isdir = stat(path, &st) == 0 && S_ISDIR(st.st_mode);
        if (isdir && build_dir(nm))
            continue;
        TreeRow* r = new TreeRow;
        r->path = dup_str(path);
        r->name = dup_str(nm);
        r->depth = depth;
        r->dir = isdir;
        r->open = false;
        found.push_back(r);
    }
    closedir(dp);
    if (found.size() == 0)
        return;
    TreeRow** arr = (TreeRow**)malloc(sizeof(TreeRow*) * (size_t)found.size());
    for (int i = 0; i < found.size(); i++)
        arr[i] = found[i];
    qsort(arr, (size_t)found.size(), sizeof(TreeRow*), compare_rows);
    /* Insert after 'at'. */
    ttr::Vector<TreeRow*> all;
    for (int i = 0; i <= at && i < rows_.size(); i++)
        all.push_back(rows_[i]);
    for (int i = 0; i < found.size(); i++)
        all.push_back(arr[i]);
    for (int i = at + 1; i < rows_.size(); i++)
        all.push_back(rows_[i]);
    free(arr);
    rows_.clear();
    for (int i = 0; i < all.size(); i++)
        rows_.push_back(all[i]);
}

void App::tree_fill()
{
    int top = 0;
    XtVaGetValues(tree_, XmNtopItemPosition, &top, (char*)0);
    XmListDeleteAllItems(tree_);
    int n = rows_.size();
    if (!n)
        return;
    XmString* items = (XmString*)XtMalloc((Cardinal)(sizeof(XmString) * (size_t)n));
    for (int i = 0; i < n; i++) {
        TreeRow* r = rows_[i];
        char line[600];
        int ind = r->depth * 2 < 80 ? r->depth * 2 : 80;
        snprintf(line, sizeof(line), "%*s%s%s%s", ind, "", r->dir ? (r->open ? "- " : "+ ") : "  ",
                 r->name, r->dir ? "/" : "");
        items[i] = XmStringCreateLocalized(line);
    }
    XmListAddItems(tree_, items, n, 0);
    for (int i = 0; i < n; i++)
        XmStringFree(items[i]);
    XtFree((char*)items);
    if (top > 0)
        XmListSetPos(tree_, top);
}

void App::tree_toggle(int row)
{
    TreeRow* r = rows_[row];
    if (!r->open) {
        r->open = true;
        tree_read(row, r->path, r->depth + 1);
    } else {
        r->open = false;
        int end = row + 1;
        while (end < rows_.size() && rows_[end]->depth > r->depth)
            end++;
        for (int i = end - 1; i > row; i--) {
            free(rows_[i]->path);
            free(rows_[i]->name);
            delete rows_[i];
            rows_.remove_at(i);
        }
    }
    tree_fill();
    XmListSelectPos(tree_, row + 1, False);
}

void App::tree_activate(int row)
{
    if (row < 0 || row >= rows_.size())
        return;
    if (rows_[row]->dir) {
        tree_toggle(row);
        return;
    }
    open_file(rows_[row]->path);
    XmProcessTraversal(grid_, XmTRAVERSE_CURRENT);
}

} /* namespace ttrcode */
