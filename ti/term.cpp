#include "term.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ti {

/* ------------------------------------------------------------------ */
/* Choosing                                                            */

const char* protocol_name(Protocol p)
{
    static const char* const names[PROTO_COUNT] = { "off", "blocks", "kitty", "sixel", "iterm" };
    return p >= 0 && p < PROTO_COUNT ? names[p] : "blocks";
}

Protocol protocol_from_setting(const char* s)
{
    if (s && *s && strcmp(s, "auto") != 0)
        for (int i = 0; i < PROTO_COUNT; i++)
            if (strcmp(s, protocol_name((Protocol)i)) == 0)
                return (Protocol)i;
    return detect_protocol();
}

namespace {

bool env_is(const char* name, const char* value)
{
    const char* v = getenv(name);
    return v && strcmp(v, value) == 0;
}

bool env_starts(const char* name, const char* prefix)
{
    const char* v = getenv(name);
    return v && strncmp(v, prefix, strlen(prefix)) == 0;
}

} /* namespace */

Protocol detect_protocol()
{
    /* A multiplexer passes none of them through by default. */
    if (getenv("TMUX") || env_starts("TERM", "screen") || env_starts("TERM", "tmux"))
        return PROTO_BLOCKS;
    if (getenv("KITTY_WINDOW_ID") || env_is("TERM", "xterm-kitty") ||
        env_is("TERM_PROGRAM", "ttrt") || env_starts("TERM", "ttrt") ||
        env_is("TERM_PROGRAM", "ghostty") || env_is("TERM", "xterm-ghostty"))
        return PROTO_KITTY;
    if (env_is("TERM_PROGRAM", "iTerm.app") || env_is("TERM_PROGRAM", "WezTerm"))
        return PROTO_ITERM;
    if (env_starts("TERM", "foot") || env_starts("TERM", "mlterm") || env_starts("TERM", "yaft") ||
        env_is("TERM_PROGRAM", "contour") || getenv("KONSOLE_VERSION") || getenv("WT_SESSION"))
        return PROTO_SIXEL;
    return PROTO_BLOCKS;
}

/* ------------------------------------------------------------------ */
/* Bytes                                                               */

Bytes::Bytes() : d(0), n(0), failed(false), cap_(0) {}

Bytes::~Bytes()
{
    free(d);
}

void Bytes::add(const char* s, size_t k)
{
    if (!k)
        return;
    if (n + k > cap_) {
        size_t c = cap_ ? cap_ : 4096;
        while (c < n + k)
            c *= 2;
        char* nd = (char*)realloc(d, c);
        if (!nd) {
            failed = true;
            return;
        }
        d = nd;
        cap_ = c;
    }
    memcpy(d + n, s, k);
    n += k;
}

void Bytes::add(const char* s)
{
    add(s, strlen(s));
}

void Bytes::addf(const char* fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    add(buf);
}

namespace {

void base64(const unsigned char* p, size_t n, Bytes* out)
{
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char buf[4096];
    size_t k = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned long v = (unsigned long)p[i] << 16;
        if (i + 1 < n)
            v |= (unsigned long)p[i + 1] << 8;
        if (i + 2 < n)
            v |= p[i + 2];
        buf[k++] = t[(v >> 18) & 63];
        buf[k++] = t[(v >> 12) & 63];
        buf[k++] = i + 1 < n ? t[(v >> 6) & 63] : '=';
        buf[k++] = i + 2 < n ? t[v & 63] : '=';
        if (k + 4 > sizeof(buf)) {
            out->add(buf, k);
            k = 0;
        }
    }
    out->add(buf, k);
}

} /* namespace */

/* ------------------------------------------------------------------ */
/* kitty                                                               */

