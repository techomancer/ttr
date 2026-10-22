/*
 * The line under the panels: UTF-8 text with a cursor (byte offsets on
 * character boundaries).
 */
#ifndef TTRC_CMDLINE_H
#define TTRC_CMDLINE_H

#include "../../tl/port.h"

#include <stddef.h>
#include <string.h>

namespace ttrc {

struct CmdLine {
    char buf[2048];
    size_t len, pos;

    CmdLine() : len(0), pos(0) { buf[0] = 0; }
    void clear()
    {
        len = pos = 0;
        buf[0] = 0;
    }
    void set(const char* s)
    {
        clear();
        insert_text(s);
    }
    void insert_text(const char* s)
    {
        size_t n = strlen(s);
        if (len + n >= sizeof(buf))
            n = sizeof(buf) - 1 - len;
        memmove(buf + pos + n, buf + pos, len - pos);
        memcpy(buf + pos, s, n);
        len += n;
        pos += n;
        buf[len] = 0;
    }
    void insert(uint32_t cp)
    {
        char b[5];
        size_t n;
        if (cp < 0x80) {
            b[0] = (char)cp;
            n = 1;
        } else if (cp < 0x800) {
            b[0] = (char)(0xc0 | (cp >> 6));
            b[1] = (char)(0x80 | (cp & 0x3f));
            n = 2;
        } else if (cp < 0x10000) {
            b[0] = (char)(0xe0 | (cp >> 12));
            b[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
            b[2] = (char)(0x80 | (cp & 0x3f));
            n = 3;
        } else {
            b[0] = (char)(0xf0 | (cp >> 18));
            b[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
            b[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
            b[3] = (char)(0x80 | (cp & 0x3f));
            n = 4;
        }
        b[n] = 0;
        insert_text(b);
    }
    size_t prev(size_t p) const
    {
        if (p == 0)
            return 0;
        p--;
        while (p > 0 && ((unsigned char)buf[p] & 0xc0) == 0x80)
            p--;
        return p;
    }
    size_t next(size_t p) const
    {
        if (p >= len)
            return len;
        p++;
        while (p < len && ((unsigned char)buf[p] & 0xc0) == 0x80)
            p++;
        return p;
    }
    void erase(size_t from, size_t to)
    {
        memmove(buf + from, buf + to, len - to);
        len -= to - from;
        buf[len] = 0;
        pos = from;
    }
};

} /* namespace ttrc */

#endif
