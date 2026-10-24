/*
 * ttrt, the ttr terminal (ttrt/DESIGN.md). One window: a menu bar over a
 * tt::TermPane (a tl::Terminal drawn by TwGrid, its scroll bar, and the
 * shell on a tp::Pty). Its settings are its own (settings.h); it does not
 * need ttr.
 */
#include "settings.h"
#include "settingsdlg.h"

#include "../../th/scheme.h"
#include "../../tt/pane.h"
#include "core/settings.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/Xutil.h>
#include <Xm/CascadeB.h>
#include <Xm/Form.h>
#include <Xm/Protocols.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/Separator.h>

#if !defined(TTR_DATADIR)
#define TTR_DATADIR "/usr/share/ttr"
#endif

namespace {

struct Resources {
    String font;
    String foreground;
    String background;
    String cursor_color;
    int scrollback;
    Boolean login_shell;
    Boolean menu_bar;
    Boolean bold_is_bright;
    String backspace;
    String bell;
    String color_scheme;
    String renderer;
    String dynamic_background;
    String text_rendering;
};

/* Names and defaults come from settings.h, the list the Settings dialog
 * shares. */
#define ROFF(f) XtOffsetOf(Resources, f)
#define TS(i) (String)ttrt::k_settings[ttrt::i].name
#define TSD(i) (XtPointer)ttrt::k_settings[ttrt::i].fallback
XtResource k_resources[] = {
    { TS(S_FONT), (String)"Font", (String)XtRString, sizeof(String),
      ROFF(font), (String)XtRString, TSD(S_FONT) },
    { TS(S_FOREGROUND), (String)"Foreground", (String)XtRString, sizeof(String),
      ROFF(foreground), (String)XtRString, TSD(S_FOREGROUND) },
    { TS(S_BACKGROUND), (String)"Background", (String)XtRString, sizeof(String),
      ROFF(background), (String)XtRString, TSD(S_BACKGROUND) },
    { TS(S_CURSOR_COLOR), (String)"CursorColor", (String)XtRString, sizeof(String),
      ROFF(cursor_color), (String)XtRString, TSD(S_CURSOR_COLOR) },
    { TS(S_SAVE_LINES), (String)"SaveLines", (String)XtRInt, sizeof(int),
      ROFF(scrollback), (String)XtRString, TSD(S_SAVE_LINES) },
    { TS(S_LOGIN_SHELL), (String)"LoginShell", (String)XtRBoolean, sizeof(Boolean),
      ROFF(login_shell), (String)XtRString, TSD(S_LOGIN_SHELL) },
    { TS(S_MENU_BAR), (String)"MenuBar", (String)XtRBoolean, sizeof(Boolean),
      ROFF(menu_bar), (String)XtRString, TSD(S_MENU_BAR) },
    { TS(S_BOLD_IS_BRIGHT), (String)"BoldIsBright", (String)XtRBoolean, sizeof(Boolean),
      ROFF(bold_is_bright), (String)XtRString, TSD(S_BOLD_IS_BRIGHT) },
    { TS(S_BACKSPACE), (String)"Backspace", (String)XtRString, sizeof(String),
      ROFF(backspace), (String)XtRString, TSD(S_BACKSPACE) },
    { TS(S_BELL), (String)"Bell", (String)XtRString, sizeof(String),
      ROFF(bell), (String)XtRString, TSD(S_BELL) },
    { TS(S_COLOR_SCHEME), (String)"ColorScheme", (String)XtRString, sizeof(String),
      ROFF(color_scheme), (String)XtRString, TSD(S_COLOR_SCHEME) },
    { TS(S_RENDERER), (String)"Renderer", (String)XtRString, sizeof(String),
      ROFF(renderer), (String)XtRString, TSD(S_RENDERER) },
    { TS(S_DYNAMIC_BACKGROUND), (String)"DynamicBackground", (String)XtRString, sizeof(String),
      ROFF(dynamic_background), (String)XtRString, TSD(S_DYNAMIC_BACKGROUND) },
    { TS(S_TEXT_RENDERING), (String)"TextRendering", (String)XtRString, sizeof(String),
      ROFF(text_rendering), (String)XtRString, TSD(S_TEXT_RENDERING) }
};
#undef TSD
#undef TS
#undef ROFF

/* Options set the application's own resources ("ttrt.font"), not every
 * widget's ("*font"): -fg is the terminal's text colour, not the menu
 * bar's. The same table re-applies the command line over the settings
 * file (merge_settings). */
XrmOptionDescRec k_options[] = {
    { (char*)"-fn", (char*)".font", XrmoptionSepArg, 0 },
    { (char*)"-font", (char*)".font", XrmoptionSepArg, 0 },
    { (char*)"-fg", (char*)".foreground", XrmoptionSepArg, 0 },
    { (char*)"-foreground", (char*)".foreground", XrmoptionSepArg, 0 },
    { (char*)"-bg", (char*)".background", XrmoptionSepArg, 0 },
    { (char*)"-background", (char*)".background", XrmoptionSepArg, 0 },
    { (char*)"-cr", (char*)".cursorColor", XrmoptionSepArg, 0 },
    { (char*)"-sl", (char*)".saveLines", XrmoptionSepArg, 0 },
    { (char*)"-ls", (char*)".loginShell", XrmoptionNoArg, (XPointer)"true" },
    { (char*)"+ls", (char*)".loginShell", XrmoptionNoArg, (XPointer)"false" },
    { (char*)"-nomenu", (char*)".menuBar", XrmoptionNoArg, (XPointer)"false" },
    { (char*)"-vb", (char*)".bell", XrmoptionNoArg, (XPointer)"visual" },
    { (char*)"-renderer", (char*)".renderer", XrmoptionSepArg, 0 },
    { (char*)"-dynbg", (char*)".dynamicBackground", XrmoptionSepArg, 0 },
    { (char*)"-text", (char*)".textRendering", XrmoptionSepArg, 0 },
    { (char*)"-xrm", 0, XrmoptionResArg, 0 }
};
/* XtAppInitialize handles -xrm itself; only merge_settings needs it. */
const int k_noptions = (int)(sizeof(k_options) / sizeof(k_options[0])) - 1;

class App : public ttrt::SettingsHost, public tt::PaneHost {
public:
    XtAppContext ctx;
    Widget top, main_w, bar;
    tt::TermPane* pane;
    Resources res;
    char** cmd;                 /* -e command, or null for the shell */

