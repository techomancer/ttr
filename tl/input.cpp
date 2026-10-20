/* Keys, text, paste and focus encoded as xterm does. */
#include "terminal.h"

#include <stdio.h>
#include <string.h>

namespace tl {

namespace {

/* xterm's modifier parameter: 1 + shift + 2 alt + 4 ctrl + 8 meta. */
int mod_param(unsigned mods)
{
    int m = 1;
    if (mods & MOD_SHIFT) m += 1;
    if (mods & MOD_ALT) m += 2;
    if (mods & MOD_CTRL) m += 4;
    if (mods & MOD_META) m += 8;
    return m;
}

struct Tilde {
    int key;
    int num;
};

const Tilde k_tilde[] = {
    { KEY_INSERT, 2 }, { KEY_DELETE, 3 }, { KEY_PAGE_UP, 5 }, { KEY_PAGE_DOWN, 6 },
    { KEY_F1 + 4, 15 }, { KEY_F1 + 5, 17 }, { KEY_F1 + 6, 18 }, { KEY_F1 + 7, 19 },
    { KEY_F1 + 8, 20 }, { KEY_F1 + 9, 21 }, { KEY_F1 + 10, 23 }, { KEY_F1 + 11, 24 },
    { KEY_F1 + 12, 25 }, { KEY_F1 + 13, 26 }, { KEY_F1 + 14, 28 }, { KEY_F1 + 15, 29 },
    { KEY_F1 + 16, 31 }, { KEY_F1 + 17, 32 }, { KEY_F1 + 18, 33 }, { KEY_F1 + 19, 34 },
    { 0, 0 }
};

} /* namespace */

bool Terminal::key(int k, unsigned mods)
{
    char b[32];
    int m = mod_param(mods);
    bool alt = (mods & (MOD_ALT | MOD_META)) != 0;

    /* Cursor keys, Home, End, Begin and F1-F4: one final letter. */
    char letter = 0;
    switch (k) {
    case KEY_UP: letter = 'A'; break;
    case KEY_DOWN: letter = 'B'; break;
    case KEY_RIGHT: letter = 'C'; break;
    case KEY_LEFT: letter = 'D'; break;
    case KEY_HOME: letter = 'H'; break;
    case KEY_END: letter = 'F'; break;
    case KEY_BEGIN: letter = 'E'; break;
    case KEY_F1: letter = 'P'; break;
    case KEY_F1 + 1: letter = 'Q'; break;
    case KEY_F1 + 2: letter = 'R'; break;
    case KEY_F1 + 3: letter = 'S'; break;
    default: break;
    }
    if (letter) {
        bool fkey = k >= KEY_F1 && k <= KEY_F1 + 3;
        if (m > 1)
            sprintf(b, "\x1b[1;%d%c", m, letter);
        else if (fkey || modes_.app_cursor)
            sprintf(b, "\x1bO%c", letter);
        else
            sprintf(b, "\x1b[%c", letter);
        reply(b);
        return true;
    }
    for (int i = 0; k_tilde[i].key; i++) {
        if (k_tilde[i].key != k)
            continue;
        if (m > 1)
            sprintf(b, "\x1b[%d;%d~", k_tilde[i].num, m);
        else
            sprintf(b, "\x1b[%d~", k_tilde[i].num);
        reply(b);
        return true;
    }

    /* Keypad in application mode. */
    if (k >= KEY_KP_0 && k <= KEY_KP_SEPARATOR) {
        static const char app[] = "pqrstuvwxynojmkMXl";      /* 0-9 . / * - + Enter = , */
        static const char num[] = "0123456789./*-+\r=,";
        int i = k - KEY_KP_0;
        if (modes_.app_keypad) {
            sprintf(b, "\x1bO%c", app[i]);
        } else {
            b[0] = num[i];
            b[1] = 0;
            if (k == KEY_KP_ENTER && modes_.newline)
                strcpy(b, "\r\n");
        }
        reply(b);
        return true;
    }

    const char* s = 0;
    switch (k) {
    case KEY_ENTER:
        s = modes_.newline ? "\r\n" : "\r";
        break;
    case KEY_TAB:
        s = (mods & MOD_SHIFT) ? "\x1b[Z" : "\t";
        if (mods & MOD_SHIFT)
            alt = false;
        break;
    case KEY_BACKSPACE:
        if (mods & MOD_CTRL)
            s = backspace_del_ ? "\b" : "\x7f";
        else
            s = backspace_del_ ? "\x7f" : "\b";
        break;
    case KEY_ESCAPE:
        s = "\x1b";
        break;
    default:
        return false;
    }
    if (alt)
        reply("\x1b");
    reply(s);
    return true;
}

void Terminal::text_input(const char* utf8, size_t n, unsigned mods)
{
    if (n == 0)
        return;
    if (mods & (MOD_ALT | MOD_META)) {
        static const uint8_t esc = 0x1b;
        host_->write_pty(&esc, 1);
    }
    host_->write_pty((const uint8_t*)utf8, n);
}

void Terminal::paste(const char* utf8, size_t n)
{
    Buffer b;
    bool bracket = modes_.bracketed_paste;
    if (bracket)
        b.append("\x1b[200~");
    for (size_t i = 0; i < n; i++) {
        uint8_t c = (uint8_t)utf8[i];
        if (c == '\r' && i + 1 < n && utf8[i + 1] == '\n')
            continue;                   /* CR LF becomes one CR */
        if (c == '\n')
            c = '\r';
        /* Inside brackets, no byte may end the paste early or start a
         * sequence; outside, C0 controls other than tab and CR could
         * make the shell run things the user did not see. */
        if (c == 0x1b || (c < 0x20 && c != '\t' && c != '\r') || c == 0x7f)
            continue;
        b.append_byte(c);
    }
    if (bracket)
        b.append("\x1b[201~");
    host_->write_pty(b.data(), b.size());
}

void Terminal::focus(bool in)
{
    if (modes_.focus_events)
        reply(in ? "\x1b[I" : "\x1b[O");
}

} /* namespace tl */

