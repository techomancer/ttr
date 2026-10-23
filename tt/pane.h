/*
 * tt/pane.h - a terminal in a Motif widget: tl's emulation drawn by tw's
 * TwGrid, beside a scroll bar for the scrollback, running a program on a
 * tp::Pty. ttrt is one TermPane in a window; ttrcode docks several. The
 * pane does what every terminal does (output, keys, the mouse, PRIMARY
 * and CLIPBOARD, the bell); menus, settings and windows are the host's.
 * See tt/DESIGN.md.
 */
#ifndef TT_PANE_H
#define TT_PANE_H

#include "source.h"

#include "../tl/terminal.h"
#include "../tp/tp.h"
#include "../tw/grid_widget.h"

#include <Xm/Xm.h>

namespace tt {

class TermPane;

/* What the pane tells the program around it. All but exited() can be
 * left alone. */
class PaneHost {
public:
    virtual ~PaneHost() {}
    /* The program ended (the pane stays, showing its last screen). */
    virtual void pane_exited(TermPane* p, int exit_code) = 0;
    virtual void pane_title(TermPane*, tl::TitleKind, const char*) {}
    /* Ctrl+button 3: the host's menu. */
    virtual void pane_context_menu(TermPane*, XEvent*) {}
    /* A key before the terminal sees it; true if the host used it. */
    virtual bool pane_key(TermPane*, TwGridCallbackStruct*) { return false; }
    virtual void pane_focus(TermPane*, bool) {}
};

enum BellMode {
    BELL_OFF,
    BELL_AUDIBLE,
    BELL_VISUAL             /* reverse video for a moment */
};

struct PaneOptions {
    const char* font;               /* XLFD; null or "": pick_font() */
    const char* fallback_fonts;     /* TwGrid's BDF font path, or null */
    uint32_t foreground, background;
    const uint32_t* palette;        /* 16 colours (0xRRGGBB), or null: xterm's */
    bool have_cursor_color;
    uint32_t cursor_color;
    int scrollback;                 /* lines */
    bool bold_is_bright;
    bool backspace_is_del;
    BellMode bell;
    bool scroll_bar;
    const char* renderer;           /* TwGrid's: auto, core, soft, gl */
    const char* dynamic_background; /* an animated background (GL), or null */
    /* What runs: argv (null: the user's shell, a login shell if login),
     * in cwd (null: ours), with env changes ("NAME=value", "NAME"). TERM,
     * TERMINFO, COLORTERM and WINDOWID are set by the pane. */
    const char* const* argv;
    bool login;
    const char* cwd;
    const char* const* env;

    PaneOptions()
        : font(0), fallback_fonts(0), foreground(0xe5e5e5), background(0), palette(0),
          have_cursor_color(false), cursor_color(0), scrollback(1000), bold_is_bright(true),
          backspace_is_del(true), bell(BELL_AUDIBLE), scroll_bar(true), renderer(0),
          dynamic_background(0), argv(0), login(false),
          cwd(0), env(0)
    {
    }
};

class TermPane {
public:
    /* Makes the widgets (an XmForm named 'name', managed) in parent; the
     * program starts when the grid first has a size on screen. */
    TermPane(Widget parent, const char* name, const PaneOptions& o, PaneHost* host);
    /* Hangs up the program and destroys the widgets. */
    ~TermPane();

    Widget widget() const { return form_; }     /* attach this one */
    Widget grid() const { return grid_; }
    Widget scroll_bar() const { return scroll_; }
    tl::Terminal& terminal() { return term_; }
    TermSource& source() { return src_; }

    /* Start now if the grid is realized and not started yet (call after
     * realizing, in case the first resize came too early). */
    void start();
    bool running() const { return running_; }
    /* The program itself is in the foreground (for a shell: at its
     * prompt, not running a command). */
    bool at_prompt() const;
    /* Bytes to the program, as if typed. */
    void send(const char* s, size_t n) { write_pty((const uint8_t*)s, n); }
    /* SIGHUP to the program's process group, waiting up to ms for it. */
    void hangup(int ms);
    void focus();

