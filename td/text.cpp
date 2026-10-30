#include "td.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace td {

Text::Text() : p_(0), n_(0), cap_(0) {}

Text::~Text()
{
    free(p_);
}

void Text::add(const char* s, size_t n)
{
    if (n_ + n + 1 > cap_) {
        size_t cap = cap_ ? cap_ : 64;
        while (cap < n_ + n + 1)
            cap *= 2;
        char* p = (char*)realloc(p_, cap);
        if (!p)
            return;
        p_ = p;
        cap_ = cap;
    }
    memcpy(p_ + n_, s, n);
    n_ += n;
    p_[n_] = 0;
}

void Text::add(const char* s)
{
    add(s, strlen(s));
}

void Text::add_char(char c)
{
    add(&c, 1);
}

void Text::addf(const char* fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    add(buf);
}

void Text::truncate(size_t n)
{
    if (n < n_) {
        n_ = n;
        p_[n_] = 0;
    }
}

void Text::add_uint(uint64_t v)
{
    char t[24];
    int n = 0;
    do {
        t[n++] = (char)('0' + (int)(v % 10));
        v /= 10;
    } while (v);
    while (n)
        add_char(t[--n]);
}

void Text::add_int(int64_t v)
{
    if (v < 0) {
        add_char('-');
        add_uint((uint64_t)0 - (uint64_t)v);
    } else {
        add_uint((uint64_t)v);
    }
}

void Text::add_hex(uint64_t v, int digits)
{
    char t[17];
    int n = 0;
    do {
        t[n++] = "0123456789abcdef"[v & 15];
        v >>= 4;
    } while (v);
    add("0x");
    int i;
    for (i = n; i < digits; i++)
        add_char('0');
    while (n)
        add_char(t[--n]);
}

} /* namespace td */