    App() : ctx(0), top(0), main_w(0), bar(0), pane(0), cmd(0), settings(0), popup(0),
            base_w(0), base_h(0)
    {
    }

    /* tt::PaneHost */
    void pane_exited(tt::TermPane*, int code) { exit(code); }
    void pane_title(tt::TermPane*, tl::TitleKind kind, const char* utf8);
    void pane_context_menu(tt::TermPane*, XEvent* ev) { post_popup(ev); }

    /* Settings: the window's values, the dialog, applying changes. */
    ttr::Str cur[ttrt::S_COUNT];
    ttrt::SettingsDialog* settings;
    Widget popup;               /* Ctrl+button 3 */
    int base_w, base_h;         /* window size that is not cells */
    const char* current(int setting) const { return cur[setting].c_str(); }
    void apply(const ttr::Str* values, const bool* changed);
    void update_size_hints();
    void resize_to(int cols, int rows);
    void show_menu_bar(bool on);
    void post_popup(XEvent* ev);
    Time now() const { return XtLastTimestampProcessed(XtDisplay(top)); }
};

uint32_t parse_color(Display* dpy, const char* name, uint32_t dflt)
{
    XColor c;
    if (!name || !XParseColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)), name, &c))
        return dflt;
    return ((uint32_t)(c.red >> 8) << 16) | ((uint32_t)(c.green >> 8) << 8) | (uint32_t)(c.blue >> 8);
}

