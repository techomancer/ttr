#include "pane.h"

#include "../tw/surface.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <Xm/Form.h>
#include <Xm/ScrollBar.h>

namespace tt {

namespace {

/* Read at most this much per wakeup before letting X events through. */
const size_t k_read_chunk = 16384;
const size_t k_read_budget = 262144;

char* dup_str(const char* s)
{
    if (!s)
        return 0;
    char* d = (char*)malloc(strlen(s) + 1);
    if (d)
        strcpy(d, s);
    return d;
}

char** dup_list(const char* const* v)
{
    if (!v)
        return 0;
    int n = 0;
    while (v[n])
        n++;
    char** d = (char**)malloc(sizeof(char*) * (size_t)(n + 1));
    if (!d)
        return 0;
    for (int i = 0; i < n; i++)
        d[i] = dup_str(v[i]);
    d[n] = 0;
    return d;
}

void free_list(char** v)
{
    for (int i = 0; v && v[i]; i++)
        free(v[i]);
    free(v);
}

TermPane* pane_of(Widget w)
{
    XtPointer p = 0;
    XtVaGetValues(w, XmNuserData, &p, (char*)0);
    return (TermPane*)p;
}

tl::Rgb to_rgb(uint32_t v)
{
    tl::Rgb c;
    c.r = (uint8_t)(v >> 16);
    c.g = (uint8_t)(v >> 8);
    c.b = (uint8_t)v;
    return c;
}

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

/* ---- trampolines ---- */

void in_ready(XtPointer data, int*, XtInputId*)
{
    ((TermPane*)data)->read_pty();
}

void out_ready(XtPointer data, int*, XtInputId*)
{
    ((TermPane*)data)->flush_output();
}

void bell_timeout(XtPointer data, XtIntervalId*)
{
    ((TermPane*)data)->bell_end();
}

void autoscroll_timeout(XtPointer data, XtIntervalId*)
{
    ((TermPane*)data)->autoscroll_step();
}

void input_cb(Widget, XtPointer data, XtPointer call)
{
    ((TermPane*)data)->key((TwGridCallbackStruct*)call);
}

void pointer_cb(Widget, XtPointer data, XtPointer call)
{
    ((TermPane*)data)->pointer((TwGridCallbackStruct*)call);
}

void resize_cb(Widget, XtPointer data, XtPointer call)
{
    ((TermPane*)data)->resized((TwGridCallbackStruct*)call);
}

void focus_cb(Widget, XtPointer data, XtPointer call)
{
    ((TermPane*)data)->focus_changed(((TwGridCallbackStruct*)call)->reason == TW_CR_FOCUS_IN);
}

void scroll_cb(Widget, XtPointer data, XtPointer call)
{
    ((TermPane*)data)->scroll_moved(((XmScrollBarCallbackStruct*)call)->value);
}

Boolean convert_sel(Widget w, Atom* sel, Atom* target, Atom* type, XtPointer* value,
                    unsigned long* length, int* format)
{
    TermPane* p = pane_of(w);
    return p ? p->convert(*sel, *target, type, value, length, format) : False;
}

void lose_sel(Widget w, Atom* sel)
{
    TermPane* p = pane_of(w);
    if (p)
        p->lost(*sel);
}

void got_paste_cb(Widget w, XtPointer data, Atom* sel, Atom* type, XtPointer value,
                  unsigned long* length, int* format)
{
    ((TermPane*)data)->got_paste(w, *sel, *type, value, *length, *format, false);
}

void got_paste_string_cb(Widget w, XtPointer data, Atom* sel, Atom* type, XtPointer value,
                         unsigned long* length, int* format)
{
    ((TermPane*)data)->got_paste(w, *sel, *type, value, *length, *format, true);
}

/* The fonts tried in order when none is set: the ISO 10646 ones cover
 * Unicode on Linux; IRIX servers have only Latin-1 fonts. */
const char* const k_auto_fonts[] = {
    "-misc-fixed-medium-r-normal--15-140-75-75-c-90-iso10646-1",
    "-misc-fixed-medium-r-normal--13-120-75-75-c-80-iso10646-1",
    "-sgi-screen-medium-r-normal--15-150-72-72-m-90-iso8859-1",
    "9x15",
    "fixed",
    0
};

} /* namespace */

TermPane::TermPane(Widget parent, const char* name, const PaneOptions& o, PaneHost* host)
    : host_(host), form_(0), grid_(0), scroll_(0), src_(term_), argv_(dup_list(o.argv)),
      env_(dup_list(o.env)), cwd_(dup_str(o.cwd)), login_(o.login), font_(0), started_(false),
      running_(false), in_scroll_update_(false), outq_off_(0), in_id_(0), out_id_(0),
      bell_id_(0), bell_(o.bell), selecting_(false), sel_moved_(false), click_count_(0),
      last_click_(0), last_col_(-1), last_row_(-1), drag_col_(0), drag_row_(0), autoscroll_(0),
      autoscroll_id_(0)
{
    thost_.pane = this;
    Display* dpy = XtDisplay(parent);
    term_.init(80, 24, (size_t)(o.scrollback > 0 ? o.scrollback : 0));
    term_.set_default_colors(to_rgb(o.foreground), to_rgb(o.background));
    if (o.palette)
        set_palette(o.palette);
    if (o.have_cursor_color)
        src_.set_cursor_color(o.cursor_color);
    term_.set_host(&thost_);
    term_.set_bold_is_bright(o.bold_is_bright);
    term_.set_backspace_is_del(o.backspace_is_del);

    a_clipboard_ = XInternAtom(dpy, "CLIPBOARD", False);
    a_utf8_ = XInternAtom(dpy, "UTF8_STRING", False);
    a_targets_ = XInternAtom(dpy, "TARGETS", False);
    a_text_ = XInternAtom(dpy, "TEXT", False);

    form_ = XtVaCreateWidget(name, xmFormWidgetClass, parent, (char*)0);
    if (o.scroll_bar) {
        scroll_ = XtVaCreateManagedWidget("scrollBar", xmScrollBarWidgetClass, form_,
                                          XmNorientation, XmVERTICAL,
                                          XmNtraversalOn, False,
                                          XmNtopAttachment, XmATTACH_FORM,
                                          XmNrightAttachment, XmATTACH_FORM,
                                          XmNbottomAttachment, XmATTACH_FORM,
                                          XmNminimum, 0, XmNmaximum, 24, XmNsliderSize, 24,
                                          (char*)0);
        XtAddCallback(scroll_, XmNvalueChangedCallback, scroll_cb, (XtPointer)this);
        XtAddCallback(scroll_, XmNdragCallback, scroll_cb, (XtPointer)this);
    }
    font_ = dup_str(o.font && o.font[0] ? o.font : pick_font(dpy));
    Arg args[12];
    Cardinal n = 0;
    XtSetArg(args[n], TwNfont, font_); n++;
    if (o.fallback_fonts) {
        XtSetArg(args[n], TwNfallbackFonts, o.fallback_fonts); n++;
    }
    XtSetArg(args[n], TwNsource, (tw::Source*)&src_); n++;
    XtSetArg(args[n], XmNuserData, (XtPointer)this); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
    if (scroll_) {
        XtSetArg(args[n], XmNrightAttachment, XmATTACH_WIDGET); n++;
        XtSetArg(args[n], XmNrightWidget, scroll_); n++;
    } else {
        XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    }
    grid_ = TwCreateGrid(form_, "grid", args, n);
    XtAddCallback(grid_, TwNinputCallback, input_cb, (XtPointer)this);
    XtAddCallback(grid_, TwNpointerCallback, pointer_cb, (XtPointer)this);
    XtAddCallback(grid_, TwNresizeCallback, resize_cb, (XtPointer)this);
    XtAddCallback(grid_, TwNfocusCallback, focus_cb, (XtPointer)this);
    XtManageChild(grid_);
    XtVaSetValues(form_, XmNinitialFocus, grid_, (char*)0);
    XtManageChild(form_);
}

TermPane::~TermPane()
{
    if (running_)
        hangup(500);
    if (in_id_)
        XtRemoveInput(in_id_);
    if (out_id_)
        XtRemoveInput(out_id_);
    if (bell_id_)
        XtRemoveTimeOut(bell_id_);
    if (autoscroll_id_)
        XtRemoveTimeOut(autoscroll_id_);
    /* Xt destroys widgets at the end of the event: until then nothing in
     * them may reach this pane or its source. */
    XtVaSetValues(grid_, XmNuserData, (XtPointer)0, (char*)0);
    TwGridSetSource(grid_, 0);
    XtRemoveAllCallbacks(grid_, TwNinputCallback);
    XtRemoveAllCallbacks(grid_, TwNpointerCallback);
    XtRemoveAllCallbacks(grid_, TwNresizeCallback);
    XtRemoveAllCallbacks(grid_, TwNfocusCallback);
    if (scroll_) {
        XtRemoveAllCallbacks(scroll_, XmNvalueChangedCallback);
        XtRemoveAllCallbacks(scroll_, XmNdragCallback);
    }
    XtDestroyWidget(form_);
    pty_.close();
    free_list(argv_);
    free_list(env_);
    free(cwd_);
    /* TwGrid holds the font name until the widget is gone, which Xt does
     * at the end of this event; keep it. */
}

void TermPane::start()
{
    if (started_ || !XtIsRealized(grid_))
        return;
    tw::Surface* s = TwGridSurface(grid_);
    int c = s->fit_cols(), r = s->fit_rows();
    term_.resize(c, r);
    spawn(c, r);
    update_scrollbar();
}

void TermPane::spawn(int cols, int rows)
{
    started_ = true;
    int cw, ch;
    TwGridGetCellSize(grid_, &cw, &ch);
    tp::WinSize ws;
    ws.cols = cols;
    ws.rows = rows;
    ws.width_px = cols * cw;
    ws.height_px = rows * ch;
    if (!pty_.open(ws)) {
        char m[300];
        snprintf(m, sizeof(m), "\r\ntt: %.250s\r\n", pty_.error());
        term_.feed((const uint8_t*)m, strlen(m));
        TwGridDamage(grid_);
        host_->pane_exited(this, 127);
        return;
    }
    /* The window id is the top level's, as xterm gives it. */
    Widget top = grid_;
    while (XtParent(top) && !XtIsShell(top))
        top = XtParent(top);
    char windowid[48];
    sprintf(windowid, "WINDOWID=%lu", (unsigned long)XtWindow(top));
    Display* dpy = XtDisplay(grid_);
    Visual* vis = DefaultVisual(dpy, DefaultScreen(dpy));
    bool truecolor = vis->c_class == TrueColor && DefaultDepth(dpy, DefaultScreen(dpy)) >= 15;
    const char* tidir = "";
    char term_env[64];
    sprintf(term_env, "TERM=%.50s", pick_term(&tidir));
    char terminfo_env[540];
    if (tidir[0])
        sprintf(terminfo_env, "TERMINFO=%.520s", tidir);
    else
        strcpy(terminfo_env, "COLUMNS");    /* nothing to set: unset a harmless name */
    const char* base[] = { term_env, terminfo_env,
                           truecolor ? "COLORTERM=truecolor" : "COLORTERM", windowid,
                           "COLUMNS", "LINES", "TERMCAP" };
    const int nbase = (int)(sizeof(base) / sizeof(base[0]));
    int nextra = 0;
    while (env_ && env_[nextra])
        nextra++;
    const char** env = (const char**)malloc(sizeof(char*) * (size_t)(nbase + nextra + 1));
    int k = 0;
    for (int i = 0; env && i < nbase; i++)
        env[k++] = base[i];
    for (int i = 0; env && i < nextra; i++)
        env[k++] = env_[i];
    if (env)
        env[k] = 0;
    tp::SpawnOptions so;
    so.argv = (const char* const*)argv_;
    so.login = login_ && !argv_;
    so.env = env;
    so.cwd = cwd_;
    bool ok = proc_.spawn(pty_, so);
    free(env);
    if (!ok) {
        char m[300];
        snprintf(m, sizeof(m), "\r\ntt: %.250s\r\n", proc_.error());
        term_.feed((const uint8_t*)m, strlen(m));
        TwGridDamage(grid_);
        pty_.close();
        host_->pane_exited(this, 127);
        return;
    }
    running_ = true;
    in_id_ = XtAppAddInput(XtWidgetToApplicationContext(grid_), pty_.fd(),
                           (XtPointer)XtInputReadMask, in_ready, (XtPointer)this);
}

void TermPane::hangup(int ms)
{
    if (!running_)
        return;
    proc_.signal_group(SIGHUP);
    proc_.wait(ms);
}

void TermPane::focus()
{
    XmProcessTraversal(grid_, XmTRAVERSE_CURRENT);
}

/* ---- the pty ---- */

void TermPane::read_pty()
{
    char buf[k_read_chunk];
    size_t total = 0;
    bool fed = false;
    while (total < k_read_budget) {
        long n = pty_.read(buf, sizeof(buf));
        if (n > 0) {
            term_.feed((const uint8_t*)buf, (size_t)n);
            total += (size_t)n;
            fed = true;
            continue;
        }
        if (n < 0) {
            if (fed)
                TwGridDamage(grid_);
            ended();
            return;
        }
        break;
    }
    if (fed) {
        TwGridDamage(grid_);
        update_scrollbar();
    }
}

void TermPane::ended()
{
    if (in_id_) {
        XtRemoveInput(in_id_);
        in_id_ = 0;
    }
    if (out_id_) {
        XtRemoveInput(out_id_);
        out_id_ = 0;
    }
    proc_.wait(2000);
    running_ = false;
    int code = proc_.exited() ? proc_.exit_code() : 128 + proc_.term_signal();
    pty_.close();
    host_->pane_exited(this, code);
}

void TermPane::write_pty(const uint8_t* data, size_t n)
{
    if (!pty_.is_open())
        return;
    if (outq_.size() == outq_off_) {
        long w = pty_.write(data, n);
        if (w < 0)
            return;
        data += w;
        n -= (size_t)w;
        if (!n)
            return;
    }
    outq_.append(data, n);
    if (!out_id_)
        out_id_ = XtAppAddInput(XtWidgetToApplicationContext(grid_), pty_.fd(),
                                (XtPointer)XtInputWriteMask, out_ready, (XtPointer)this);
}

void TermPane::flush_output()
{
    while (outq_off_ < outq_.size()) {
        long w = pty_.write(outq_.data() + outq_off_, outq_.size() - outq_off_);
        if (w <= 0)
            break;
        outq_off_ += (size_t)w;
    }
    if (outq_off_ == outq_.size()) {
        outq_.clear();
        outq_off_ = 0;
        if (out_id_) {
            XtRemoveInput(out_id_);
            out_id_ = 0;
        }
    }
}

/* ---- the terminal's requests ---- */

void TermPane::bell()
{
    if (bell_ == BELL_OFF)
        return;
    if (bell_ == BELL_AUDIBLE) {
        XBell(XtDisplay(grid_), 0);
        return;
    }
    /* Flash: reverse video for a moment, as xterm's visualBell. */
    if (bell_id_)
        return;
    term_.set_reverse_video(!term_.modes().reverse);
    TwGridDamage(grid_);
    bell_id_ = XtAppAddTimeOut(XtWidgetToApplicationContext(grid_), 100, bell_timeout,
                               (XtPointer)this);
}

void TermPane::bell_end()
{
    bell_id_ = 0;
    term_.set_reverse_video(!term_.modes().reverse);
    TwGridDamage(grid_);
}

void TermPane::title(tl::TitleKind kind, const char* utf8)
{
    host_->pane_title(this, kind, utf8);
}

/* ---- the scroll bar ---- */

void TermPane::update_scrollbar()
{
    if (!scroll_)
        return;
    const tl::Grid& g = term_.grid();
    int total = (int)g.total_rows();
    int rows = term_.rows();
    int value = (int)(term_.viewport_top() - g.base());
    if (total < rows)
        total = rows;
    if (value > total - rows)
        value = total - rows;
    in_scroll_update_ = true;
    XtVaSetValues(scroll_, XmNmaximum, total, XmNsliderSize, rows, XmNvalue, value,
                  XmNpageIncrement, rows > 1 ? rows - 1 : 1, (char*)0);
    in_scroll_update_ = false;
}

void TermPane::scroll_moved(int value)
{
    if (in_scroll_update_)
        return;
    uint64_t target = term_.grid().base() + (uint64_t)value;
    long delta = (long)target - (long)term_.viewport_top();
    term_.scroll_viewport(delta);
    TwGridDamage(grid_);
}

void TermPane::to_bottom()
{
    if (!term_.viewport_at_bottom()) {
        term_.scroll_to_bottom();
        TwGridDamage(grid_);
        update_scrollbar();
    }
}

/* ---- keys ---- */

void TermPane::key(TwGridCallbackStruct* cb)
{
    KeySym ks = cb->keysym;
    if (ks == XK_Shift_L || ks == XK_Shift_R || ks == XK_Control_L || ks == XK_Control_R ||
        ks == XK_Alt_L || ks == XK_Alt_R || ks == XK_Meta_L || ks == XK_Meta_R ||
        ks == XK_Caps_Lock || ks == XK_Num_Lock || ks == XK_Super_L || ks == XK_Super_R)
        return;
    if (host_->pane_key(this, cb))
        return;
    unsigned mods = mods_of(cb->state);
    /* Scrollback keys stay in the terminal. */
    if (mods == tl::MOD_SHIFT && (ks == XK_Prior || ks == XK_Next)) {
        term_.scroll_viewport(ks == XK_Prior ? -(term_.rows() - 1) : term_.rows() - 1);
        TwGridDamage(grid_);
        update_scrollbar();
        return;
    }
    Time t = cb->event ? cb->event->xkey.time : CurrentTime;
    if (mods == (tl::MOD_CTRL | tl::MOD_SHIFT) && (ks == XK_C || ks == XK_c)) {
        copy(t);
        return;
    }
    if (mods == (tl::MOD_CTRL | tl::MOD_SHIFT) && (ks == XK_V || ks == XK_v)) {
        paste(t);
        return;
    }
    if (mods == tl::MOD_SHIFT && (ks == XK_Insert || ks == XK_KP_Insert)) {
        request_paste(XA_PRIMARY, t);
        return;
    }
    if (!running_)
        return;
    int k = key_of(ks);
    bool sent = false;
    if (k != tl::KEY_NONE)
        sent = term_.key(k, mods);
    if (!sent && cb->text_len > 0)
        term_.text_input(cb->text, (size_t)cb->text_len, (mods & tl::MOD_ALT) != 0);
    else if (!sent)
        return;
    /* Typing brings the view back to the bottom. */
    to_bottom();
}

void TermPane::focus_changed(bool in)
{
    term_.focus(in);
    host_->pane_focus(this, in);
}

void TermPane::resized(TwGridCallbackStruct* cb)
{
    if (!XtIsRealized(grid_))
        return;
    term_.resize(cb->cols, cb->rows);
    if (!started_) {
        spawn(cb->cols, cb->rows);
    } else if (running_) {
        int cw, ch;
        TwGridGetCellSize(grid_, &cw, &ch);
        tp::WinSize ws;
        ws.cols = cb->cols;
        ws.rows = cb->rows;
        ws.width_px = cb->cols * cw;
        ws.height_px = cb->rows * ch;
        pty_.resize(ws);
    }
    TwGridDamage(grid_);
    update_scrollbar();
}

/* ---- selections and the clipboard ---- */

Boolean TermPane::convert(Atom sel, Atom target, Atom* type, XtPointer* value,
                          unsigned long* length, int* format)
{
    tl::Buffer& src = sel == XA_PRIMARY ? primary_ : clipboard_;
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
    tl::Buffer conv;
    const tl::Buffer* data = &src;
    if (target == a_utf8_) {
        *type = a_utf8_;
    } else if (target == XA_STRING || target == a_text_) {
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

void TermPane::lost(Atom sel)
{
    if (sel == XA_PRIMARY) {
        /* Someone else selected something: drop our highlight, as xterm does. */
        primary_.clear();
        term_.select_clear();
        TwGridDamage(grid_);
    } else {
        clipboard_.clear();
    }
}

void TermPane::own(Atom sel, Time t)
{
    if (!XtOwnSelection(grid_, sel, t, convert_sel, lose_sel, 0)) {
        if (sel == XA_PRIMARY)
            primary_.clear();
        else
            clipboard_.clear();
    }
}

void TermPane::copy_to(Atom sel, Time t)
{
    tl::Buffer& dst = sel == XA_PRIMARY ? primary_ : clipboard_;
    dst.clear();
    if (!term_.selection_text(dst) || dst.size() == 0)
        return;
    own(sel, t);
}

void TermPane::copy(Time t)
{
    copy_to(a_clipboard_, t);
}

void TermPane::paste(Time t)
{
    request_paste(a_clipboard_, t);
}

void TermPane::select_all(Time t)
{
    term_.select_all();
    copy_to(XA_PRIMARY, t);
    TwGridDamage(grid_);
}

void TermPane::clear_scrollback()
{
    term_.clear_scrollback();
    TwGridDamage(grid_);
    update_scrollbar();
}

void TermPane::reset()
{
    term_.reset();
    TwGridDamage(grid_);
    update_scrollbar();
}

void TermPane::request_paste(Atom sel, Time t)
{
    if (running_)
        XtGetSelectionValue(grid_, sel, a_utf8_, got_paste_cb, (XtPointer)this, t);
}

/* First UTF8_STRING; owners that do not know it (IRIX's own clients)
 * refuse, and then STRING, Latin-1 by definition, is asked for. */
void TermPane::got_paste(Widget w, Atom sel, Atom type, XtPointer value, unsigned long length,
                         int format, bool second_try)
{
    if (value && format == 8 && type == a_utf8_ && !second_try) {
        term_.paste((const char*)value, (size_t)length);
        to_bottom();
    } else if (value && format == 8 && (type == XA_STRING || type == a_text_)) {
        tl::Buffer u;
        const unsigned char* p = (const unsigned char*)value;
        for (unsigned long i = 0; i < length; i++)
            u.append_utf8(p[i]);
        term_.paste(u.c_str(), u.size());
        to_bottom();
    } else if (!second_try) {
        XtGetSelectionValue(w, sel, XA_STRING, got_paste_string_cb, (XtPointer)this,
                            XtLastTimestampProcessed(XtDisplay(w)));
    }
    if (value)
        XtFree((char*)value);
}

/* ---- the mouse ---- */

void TermPane::autoscroll_step()
{
    autoscroll_id_ = 0;
    if (!selecting_ || !autoscroll_)
        return;
    term_.scroll_viewport(autoscroll_);
    int row = autoscroll_ < 0 ? 0 : term_.rows() - 1;
    term_.select_extend(term_.viewport_top() + (uint64_t)row, autoscroll_ < 0 ? 0 : term_.cols() - 1);
    TwGridDamage(grid_);
    update_scrollbar();
    autoscroll_id_ = XtAppAddTimeOut(XtWidgetToApplicationContext(grid_), 60, autoscroll_timeout,
                                     (XtPointer)this);
}

void TermPane::select_at(TwGridCallbackStruct* cb)
{
    term_.select_extend(term_.viewport_top() + (uint64_t)cb->row, cb->col);
    TwGridDamage(grid_);
}

void TermPane::pointer(TwGridCallbackStruct* cb)
{
    unsigned state = cb->state;
    Time t = cb->event ? cb->event->xbutton.time : CurrentTime;

    /* Ctrl+button 3: the host's menu. */
    if (cb->reason == TW_CR_BUTTON_PRESS && cb->button == 3 && (state & ControlMask) &&
        cb->event && !selecting_) {
        host_->pane_context_menu(this, cb->event);
        return;
    }

    /* The application wants the mouse, unless Shift is held. */
    if (running_ && term_.mouse_reporting() && !(state & ShiftMask) && !selecting_) {
        tl::MouseAction act = cb->reason == TW_CR_BUTTON_PRESS ? tl::MOUSE_PRESS
                            : cb->reason == TW_CR_BUTTON_RELEASE ? tl::MOUSE_RELEASE
                            : tl::MOUSE_MOTION;
        term_.mouse(act, cb->button, cb->col, cb->row, mods_of(state),
                    cb->x > 2 ? cb->x - 2 : 0, cb->y > 2 ? cb->y - 2 : 0);
        return;
    }

    if (cb->reason == TW_CR_BUTTON_PRESS) {
        switch (cb->button) {
        case 1: {
            bool same = cb->col == last_col_ && cb->row == last_row_;
            Time multi = (Time)XtGetMultiClickTime(XtDisplay(grid_));
            click_count_ = same && t - last_click_ < multi ? click_count_ % 3 + 1 : 1;
            last_click_ = t;
            last_col_ = cb->col;
            last_row_ = cb->row;
            tl::SelectMode mode = click_count_ == 3 ? tl::SELECT_LINE
                                : click_count_ == 2 ? tl::SELECT_WORD : tl::SELECT_CHAR;
            term_.select_start(term_.viewport_top() + (uint64_t)cb->row, cb->col, mode);
            selecting_ = true;
            sel_moved_ = click_count_ > 1;
            drag_col_ = cb->col;
            drag_row_ = cb->row;
            TwGridDamage(grid_);
            break;
        }
        case 2:
            request_paste(XA_PRIMARY, t);
            break;
        case 3:
            if (term_.has_selection()) {
                select_at(cb);
                copy_to(XA_PRIMARY, t);
            }
            break;
        case 4: case 5:
            if (term_.alt_screen()) {
                /* Wheel in full-screen programs: arrow keys (alternate scroll). */
                for (int i = 0; running_ && i < 3; i++)
                    term_.key(cb->button == 4 ? tl::KEY_UP : tl::KEY_DOWN, 0);
            } else {
                term_.scroll_viewport(cb->button == 4 ? -3 : 3);
                TwGridDamage(grid_);
                update_scrollbar();
            }
            break;
        default:
            break;
        }
        return;
    }
    if (cb->reason == TW_CR_MOTION) {
        if (!selecting_ || !(state & Button1Mask))
            return;
        int cw, ch;
        TwGridGetCellSize(grid_, &cw, &ch);
        int grid_h = term_.rows() * ch + 4;
        autoscroll_ = cb->y < 0 ? -1 : cb->y >= grid_h ? 1 : 0;
        if (autoscroll_ && !autoscroll_id_)
            autoscroll_step();
        if (cb->col != drag_col_ || cb->row != drag_row_) {
            sel_moved_ = true;
            drag_col_ = cb->col;
            drag_row_ = cb->row;
            select_at(cb);
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
        if (!sel_moved_) {
            /* A plain click clears the selection. */
            term_.select_clear();
            TwGridDamage(grid_);
            return;
        }
        copy_to(XA_PRIMARY, t);
    }
}

/* ---- settings ---- */

void TermPane::set_font(const char* name)
{
    const char* f = name && *name ? name : pick_font(XtDisplay(grid_));
    char* copy = dup_str(f);
    XtVaSetValues(grid_, TwNfont, copy, (char*)0);
    free(font_);
    font_ = copy;
}

void TermPane::set_colors(uint32_t fg, uint32_t bg)
{
    term_.set_default_colors(to_rgb(fg), to_rgb(bg));
    TwGridDamage(grid_);
}

void TermPane::set_palette(const uint32_t* colors16)
{
    static const uint32_t xterm[16] = {
        0x000000, 0xcd0000, 0x00cd00, 0xcdcd00, 0x0000ee, 0xcd00cd, 0x00cdcd, 0xe5e5e5,
        0x7f7f7f, 0xff0000, 0x00ff00, 0xffff00, 0x5c5cff, 0xff00ff, 0x00ffff, 0xffffff
    };
    for (int i = 0; i < 16; i++)
        term_.set_palette(i, to_rgb(colors16 ? colors16[i] : xterm[i]));
    if (grid_)
        TwGridDamage(grid_);
}

void TermPane::set_cursor_color(bool have, uint32_t rgb)
{
    if (have)
        src_.set_cursor_color(rgb);
    else
        src_.clear_cursor_color();
    TwGridDamage(grid_);
}

void TermPane::set_bold_is_bright(bool on)
{
    term_.set_bold_is_bright(on);
    TwGridDamage(grid_);
}

void TermPane::set_backspace_is_del(bool on)
{
    term_.set_backspace_is_del(on);
}

void TermPane::cell_size(int* w, int* h) const
{
    TwGridGetCellSize(grid_, w, h);
}

void TermPane::extra_size(int* w, int* h) const
{
    *w = 4;
    *h = 4;                 /* the grid's margins */
    XtWidgetGeometry pref;
    if (scroll_ && (XtQueryGeometry(scroll_, 0, &pref) != XtGeometryNo || pref.width))
        *w += pref.width;
}

/* ---- the environment ---- */

const char* pick_term(const char** terminfo_dir)
{
    static char dir[512];
    dir[0] = 0;
    *terminfo_dir = dir;
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
            if (!dirs[d] || !dirs[d][0] || strlen(dirs[d]) > 500)
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
                    sprintf(dir, "%.500s", dirs[d]);
                return names[n];
            }
        }
    }
    return "xterm";
}

const char* pick_font(Display* dpy)
{
    for (int i = 0; k_auto_fonts[i]; i++) {
        int n = 0;
        char** names = XListFonts(dpy, (char*)k_auto_fonts[i], 1, &n);
        if (names)
            XFreeFontNames(names);
        if (n > 0)
            return k_auto_fonts[i];
    }
    return "fixed";
}

void font_path(const char* argv0, const char* datadir, char* out, size_t len)
{
    char prog_dir[600];
    prog_dir[0] = 0;
    if (strchr(argv0, '/')) {
        strncpy(prog_dir, argv0, sizeof(prog_dir) - 1);
        prog_dir[sizeof(prog_dir) - 1] = 0;
        *strrchr(prog_dir, '/') = 0;
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
    const char* pd = prog_dir[0] ? prog_dir : ".";
    snprintf(out, len, "%.200s%s%.400s/../third_party/misc-misc:%.400s/../share/ttr/fonts:%.200s/fonts",
             env ? env : "", env ? ":" : "", pd, pd, datadir);
}

} /* namespace tt */
