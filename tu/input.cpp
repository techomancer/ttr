#include "input.h"
#include "terminfo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace tu {

Input::Input() : nseqs_(0), utf8_(false), len_(0)
{
}

void Input::add(const char* s, int key, unsigned mods)
{
    if (!s || !*s || nseqs_ >= (int)(sizeof(seqs_) / sizeof(seqs_[0])))
        return;
    size_t n = strlen(s);
    if (n >= sizeof(seqs_[0].s))
        return;
    Seq& q = seqs_[nseqs_++];
    memcpy(q.s, s, n + 1);
    q.len = (int)n;
    q.key = key;
    q.mods = mods;
}

void Input::init(const Terminfo& ti, bool utf8)
{
    utf8_ = utf8;
    nseqs_ = 0;
    static const struct {
        const char* cap;
        int key;
        unsigned mods;
    } caps[] = {
        { "kcuu1", KEY_UP, 0 }, { "kcud1", KEY_DOWN, 0 }, { "kcub1", KEY_LEFT, 0 },
        { "kcuf1", KEY_RIGHT, 0 }, { "khome", KEY_HOME, 0 }, { "kend", KEY_END, 0 },
        { "kpp", KEY_PGUP, 0 }, { "knp", KEY_PGDN, 0 }, { "kich1", KEY_INSERT, 0 },
        { "kdch1", KEY_DELETE, 0 }, { "kbs", KEY_BACKSPACE, 0 }, { "kcbt", KEY_TAB, MOD_SHIFT },
        { "kent", KEY_ENTER, 0 },
        { "kLFT", KEY_LEFT, MOD_SHIFT }, { "kRIT", KEY_RIGHT, MOD_SHIFT },
        { "kHOM", KEY_HOME, MOD_SHIFT }, { "kEND", KEY_END, MOD_SHIFT },
        { "kDC", KEY_DELETE, MOD_SHIFT }, { "kIC", KEY_INSERT, MOD_SHIFT },
        { 0, 0, 0 }
    };
    for (int i = 0; caps[i].cap; i++)
        add(ti.str(caps[i].cap), caps[i].key, caps[i].mods);
    for (int f = 1; f <= 24; f++) {
        char name[8];
        sprintf(name, "kf%d", f);
        add(ti.str(name), KEY_F1 + f - 1, 0);
    }
}

void Input::feed(const char* data, size_t n)
{
    if (n > sizeof(buf_) - len_)
        n = sizeof(buf_) - len_;    /* a flood: drop the excess */
    memcpy(buf_ + len_, data, n);
    len_ += n;
}

void Input::consume(size_t n)
{
    if (n > len_)
        n = len_;
    memmove(buf_, buf_ + n, len_ - n);
    len_ -= n;
}

int Input::match_table(Event* ev, size_t* used) const
{
    int best = -1;
    bool partial = false;
    for (int i = 0; i < nseqs_; i++) {
        const Seq& q = seqs_[i];
        if ((size_t)q.len <= len_) {
            if (memcmp(buf_, q.s, (size_t)q.len) == 0 && (best < 0 || q.len > seqs_[best].len))
                best = i;
        } else if (memcmp(buf_, q.s, len_) == 0) {
            partial = true;
        }
    }
    if (best >= 0 && !partial) {
        ev->key = seqs_[best].key;
        ev->mods = seqs_[best].mods;
        *used = (size_t)seqs_[best].len;
        return 1;
    }
    return partial ? -1 : 0;
}

/* xterm's modifier parameter: 1 + shift(1) + alt(2) + ctrl(4) */
static unsigned xmods(int m)
{
    if (m < 2)
        return 0;
    m -= 1;
    unsigned r = 0;
    if (m & 1)
        r |= MOD_SHIFT;
    if (m & 2)
        r |= MOD_ALT;
    if (m & 4)
        r |= MOD_CTRL;
    return r;
}

static int letter_key(char c)
{
    switch (c) {
    case 'A': return KEY_UP;
    case 'B': return KEY_DOWN;
    case 'C': return KEY_RIGHT;
    case 'D': return KEY_LEFT;
    case 'H': return KEY_HOME;
    case 'F': return KEY_END;
    case 'P': return KEY_F1;
    case 'Q': return KEY_F1 + 1;
    case 'R': return KEY_F1 + 2;
    case 'S': return KEY_F1 + 3;
    case 'M': return KEY_ENTER;
    default: return KEY_NONE;
    }
}

static int tilde_key(int n)
{
    switch (n) {
    case 1: case 7: return KEY_HOME;
    case 2: return KEY_INSERT;
    case 3: return KEY_DELETE;
    case 4: case 8: return KEY_END;
    case 5: return KEY_PGUP;
    case 6: return KEY_PGDN;
    case 11: case 12: case 13: case 14: case 15: return KEY_F1 + n - 11;
    case 17: case 18: case 19: case 20: case 21: return KEY_F1 + 5 + n - 17;
    case 23: case 24: case 25: case 26: return KEY_F1 + 10 + n - 23;
    case 28: case 29: return KEY_F1 + 14 + n - 28;
    case 31: case 32: case 33: case 34: return KEY_F1 + 16 + n - 31;
    default: return KEY_NONE;
    }
}