void encode_kitty(const Image& im, int cols, int rows, unsigned id, Bytes* out)
{
    if (im.empty())
        return;
    /* RGBA (f=32) in chunks of at most 4096 base64 bytes (3072 raw);
     * q=2: no replies, C=1: the cursor stays. */
    const size_t chunk = 3072;
    size_t total = (size_t)im.w * (size_t)im.h * 4;
    for (size_t at = 0; at < total; at += chunk) {
        size_t k = total - at < chunk ? total - at : chunk;
        bool more = at + k < total;
        if (at == 0) {
            out->addf("\033_Ga=T,f=32,s=%d,v=%d,", im.w, im.h);
            if (cols > 0)
                out->addf("c=%d,", cols);
            if (rows > 0)
                out->addf("r=%d,", rows);
            out->addf("i=%u,q=2,C=1,m=%d;", id, more ? 1 : 0);
        } else
            out->addf("\033_Gm=%d;", more ? 1 : 0);
        base64(im.rgba + at, k, out);
        out->add("\033\\");
    }
}

void kitty_delete(unsigned id, Bytes* out)
{
    out->addf("\033_Ga=d,d=I,i=%u,q=2\033\\", id);
}

/* ------------------------------------------------------------------ */
/* Sixel                                                               */

namespace {

/* A pixel's colour in the 6x6x6 cube; a 4x4 Bayer matrix spreads the
 * error. */
int cube_index(const unsigned char* p, int x, int y)
{
    static const int bayer[4][4] = {
        { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 }
    };
    /* 51 levels between cube steps; the threshold dithers. */
    int t = bayer[y & 3][x & 3] * 51 / 16;
    int q[3];
    for (int k = 0; k < 3; k++) {
        int v = p[k] / 51, rest = p[k] % 51;
        q[k] = v + (rest > t && v < 5 ? 1 : 0);
    }
    return q[0] * 36 + q[1] * 6 + q[2];
}

} /* namespace */

void encode_sixel(const Image& im, Bytes* out)
{
    if (im.empty())
        return;
    int w = im.w, h = im.h;
    out->addf("\033P0;1;0q\"1;1;%d;%d", w, h);
    /* Only the registers the image uses. */
    bool any[216];
    memset(any, 0, sizeof(any));
    for (int y = 0; y < h; y++) {
        const unsigned char* p = im.row(y);
        for (int x = 0; x < w; x++, p += 4)
            any[cube_index(p, x, y)] = true;
    }
    for (int i = 0; i < 216; i++)
        if (any[i])
            out->addf("#%d;2;%d;%d;%d", i, (i / 36) * 20, (i / 6 % 6) * 20, (i % 6) * 20);
    unsigned char* idx = (unsigned char*)malloc((size_t)w * 6);
    unsigned char* bits = (unsigned char*)malloc((size_t)w);
    if (!idx || !bits) {
        free(idx);
        free(bits);
        out->failed = true;
        return;
    }
    for (int band = 0; band < h; band += 6) {
        int bh = h - band < 6 ? h - band : 6;
        bool used[216];
        memset(used, 0, sizeof(used));
        for (int r = 0; r < bh; r++) {
            const unsigned char* p = im.row(band + r);
            for (int x = 0; x < w; x++, p += 4) {
                int c = cube_index(p, x, band + r);
                idx[r * w + x] = (unsigned char)c;
                used[c] = true;
            }
        }
        bool first = true;
        for (int c = 0; c < 216; c++) {
            if (!used[c])
                continue;
            int last = -1;
            for (int x = 0; x < w; x++) {
                unsigned char b = 0;
                for (int r = 0; r < bh; r++)
                    if (idx[r * w + x] == c)
                        b |= (unsigned char)(1 << r);
                bits[x] = b;
                if (b)
                    last = x;
            }
            if (!first)
                out->add("$", 1);
            first = false;
            out->addf("#%d", c);
            for (int x = 0; x <= last;) {
                int run = 1;
                while (x + run <= last && bits[x + run] == bits[x])
                    run++;
                char ch = (char)(63 + bits[x]);
                if (run > 3) {
                    out->addf("!%d%c", run, ch);
                } else {
                    for (int k = 0; k < run; k++)
                        out->add(&ch, 1);
                }
                x += run;
            }
        }
        out->add("-", 1);
    }
    free(idx);
    free(bits);
    out->add("\033\\");
}

