/*
 * ttrt, the ttr terminal (ttrt/DESIGN.md). One window: a menu bar, the
 * TwGrid showing a tl::Terminal, and a scroll bar for the scrollback,
 * running a shell on a tp::Pty. Its settings are its own (settings.h);
 * it does not need ttr.
 */
#include "settings.h"
#include "settingsdlg.h"
#include "source.h"

#include "../../tl/terminal.h"
#include "../../tp/tp.h"
#include "../../tw/grid_widget.h"
#include "../../tw/surface.h"
#include "core/settings.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <Xm/CascadeB.h>
#include <Xm/Form.h>
#include <Xm/MainW.h>
#include <Xm/Protocols.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/ScrollBar.h>
#include <Xm/Separator.h>

#if !defined(TTR_DATADIR)
#define TTR_DATADIR "/usr/share/ttr"
#endif

namespace {

/* Read at most this much per wakeup before letting X events through. */
const size_t k_read_chunk = 16384;
const size_t k_read_budget = 262144;

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
      ROFF(bell), (String)XtRString, TSD(S_BELL) }
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
    { (char*)"-xrm", 0, XrmoptionResArg, 0 }
};
/* XtAppInitialize handles -xrm itself; only merge_settings needs it. */
const int k_noptions = (int)(sizeof(k_options) / sizeof(k_options[0])) - 1;

class App;
const char* pick_term();
const char* pick_font(Display* dpy);
extern char g_terminfo_dir[512];

class Host : public tl::Host {
public:
    App* app;
    void write_pty(const uint8_t* data, size_t n);
    void bell();
    void title_changed(tl::TitleKind kind, const char* utf8);
};

void visual_bell_end(XtPointer data, XtIntervalId*);

class App : public ttrt::SettingsHost {
public:
    XtAppContext ctx;
    Widget top, main_w, bar, grid, scroll;
    tl::Terminal term;
    ttrt::TermSource src;
    Host host;
    tp::Pty pty;
    tp::Process proc;
    Resources res;
    char** cmd;                 /* -e command, or null for the shell */
    bool started;
    bool in_scroll_update;
    /* Bytes for the pty that it did not take yet. */
    tl::Buffer outq;
    size_t outq_off;
    XtInputId in_id, out_id;
    XtIntervalId bell_id;       /* the visual bell is showing */

    App() : ctx(0), top(0), main_w(0), bar(0), grid(0), scroll(0), src(term), cmd(0), started(false),
            in_scroll_update(false), outq_off(0), in_id(0), out_id(0), bell_id(0),
            selecting(false),
            sel_moved(false), click_count(0), last_click(0), last_click_col(-1),
            last_click_row(-1), drag_col(0), drag_row(0), autoscroll(0), autoscroll_id(0),
            a_clipboard(None), a_utf8(None), a_targets(None), a_text(None),
            font_buf(0), settings(0), popup(0), base_w(0), base_h(0)
    {
        host.app = this;
    }

    /* Selection with the mouse. */
    bool selecting;             /* button 1 is down for a selection */
    bool sel_moved;
    int click_count;
    Time last_click;
    int last_click_col, last_click_row;
    int drag_col, drag_row;
    int autoscroll;             /* -1 up, 1 down, 0 off */
    XtIntervalId autoscroll_id;
    /* What we offer as PRIMARY and CLIPBOARD. */
    tl::Buffer primary_text, clipboard_text;
    Atom a_clipboard, a_utf8, a_targets, a_text;

    void start_session(int cols, int rows);
    void own(Atom sel, Time t);
    void request_paste(Atom sel, Time t);
    void copy_selection(Atom sel, Time t);
    void select_at(TwGridCallbackStruct* cb);
    void autoscroll_step();
    void flush_output();
    void read_pty();
    void update_scrollbar();
    void handle_key(TwGridCallbackStruct* cb);
    void handle_pointer(TwGridCallbackStruct* cb);
    void session_ended();

    /* Settings: the window's values, the dialog, applying changes. */
    ttr::Str cur[ttrt::S_COUNT];
    char* font_buf;             /* the font name TwGrid has */
    ttrt::SettingsDialog* settings;
    Widget popup;               /* Ctrl+button 3 */
    int base_w, base_h;         /* window size that is not cells */
    const char* current(int setting) const { return cur[setting].c_str(); }
    void apply(const ttr::Str* values, const bool* changed);
    void set_font(const char* name);
    void update_size_hints();
    void resize_to(int cols, int rows);
    void show_menu_bar(bool on);
    void post_popup(XEvent* ev);
};


uint32_t parse_color(Display* dpy, const char* name, uint32_t dflt)
{
    XColor c;
    if (!name || !XParseColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)), name, &c))
        return dflt;
    return ((uint32_t)(c.red >> 8) << 16) | ((uint32_t)(c.green >> 8) << 8) | (uint32_t)(c.blue >> 8);
}

tl::Rgb to_rgb(uint32_t v)
{
    tl::Rgb c;
    c.r = (uint8_t)(v >> 16);
    c.g = (uint8_t)(v >> 8);
    c.b = (uint8_t)v;
    return c;
}

/* ---- pty output and input ---------------------------------------------------- */