/* ESC [ ... : cursor and function keys with modifiers, kitty keys,
 * mouse, paste and focus. */
int Input::parse_csi(Event* ev, size_t* used, bool timed_out) const
{
    size_t i = 2;
    /* X10 mouse: CSI M b x y */
    if (len_ >= 3 && buf_[2] == 'M') {
        if (len_ < 6)
            return timed_out ? 0 : -1;
        int b = (unsigned char)buf_[3] - 32;
        ev->key = KEY_MOUSE;
        ev->x = (unsigned char)buf_[4] - 33;
        ev->y = (unsigned char)buf_[5] - 33;
        if (b & 64) {
            ev->action = (b & 1) ? MOUSE_WHEEL_DOWN : MOUSE_WHEEL_UP;
        } else if ((b & 3) == 3) {
            ev->action = MOUSE_RELEASE;
        } else {
            ev->action = (b & 32) ? MOUSE_DRAG : MOUSE_PRESS;
            ev->button = (b & 3) + 1;
        }
        *used = 6;
        return 1;
    }
    char priv = 0;
    if (i < len_ && (buf_[i] == '<' || buf_[i] == '>' || buf_[i] == '?' || buf_[i] == '='))
        priv = buf_[i++];
    int params[8];
    int np = 0;
    params[0] = 0;
    bool have = false;
    while (i < len_) {
        char c = buf_[i];
        if (c >= '0' && c <= '9') {
            if (np < 8)
                params[np] = params[np] * 10 + (c - '0');
            have = true;
        } else if (c == ';' || c == ':') {
            if (np < 7)
                params[++np] = 0;
            have = true;
        } else {
            break;
        }
        i++;
    }
    if (i >= len_)
        return timed_out ? 0 : -1;
    char fin = buf_[i];
    if ((unsigned char)fin < 0x40 || (unsigned char)fin > 0x7e)
        return 0;
    int count = have ? np + 1 : 0;
    *used = i + 1;
    int p0 = count > 0 ? params[0] : 0;
    unsigned mods = count > 1 ? xmods(params[1]) : 0;

    if (priv == '<' && (fin == 'M' || fin == 'm') && count >= 3) {
        int b = params[0];
        ev->key = KEY_MOUSE;
        ev->x = params[1] - 1;
        ev->y = params[2] - 1;
        if (b & 4)
            ev->mods |= MOD_SHIFT;
        if (b & 8)
            ev->mods |= MOD_ALT;
        if (b & 16)
            ev->mods |= MOD_CTRL;
        if (b & 64) {
            ev->action = (b & 1) ? MOUSE_WHEEL_DOWN : MOUSE_WHEEL_UP;
        } else {
            ev->button = (b & 3) + 1;
            ev->action = fin == 'm' ? MOUSE_RELEASE : (b & 32) ? MOUSE_DRAG : MOUSE_PRESS;
        }
        return 1;
    }
    if (priv)
        return 2;               /* a reply or something unknown: drop it */
    switch (fin) {
    case '~':
        if (p0 == 200) {
            ev->key = KEY_PASTE_BEGIN;
            return 1;
        }
        if (p0 == 201) {
            ev->key = KEY_PASTE_END;
            return 1;
        }
        ev->key = tilde_key(p0);
        ev->mods = mods;
        return ev->key ? 1 : 2;
    case '^': case '$': case '@':   /* rxvt: Ctrl, Shift, Ctrl+Shift */
        ev->key = tilde_key(p0);
        ev->mods = fin == '^' ? MOD_CTRL : fin == '$' ? MOD_SHIFT : MOD_CTRL | MOD_SHIFT;
        return ev->key ? 1 : 2;
    case 'Z':
        ev->key = KEY_TAB;
        ev->mods = MOD_SHIFT;
        return 1;
    case 'I':
        ev->key = KEY_FOCUS_IN;
        return 1;
    case 'O':
        ev->key = KEY_FOCUS_OUT;
        return 1;
    case 'u': {                 /* kitty: CSI code ; mods u */
        ev->mods = mods;
        switch (p0) {
        case 13: ev->key = KEY_ENTER; break;
        case 9: ev->key = KEY_TAB; break;
        case 27: ev->key = KEY_ESC; break;
        case 127: ev->key = KEY_BACKSPACE; break;
        default:
            ev->key = KEY_CHAR;
            ev->ch = (uint32_t)p0;
            break;
        }
        return 1;
    }
    case 'q':                   /* iris-ansi: CSI nnn q */
        if (p0 >= 1 && p0 <= 12) {
            ev->key = KEY_F1 + p0 - 1;
            return 1;
        }
        if (p0 >= 13 && p0 <= 24) {
            ev->key = KEY_F1 + p0 - 13;
            ev->mods = MOD_SHIFT;
            return 1;
        }
        if (p0 >= 25 && p0 <= 36) {
            ev->key = KEY_F1 + p0 - 25;
            ev->mods = MOD_CTRL;
            return 1;
        }
        switch (p0) {
        case 139: ev->key = KEY_INSERT; break;
        case 146: ev->key = KEY_END; break;
        case 150: ev->key = KEY_PGUP; break;
        case 154: ev->key = KEY_PGDN; break;
        case 143: ev->key = KEY_HOME; ev->mods = MOD_SHIFT; break;
        case 147: ev->key = KEY_END; ev->mods = MOD_SHIFT; break;
        case 158: ev->key = KEY_LEFT; ev->mods = MOD_SHIFT; break;
        case 167: ev->key = KEY_RIGHT; ev->mods = MOD_SHIFT; break;
        default: return 2;
        }
        return 1;
    case '[':                   /* Linux console: CSI [ A..E is F1..F5 */
        if (i + 1 < len_ && buf_[i + 1] >= 'A' && buf_[i + 1] <= 'E') {
            ev->key = KEY_F1 + (buf_[i + 1] - 'A');
            *used = i + 2;
            return 1;
        }
        if (i + 1 >= len_)
            return timed_out ? 2 : -1;
        return 2;
    default:
        ev->key = letter_key(fin);
        if (fin == 'M')
            ev->key = KEY_NONE;
        ev->mods = mods;
        return ev->key ? 1 : 2;
    }
}