    /* Commands, for the host's menus. */
    void copy(Time t);              /* to CLIPBOARD */
    void paste(Time t);             /* from CLIPBOARD */
    void select_all(Time t);
    void clear_scrollback();
    void reset();

    /* Settings that apply at once. */
    void set_font(const char* xlfd);
    void set_colors(uint32_t fg, uint32_t bg);
    /* The 16 ANSI colours; null puts xterm's back. */
    void set_palette(const uint32_t* colors16);
    void set_cursor_color(bool have, uint32_t rgb);
    void set_bold_is_bright(bool on);
    void set_backspace_is_del(bool on);
    void set_bell(BellMode b) { bell_ = b; }
    /* False if the renderer cannot draw it (only gl can). */
    bool set_dynamic_background(const char* name) { return TwGridSetBackground(grid_, name) != False; }

    void cell_size(int* w, int* h) const;
    int cols() { return term_.cols(); }
    int rows() { return term_.rows(); }
    /* Pixels around the cells: the grid's margins and the scroll bar. */
    void extra_size(int* w, int* h) const;

    /* Callbacks; public for the C trampolines in pane.cpp. */
    void read_pty();
    void flush_output();
    void write_pty(const uint8_t* data, size_t n);
    void bell();
    void bell_end();
    void title(tl::TitleKind kind, const char* utf8);
    void key(TwGridCallbackStruct* cb);
    void pointer(TwGridCallbackStruct* cb);
    void resized(TwGridCallbackStruct* cb);
    void focus_changed(bool in);
    void scroll_moved(int value);
    void autoscroll_step();
    Boolean convert(Atom sel, Atom target, Atom* type, XtPointer* value, unsigned long* length,
                    int* format);
    void lost(Atom sel);
    void got_paste(Widget w, Atom sel, Atom type, XtPointer value, unsigned long length,
                   int format, bool second_try);

private:
    void spawn(int cols, int rows);
    void ended();
    void update_scrollbar();
    void to_bottom();
    void own(Atom sel, Time t);
    void copy_to(Atom sel, Time t);
    void request_paste(Atom sel, Time t);
    void select_at(TwGridCallbackStruct* cb);

    class Host : public tl::Host {
    public:
        TermPane* pane;
        void write_pty(const uint8_t* data, size_t n) { pane->write_pty(data, n); }
        void bell() { pane->bell(); }
        void title_changed(tl::TitleKind kind, const char* utf8) { pane->title(kind, utf8); }
    };

    PaneHost* host_;
    Widget form_, grid_, scroll_;
    tl::Terminal term_;
    TermSource src_;
    Host thost_;
    tp::Pty pty_;
    tp::Process proc_;
    /* What to run, copied for spawn(). */
    char** argv_;
    char** env_;
    char* cwd_;
    bool login_;
    char* font_;                    /* the font name TwGrid holds */
    bool started_, running_;
    bool in_scroll_update_;
    tl::Buffer outq_;               /* bytes the pty did not take yet */
    size_t outq_off_;
    XtInputId in_id_, out_id_;
    XtIntervalId bell_id_;
    BellMode bell_;
    /* Selecting with the mouse. */
    bool selecting_, sel_moved_;
    int click_count_;
    Time last_click_;
    int last_col_, last_row_, drag_col_, drag_row_;
    int autoscroll_;                /* -1 up, 1 down, 0 off */
    XtIntervalId autoscroll_id_;
    tl::Buffer primary_, clipboard_;   /* what we offer */
    Atom a_clipboard_, a_utf8_, a_targets_, a_text_;

    TermPane(const TermPane&);
    TermPane& operator=(const TermPane&);
};

/* The first TERM name this system's terminfo knows (ttrt, xterm-256color,
 * xterm), and the directory of a private entry to pass on as TERMINFO
 * ("" for the system's). */
const char* pick_term(const char** terminfo_dir);
/* The best fixed font the server has, for an empty font setting. */
const char* pick_font(Display* dpy);
/* TwGrid's fallback BDF font path for a program run as argv0: the
 * source tree's third_party/misc-misc, then the installed copy in
 * datadir/fonts, after $TTRT_FONTS. */
void font_path(const char* argv0, const char* datadir, char* out, size_t len);

} /* namespace tt */

#endif
