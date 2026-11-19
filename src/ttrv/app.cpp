/* ttrv's window (app.h). */
#include "app.h"

#include "../../tf/path.h"
#include "../../tf/vfs.h"
#include "../../th/theme.h"
#include "../../tx/ttr.h"
#include "core/settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <Xm/CascadeB.h>
#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/MessageB.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/Separator.h>
#include <Xm/Text.h>
#include <Xm/TextF.h>
#include <Xm/ToggleB.h>

namespace ttrv {

namespace {

App* g_app = 0;

void command_cb(Widget, XtPointer closure, XtPointer)
{
    g_app->command((Command)(long)closure);
}

void input_cb(XtPointer, int*, XtInputId*)
{
    g_app->loader_ready();
}

void location_cb(Widget, XtPointer, XtPointer)
{
    g_app->location_entered();
}

void wants_cb(XtPointer, XtIntervalId*)
{
    g_app->wants_timer();
}

Widget menu_item(Widget menu, const char* label, char mnemonic, const char* accel,
                 const char* accel_text, Command cmd)
{
    XmString l = XmStringCreateLocalized((char*)label);
    Arg a[6];
    int n = 0;
    XtSetArg(a[n], XmNlabelString, l); n++;
    if (mnemonic) {
        XtSetArg(a[n], XmNmnemonic, (KeySym)mnemonic); n++;
    }
    XmString at = 0;
    if (accel) {
        XtSetArg(a[n], XmNaccelerator, accel); n++;
    }
    if (accel_text) {
        at = XmStringCreateLocalized((char*)accel_text);
        XtSetArg(a[n], XmNacceleratorText, at); n++;
    }
    Widget b = XmCreatePushButton(menu, (char*)"item", a, n);
    XtAddCallback(b, XmNactivateCallback, command_cb, (XtPointer)(long)cmd);
    XtManageChild(b);
    XmStringFree(l);
    if (at)
        XmStringFree(at);
    return b;
}

Widget menu_toggle(Widget menu, const char* label, char mnemonic, bool on, Command cmd)
{
    XmString l = XmStringCreateLocalized((char*)label);
    Arg a[5];
    int n = 0;
    XtSetArg(a[n], XmNlabelString, l); n++;
    XtSetArg(a[n], XmNmnemonic, (KeySym)mnemonic); n++;
    XtSetArg(a[n], XmNset, on ? True : False); n++;
    XtSetArg(a[n], XmNvisibleWhenOff, True); n++;
    Widget b = XmCreateToggleButton(menu, (char*)"toggle", a, n);
    XtAddCallback(b, XmNvalueChangedCallback, command_cb, (XtPointer)(long)cmd);
    XtManageChild(b);
    XmStringFree(l);
    return b;
}

Widget pulldown(Widget bar, const char* title, char mnemonic)
{
    Widget menu = XmCreatePulldownMenu(bar, (char*)"menu", 0, 0);
    XmString t = XmStringCreateLocalized((char*)title);
    Widget c = XtVaCreateManagedWidget("cascade", xmCascadeButtonWidgetClass, bar,
                                       XmNlabelString, t, XmNmnemonic, (KeySym)mnemonic,
                                       XmNsubMenuId, menu, (char*)0);
    XmStringFree(t);
    if (strcmp(title, "Help") == 0)
        XtVaSetValues(bar, XmNmenuHelpWidget, c, (char*)0);
    return menu;
}

void separator(Widget menu)
{
    XtManageChild(XmCreateSeparator(menu, (char*)"sep", 0, 0));
}

void set_label(Widget w, const char* text)
{
    XmString s = XmStringCreateLocalized((char*)text);
    XtVaSetValues(w, XmNlabelString, s, (char*)0);
    XmStringFree(s);
}

/* The folder above path, and the name to select in it; false at "/".
 * "/a/b.zip/uzip://" is in "/a", named "b.zip". */
bool parent_of(const char* path, ttr::Str* parent, ttr::Str* name)
{
    char p[4300];
    snprintf(p, sizeof(p), "%s", path);
    size_t n = strlen(p);
    if (n <= 1)
        return false;
    /* An archive's root: drop "/scheme://". */
    if (n > 3 && strcmp(p + n - 3, "://") == 0) {
        p[n - 3] = 0;
        char* s = strrchr(p, '/');
        if (!s)
            return false;
        *s = 0;
        n = strlen(p);
    } else if (p[n - 1] == '/') {
        p[--n] = 0;
    }
    char* s = strrchr(p, '/');
    if (!s)
        return false;
    *name = s + 1;
    /* Inside an archive the parent may be its root ("x.zip/uzip://"). */
    if (s > p && s[-1] == '/' && s - p >= 3 && s[-2] == ':') {
        s[1] = 0;
        *parent = p;
        return true;
    }
    if (s == p)
        p[1] = 0;
    else
        *s = 0;
    *parent = p;
    return true;
}

unsigned long alloc_rgb(Display* dpy, Colormap cmap, unsigned long rgb)
{
    XColor c;
    c.red = (unsigned short)(((rgb >> 16) & 255) * 257);
    c.green = (unsigned short)(((rgb >> 8) & 255) * 257);
    c.blue = (unsigned short)((rgb & 255) * 257);
    c.flags = DoRed | DoGreen | DoBlue;
    if (XAllocColor(dpy, cmap, &c))
        return c.pixel;
    return th::brightness(rgb) > 127 ? WhitePixel(dpy, DefaultScreen(dpy))
                                     : BlackPixel(dpy, DefaultScreen(dpy));
}

} /* namespace */

ttr::Str settings_path()
{
    const char* xdg = getenv("XDG_CONFIG_HOME");
    const char* home = getenv("HOME");
    char path[1100];
    if (xdg && *xdg && strlen(xdg) < 1000)
        sprintf(path, "%s/ttrv/settings", xdg);
    else
        sprintf(path, "%.1000s/.config/ttrv/settings", home ? home : ".");
    return ttr::Str(path);
}

App::App(Widget top, const Options& o)
    : top_(top), info_(0), docks_(0), list_(0), tree_(0), preview_(0), input_(0),
      wants_timer_(0), opt_(o), list_err_(0), cache_bytes_(0), use_clock_(0),
      show_hidden_(o.show_hidden), vis_first_(-1), vis_last_(-1)
{
    g_app = this;
    tree_ = new FolderTree(this);
    tree_->set_show_hidden(show_hidden_);
    form_ = XtVaCreateManagedWidget("window", xmFormWidgetClass, top, (char*)0);
    build_menus();
    build_toolbar();
    build_status();
    body_ = XtVaCreateWidget("body", xmFormWidgetClass, form_,
                             XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, locbar_,
                             XmNbottomAttachment, XmATTACH_WIDGET,
                             XmNbottomWidget, XtParent(status_[0]),
                             XmNleftAttachment, XmATTACH_FORM,
                             XmNrightAttachment, XmATTACH_FORM, (char*)0);
    Pixel bg = 0, sel = 0;
    XtVaGetValues(form_, XmNbackground, &bg, (char*)0);
    XmGetColors(XtScreen(form_), DefaultColormapOfScreen(XtScreen(form_)), bg, 0, 0, 0, &sel);
    docks_ = new tx::Docks(body_, this, sel);
    list_ = new FileList(docks_->center(), "list", this, list_colors(), opt_.list_font);
    XtVaSetValues(list_->widget(), XmNtopAttachment, XmATTACH_FORM,
                  XmNbottomAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, (char*)0);
    list_->sort_by((SortKey)(opt_.sort_key >= 0 && opt_.sort_key < SORT_COUNT ? opt_.sort_key : 0),
                   opt_.sort_reverse);
    pane_folders_ = new tx::Pane("folders", "Folders", tx::SIDE_LEFT);
    pane_preview_ = new tx::Pane("preview", "Preview", tx::SIDE_BOTTOM);
    pane_info_ = new tx::Pane("info", "Information", tx::SIDE_BOTTOM);
    docks_->add(pane_folders_);
    docks_->add(pane_preview_);
    docks_->add(pane_info_);
    layout_ = opt_.layout && *opt_.layout ? opt_.layout
                                          : "left:240=folders;bottom:240=info@300|preview;right:200=";
    docks_->apply(layout_.c_str());
    XtManageChild(body_);
    XtVaSetValues(form_, XmNinitialFocus, body_, (char*)0);

    if (!loader_.start(this))
        fprintf(stderr, "ttrv: cannot start the loader threads\n");
    else
        input_ = XtAppAddInput(XtWidgetToApplicationContext(top), loader_.notify_fd(),
                               (XtPointer)XtInputReadMask, input_cb, 0);

    /* The start: a folder, or a file in it (selected). */
    ttr::Str start(opt_.start && *opt_.start ? opt_.start : "");
    if (start.empty()) {
        char cwd[2048];
        start = getcwd(cwd, sizeof(cwd)) ? cwd : "/";
    }
    tf::Stat st;
    if (tf::local_fs()->stat(start.c_str(), &st, true) == 0 && st.type == tf::FT_FILE &&
        !tf::archive_scheme(start.c_str())) {
        ttr::Str parent, name;
        if (parent_of(start.c_str(), &parent, &name))
            go(parent.c_str(), name.c_str(), false);
        else
            go("/", 0, false);
    } else if (st.type == tf::FT_FILE && tf::archive_scheme(start.c_str())) {
        go(child_path(tf::Path(start.c_str()).parent().c_str(),
                      tf::Path(start.c_str()).name(), true).c_str(), 0, false);
    } else {
        go(start.c_str(), 0, false);
    }
}

App::~App()
{
    loader_.stop();
    for (int i = 0; i < cache_.size(); i++) {
        delete cache_[i]->r;
        delete cache_[i];
    }
    for (int i = 0; i < back_.size(); i++)
        delete back_[i];
    for (int i = 0; i < forward_.size(); i++)
        delete forward_[i];
    for (int i = 0; i < tree_wants_.size(); i++)
        delete tree_wants_[i];
    delete list_;
    delete preview_;
    delete docks_;
    delete tree_;
}

ListColors App::list_colors()
{
    th::Theme t;
    th::find_theme(tx::ttr_theme(), &t);
    Display* dpy = XtDisplay(top_);
    Colormap cm = DefaultColormapOfScreen(XtScreen(top_));
    th::ColorSet hdr = th::motif_colors(t.get(th::ROLE_BACKGROUND));
    ListColors c;
    c.bg = alloc_rgb(dpy, cm, t.get(th::ROLE_LIST));
    c.fg = alloc_rgb(dpy, cm, t.get(th::ROLE_FOREGROUND));
    c.sel_bg = alloc_rgb(dpy, cm, t.get(th::ROLE_ACTIVE));
    c.sel_fg = alloc_rgb(dpy, cm, t.get(th::ROLE_ACTIVE_TEXT));
    c.header_bg = alloc_rgb(dpy, cm, hdr.bg);
    c.header_fg = alloc_rgb(dpy, cm, hdr.fg);
    c.header_top = alloc_rgb(dpy, cm, hdr.top);
    c.header_bottom = alloc_rgb(dpy, cm, hdr.bottom);
    c.dim = alloc_rgb(dpy, cm, th::mix(t.get(th::ROLE_LIST), t.get(th::ROLE_FOREGROUND), 50));
    c.folder = alloc_rgb(dpy, cm, 0xe8c860);
    c.image = alloc_rgb(dpy, cm, 0x6890d0);
    c.archive = alloc_rgb(dpy, cm, t.dark ? 0xe07070 : 0xa02020);
    return c;
}

void App::build_menus()
{
    menubar_ = XmCreateMenuBar(form_, (char*)"menuBar", 0, 0);
    XtVaSetValues(menubar_, XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, (char*)0);
    Widget m = pulldown(menubar_, "File", 'F');
    menu_item(m, "Open Location...", 'O', "Ctrl<Key>l", "Ctrl+L", CMD_LOCATION);
    separator(m);
    menu_item(m, "Exit", 'x', "Alt<Key>F4", "Alt+F4", CMD_EXIT);
    m = pulldown(menubar_, "Edit", 'E');
    menu_item(m, "Mark All", 'A', "Ctrl<Key>a", "Ctrl+A", CMD_MARK_ALL);
    menu_item(m, "Unmark All", 'U', 0, 0, CMD_UNMARK_ALL);
    m = pulldown(menubar_, "View", 'V');
    menu_toggle(m, "Folders", 'F', true, CMD_PANE_FOLDERS);
    menu_toggle(m, "Preview", 'P', true, CMD_PANE_PREVIEW);
    menu_toggle(m, "Information", 'I', true, CMD_PANE_INFO);
    separator(m);
    menu_toggle(m, "Hidden Files", 'H', show_hidden_, CMD_HIDDEN);
    separator(m);
    menu_item(m, "Sort by Name", 'N', 0, 0, CMD_SORT_NAME);
    menu_item(m, "Sort by Size", 'S', 0, 0, CMD_SORT_SIZE);
    menu_item(m, "Sort by Image Size", 'm', 0, 0, CMD_SORT_IMAGE);
    menu_item(m, "Sort by Type", 'T', 0, 0, CMD_SORT_TYPE);
    menu_item(m, "Sort by Date", 'D', 0, 0, CMD_SORT_TIME);
    separator(m);
    menu_item(m, "Refresh", 'R', "<Key>F5", "F5", CMD_REFRESH);
    m = pulldown(menubar_, "Go", 'G');
    menu_item(m, "Back", 'B', "Alt<Key>Left", "Alt+Left", CMD_BACK);
    menu_item(m, "Forward", 'F', "Alt<Key>Right", "Alt+Right", CMD_FORWARD);
    menu_item(m, "Up", 'U', "Alt<Key>Up", "Alt+Up", CMD_UP);
    menu_item(m, "Home", 'H', "Alt<Key>Home", "Alt+Home", CMD_HOME);
    m = pulldown(menubar_, "Help", 'H');
    menu_item(m, "About ttrv", 'A', 0, 0, CMD_ABOUT);
    XtManageChild(menubar_);
}

void App::build_toolbar()
{
    toolbar_ = XtVaCreateManagedWidget("toolbar", xmRowColumnWidgetClass, form_,
                                       XmNorientation, XmHORIZONTAL, XmNpacking, XmPACK_TIGHT,
                                       XmNtopAttachment, XmATTACH_WIDGET,
                                       XmNtopWidget, menubar_,
                                       XmNleftAttachment, XmATTACH_FORM,
                                       XmNrightAttachment, XmATTACH_FORM, (char*)0);
    static const struct { const char* label; Command cmd; } buttons[] = {
        { "Back", CMD_BACK }, { "Forward", CMD_FORWARD }, { "Up", CMD_UP },
        { "Refresh", CMD_REFRESH }, { 0, CMD_EXIT }
    };
    for (int i = 0; buttons[i].label; i++) {
        XmString l = XmStringCreateLocalized((char*)buttons[i].label);
        Widget b = XtVaCreateManagedWidget("button", xmPushButtonWidgetClass, toolbar_,
                                           XmNlabelString, l, XmNtraversalOn, False, (char*)0);
        XtAddCallback(b, XmNactivateCallback, command_cb, (XtPointer)(long)buttons[i].cmd);
        XmStringFree(l);
    }
    locbar_ = XtVaCreateManagedWidget("locationBar", xmFormWidgetClass, form_,
                                      XmNtopAttachment, XmATTACH_WIDGET,
                                      XmNtopWidget, toolbar_,
                                      XmNleftAttachment, XmATTACH_FORM,
                                      XmNrightAttachment, XmATTACH_FORM, (char*)0);
    XmString l = XmStringCreateLocalized((char*)"Location:");
    Widget label = XtVaCreateManagedWidget("label", xmLabelWidgetClass, locbar_,
                                           XmNlabelString, l,
                                           XmNleftAttachment, XmATTACH_FORM,
                                           XmNleftOffset, 4,
                                           XmNtopAttachment, XmATTACH_FORM,
                                           XmNbottomAttachment, XmATTACH_FORM, (char*)0);
    XmStringFree(l);
    location_ = XtVaCreateManagedWidget("location", xmTextFieldWidgetClass, locbar_,
                                        XmNleftAttachment, XmATTACH_WIDGET,
                                        XmNleftWidget, label,
                                        XmNrightAttachment, XmATTACH_FORM,
                                        XmNrightOffset, 2,
                                        XmNtopAttachment, XmATTACH_FORM,
                                        XmNbottomAttachment, XmATTACH_FORM, (char*)0);
    XtAddCallback(location_, XmNactivateCallback, location_cb, 0);
}

void App::build_status()
{
    Widget bar = XtVaCreateManagedWidget("statusBar", xmFormWidgetClass, form_,
                                         XmNbottomAttachment, XmATTACH_FORM,
                                         XmNleftAttachment, XmATTACH_FORM,
                                         XmNrightAttachment, XmATTACH_FORM, (char*)0);
    static const int widths[4] = { 220, 200, 220, 0 };
    Widget prev = 0;
    for (int i = 0; i < 4; i++) {
        Arg a[8];
        int n = 0;
        XtSetArg(a[n], XmNshadowType, XmSHADOW_IN); n++;
        XtSetArg(a[n], XmNtopAttachment, XmATTACH_FORM); n++;
        XtSetArg(a[n], XmNbottomAttachment, XmATTACH_FORM); n++;
        if (prev) {
            XtSetArg(a[n], XmNleftAttachment, XmATTACH_WIDGET); n++;
            XtSetArg(a[n], XmNleftWidget, prev); n++;
        } else {
            XtSetArg(a[n], XmNleftAttachment, XmATTACH_FORM); n++;
        }
        if (i == 3) {
            XtSetArg(a[n], XmNrightAttachment, XmATTACH_FORM); n++;
        }
        Widget f = XmCreateFrame(bar, (char*)"field", a, n);
        status_[i] = XtVaCreateManagedWidget("text", xmLabelWidgetClass, f,
                                             XmNalignment, XmALIGNMENT_BEGINNING,
                                             XmNrecomputeSize, False,
                                             XmNmarginWidth, 4, (char*)0);
        if (widths[i])
            XtVaSetValues(status_[i], XmNwidth, widths[i], (char*)0);
        set_label(status_[i], " ");
        XtManageChild(f);
        prev = f;
    }
}

void App::realized()
{
    XmProcessTraversal(list_->canvas(), XmTRAVERSE_CURRENT);
}

/* ------------------------------------------------------------------ */
/* Panes                                                               */

Widget App::build_pane(tx::Pane* p, Widget parent)
{
    if (p == pane_folders_) {
        if (tree_->list())
            XtDestroyWidget(XtParent(tree_->list()));
        Widget w = tree_->build(parent);
        if (!dir_.empty())
            tree_->show_path(dir_.c_str());
        return w;
    }
    if (p == pane_preview_) {
        if (preview_) {
            Widget old = preview_->widget();
            delete preview_;
            XtDestroyWidget(old);
        }
        preview_ = new ImageView(parent, "preview", this);
        Pixel bg = 0;
        XtVaGetValues(parent, XmNbackground, &bg, (char*)0);
        XColor c;
        c.pixel = bg;
        XQueryColor(XtDisplay(parent), DefaultColormapOfScreen(XtScreen(parent)), &c);
        preview_->set_background(((unsigned long)(c.red >> 8) << 16) |
                                 ((unsigned long)(c.green >> 8) << 8) | (c.blue >> 8));
        show_preview();
        return preview_->widget();
    }
    if (p == pane_info_) {
        if (info_)
            XtDestroyWidget(XtParent(info_));
        Arg a[6];
        int n = 0;
        XtSetArg(a[n], XmNeditMode, XmMULTI_LINE_EDIT); n++;
        XtSetArg(a[n], XmNeditable, False); n++;
        XtSetArg(a[n], XmNcursorPositionVisible, False); n++;
        XtSetArg(a[n], XmNwordWrap, True); n++;
        XtSetArg(a[n], XmNscrollHorizontal, False); n++;
        info_ = XmCreateScrolledText(parent, (char*)"info", a, n);
        XtManageChild(info_);
        show_info();
        return XtParent(info_);
    }
    return 0;
}

void App::drop_pane(tx::Pane* p)
{
    if (p == pane_folders_) {
        if (tree_->list())
            XtDestroyWidget(XtParent(tree_->list()));
        tree_->drop();
    } else if (p == pane_preview_) {
        if (preview_) {
            Widget old = preview_->widget();
            delete preview_;
            XtDestroyWidget(old);
            preview_ = 0;
        }
    } else if (p == pane_info_) {
        if (info_)
            XtDestroyWidget(XtParent(info_));
        info_ = 0;
    }
    if (list_)
        XmProcessTraversal(list_->canvas(), XmTRAVERSE_CURRENT);
    schedule_wants();
}

void App::layout_changed()
{
    layout_ = docks_->layout();
    save_settings();
}

void App::set_pane(tx::Pane* p, bool on)
{
    if (on)
        docks_->show(p);
    else
        docks_->close(p);
    layout_changed();
}

/* ------------------------------------------------------------------ */
/* Folders                                                             */

void App::go(const char* path, const char* select, bool remember)
{
    if (remember && !dir_.empty() && strcmp(dir_.c_str(), path) != 0) {
        back_.push_back(here());
        for (int i = 0; i < forward_.size(); i++)
            delete forward_[i];
        forward_.clear();
    }
    listing_ = path;
    select_ = select ? select : "";
    XmTextFieldSetString(location_, (char*)path);
    if (tree_)
        tree_->show_path(path);
    set_label(status_[0], "Reading...");
    update_wants();
}

App::Place* App::here() const
{
    Place* p = new Place;
    p->dir = dir_;
    Row* r = list_->row(list_->cursor());
    if (r && !r->up)
        p->name = r->name;
    return p;
}

void App::fill_list(const Result* r)
{
    ttr::Str was_dir(dir_);
    dir_ = r->path;
    list_err_ = r->err;
    list_->clear();
    ttr::Vector<Row*>& rows = list_->rows();
    ttr::Str pname, pchild;
    if (parent_of(dir_.c_str(), &pname, &pchild)) {
        Row* up = new Row;
        up->name = "..";
        up->up = true;
        up->dir = true;
        rows.push_back(up);
    }
    for (int i = 0; i < r->entries.size(); i++) {
        const Entry* e = r->entries[i];
        if (!show_hidden_ && e->name.c_str()[0] == '.')
            continue;
        Row* row = new Row;
        row->name = e->name;
        row->st = e->st;
        row->archive = e->archive;
        row->dir = e->st.type == tf::FT_DIR || e->archive;
        row->image = !row->dir && image_name(e->name.c_str());
        rows.push_back(row);
    }
    /* Back in the folder we came out of: the cursor on that one. */
    const char* keep = select_.empty() ? 0 : select_.c_str();
    list_->rows_changed(keep);
    if (!keep)
        list_->set_cursor(0);
    select_ = "";
    vis_first_ = vis_last_ = -1;
    int f, l;
    list_->visible(&f, &l);
    list_visible(list_, f, l);
    show_status();
}

void App::tree_list(const char* path)
{
    for (int i = 0; i < tree_wants_.size(); i++)
        if (strcmp(tree_wants_[i]->c_str(), path) == 0)
            return;
    tree_wants_.push_back(new ttr::Str(path));
    schedule_wants();
}

void App::tree_chosen(const char* path)
{
    if (strcmp(path, dir_.c_str()) != 0 || !listing_.empty())
        go(path);
}

/* ------------------------------------------------------------------ */
/* The loader                                                          */

void App::loader_ready()
{
    loader_.dispatch();
}

ttr::Str App::path_of(const Row* r) const
{
    return child_path(dir_.c_str(), r->name.c_str(), false);
}

void App::loaded(Result* r)
{
    bool keep = false;
    if (r->kind == JOB_LIST) {
        for (int i = 0; i < tree_wants_.size(); i++)
            if (strcmp(tree_wants_[i]->c_str(), r->path.c_str()) == 0) {
                delete tree_wants_[i];
                tree_wants_.remove_at(i);
                break;
            }
        tree_->listed(r);
        if (strcmp(r->path.c_str(), listing_.c_str()) == 0) {
            listing_ = "";
            if (r->err) {
                char m[300];
                snprintf(m, sizeof(m), "Cannot read %s: %s", r->path.c_str(), strerror(r->err));
                set_label(status_[0], m);
                /* Back where we were, if anywhere. */
                if (!dir_.empty())
                    XmTextFieldSetString(location_, (char*)dir_.c_str());
                if (tree_ && !dir_.empty())
                    tree_->show_path(dir_.c_str());
            } else {
                fill_list(r);
            }
        }
    } else if (r->kind == JOB_HEADER) {
        size_t dl = strlen(dir_.c_str());
        const char* p = r->path.c_str();
        if (strncmp(p, dir_.c_str(), dl) == 0) {
            const char* name = p + dl;
            if (*name == '/')
                name++;
            int i = list_->find(name);
            Row* row = list_->row(i);
            if (row && !row->probed) {
                row->probed = true;
                row->info = r->info;
                list_->row_changed(i);
                if (i == list_->cursor()) {
                    show_info();
                    show_status();
                }
            }
        }
    } else {
        cache_put(r);
        keep = true;
        Row* cur = list_->row(list_->cursor());
        if (cur && strcmp(path_of(cur).c_str(), r->path.c_str()) == 0) {
            if (!cur->probed && r->info.width) {
                cur->probed = true;
                cur->info = r->info;
                list_->row_changed(list_->cursor());
            }
            show_preview();
            show_info();
            show_status();
        }
    }
    if (!keep)
        delete r;
    schedule_wants();
}

void App::schedule_wants()
{
    if (!wants_timer_)
        wants_timer_ = XtAppAddTimeOut(XtWidgetToApplicationContext(top_), 0, wants_cb, 0);
}

void App::wants_timer()
{
    wants_timer_ = 0;
    update_wants();
}

/* What to load, most important first (ttrv/DESIGN.md section 4.2):
 * the folder, the file under the cursor, its neighbours, the headers in
 * sight, the tree's folders, then the rest of the headers. */
void App::update_wants()
{
    ttr::Vector<Want*> w;
    if (!listing_.empty())
        w.push_back(new Want(JOB_LIST, listing_.c_str()));
    int pw = 0, ph = 0;
    if (preview_)
        preview_->size(&pw, &ph);
    int cur = list_->cursor();
    if (listing_.empty() && preview_ && pw > 8 && ph > 8) {
        static const int order[] = { 0, 1, -1, 2 };
        for (int k = 0; k < 4; k++) {
            Row* r = list_->row(cur + order[k]);
            if (!r || !r->image)
                continue;
            ttr::Str p = path_of(r);
            if (!cached(p.c_str(), pw, ph))
                w.push_back(new Want(JOB_IMAGE, p.c_str(), pw, ph));
        }
    }
    for (int i = 0; i < tree_wants_.size(); i++)
        w.push_back(new Want(JOB_LIST, tree_wants_[i]->c_str()));
    if (listing_.empty()) {
        int f = vis_first_ >= 0 ? vis_first_ : 0;
        int l = vis_last_ >= 0 ? vis_last_ : -1;
        for (int i = f; i <= l; i++) {
            Row* r = list_->row(i);
            if (r && r->image && !r->probed)
                w.push_back(new Want(JOB_HEADER, path_of(r).c_str()));
        }
        /* The rest, a few hundred at a time. */
        int more = 0;
        for (int i = 0; i < list_->count() && more < 300; i++) {
            if (i >= f && i <= l)
                continue;
            Row* r = list_->row(i);
            if (r && r->image && !r->probed) {
                w.push_back(new Want(JOB_HEADER, path_of(r).c_str()));
                more++;
            }
        }
    }
    Want* arr = new Want[w.size() > 0 ? w.size() : 1];
    for (int i = 0; i < w.size(); i++) {
        arr[i] = *w[i];
        delete w[i];
    }
    loader_.set_wants(arr, w.size());
    delete [] arr;
}

/* ------------------------------------------------------------------ */
/* The cache                                                           */

App::Cached* App::cached(const char* path, int w, int h)
{
    for (int i = 0; i < cache_.size(); i++) {
        Cached* c = cache_[i];
        if (c->w == w && c->h == h && strcmp(c->path.c_str(), path) == 0) {
            c->used = ++use_clock_;
            return c;
        }
    }
    return 0;
}

/* Any size of it, the most recently used: shown while the right size
 * decodes. */
App::Cached* App::cached_any(const char* path)
{
    Cached* best = 0;
    for (int i = 0; i < cache_.size(); i++) {
        Cached* c = cache_[i];
        if (strcmp(c->path.c_str(), path) == 0 && (!best || c->used > best->used))
            best = c;
    }
    return best;
}

void App::cache_put(Result* r)
{
    Cached* old = cached(r->path.c_str(), r->w, r->h);
    if (old) {
        cache_bytes_ -= old->r->image.empty() ? 0 : (unsigned long)old->r->image.w * old->r->image.h * 4;
        delete old->r;
        old->r = r;
    } else {
        old = new Cached;
        old->path = r->path;
        old->w = r->w;
        old->h = r->h;
        old->r = r;
        cache_.push_back(old);
    }
    old->used = ++use_clock_;
    cache_bytes_ += r->image.empty() ? 0 : (unsigned long)r->image.w * r->image.h * 4;
    /* Over the budget: the least recently used go, never the one shown. */
    unsigned long budget = (unsigned long)(opt_.cache_mb > 4 ? opt_.cache_mb : 4) << 20;
    const ti::Image* shown = preview_ ? preview_->shown() : 0;
    while (cache_bytes_ > budget && cache_.size() > 1) {
        int lru = -1;
        for (int i = 0; i < cache_.size(); i++) {
            Cached* c = cache_[i];
            if (c == old || &c->r->image == shown)
                continue;
            if (lru < 0 || c->used < cache_[lru]->used)
                lru = i;
        }
        if (lru < 0)
            break;
        Cached* c = cache_[lru];
        cache_bytes_ -= c->r->image.empty() ? 0 : (unsigned long)c->r->image.w * c->r->image.h * 4;
        delete c->r;
        delete c;
        cache_.remove_at(lru);
    }
}

/* ------------------------------------------------------------------ */
/* What the panes show                                                 */

void App::show_preview()
{
    if (!preview_)
        return;
    Row* r = list_->row(list_->cursor());
    if (!r || !r->image) {
        preview_->show(0);
        preview_->set_text(r && r->dir && !r->up ? r->name.c_str() : "");
        return;
    }
    int pw, ph;
    preview_->size(&pw, &ph);
    ttr::Str p = path_of(r);
    Cached* c = cached(p.c_str(), pw, ph);
    bool exact = c != 0;
    if (!c)
        c = cached_any(p.c_str());
    if (c && !c->r->image.empty()) {
        preview_->show(&c->r->image);
        preview_->set_text(exact ? "" : "Decoding...");
    } else if (c && exact) {
        preview_->show(0);
        char m[300];
        snprintf(m, sizeof(m), "%s", c->r->err ? strerror(c->r->err) : c->r->msg);
        preview_->set_text(m);
    } else {
        preview_->show(0);
        preview_->set_text("Decoding...");
    }
}

void App::show_info()
{
    if (!info_)
        return;
    Row* r = list_->row(list_->cursor());
    char t[3000];
    t[0] = 0;
    if (r && !r->up) {
        char size[40], when[64] = "";
        format_size(r->st.size, size, sizeof(size));
        time_t tt = (time_t)r->st.mtime;
        struct tm* tm = localtime(&tt);
        if (tm)
            strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", tm);
        int n = snprintf(t, sizeof(t), "Name: %s\nFolder: %s\n", r->name.c_str(), dir_.c_str());
        if (!r->dir)
            n += snprintf(t + n, sizeof(t) - (size_t)n, "Size: %s (%lu bytes)\n", size,
                          (unsigned long)r->st.size);
        n += snprintf(t + n, sizeof(t) - (size_t)n, "Modified: %s\n", when);
        if (r->probed && r->info.format) {
            n += snprintf(t + n, sizeof(t) - (size_t)n, "\nFormat: %s\nDimensions: %d x %d\n",
                          ti::format_name(r->info.format), r->info.width, r->info.height);
            if (r->info.colors)
                n += snprintf(t + n, sizeof(t) - (size_t)n, "Colours: %d (%d bits)\n",
                              r->info.colors, r->info.bits);
            else
                n += snprintf(t + n, sizeof(t) - (size_t)n, "Depth: %d bits\n", r->info.bits);
        }
        Cached* c = r->image ? cached_any(path_of(r).c_str()) : 0;
        if (c && c->r->info.decoder)
            snprintf(t + n, sizeof(t) - (size_t)n, "Decoder: %s\n", c->r->info.decoder);
    }
    XmTextSetString(info_, t);
}

void App::show_status()
{
    char b[300];
    int files = 0;
    uint64_t bytes = 0;
    for (int i = 0; i < list_->count(); i++) {
        Row* r = list_->row(i);
        if (!r->dir) {
            files++;
            bytes += r->st.size;
        }
    }
    char sz[40];
    format_size(bytes, sz, sizeof(sz));
    int marked = list_->marked_count();
    if (marked)
        snprintf(b, sizeof(b), "Total %d files (%s), %d marked", files, sz, marked);
    else
        snprintf(b, sizeof(b), "Total %d files (%s)", files, sz);
    set_label(status_[0], b);
    Row* r = list_->row(list_->cursor());
    if (r && !r->up) {
        char when[64] = "";
        time_t tt = (time_t)r->st.mtime;
        struct tm* tm = localtime(&tt);
        if (tm)
            strftime(when, sizeof(when), "%Y-%m-%d %H:%M", tm);
        if (r->dir)
            snprintf(b, sizeof(b), "%s", when);
        else {
            format_size(r->st.size, sz, sizeof(sz));
            snprintf(b, sizeof(b), "%s, %s", sz, when);
        }
        set_label(status_[1], b);
        set_label(status_[2], r->name.c_str());
        if (r->probed && r->info.width) {
            char im[60];
            format_image(r->info, im, sizeof(im));
            snprintf(b, sizeof(b), "%s %s", im, ti::format_name(r->info.format));
            set_label(status_[3], b);
        } else {
            set_label(status_[3], " ");
        }
    } else {
        set_label(status_[1], " ");
        set_label(status_[2], " ");
        set_label(status_[3], " ");
    }
}

/* ------------------------------------------------------------------ */
/* The list                                                            */

void App::list_cursor(FileList*, int)
{
    show_preview();
    show_info();
    show_status();
    schedule_wants();
}

void App::list_activate(FileList*, int i)
{
    Row* r = list_->row(i);
    if (!r)
        return;
    if (r->up) {
        command(CMD_UP);
        return;
    }
    if (r->dir) {
        go(child_path(dir_.c_str(), r->name.c_str(), r->archive).c_str());
        return;
    }
    if (r->image)
        set_label(status_[0], "The viewer comes in the next milestone.");
}

void App::list_back(FileList*)
{
    command(CMD_UP);
}

void App::list_visible(FileList*, int first, int last)
{
    vis_first_ = first;
    vis_last_ = last;
    schedule_wants();
}

void App::view_resized(ImageView*)
{
    show_preview();
    schedule_wants();
}

void App::location_entered()
{
    char* t = XmTextFieldGetString(location_);
    ttr::Str p(t ? t : "");
    XtFree(t);
    if (p.empty())
        return;
    const char* s = p.c_str();
    if (s[0] == '~') {
        const char* home = getenv("HOME");
        char b[4200];
        snprintf(b, sizeof(b), "%s%s", home ? home : "", s + 1);
        p = b;
    } else if (s[0] != '/') {
        p = child_path(dir_.c_str(), s, false);
    }
    go(p.c_str());
    XmProcessTraversal(list_->canvas(), XmTRAVERSE_CURRENT);
}

void App::command(Command c)
{
    switch (c) {
    case CMD_BACK:
    case CMD_FORWARD: {
        ttr::Vector<Place*>& from = c == CMD_BACK ? back_ : forward_;
        ttr::Vector<Place*>& to = c == CMD_BACK ? forward_ : back_;
        if (!from.empty()) {
            to.push_back(here());
            Place* p = from.back();
            from.remove_at(from.size() - 1);
            go(p->dir.c_str(), p->name.c_str(), false);
            delete p;
        }
        break;
    }
    case CMD_UP: {
        ttr::Str parent, name;
        if (parent_of(dir_.c_str(), &parent, &name))
            go(parent.c_str(), name.c_str());
        break;
    }
    case CMD_HOME: {
        const char* home = getenv("HOME");
        go(home && *home ? home : "/");
        break;
    }
    case CMD_REFRESH:
        if (!dir_.empty()) {
            Row* r = list_->row(list_->cursor());
            ttr::Str keep(r ? r->name.c_str() : "");
            for (int i = cache_.size() - 1; i >= 0; i--) {
                delete cache_[i]->r;
                delete cache_[i];
            }
            cache_.clear();
            cache_bytes_ = 0;
            if (preview_)
                preview_->show(0);
            go(dir_.c_str(), keep.c_str(), false);
        }
        break;
    case CMD_LOCATION:
        XmProcessTraversal(location_, XmTRAVERSE_CURRENT);
        XmTextFieldSetSelection(location_, 0, XmTextFieldGetLastPosition(location_),
                                CurrentTime);
        break;
    case CMD_HIDDEN:
        show_hidden_ = !show_hidden_;
        tree_->set_show_hidden(show_hidden_);
        save_settings();
        command(CMD_REFRESH);
        break;
    case CMD_SORT_NAME: case CMD_SORT_SIZE: case CMD_SORT_IMAGE: case CMD_SORT_TYPE:
    case CMD_SORT_TIME: {
        SortKey k = (SortKey)(c - CMD_SORT_NAME);
        list_->sort_by(k, k == list_->sort_key() ? !list_->sort_reverse() : false);
        save_settings();
        break;
    }
    case CMD_PANE_FOLDERS:
        set_pane(pane_folders_, !docks_->open(pane_folders_));
        break;
    case CMD_PANE_PREVIEW:
        set_pane(pane_preview_, !docks_->open(pane_preview_));
        break;
    case CMD_PANE_INFO:
        set_pane(pane_info_, !docks_->open(pane_info_));
        break;
    case CMD_MARK_ALL: case CMD_UNMARK_ALL:
        for (int i = 0; i < list_->count(); i++)
            if (!list_->row(i)->up && !list_->row(i)->dir)
                list_->row(i)->marked = c == CMD_MARK_ALL;
        list_->row_changed(list_->cursor());
        list_->rows_changed(0);
        show_status();
        break;
    case CMD_ABOUT: {
        Widget d = XmCreateInformationDialog(top_, (char*)"about", 0, 0);
        XmString m = XmStringGenerate((XtPointer) "ttrv, the To The Roots image viewer.\n\n"
                                                  "Browse with the arrows; Enter opens a folder,\n"
                                                  "Backspace goes up. Archives are folders.",
                                      (char*)XmFONTLIST_DEFAULT_TAG, XmCHARSET_TEXT, 0);
        XtVaSetValues(d, XmNmessageString, m, (char*)0);
        XmStringFree(m);
        XtUnmanageChild(XtNameToWidget(d, "Cancel"));
        XtUnmanageChild(XtNameToWidget(d, "Help"));
        XtManageChild(d);
        break;
    }
    case CMD_EXIT:
        save_settings();
        exit(0);
    }
}

void App::save_settings()
{
    ttr::SettingsFile f;
    ttr::Str path = settings_path();
    static const char* const header[] = {
        "# ttrv's settings: X resource names (TTRv*name) and values.", 0
    };
    f.set_header(header);
    f.load(path.c_str());
    char v[32];
    f.set("layout", layout_.c_str());
    sprintf(v, "%d", (int)list_->sort_key());
    f.set("sortKey", v);
    f.set("sortReverse", list_->sort_reverse() ? "true" : "false");
    f.set("showHidden", show_hidden_ ? "true" : "false");
    f.save(path.c_str());
}

} /* namespace ttrv */