/* ------------------------------------------------------------------ */
/* PNG and iTerm2                                                      */

namespace {

unsigned long crc_table[256];

void init_crc()
{
    if (crc_table[1])
        return;
    for (unsigned long n = 0; n < 256; n++) {
        unsigned long c = n;
        for (int k = 0; k < 8; k++)
            c = c & 1 ? 0xedb88320UL ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

unsigned long crc(unsigned long c, const unsigned char* p, size_t n)
{
    c ^= 0xffffffffUL;
    for (size_t i = 0; i < n; i++)
        c = crc_table[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return (c ^ 0xffffffffUL) & 0xffffffffUL;
}

void put32(unsigned char* p, unsigned long v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

void chunk(Bytes* out, const char* type, const unsigned char* data, size_t n)
{
    unsigned char b[8];
    put32(b, (unsigned long)n);
    memcpy(b + 4, type, 4);
    out->add((const char*)b, 8);
    out->add((const char*)data, n);
    unsigned long c = crc(crc(0, b + 4, 4), data, n);
    put32(b, c);
    out->add((const char*)b, 4);
}

} /* namespace */

void encode_png(const Image& im, Bytes* out)
{
    if (im.empty())
        return;
    init_crc();
    out->add("\x89PNG\r\n\x1a\n", 8);
    unsigned char ihdr[13];
    put32(ihdr, (unsigned long)im.w);
    put32(ihdr + 4, (unsigned long)im.h);
    ihdr[8] = 8;        /* bits */
    ihdr[9] = 6;        /* RGBA */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(out, "IHDR", ihdr, 13);
    /* zlib with stored blocks: each row is filter 0 and its pixels. */
    size_t rb = (size_t)im.w * 4 + 1;
    size_t raw = rb * (size_t)im.h;
    size_t blocks = (raw + 65534) / 65535;
    size_t zn = 2 + raw + blocks * 5 + 4;
    unsigned char* z = (unsigned char*)malloc(zn);
    if (!z) {
        out->failed = true;
        return;
    }
    size_t k = 0;
    z[k++] = 0x78;
    z[k++] = 0x01;
    unsigned long a1 = 1, a2 = 0;
    size_t done = 0;
    int y = 0;
    size_t in_row = 0;          /* bytes of row y already written */
    while (done < raw) {
        size_t len = raw - done < 65535 ? raw - done : 65535;
        z[k++] = (unsigned char)(done + len == raw ? 1 : 0);
        z[k++] = (unsigned char)(len & 0xff);
        z[k++] = (unsigned char)(len >> 8);
        z[k++] = (unsigned char)(~len & 0xff);
        z[k++] = (unsigned char)((~len >> 8) & 0xff);
        size_t left = len;
        while (left) {
            unsigned char b;
            if (in_row == 0)
                b = 0;
            else
                b = im.row(y)[in_row - 1];
            z[k++] = b;
            a1 = (a1 + b) % 65521;
            a2 = (a2 + a1) % 65521;
            if (++in_row == rb) {
                in_row = 0;
                y++;
            }
            left--;
        }
        done += len;
    }
    put32(z + k, (a2 << 16) | a1);
    k += 4;
    chunk(out, "IDAT", z, k);
    free(z);
    chunk(out, "IEND", 0, 0);
}

void encode_iterm(const Image& im, int cols, int rows, Bytes* out)
{
    Bytes png;
    encode_png(im, &png);
    if (png.failed || !png.n) {
        out->failed = true;
        return;
    }
    out->addf("\033]1337;File=inline=1;size=%lu;width=%d;height=%d;preserveAspectRatio=1:",
              (unsigned long)png.n, cols, rows);
    base64((const unsigned char*)png.d, png.n, out);
    out->add("\a", 1);
}

} /* namespace ti */