/* The terminal's colours from the settings: the colour scheme (th's
 * text schemes, shared with ttrcode), under the text, background and
 * cursor colours set apart from their defaults. */
struct Colors {
    uint32_t fg, bg, cursor;
    bool have_cursor;
    uint32_t palette[16];
    bool have_palette;
};

bool set_apart(const ttr::Str* v, int setting)
{
    const char* d = ttrt::k_settings[setting].fallback;
    return !v[setting].empty() && !(d && strcmp(v[setting].c_str(), d) == 0);
}

Colors colors_of(Display* dpy, const ttr::Str* v)
{
    using namespace ttrt;
    Colors c;
    th::Scheme s;
    c.have_palette = !v[S_COLOR_SCHEME].empty() && th::find_scheme(v[S_COLOR_SCHEME].c_str(), &s);
    if (c.have_palette) {
        c.fg = (uint32_t)s.get(th::SC_FOREGROUND);
        c.bg = (uint32_t)s.get(th::SC_BACKGROUND);
        c.cursor = (uint32_t)s.get(th::SC_CURSOR);
        c.have_cursor = true;
        for (int i = 0; i < 16; i++)
            c.palette[i] = (uint32_t)s.ansi(i);
    } else {
        c.fg = parse_color(dpy, k_settings[S_FOREGROUND].fallback, 0xe5e5e5);
        c.bg = parse_color(dpy, k_settings[S_BACKGROUND].fallback, 0x000000);
        c.cursor = c.fg;
        c.have_cursor = false;
    }
    if (set_apart(v, S_FOREGROUND))
        c.fg = parse_color(dpy, v[S_FOREGROUND].c_str(), c.fg);
    if (set_apart(v, S_BACKGROUND))
        c.bg = parse_color(dpy, v[S_BACKGROUND].c_str(), c.bg);
    if (set_apart(v, S_CURSOR_COLOR)) {
        c.cursor = parse_color(dpy, v[S_CURSOR_COLOR].c_str(), c.fg);
        c.have_cursor = true;
    }
    return c;
}

tt::BellMode bell_mode(const char* s)
{
    return strcmp(s, "off") == 0 ? tt::BELL_OFF
         : strcmp(s, "visual") == 0 ? tt::BELL_VISUAL : tt::BELL_AUDIBLE;
}

void App::pane_title(tt::TermPane*, tl::TitleKind kind, const char* utf8)
{
    if (kind & tl::TITLE_WINDOW)
        XtVaSetValues(top, XmNtitle, utf8, (char*)0);
    if (kind & tl::TITLE_ICON)
        XtVaSetValues(top, XmNiconName, utf8, (char*)0);
}

/* ---- menus --------------------------------------------------------------------- */

void new_window_cb(Widget, XtPointer, XtPointer)
{
    if (fork() == 0) {
        execlp("ttrt", "ttrt", (char*)0);
        _exit(127);
    }
}

void close_cb(Widget, XtPointer data, XtPointer)
{
    App* a = (App*)data;
    a->pane->hangup(1000);
    exit(0);
}

void menu_item(Widget menu, const char* label, char mnemonic, const char* accel,
               XtCallbackProc cb, App* a)
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
    XtAddCallback(w, XmNactivateCallback, cb, (XtPointer)a);
}

void copy_cb(Widget, XtPointer data, XtPointer)
{
    App* a = (App*)data;
    a->pane->copy(a->now());
}

void paste_cb(Widget, XtPointer data, XtPointer)
{
    App* a = (App*)data;
    a->pane->paste(a->now());
}

void select_all_cb(Widget, XtPointer data, XtPointer)
{
    App* a = (App*)data;
    a->pane->select_all(a->now());
}

void clear_sb_cb(Widget, XtPointer data, XtPointer)
{
    ((App*)data)->pane->clear_scrollback();
}