void out_ready(XtPointer data, int*, XtInputId*)
{
    ((App*)data)->flush_output();
}

void in_ready(XtPointer data, int*, XtInputId*)
{
    ((App*)data)->read_pty();
}

void Host::write_pty(const uint8_t* data, size_t n)
{
    App* a = app;
    if (!a->pty.is_open())
        return;
    if (a->outq.size() == a->outq_off) {
        long w = a->pty.write(data, n);
        if (w < 0)
            return;
        data += w;
        n -= (size_t)w;
        if (!n)
            return;
    }
    a->outq.append(data, n);
    if (!a->out_id)
        a->out_id = XtAppAddInput(a->ctx, a->pty.fd(), (XtPointer)XtInputWriteMask, out_ready,
                                  (XtPointer)a);
}

void App::flush_output()
{
    while (outq_off < outq.size()) {
        long w = pty.write(outq.data() + outq_off, outq.size() - outq_off);
        if (w <= 0)
            break;
        outq_off += (size_t)w;
    }
    if (outq_off == outq.size()) {
        outq.clear();
        outq_off = 0;
        if (out_id) {
            XtRemoveInput(out_id);
            out_id = 0;
        }
    }
}

void Host::bell()
{
    App* a = app;
    const char* how = a->cur[ttrt::S_BELL].c_str();
    if (strcmp(how, "off") == 0)
        return;
    if (strcmp(how, "visual") != 0) {
        XBell(XtDisplay(a->top), 0);
        return;
    }
    /* Flash: reverse video for a moment, as xterm's visualBell. */
    if (a->bell_id)
        return;
    a->term.set_reverse_video(!a->term.modes().reverse);
    TwGridDamage(a->grid);
    a->bell_id = XtAppAddTimeOut(a->ctx, 100, visual_bell_end, (XtPointer)a);
}

void visual_bell_end(XtPointer data, XtIntervalId*)
{
    App* a = (App*)data;
    a->bell_id = 0;
    a->term.set_reverse_video(!a->term.modes().reverse);
    TwGridDamage(a->grid);
}

void Host::title_changed(tl::TitleKind kind, const char* utf8)
{
    if (kind & tl::TITLE_WINDOW)
        XtVaSetValues(app->top, XmNtitle, utf8, (char*)0);
    if (kind & tl::TITLE_ICON)
        XtVaSetValues(app->top, XmNiconName, utf8, (char*)0);
}

void App::read_pty()
{
    char buf[k_read_chunk];
    size_t total = 0;
    bool fed = false;
    while (total < k_read_budget) {
        long n = pty.read(buf, sizeof(buf));
        if (n > 0) {
            term.feed((const uint8_t*)buf, (size_t)n);
            total += (size_t)n;
            fed = true;
            continue;
        }
        if (n < 0) {
            session_ended();
            return;
        }
        break;
    }
    if (fed) {
        TwGridDamage(grid);
        update_scrollbar();
    }
}

void App::session_ended()
{
    if (in_id) {
        XtRemoveInput(in_id);
        in_id = 0;
    }
    proc.wait(2000);
    exit(proc.exited() ? proc.exit_code() : 0);
}

void App::start_session(int cols, int rows)
{
    int cw, ch;
    TwGridGetCellSize(grid, &cw, &ch);
    tp::WinSize ws;
    ws.cols = cols;
    ws.rows = rows;
    ws.width_px = cols * cw;
    ws.height_px = rows * ch;
    if (!pty.open(ws)) {
        fprintf(stderr, "ttrt: %s\n", pty.error());
        exit(1);
    }
    /* Environment of the child. */
    char windowid[48];
    sprintf(windowid, "WINDOWID=%lu", (unsigned long)XtWindow(top));
    Visual* vis = DefaultVisual(XtDisplay(top), DefaultScreen(XtDisplay(top)));
    bool truecolor = vis->c_class == TrueColor && DefaultDepth(XtDisplay(top), DefaultScreen(XtDisplay(top))) >= 15;
    char term_env[64];
    sprintf(term_env, "TERM=%s", pick_term());
    char terminfo_env[540];
    if (g_terminfo_dir[0])
        sprintf(terminfo_env, "TERMINFO=%s", g_terminfo_dir);
    else
        strcpy(terminfo_env, "COLUMNS");    /* nothing to set: unset a harmless name */
    const char* env[] = {
        term_env,
        terminfo_env,
        truecolor ? "COLORTERM=truecolor" : "COLORTERM",
        windowid,
        "COLUMNS", "LINES", "TERMCAP",
        0
    };
    tp::SpawnOptions o;
    o.argv = (const char* const*)cmd;
    o.login = res.login_shell && !cmd;
    o.env = env;
    if (!proc.spawn(pty, o)) {
        fprintf(stderr, "ttrt: %s\n", proc.error());
        exit(1);
    }
    in_id = XtAppAddInput(ctx, pty.fd(), (XtPointer)XtInputReadMask, in_ready, (XtPointer)this);
    started = true;
}

/* ---- scroll bar -------------------------------------------------------------- */

