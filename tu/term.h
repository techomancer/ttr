/*
 * tu/term.h - the terminal tu draws on: raw mode, size, resize signals,
 * and the output primitives the screen diff needs (ttrc/DESIGN.md
 * sections 3.1 to 3.4).
 *
 * Term loads the terminfo entry, fills in what known terminals have but
 * their entries do not say (iris-ansi has colours and line drawing),
 * decides the tier and the character set, and turns tu's styles and
 * Unicode cells into the terminal's own sequences: true colour, 256, 16,
 * 8 or none; UTF-8, the alternate character set (ACS) for lines, or
 * Latin-1 and ASCII.
 */
#ifndef TU_TERM_H
#define TU_TERM_H

#include "terminfo.h"
#include "tu.h"

#include <termios.h>

namespace tu {

class Term {
public:
    Term();
    ~Term();

    /* Set up on these descriptors (usually 0 and 1) for terminal 'term'
     * ($TERM if null). Does not touch the terminal yet. */
    bool open(int in_fd, int out_fd, const char* term = 0);

    /* Take the terminal: raw mode, alternate screen, keypad mode, mouse
     * and bracketed paste where available, cursor hidden. stop() gives
     * it back exactly as it was (for the shell, and at exit). */
    bool start();
    void stop();
    bool started() const { return started_; }

    /* Hand the screen to a program (ttrc's shell) and take it back,
     * staying in raw mode: suspend_ui() undoes what start() set up
     * (mouse, paste, keypad, hidden cursor) and leaves the alternate
     * screen if leave_alt; resume_ui() sets it up again and enters the
     * alternate screen if enter_alt. */
    void suspend_ui(bool leave_alt);
    void resume_ui(bool enter_alt);
    /* Bytes straight to the terminal (after what is buffered). The
     * cursor position and pen are unknown afterwards. */
    void write_raw(const char* data, size_t n);

    /* Size in cells; update_size() re-reads it after SIGWINCH and says
     * whether it changed. */
    int cols() const { return cols_; }
    int rows() const { return rows_; }
    bool update_size();
    /* For tests and non-terminals: a fixed size. */
    void set_size(int cols, int rows) { cols_ = cols; rows_ = rows; }

    /* A descriptor that becomes readable when a signal arrived: read
     * bytes 'W' (window size changed), 'T' (SIGTERM), 'H' (SIGHUP). */
    int signal_fd() const;
    int in_fd() const { return in_fd_; }

    Tier tier() const { return tier_; }
    void set_tier(Tier t);
    bool utf8() const { return utf8_; }
    void set_utf8(bool on) { utf8_ = on; }
    bool has_acs() const { return acs_[(unsigned char)'q'] != 0; }
    const Terminfo& info() const { return ti_; }
    bool xterm_like() const { return xterm_like_; }
    /* Writing the bottom right cell would scroll the screen. */
    bool last_cell_scrolls() const { return am_ && !xenl_; }

    /* ---- output, buffered until flush() ---- */
    void out(const char* s, size_t n);
    void out(const char* s);
    void flush();

    void move(int x, int y);
    void set_style(const Style& s);
    void reset_style();
    /* One character of the given width at the cursor. */
    void put_char(uint32_t cp, int width);
    /* Clear to the end of the line in the current style, if the terminal
     * can do it with that background. */
    bool clear_eol();
    void clear_screen();
    void show_cursor(bool on);
    /* Wrap a frame's output so modern terminals show it at once. */
    void begin_sync();
    void end_sync();

private:
    void supplement();
    void decide_tier();
    void set_colors(const Style& s);
    void color_sgr(Color c, bool bg, unsigned* extra_attrs);
    void cap(const char* name);
    void cap_str(const char* s);
    void acs(bool on);

    Terminfo ti_;
    int in_fd_, out_fd_;
    struct termios saved_;
    bool have_saved_;
    bool started_;
    int cols_, rows_;
    Tier tier_;
    int colors_;
    bool utf8_;
    bool am_, xenl_, bce_;
    bool xterm_like_;
    bool truecolor_;
    char acs_[128];             /* vt100 ACS letter -> terminal's character */
    bool acs_on_;

    /* Where the terminal's cursor is (-1: unknown) and its pen. */
    int cx_, cy_;
    Style pen_;
    bool pen_known_;
    bool cursor_shown_;

    char* buf_;
    size_t len_, cap_;

    Term(const Term&);
    Term& operator=(const Term&);
};

} /* namespace tu */

#endif