void reset_cb(Widget, XtPointer data, XtPointer)
{
    ((App*)data)->pane->reset();
}

void settings_cb(Widget, XtPointer data, XtPointer);

Widget make_menu_bar(App* a, Widget parent)
{
    /* Alt+letter and F10 belong to the programs in the terminal (mc, ttrc,
     * emacs): no mnemonics on the menu titles, and no F10 to enter the
     * menu bar. The menus are for the mouse. */
    Arg bargs[2];
    XtSetArg(bargs[0], XmNpopupEnabled, False);
    /* An empty accelerator means Motif's default F10; this one is out of
     * the way. */
    XtSetArg(bargs[1], XmNmenuAccelerator, "Ctrl Shift<KeyUp>F12");
    Widget bar = XmCreateMenuBar(parent, (char*)"menuBar", bargs, 2);
    Widget file = XmCreatePulldownMenu(bar, (char*)"fileMenu", 0, 0);
    XtVaCreateManagedWidget("File", xmCascadeButtonWidgetClass, bar, XmNsubMenuId, file,
                            (char*)0);
    Widget nw = XtVaCreateManagedWidget("New Window", xmPushButtonWidgetClass, file,
                                        XmNmnemonic, 'N', (char*)0);
    XtAddCallback(nw, XmNactivateCallback, new_window_cb, a);
    XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, file, (char*)0);
    Widget cl = XtVaCreateManagedWidget("Close", xmPushButtonWidgetClass, file,
                                        XmNmnemonic, 'C', (char*)0);
    XtAddCallback(cl, XmNactivateCallback, close_cb, a);

    Widget edit = XmCreatePulldownMenu(bar, (char*)"editMenu", 0, 0);
    XtVaCreateManagedWidget("Edit", xmCascadeButtonWidgetClass, bar, XmNsubMenuId, edit,
                            (char*)0);
    menu_item(edit, "Copy", 'C', "Ctrl+Shift+C", copy_cb, a);
    menu_item(edit, "Paste", 'P', "Ctrl+Shift+V", paste_cb, a);
    menu_item(edit, "Select All", 'A', 0, select_all_cb, a);

    Widget term = XmCreatePulldownMenu(bar, (char*)"terminalMenu", 0, 0);
    XtVaCreateManagedWidget("Terminal", xmCascadeButtonWidgetClass, bar, XmNsubMenuId, term,
                            (char*)0);
    menu_item(term, "Clear Scrollback", 'C', 0, clear_sb_cb, a);
    menu_item(term, "Reset", 'R', 0, reset_cb, a);
    XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, term, (char*)0);
    menu_item(term, "Settings...", 'S', 0, settings_cb, a);
    XtManageChild(bar);
    return bar;
}

/* The same commands as a popup menu, for Ctrl+button 3. */
void App::post_popup(XEvent* ev)
{
    if (ev->type != ButtonPress)
        return;
    if (!popup) {
        popup = XmCreatePopupMenu(pane->grid(), (char*)"popupMenu", 0, 0);
        menu_item(popup, "New Window", 'N', 0, new_window_cb, this);
        XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, popup, (char*)0);
        menu_item(popup, "Copy", 'C', "Ctrl+Shift+C", copy_cb, this);
        menu_item(popup, "Paste", 'P', "Ctrl+Shift+V", paste_cb, this);
        menu_item(popup, "Select All", 'A', 0, select_all_cb, this);
        XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, popup, (char*)0);
        menu_item(popup, "Clear Scrollback", 'b', 0, clear_sb_cb, this);
        menu_item(popup, "Reset", 'R', 0, reset_cb, this);
        menu_item(popup, "Settings...", 'S', 0, settings_cb, this);
        XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, popup, (char*)0);
        menu_item(popup, "Close", 'l', 0, close_cb, this);
    }
    XmMenuPosition(popup, &ev->xbutton);
    XtManageChild(popup);
}

/* ---- settings ------------------------------------------------------------------ */