void App::update_scrollbar()
{
    if (!scroll)
        return;
    const tl::Grid& g = term.grid();
    int total = (int)g.total_rows();
    int rows = term.rows();
    int value = (int)(term.viewport_top() - g.base());
    if (total < rows)
        total = rows;
    if (value > total - rows)
        value = total - rows;
    in_scroll_update = true;
    XtVaSetValues(scroll, XmNmaximum, total, XmNsliderSize, rows, XmNvalue, value,
                  XmNpageIncrement, rows > 1 ? rows - 1 : 1, (char*)0);
    in_scroll_update = false;
}

void scroll_moved(Widget, XtPointer data, XtPointer call)
{
    App* a = (App*)data;
    if (a->in_scroll_update)
        return;
    XmScrollBarCallbackStruct* cb = (XmScrollBarCallbackStruct*)call;
    uint64_t target = a->term.grid().base() + (uint64_t)cb->value;
    long delta = (long)target - (long)a->term.viewport_top();
    a->term.scroll_viewport(delta);
    TwGridDamage(a->grid);
}

/* ---- keys and pointer ---------------------------------------------------------- */

unsigned mods_of(unsigned state)
{
    unsigned m = 0;
    if (state & ShiftMask) m |= tl::MOD_SHIFT;
    if (state & ControlMask) m |= tl::MOD_CTRL;
    if (state & Mod1Mask) m |= tl::MOD_ALT;
    return m;
}

int key_of(KeySym ks)
{
    switch (ks) {
    case XK_Return: return tl::KEY_ENTER;
    case XK_Tab: case XK_ISO_Left_Tab: return tl::KEY_TAB;
    case XK_BackSpace: return tl::KEY_BACKSPACE;
    case XK_Escape: return tl::KEY_ESCAPE;
    case XK_Up: case XK_KP_Up: return tl::KEY_UP;
    case XK_Down: case XK_KP_Down: return tl::KEY_DOWN;
    case XK_Right: case XK_KP_Right: return tl::KEY_RIGHT;
    case XK_Left: case XK_KP_Left: return tl::KEY_LEFT;
    case XK_Home: case XK_KP_Home: return tl::KEY_HOME;
    case XK_End: case XK_KP_End: return tl::KEY_END;
    case XK_Insert: case XK_KP_Insert: return tl::KEY_INSERT;
    case XK_Delete: case XK_KP_Delete: return tl::KEY_DELETE;
    case XK_Prior: case XK_KP_Prior: return tl::KEY_PAGE_UP;
    case XK_Next: case XK_KP_Next: return tl::KEY_PAGE_DOWN;
    case XK_KP_Begin: return tl::KEY_BEGIN;
    case XK_KP_Enter: return tl::KEY_KP_ENTER;
    case XK_KP_Decimal: return tl::KEY_KP_DECIMAL;
    case XK_KP_Divide: return tl::KEY_KP_DIVIDE;
    case XK_KP_Multiply: return tl::KEY_KP_MULTIPLY;
    case XK_KP_Subtract: return tl::KEY_KP_SUBTRACT;
    case XK_KP_Add: return tl::KEY_KP_ADD;
    case XK_KP_Equal: return tl::KEY_KP_EQUAL;
    case XK_KP_Separator: return tl::KEY_KP_SEPARATOR;
    default: break;
    }
    if (ks >= XK_F1 && ks <= XK_F20)
        return tl::KEY_F1 + (int)(ks - XK_F1);
    if (ks >= XK_KP_0 && ks <= XK_KP_9)
        return tl::KEY_KP_0 + (int)(ks - XK_KP_0);
    return tl::KEY_NONE;
}

void App::handle_key(TwGridCallbackStruct* cb)
{
    KeySym ks = cb->keysym;
    unsigned mods = mods_of(cb->state);
    /* Scrollback keys stay in the terminal window. */
    if (mods == tl::MOD_SHIFT && (ks == XK_Prior || ks == XK_Next)) {
        term.scroll_viewport(ks == XK_Prior ? -(term.rows() - 1) : term.rows() - 1);
        TwGridDamage(grid);
        update_scrollbar();
        return;
    }
    if (ks == XK_Shift_L || ks == XK_Shift_R || ks == XK_Control_L || ks == XK_Control_R ||
        ks == XK_Alt_L || ks == XK_Alt_R || ks == XK_Meta_L || ks == XK_Meta_R ||
        ks == XK_Caps_Lock || ks == XK_Num_Lock || ks == XK_Super_L || ks == XK_Super_R)
        return;
    Time t = cb->event ? cb->event->xkey.time : CurrentTime;
    if (mods == (tl::MOD_CTRL | tl::MOD_SHIFT) && (ks == XK_C || ks == XK_c)) {
        copy_selection(a_clipboard, t);
        return;
    }
    if (mods == (tl::MOD_CTRL | tl::MOD_SHIFT) && (ks == XK_V || ks == XK_v)) {
        request_paste(a_clipboard, t);
        return;
    }
    if (mods == tl::MOD_SHIFT && (ks == XK_Insert || ks == XK_KP_Insert)) {
        request_paste(XA_PRIMARY, t);
        return;
    }
    int k = key_of(ks);
    bool sent = false;
    if (k != tl::KEY_NONE)
        sent = term.key(k, mods);
    if (!sent && cb->text_len > 0)
        term.text_input(cb->text, (size_t)cb->text_len, mods & tl::MOD_ALT);
    else if (!sent)
        return;
    /* Typing brings the view back to the bottom. */
    if (!term.viewport_at_bottom()) {
        term.scroll_to_bottom();
        TwGridDamage(grid);
        update_scrollbar();
    }
}

