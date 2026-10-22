/*
 * tu/input.h - turns the bytes a terminal sends into keys and mouse
 * events (ttrc/DESIGN.md section 3.2).
 *
 * The terminfo entry's key strings are matched first (iris-ansi's
 * CSI 001 q function keys, single-byte Delete and Backspace), then the
 * usual xterm, rxvt and Linux console sequences with modifiers, the kitty
 * keyboard protocol, SGR and X10 mouse reports and bracketed paste. A
 * lone Esc and Alt+key (Esc then the key) are told apart by a timeout:
 * when next() says it needs more, the caller waits for input up to
 * timeout_ms() and then calls next() with timed_out set.
 */
#ifndef TU_INPUT_H
#define TU_INPUT_H

#include "tu.h"

#include <stddef.h>

namespace tu {

class Terminfo;

enum Key {
    KEY_NONE = 0,
    KEY_CHAR,           /* ch holds the character */
    KEY_ENTER,
    KEY_TAB,
    KEY_BACKSPACE,
    KEY_ESC,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_PGUP,
    KEY_PGDN,
    KEY_INSERT,
    KEY_DELETE,
    KEY_F1,             /* KEY_F1 + n - 1 for Fn, up to F24 */
    KEY_F24 = KEY_F1 + 23,
    KEY_MOUSE,
    KEY_PASTE_BEGIN,
    KEY_PASTE_END,
    KEY_FOCUS_IN,
    KEY_FOCUS_OUT
};

enum {
    MOD_SHIFT = 1,
    MOD_ALT = 2,
    MOD_CTRL = 4
};

enum MouseAction {
    MOUSE_PRESS,
    MOUSE_RELEASE,
    MOUSE_DRAG,
    MOUSE_WHEEL_UP,
    MOUSE_WHEEL_DOWN
};

struct Event {
    int key;
    uint32_t ch;        /* KEY_CHAR: the character (Ctrl+A is 'a' + MOD_CTRL) */
    unsigned mods;
    /* KEY_MOUSE */
    int x, y;           /* cell, from 0 */
    int button;         /* 1 left, 2 middle, 3 right */
    MouseAction action;

    Event() : key(KEY_NONE), ch(0), mods(0), x(0), y(0), button(0), action(MOUSE_PRESS) {}
};

class Input {
public:
    Input();

    /* The key strings of this terminal; utf8 decides how bytes >= 0x80
     * are read. */
    void init(const Terminfo& ti, bool utf8);

    void feed(const char* data, size_t n);
    /* The next event, if one is complete. With timed_out, an incomplete
     * escape sequence is taken as Esc or Alt+key. */
    bool next(Event* ev, bool timed_out);
    /* Bytes are waiting for the rest of a sequence. */
    bool pending() const { return len_ > 0; }
    int timeout_ms() const { return 60; }

private:
    struct Seq {
        char s[24];
        int len;
        int key;
        unsigned mods;
    };

    void add(const char* s, int key, unsigned mods);
    /* 1: matched (sets *used), 0: no match, -1: could match with more */
    int match_table(Event* ev, size_t* used) const;
    int parse_csi(Event* ev, size_t* used, bool timed_out) const;
    int parse_ss3(Event* ev, size_t* used) const;
    void control(unsigned char c, Event* ev) const;
    void consume(size_t n);

    Seq seqs_[96];
    int nseqs_;
    bool utf8_;
    char buf_[512];
    size_t len_;
};

} /* namespace tu */

#endif