/* ESC O x, and old xterm's ESC O <mod> x */
int Input::parse_ss3(Event* ev, size_t* used) const
{
    if (len_ < 3)
        return -1;
    size_t i = 2;
    unsigned mods = 0;
    if (buf_[i] >= '2' && buf_[i] <= '8') {
        if (len_ < 4)
            return -1;
        mods = xmods(buf_[i] - '0');
        i++;
    }
    ev->key = letter_key(buf_[i]);
    ev->mods = mods;
    *used = i + 1;
    return ev->key ? 1 : 2;
}

void Input::control(unsigned char c, Event* ev) const
{
    switch (c) {
    case '\r': case '\n': ev->key = KEY_ENTER; return;
    case '\t': ev->key = KEY_TAB; return;
    case 0x7f: case 0x08: ev->key = KEY_BACKSPACE; return;
    case 0x1b: ev->key = KEY_ESC; return;
    case 0: ev->key = KEY_CHAR; ev->ch = ' '; ev->mods = MOD_CTRL; return;
    default:
        ev->key = KEY_CHAR;
        ev->mods = MOD_CTRL;
        ev->ch = c <= 26 ? (uint32_t)('a' + c - 1) : (uint32_t)("\\]^_"[c - 28]);
        return;
    }
}

bool Input::next(Event* ev, bool timed_out)
{
    for (;;) {
        if (!len_)
            return false;
        *ev = Event();
        size_t used = 0;
        int r = match_table(ev, &used);
        if (r == 1) {
            consume(used);
            return true;
        }
        if (r == -1 && !timed_out)
            return false;
        unsigned char c = (unsigned char)buf_[0];
        if (c == 0x1b) {
            if (len_ == 1) {
                if (!timed_out)
                    return false;
                ev->key = KEY_ESC;
                consume(1);
                return true;
            }
            int got = 0;
            if (buf_[1] == '[')
                got = parse_csi(ev, &used, timed_out);
            else if (buf_[1] == 'O')
                got = parse_ss3(ev, &used);
            if (got == -1) {
                if (!timed_out)
                    return false;
                got = 0;
            }
            if (got == 1) {
                consume(used);
                return true;
            }
            if (got == 2) {             /* known shape, nothing to report */
                consume(used);
                continue;
            }
            if (buf_[1] == 0x1b) {      /* Esc Esc */
                ev->key = KEY_ESC;
                consume(2);
                return true;
            }
            /* Alt + the next key */
            consume(1);
            Event inner;
            if (!next(&inner, true)) {
                ev->key = KEY_ESC;
                return true;
            }
            *ev = inner;
            ev->mods |= MOD_ALT;
            return true;
        }
        if (c < 0x20 || c == 0x7f) {
            control(c, ev);
            consume(1);
            return true;
        }
        if (c >= 0x80 && utf8_) {
            int n = (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 1;
            if ((size_t)n > len_) {
                if (!timed_out)
                    return false;
                n = 1;
            }
            uint32_t v = n == 2 ? c & 0x1f : n == 3 ? c & 0x0f : n == 4 ? c & 0x07 : c;
            for (int k = 1; k < n; k++)
                v = (v << 6) | ((unsigned char)buf_[k] & 0x3f);
            ev->key = KEY_CHAR;
            ev->ch = v;
            consume((size_t)n);
            return true;
        }
        ev->key = KEY_CHAR;
        ev->ch = c;
        consume(1);
        return true;
    }
}

} /* namespace tu */