/* ---- selections and the clipboard ------------------------------------------ */

App* g_sel_app;                 /* Xt selection procedures get only the widget */

/* UTF-8 to Latin-1 for the STRING target; other characters become '?'. */
void to_latin1(const char* in, size_t n, tl::Buffer& out)
{
    for (size_t i = 0; i < n;) {
        unsigned char c = (unsigned char)in[i];
        uint32_t cp;
        int len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xe0) == 0xc0 && i + 1 < n) { cp = ((c & 0x1fu) << 6) | (in[i + 1] & 0x3f); len = 2; }
        else if ((c & 0xf0) == 0xe0) { cp = 0x10000; len = 3; }
        else if ((c & 0xf8) == 0xf0) { cp = 0x10000; len = 4; }
        else { cp = '?'; len = 1; }
        out.append_byte((uint8_t)(cp < 256 ? cp : '?'));
        i += (size_t)len;
    }
}

Boolean convert_sel(Widget, Atom* sel, Atom* target, Atom* type, XtPointer* value,
                    unsigned long* length, int* format)
{
    App* a = g_sel_app;
    tl::Buffer& src = *sel == XA_PRIMARY ? a->primary_text : a->clipboard_text;
    if (*target == a->a_targets) {
        Atom* t = (Atom*)XtMalloc(4 * sizeof(Atom));
        t[0] = a->a_targets;
        t[1] = a->a_utf8;
        t[2] = XA_STRING;
        t[3] = a->a_text;
        *value = (XtPointer)t;
        *length = 4;
        *type = XA_ATOM;
        *format = 32;
        return True;
    }
    tl::Buffer conv;
    const tl::Buffer* data = &src;
    if (*target == a->a_utf8) {
        *type = a->a_utf8;
    } else if (*target == XA_STRING || *target == a->a_text) {
        to_latin1(src.c_str(), src.size(), conv);
        data = &conv;
        *type = XA_STRING;
    } else {
        return False;
    }
    char* v = XtMalloc((Cardinal)(data->size() + 1));
    memcpy(v, data->data(), data->size());
    v[data->size()] = 0;
    *value = (XtPointer)v;
    *length = data->size();
    *format = 8;
    return True;
}

void lose_sel(Widget, Atom* sel)
{
    App* a = g_sel_app;
    if (*sel == XA_PRIMARY) {
        /* Someone else selected something: drop our highlight, as xterm does. */
        a->primary_text.clear();
        a->term.select_clear();
        TwGridDamage(a->grid);
    } else {
        a->clipboard_text.clear();
    }
}

void App::own(Atom sel, Time t)
{
    if (!XtOwnSelection(grid, sel, t, convert_sel, lose_sel, 0)) {
        if (sel == XA_PRIMARY)
            primary_text.clear();
        else
            clipboard_text.clear();
    }
}

void App::copy_selection(Atom sel, Time t)
{
    tl::Buffer& dst = sel == XA_PRIMARY ? primary_text : clipboard_text;
    dst.clear();
    if (!term.selection_text(dst) || dst.size() == 0)
        return;
    own(sel, t);
}

void after_paste(App* a)
{
    if (!a->term.viewport_at_bottom()) {
        a->term.scroll_to_bottom();
        TwGridDamage(a->grid);
        a->update_scrollbar();
    }
}

/* The second try: STRING, Latin-1 by definition. */
void got_paste_string(Widget, XtPointer data, Atom*, Atom* type, XtPointer value,
                      unsigned long* length, int* format)
{
    App* a = (App*)data;
    if (value && *format == 8 && (*type == XA_STRING || *type == a->a_text)) {
        tl::Buffer u;
        const unsigned char* p = (const unsigned char*)value;
        for (unsigned long i = 0; i < *length; i++)
            u.append_utf8(p[i]);
        a->term.paste(u.c_str(), u.size());
        after_paste(a);
    }
    if (value)
        XtFree((char*)value);
}

/* The first try: UTF8_STRING. Owners that do not know it (IRIX's own
 * clients) refuse, and then STRING is asked for. */
void got_paste(Widget w, XtPointer data, Atom* sel, Atom* type, XtPointer value,
               unsigned long* length, int* format)
{
    App* a = (App*)data;
    if (value && *format == 8 && *type == a->a_utf8) {
        a->term.paste((const char*)value, (size_t)*length);
        after_paste(a);
    } else if (value && *format == 8 && *type == XA_STRING) {
        got_paste_string(w, data, sel, type, value, length, format);   /* frees value */
        return;
    } else {
        XtGetSelectionValue(w, *sel, XA_STRING, got_paste_string, data,
                            XtLastTimestampProcessed(XtDisplay(w)));
    }
    if (value)
        XtFree((char*)value);
}

void App::request_paste(Atom sel, Time t)
{
    XtGetSelectionValue(grid, sel, a_utf8, got_paste, (XtPointer)this, t);
}

/* ---- mouse --------------------------------------------------------------------- */

void autoscroll_timeout(XtPointer data, XtIntervalId*)
{
    App* a = (App*)data;
    a->autoscroll_id = 0;
    a->autoscroll_step();
}