namespace tl {

/* xterm mouse reporting: modes 9 (X10), 1000 (press and release), 1002
 * (plus motion with a button held), 1003 (all motion); encodings
 * default, 1005 (UTF-8), 1006 (SGR), 1015 (urxvt), 1016 (SGR pixels). */
bool Terminal::mouse(MouseAction action, int button, int col, int row, unsigned mods,
                     int px, int py)
{
    int mode = modes_.mouse;
    if (!mode)
        return false;
    bool wheel = button >= 4 && button <= 7;
    if (action == MOUSE_PRESS && button >= 1 && button <= 3)
        mouse_buttons_ |= 1 << (button - 1);
    if (action == MOUSE_RELEASE && button >= 1 && button <= 3)
        mouse_buttons_ &= ~(1 << (button - 1));

    /* Which events each mode reports. */
    if (mode == 9 && (action != MOUSE_PRESS || wheel))
        return true;                            /* consumed, not reported */
    if (action == MOUSE_RELEASE && wheel)
        return true;
    if (action == MOUSE_MOTION) {
        if (mode < 1002)
            return true;
        int held = mouse_buttons_;
        if (mode == 1002 && !held)
            return true;
        if (col == mouse_last_col_ && row == mouse_last_row_ && modes_.mouse_encoding != 1016)
            return true;                        /* same cell: nothing new */
        button = held & 1 ? 1 : held & 2 ? 2 : held & 4 ? 3 : 0;
    }
    mouse_last_col_ = col;
    mouse_last_row_ = row;

    int cb;
    if (button == 0)
        cb = 3;                                 /* motion with no button */
    else if (wheel)
        cb = 64 + (button - 4);
    else if (button >= 8)
        cb = 128 + (button - 8);
    else
        cb = button - 1;
    bool sgr = modes_.mouse_encoding == 1006 || modes_.mouse_encoding == 1016;
    if (action == MOUSE_RELEASE && !sgr)
        cb = 3;                                 /* legacy: release does not say which */
    if (action == MOUSE_MOTION)
        cb += 32;
    if (mode != 9) {
        if (mods & MOD_SHIFT) cb += 4;
        if (mods & (MOD_ALT | MOD_META)) cb += 8;
        if (mods & MOD_CTRL) cb += 16;
    }
    int x = col + 1, y = row + 1;
    char b[64];
    switch (modes_.mouse_encoding) {
    case 1006:
        sprintf(b, "\x1b[<%d;%d;%d%c", cb, x, y, action == MOUSE_RELEASE ? 'm' : 'M');
        break;
    case 1016:
        sprintf(b, "\x1b[<%d;%d;%d%c", cb, px + 1, py + 1, action == MOUSE_RELEASE ? 'm' : 'M');
        break;
    case 1015:
        sprintf(b, "\x1b[%d;%d;%dM", cb + 32, x, y);
        break;
    case 1005: {
        Buffer u;
        u.append("\x1b[M");
        u.append_utf8((uint32_t)(cb + 32));
        u.append_utf8((uint32_t)(x + 32));
        u.append_utf8((uint32_t)(y + 32));
        reply(u.c_str());
        return true;
    }
    default:
        /* One byte per value: positions past 223 cannot be sent. */
        if (x > 223 || y > 223)
            return true;
        b[0] = 0x1b;
        b[1] = '[';
        b[2] = 'M';
        b[3] = (char)(cb + 32);
        b[4] = (char)(x + 32);
        b[5] = (char)(y + 32);
        host_->write_pty((const uint8_t*)b, 6);
        return true;
    }
    reply(b);
    return true;
}

} /* namespace tl */