/* What is not cells (scroll bar, menu bar, margins) and the cell size,
 * so window managers resize in whole cells and show "80x24". */
void App::update_size_hints()
{
    int cw, ch;
    pane->cell_size(&cw, &ch);
    pane->extra_size(&base_w, &base_h);
    XtWidgetGeometry pref;
    if (bar && XtIsManaged(bar) &&
        (XtQueryGeometry(bar, 0, &pref) != XtGeometryNo || pref.height))
        base_h += pref.height;
    XtVaSetValues(top, XmNwidthInc, cw, XmNheightInc, ch,
                  XmNbaseWidth, base_w, XmNbaseHeight, base_h,
                  XmNminWidth, base_w + 10 * cw, XmNminHeight, base_h + 2 * ch, (char*)0);
}

/* Keep the size in cells when the cells or the menu bar change. */
void App::resize_to(int cols, int rows)
{
    update_size_hints();
    int cw, ch;
    pane->cell_size(&cw, &ch);
    XtVaSetValues(top, XmNwidth, (Dimension)(base_w + cols * cw),
                  XmNheight, (Dimension)(base_h + rows * ch), (char*)0);
}

void App::show_menu_bar(bool on)
{
    if (!bar || on == (XtIsManaged(bar) != False))
        return;
    int cols = pane->cols(), rows = pane->rows();
    unsigned char att = on ? XmATTACH_WIDGET : XmATTACH_FORM;
    XtVaSetValues(pane->widget(), XmNtopAttachment, att, XmNtopWidget, bar, (char*)0);
    if (on)
        XtManageChild(bar);
    else
        XtUnmanageChild(bar);
    if (XtIsRealized(top))
        resize_to(cols, rows);
}

void App::apply(const ttr::Str* v, const bool* changed)
{
    using namespace ttrt;
    Display* dpy = XtDisplay(top);
    if (changed[S_FONT]) {
        int cols = pane->cols(), rows = pane->rows();
        pane->set_font(v[S_FONT].c_str());
        resize_to(cols, rows);
    }
    if (changed[S_FOREGROUND] || changed[S_BACKGROUND] || changed[S_CURSOR_COLOR] ||
        changed[S_COLOR_SCHEME]) {
        Colors c = colors_of(dpy, v);
        pane->set_colors(c.fg, c.bg);
        pane->set_cursor_color(c.have_cursor, c.cursor);
        pane->set_palette(c.have_palette ? c.palette : 0);
    }
    if (changed[S_BOLD_IS_BRIGHT])
        pane->set_bold_is_bright(strcmp(v[S_BOLD_IS_BRIGHT].c_str(), "true") == 0);
    if (changed[S_BACKSPACE])
        pane->set_backspace_is_del(strcmp(v[S_BACKSPACE].c_str(), "bs") != 0);
    if (changed[S_BELL])
        pane->set_bell(bell_mode(v[S_BELL].c_str()));
    if (changed[S_MENU_BAR])
        show_menu_bar(strcmp(v[S_MENU_BAR].c_str(), "true") == 0);
    /* A background shows at once with the GL renderer; the renderer
     * itself applies to new windows. */
    if (changed[S_DYNAMIC_BACKGROUND])
        pane->set_dynamic_background(v[S_DYNAMIC_BACKGROUND].c_str());
    /* Smooth text shows at once with the GL renderer; a window that has
     * another one gets it when it is made. */
    if (changed[S_TEXT_RENDERING])
        pane->set_text_rendering(v[S_TEXT_RENDERING].c_str());
    /* Scrollback and the login shell apply to new windows. */
    for (int i = 0; i < S_COUNT; i++)
        if (changed[i])
            cur[i] = v[i];
}

void settings_cb(Widget, XtPointer data, XtPointer)
{
    App* a = (App*)data;
    if (!a->settings)
        a->settings = new ttrt::SettingsDialog(a->top, a);
    a->settings->show();
}