void App::autoscroll_step()
{
    if (!selecting || !autoscroll)
        return;
    term.scroll_viewport(autoscroll);
    int row = autoscroll < 0 ? 0 : term.rows() - 1;
    term.select_extend(term.viewport_top() + (uint64_t)row, autoscroll < 0 ? 0 : term.cols() - 1);
    TwGridDamage(grid);
    update_scrollbar();
    autoscroll_id = XtAppAddTimeOut(ctx, 60, autoscroll_timeout, (XtPointer)this);
}

void App::select_at(TwGridCallbackStruct* cb)
{
    uint64_t row = term.viewport_top() + (uint64_t)cb->row;
    term.select_extend(row, cb->col);
    TwGridDamage(grid);
}

unsigned pointer_mods(unsigned state)
{
    return mods_of(state);
}

void App::handle_pointer(TwGridCallbackStruct* cb)
{
    unsigned state = cb->state;
    Time t = cb->event ? cb->event->xbutton.time : CurrentTime;
    int cw, ch;
    TwGridGetCellSize(grid, &cw, &ch);

    /* Ctrl+button 3: the menu, as a popup (the menu bar may be hidden). */
    if (cb->reason == TW_CR_BUTTON_PRESS && cb->button == 3 && (state & ControlMask) &&
        cb->event && !selecting) {
        post_popup(cb->event);
        return;
    }

    /* The application wants the mouse, unless Shift is held. */
    if (term.mouse_reporting() && !(state & ShiftMask) && !selecting) {
        tl::MouseAction act = cb->reason == TW_CR_BUTTON_PRESS ? tl::MOUSE_PRESS
                            : cb->reason == TW_CR_BUTTON_RELEASE ? tl::MOUSE_RELEASE
                            : tl::MOUSE_MOTION;
        term.mouse(act, cb->button, cb->col, cb->row, pointer_mods(state),
                   cb->x > 2 ? cb->x - 2 : 0, cb->y > 2 ? cb->y - 2 : 0);
        return;
    }

    if (cb->reason == TW_CR_BUTTON_PRESS) {
        switch (cb->button) {
        case 1: {
            bool same = cb->col == last_click_col && cb->row == last_click_row;
            Time multi = (Time)XtGetMultiClickTime(XtDisplay(grid));
            click_count = same && t - last_click < multi ? click_count % 3 + 1 : 1;
            last_click = t;
            last_click_col = cb->col;
            last_click_row = cb->row;
            tl::SelectMode mode = click_count == 3 ? tl::SELECT_LINE
                                : click_count == 2 ? tl::SELECT_WORD : tl::SELECT_CHAR;
            term.select_start(term.viewport_top() + (uint64_t)cb->row, cb->col, mode);
            selecting = true;
            sel_moved = click_count > 1;
            drag_col = cb->col;
            drag_row = cb->row;
            TwGridDamage(grid);
            break;
        }
        case 2:
            request_paste(XA_PRIMARY, t);
            break;
        case 3:
            if (term.has_selection()) {
                select_at(cb);
                copy_selection(XA_PRIMARY, t);
            }
            break;
        case 4: case 5:
            if (term.alt_screen()) {
                /* Wheel in full-screen programs: arrow keys (alternate scroll). */
                for (int i = 0; i < 3; i++)
                    term.key(cb->button == 4 ? tl::KEY_UP : tl::KEY_DOWN, 0);
            } else {
                term.scroll_viewport(cb->button == 4 ? -3 : 3);
                TwGridDamage(grid);
                update_scrollbar();
            }
            break;
        default:
            break;
        }
        return;
    }
    if (cb->reason == TW_CR_MOTION) {
        if (!selecting || !(state & Button1Mask))
            return;
        int grid_h = term.rows() * ch + 4;
        autoscroll = cb->y < 0 ? -1 : cb->y >= grid_h ? 1 : 0;
        if (autoscroll && !autoscroll_id)
            autoscroll_step();
        if (cb->col != drag_col || cb->row != drag_row) {
            sel_moved = true;
            drag_col = cb->col;
            drag_row = cb->row;
            select_at(cb);
        }
        return;
    }
    if (cb->reason == TW_CR_BUTTON_RELEASE && cb->button == 1 && selecting) {
        selecting = false;
        autoscroll = 0;
        if (autoscroll_id) {
            XtRemoveTimeOut(autoscroll_id);
            autoscroll_id = 0;
        }
        if (!sel_moved) {
            /* A plain click clears the selection. */
            term.select_clear();
            TwGridDamage(grid);
            return;
        }
        copy_selection(XA_PRIMARY, t);
    }
}

void input_cb(Widget, XtPointer data, XtPointer call)
{
    ((App*)data)->handle_key((TwGridCallbackStruct*)call);
}

void pointer_cb(Widget, XtPointer data, XtPointer call)
{
    ((App*)data)->handle_pointer((TwGridCallbackStruct*)call);
}

void focus_cb(Widget, XtPointer data, XtPointer call)
{
    App* a = (App*)data;
    TwGridCallbackStruct* cb = (TwGridCallbackStruct*)call;
    a->term.focus(cb->reason == TW_CR_FOCUS_IN);
}

void resize_cb(Widget, XtPointer data, XtPointer call)
{
    App* a = (App*)data;
    TwGridCallbackStruct* cb = (TwGridCallbackStruct*)call;
    if (!XtIsRealized(a->top))
        return;
    a->term.resize(cb->cols, cb->rows);
    if (!a->started) {
        a->start_session(cb->cols, cb->rows);
    } else {
        int cw, ch;
        TwGridGetCellSize(a->grid, &cw, &ch);
        tp::WinSize ws;
        ws.cols = cb->cols;
        ws.rows = cb->rows;
        ws.width_px = cb->cols * cw;
        ws.height_px = cb->rows * ch;
        a->pty.resize(ws);
    }
    TwGridDamage(a->grid);
    a->update_scrollbar();
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
    a->proc.signal_group(SIGHUP);
    a->proc.wait(1000);
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
    a->copy_selection(a->a_clipboard, XtLastTimestampProcessed(XtDisplay(a->top)));
}

void paste_cb(Widget, XtPointer data, XtPointer)
{
    App* a = (App*)data;
    a->request_paste(a->a_clipboard, XtLastTimestampProcessed(XtDisplay(a->top)));
}

void select_all_cb(Widget, XtPointer data, XtPointer)
{
    App* a = (App*)data;
    a->term.select_all();
    a->copy_selection(XA_PRIMARY, XtLastTimestampProcessed(XtDisplay(a->top)));
    TwGridDamage(a->grid);
}

void clear_sb_cb(Widget, XtPointer data, XtPointer)
{
    App* a = (App*)data;
    a->term.clear_scrollback();
    TwGridDamage(a->grid);
    a->update_scrollbar();
}

void reset_cb(Widget, XtPointer data, XtPointer)
{
    App* a = (App*)data;
    a->term.reset();
    TwGridDamage(a->grid);
    a->update_scrollbar();
}

void settings_cb(Widget, XtPointer data, XtPointer);

Widget make_menu_bar(App* a, Widget parent)
{
    Widget bar = XmCreateMenuBar(parent, (char*)"menuBar", 0, 0);
    Widget file = XmCreatePulldownMenu(bar, (char*)"fileMenu", 0, 0);
    XtVaCreateManagedWidget("File", xmCascadeButtonWidgetClass, bar, XmNsubMenuId, file,
                            XmNmnemonic, 'F', (char*)0);
    Widget nw = XtVaCreateManagedWidget("New Window", xmPushButtonWidgetClass, file,
                                        XmNmnemonic, 'N', (char*)0);
    XtAddCallback(nw, XmNactivateCallback, new_window_cb, a);
    XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, file, (char*)0);
    Widget cl = XtVaCreateManagedWidget("Close", xmPushButtonWidgetClass, file,
                                        XmNmnemonic, 'C', (char*)0);
    XtAddCallback(cl, XmNactivateCallback, close_cb, a);

    Widget edit = XmCreatePulldownMenu(bar, (char*)"editMenu", 0, 0);
    XtVaCreateManagedWidget("Edit", xmCascadeButtonWidgetClass, bar, XmNsubMenuId, edit,
                            XmNmnemonic, 'E', (char*)0);
    menu_item(edit, "Copy", 'C', "Ctrl+Shift+C", copy_cb, a);
    menu_item(edit, "Paste", 'P', "Ctrl+Shift+V", paste_cb, a);
    menu_item(edit, "Select All", 'A', 0, select_all_cb, a);

    Widget term = XmCreatePulldownMenu(bar, (char*)"terminalMenu", 0, 0);
    XtVaCreateManagedWidget("Terminal", xmCascadeButtonWidgetClass, bar, XmNsubMenuId, term,
                            XmNmnemonic, 'T', (char*)0);
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
        popup = XmCreatePopupMenu(grid, (char*)"popupMenu", 0, 0);
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
    TwGridGetCellSize(grid, &cw, &ch);
    XtWidgetGeometry pref;
    base_w = 4;
    base_h = 4;                 /* the grid's margins */
    if (XtQueryGeometry(scroll, 0, &pref) != XtGeometryNo || pref.width)
        base_w += pref.width;
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
    TwGridGetCellSize(grid, &cw, &ch);
    XtVaSetValues(top, XmNwidth, (Dimension)(base_w + cols * cw),
                  XmNheight, (Dimension)(base_h + rows * ch), (char*)0);
}

void App::show_menu_bar(bool on)
{
    if (!bar || on == (XtIsManaged(bar) != False))
        return;
    int cols = term.cols(), rows = term.rows();
    unsigned char att = on ? XmATTACH_WIDGET : XmATTACH_FORM;
    XtVaSetValues(grid, XmNtopAttachment, att, XmNtopWidget, bar, (char*)0);
    XtVaSetValues(scroll, XmNtopAttachment, att, XmNtopWidget, bar, (char*)0);
    if (on)
        XtManageChild(bar);
    else
        XtUnmanageChild(bar);
    if (XtIsRealized(top))
        resize_to(cols, rows);
}

/* name, or the best font the server has when it is empty. TwGrid keeps
 * the pointer, so the copy lives until the next one. */