void delete_window_cb(Widget w, XtPointer data, XtPointer)
{
    close_cb(w, data, 0);
}

} /* namespace */

/* The settings file's lines (settings.h) as the application's resources,
 * over X resources, and then the command line again over them. */
void merge_settings(Widget top, int argc, char** argv)
{
    Display* dpy = XtDisplay(top);
    XrmDatabase db = XtDatabase(dpy);
    String app_name = 0, app_class = 0;
    XtGetApplicationNameAndClass(dpy, &app_name, &app_class);
    if (!app_name || strlen(app_name) > 100)
        return;

    ttr::SettingsFile file;
    ttr::Str path = ttrt::settings_path();
    if (!file.load(path.c_str()))
        return;
    bool any = false;
    for (int i = 0; i < file.count(); i++) {
        const char* name = file.name(i);
        if (strlen(name) > 100)
            continue;
        char spec[256];
        sprintf(spec, "%s.%s", app_name, name);
        XrmPutStringResource(&db, spec, file.value(i));
        any = true;
    }
    if (!any)
        return;

    int n = argc;
    XrmDatabase cmd = 0;
    XrmParseCommand(&cmd, k_options, (int)(sizeof(k_options) / sizeof(k_options[0])), app_name,
                    &n, argv);
    if (cmd)
        XrmCombineDatabase(cmd, &db, True);
}