void App::set_font(const char* name)
{
    const char* f = name && *name ? name : pick_font(XtDisplay(top));
    char* copy = (char*)malloc(strlen(f) + 1);
    strcpy(copy, f);
    XtVaSetValues(grid, TwNfont, copy, (char*)0);
    free(font_buf);
    font_buf = copy;
}

void App::apply(const ttr::Str* v, const bool* changed)
{
    using namespace ttrt;
    Display* dpy = XtDisplay(top);
    if (changed[S_FONT]) {
        int cols = term.cols(), rows = term.rows();
        set_font(v[S_FONT].c_str());
        resize_to(cols, rows);
    }
    if (changed[S_FOREGROUND] || changed[S_BACKGROUND]) {
        uint32_t fg = parse_color(dpy, v[S_FOREGROUND].c_str(), 0xe5e5e5);
        uint32_t bg = parse_color(dpy, v[S_BACKGROUND].c_str(), 0x000000);
        term.set_default_colors(to_rgb(fg), to_rgb(bg));
    }
    if (changed[S_CURSOR_COLOR]) {
        if (v[S_CURSOR_COLOR].empty())
            src.clear_cursor_color();
        else
            src.set_cursor_color(parse_color(dpy, v[S_CURSOR_COLOR].c_str(), 0xe5e5e5));
    }
    if (changed[S_BOLD_IS_BRIGHT])
        term.set_bold_is_bright(strcmp(v[S_BOLD_IS_BRIGHT].c_str(), "true") == 0);
    if (changed[S_BACKSPACE])
        term.set_backspace_is_del(strcmp(v[S_BACKSPACE].c_str(), "bs") != 0);
    if (changed[S_MENU_BAR])
        show_menu_bar(strcmp(v[S_MENU_BAR].c_str(), "true") == 0);
    /* Scrollback and the login shell apply to new windows. */
    for (int i = 0; i < S_COUNT; i++)
        if (changed[i])
            cur[i] = v[i];
    TwGridDamage(grid);
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

/* The directory pick_term() found a private entry in ($TERMINFO or
 * ~/.terminfo), or "" for the system's. */
char g_terminfo_dir[512];

/* The first TERM name this system's terminfo knows: ttrt's own entry,
 * then xterm-256color, then xterm (IRIX 6.5 has only xterm). */
const char* pick_term()
{
    g_terminfo_dir[0] = 0;
    static const char* const names[] = { "ttrt", "xterm-256color", "xterm", 0 };
    char home_ti[512];
    const char* home = getenv("HOME");
    home_ti[0] = 0;
    if (home && strlen(home) < 400)
        sprintf(home_ti, "%s/.terminfo", home);
    const char* dirs[] = { getenv("TERMINFO"), home_ti, "/usr/share/terminfo",
                           "/usr/share/lib/terminfo", "/usr/lib/terminfo", "/etc/terminfo",
                           "/lib/terminfo", 0 };
    for (int n = 0; names[n]; n++) {
        for (int d = 0; d < 7; d++) {
            if (!dirs[d] || !dirs[d][0])
                continue;
            char path[600];
            /* The usual layout is x/xterm; macOS uses hex: 78/xterm. */
            sprintf(path, "%s/%c/%s", dirs[d], names[n][0], names[n]);
            bool found = access(path, R_OK) == 0;
            if (!found) {
                sprintf(path, "%s/%02x/%s", dirs[d], (unsigned char)names[n][0], names[n]);
                found = access(path, R_OK) == 0;
            }
            if (found) {
                /* SVR4 curses (IRIX) reads only $TERMINFO and the system
                 * directory, not ~/.terminfo: pass a private one on. */
                if (d < 2)
                    sprintf(g_terminfo_dir, "%.500s", dirs[d]);
                return names[n];
            }
        }
    }
    return "xterm";
}

/* Where the bundled Unicode fonts are: next to the program in a source
 * tree (build/ttrt -> third_party/misc-misc), else the installed copy. */
void font_dirs(const char* argv0, char* out, size_t len)
{
    char prog_dir[600];
    prog_dir[0] = 0;
    if (strchr(argv0, '/')) {
        strncpy(prog_dir, argv0, sizeof(prog_dir) - 1);
        prog_dir[sizeof(prog_dir) - 1] = 0;
        char* slash = strrchr(prog_dir, '/');
        *slash = 0;
    } else {
        const char* path = getenv("PATH");
        while (path && *path) {
            const char* colon = strchr(path, ':');
            size_t l = colon ? (size_t)(colon - path) : strlen(path);
            char cand[700];
            if (l < 600) {
                memcpy(cand, path, l);
                sprintf(cand + l, "/%.60s", argv0);
                if (access(cand, X_OK) == 0) {
                    memcpy(prog_dir, path, l);
                    prog_dir[l] = 0;
                    break;
                }
            }
            if (!colon)
                break;
            path = colon + 1;
        }
    }
    const char* env = getenv("TTRT_FONTS");
    if (strlen(prog_dir) > 400)
        prog_dir[0] = 0;
    sprintf(out, "%.200s%s%.400s/../third_party/misc-misc:%.400s/../share/ttr/fonts:%.200s/fonts",
            env ? env : "", env ? ":" : "", prog_dir[0] ? prog_dir : ".",
            prog_dir[0] ? prog_dir : ".", TTR_DATADIR);
    (void)len;
}

const char* pick_font(Display* dpy)
{
    for (int i = 0; ttrt::k_auto_fonts[i]; i++) {
        int n = 0;
        char** names = XListFonts(dpy, ttrt::k_auto_fonts[i], 1, &n);
        if (names)
            XFreeFontNames(names);
        if (n > 0)
            return ttrt::k_auto_fonts[i];
    }
    return "fixed";
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
    merge_settings(a->top, argc_copy, argv_copy);
    delete [] argv_copy;
    XtGetApplicationResources(a->top, &a->res, k_resources, XtNumber(k_resources), 0, 0);
    if (argc > 1) {
        fprintf(stderr, "usage: ttrt [-fn font] [-fg color] [-bg color] [-cr color] [-sl lines]\n"
                        "            [-ls] [-nomenu] [-vb] [-geometry WxH] [-title title]\n"
                        "            [-xrm resource] [-e command ...]\n");
        return 2;
    }

    Display* dpy = XtDisplay(a->top);
    uint32_t fg = parse_color(dpy, a->res.foreground, 0xe5e5e5);
    uint32_t bg = parse_color(dpy, a->res.background, 0x000000);
    a->term.init(80, 24, (size_t)(a->res.scrollback > 0 ? a->res.scrollback : 0));
    a->term.set_default_colors(to_rgb(fg), to_rgb(bg));
    if (a->res.cursor_color && a->res.cursor_color[0])
        a->src.set_cursor_color(parse_color(dpy, a->res.cursor_color, fg));
    a->term.set_host(&a->host);
    a->term.set_bold_is_bright(a->res.bold_is_bright != False);
    a->term.set_backspace_is_del(strcmp(a->res.backspace, "bs") != 0);

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

    a->scroll = XtVaCreateManagedWidget("scrollBar", xmScrollBarWidgetClass, a->main_w,
                                        XmNorientation, XmVERTICAL,
                                        XmNtraversalOn, False,
                                        XmNrightAttachment, XmATTACH_FORM,
                                        XmNbottomAttachment, XmATTACH_FORM,
                                        XmNtopAttachment, show_bar ? XmATTACH_WIDGET : XmATTACH_FORM,
                                        XmNtopWidget, bar,
                                        XmNminimum, 0, XmNmaximum, 24, XmNsliderSize, 24,
                                        (char*)0);
    XtAddCallback(a->scroll, XmNvalueChangedCallback, scroll_moved, a);
    XtAddCallback(a->scroll, XmNdragCallback, scroll_moved, a);

    /* The font setting (-fn) wins; otherwise the best font the server has. */
    const char* font = a->res.font && a->res.font[0] ? a->res.font : pick_font(dpy);
    a->font_buf = (char*)malloc(strlen(font) + 1);
    strcpy(a->font_buf, font);

    Arg args[16];
    Cardinal n = 0;
    static char fdirs[2048];
    font_dirs(argv[0], fdirs, sizeof(fdirs));
    XtSetArg(args[n], TwNfont, a->font_buf); n++;
    XtSetArg(args[n], TwNfallbackFonts, fdirs); n++;
    XtSetArg(args[n], TwNsource, (tw::Source*)&a->src); n++;
    XtSetArg(args[n], XmNtopAttachment, show_bar ? XmATTACH_WIDGET : XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNtopWidget, bar); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNrightWidget, a->scroll); n++;
    a->grid = TwCreateGrid(a->main_w, "grid", args, n);
    XtAddCallback(a->grid, TwNinputCallback, input_cb, a);
    XtAddCallback(a->grid, TwNpointerCallback, pointer_cb, a);
    XtAddCallback(a->grid, TwNresizeCallback, resize_cb, a);
    XtAddCallback(a->grid, TwNfocusCallback, focus_cb, a);
    XtManageChild(a->grid);
    XtVaSetValues(a->main_w, XmNinitialFocus, a->grid, (char*)0);

    Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XmAddWMProtocolCallback(a->top, wm_delete, delete_window_cb, (XtPointer)a);
    XtVaSetValues(a->top, XmNdeleteResponse, XmDO_NOTHING, (char*)0);

    /* Size in whole cells, set before realizing so that -geometry 80x24
     * means cells (as in xterm) and window managers resize by cells. */
    a->update_size_hints();

    g_sel_app = a;
    a->a_clipboard = XInternAtom(dpy, "CLIPBOARD", False);
    a->a_utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    a->a_targets = XInternAtom(dpy, "TARGETS", False);
    a->a_text = XInternAtom(dpy, "TEXT", False);

    XtRealizeWidget(a->top);
    XDefineCursor(dpy, XtWindow(a->grid), XCreateFontCursor(dpy, XC_xterm));
    XmProcessTraversal(a->grid, XmTRAVERSE_CURRENT);

    /* The first resize callback may have come before realization ended. */
    if (!a->started) {
        int c2, r2;
        tw::Surface* s = TwGridSurface(a->grid);
        c2 = s->fit_cols();
        r2 = s->fit_rows();
        a->term.resize(c2, r2);
        a->start_session(c2, r2);
        a->update_scrollbar();
    }

    XtAppMainLoop(a->ctx);
    return 0;
}