int main(int argc, char** argv)
{
    App* a = new App();

    /* -e takes the rest of the command line, before Xt sees it. */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
            a->cmd = argv + i + 1;
            argv[i] = 0;
            argc = i;
            break;
        }
    }

    static String fallbacks[] = {
        (String)"*fontList: -*-helvetica-medium-r-normal--12-*-*-*-*-*-iso8859-1",
        (String)"*menuBar*fontList: -*-helvetica-bold-r-normal--12-*",
        (String)"*popupMenu*fontList: -*-helvetica-bold-r-normal--12-*",
        (String)"*settingsDialog*frame.title.fontList: -*-helvetica-bold-r-normal--12-*",
        (String)"*XmToggleButton.selectColor: #3c5a8c",
        /* Motif's 11 pixels are hard to grab: ttr's 17 (Windows 2000's 16
         * plus the bevel). ~/.Xresources still wins. */
        (String)"*XmScrollBar.width: 17",
        (String)"*XmScrollBar.height: 17",
        0
    };
    /* The command line as given, for merge_settings: Xt removes what it
     * parses. */
    char** argv_copy = new char*[argc + 1];
    for (int i = 0; i <= argc; i++)
        argv_copy[i] = i < argc ? argv[i] : 0;
    int argc_copy = argc;
    a->top = XtAppInitialize(&a->ctx, "TTRt", k_options, (Cardinal)k_noptions, &argc, argv,
                             fallbacks, 0, 0);
    th::set_program_path(argv_copy[0]);
    merge_settings(a->top, argc_copy, argv_copy);
    delete [] argv_copy;
    XtGetApplicationResources(a->top, &a->res, k_resources, XtNumber(k_resources), 0, 0);
    if (argc > 1) {
        fprintf(stderr, "usage: ttrt [-fn font] [-fg color] [-bg color] [-cr color] [-sl lines]\n"
                        "            [-ls] [-nomenu] [-vb] [-geometry WxH] [-title title]\n"
                        "            [-renderer auto|core|soft|gl] [-dynbg name]\n"
                        "            [-text sharp|smooth|auto]\n"
                        "            [-xrm resource] [-e command ...]\n");
        return 2;
    }

    Display* dpy = XtDisplay(a->top);

    /* The window's settings as the Settings dialog shows them. */
    {
        using namespace ttrt;
        char lines[32];
        sprintf(lines, "%d", a->res.scrollback);
        a->cur[S_FONT] = a->res.font ? a->res.font : "";
        a->cur[S_FOREGROUND] = a->res.foreground ? a->res.foreground : "";
        a->cur[S_BACKGROUND] = a->res.background ? a->res.background : "";
        a->cur[S_CURSOR_COLOR] = a->res.cursor_color ? a->res.cursor_color : "";
        a->cur[S_SAVE_LINES] = lines;
        a->cur[S_LOGIN_SHELL] = a->res.login_shell ? "true" : "false";
        a->cur[S_MENU_BAR] = a->res.menu_bar ? "true" : "false";
        a->cur[S_BOLD_IS_BRIGHT] = a->res.bold_is_bright ? "true" : "false";
        a->cur[S_BACKSPACE] = strcmp(a->res.backspace, "bs") == 0 ? "bs" : "del";
        a->cur[S_BELL] = a->res.bell ? a->res.bell : "audible";
        a->cur[S_COLOR_SCHEME] = a->res.color_scheme ? a->res.color_scheme : "";
        a->cur[S_RENDERER] = a->res.renderer ? a->res.renderer : "auto";
        a->cur[S_DYNAMIC_BACKGROUND] = a->res.dynamic_background ? a->res.dynamic_background : "";
        a->cur[S_TEXT_RENDERING] = a->res.text_rendering ? a->res.text_rendering : "auto";
    }

    a->main_w = XtVaCreateManagedWidget("main", xmFormWidgetClass, a->top, (char*)0);
    /* Always made, so Settings can bring it back; shown if set. */
    Widget bar = make_menu_bar(a, a->main_w);
    a->bar = bar;
    XtVaSetValues(bar, XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, (char*)0);
    bool show_bar = a->res.menu_bar != False;
    if (!show_bar)
        XtUnmanageChild(bar);

    static char fdirs[2048];
    tt::font_path(argv[0], TTR_DATADIR, fdirs, sizeof(fdirs));
    tt::PaneOptions o;
    o.font = a->res.font;
    o.fallback_fonts = fdirs;
    Colors colors = colors_of(dpy, a->cur);
    o.foreground = colors.fg;
    o.background = colors.bg;
    o.have_cursor_color = colors.have_cursor;
    o.cursor_color = colors.cursor;
    o.palette = colors.have_palette ? colors.palette : 0;
    o.scrollback = a->res.scrollback;
    o.bold_is_bright = a->res.bold_is_bright != False;
    o.backspace_is_del = strcmp(a->res.backspace, "bs") != 0;
    o.bell = bell_mode(a->cur[ttrt::S_BELL].c_str());
    o.renderer = a->cur[ttrt::S_RENDERER].c_str();
    o.dynamic_background = a->cur[ttrt::S_DYNAMIC_BACKGROUND].c_str();
    o.text_rendering = a->cur[ttrt::S_TEXT_RENDERING].c_str();
    o.argv = (const char* const*)a->cmd;
    o.login = a->res.login_shell != False;
    a->pane = new tt::TermPane(a->main_w, "terminal", o, a);
    XtVaSetValues(a->pane->widget(),
                  XmNtopAttachment, show_bar ? XmATTACH_WIDGET : XmATTACH_FORM,
                  XmNtopWidget, bar,
                  XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM,
                  XmNbottomAttachment, XmATTACH_FORM, (char*)0);
    XtVaSetValues(a->main_w, XmNinitialFocus, a->pane->widget(), (char*)0);

    Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XmAddWMProtocolCallback(a->top, wm_delete, delete_window_cb, (XtPointer)a);
    XtVaSetValues(a->top, XmNdeleteResponse, XmDO_NOTHING, (char*)0);

    /* Size in whole cells, set before realizing so that -geometry 80x24
     * means cells (as in xterm) and window managers resize by cells. */
    a->update_size_hints();

    XtRealizeWidget(a->top);
    XDefineCursor(dpy, XtWindow(a->pane->grid()), XCreateFontCursor(dpy, XC_xterm));
    a->pane->focus();
    /* The first resize callback may have come before realization ended. */
    a->pane->start();

    XtAppMainLoop(a->ctx);
    return 0;
}
