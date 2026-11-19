/*
 * ti::encode_png: RGBA into a PNG file in memory, with tEXt chunks
 * (thumbnails carry their source's URI and time in them). stb_image_write
 * compresses; the text chunks go in after IHDR.
 */
#include "ti.h"

#include <stdlib.h>
#include <string.h>

extern "C" {
int ti_stbiw_png(void (*fn)(void* ctx, void* data, int n), void* ctx, int w, int h,
                 const unsigned char* rgba);
}

namespace ti {

namespace {

struct Buf {
    unsigned char* d;
    size_t n, cap;
    bool failed;
};

bool grow(Buf* b, size_t more)
{
    if (b->failed)
        return false;
    if (b->n + more <= b->cap)
        return true;
    size_t cap = b->cap ? b->cap * 2 : 65536;
    while (cap < b->n + more)
        cap *= 2;
    unsigned char* nd = (unsigned char*)realloc(b->d, cap);
    if (!nd) {
        b->failed = true;
        return false;
    }
    b->d = nd;
    b->cap = cap;
    return true;
}

void put(Buf* b, const void* p, size_t n)
{
    if (grow(b, n)) {
        memcpy(b->d + b->n, p, n);
        b->n += n;
    }
}

void write_fn(void* ctx, void* data, int n)
{
    put((Buf*)ctx, data, (size_t)n);
}

unsigned long crc_table[256];
bool crc_ready = false;

unsigned long crc(const unsigned char* p, size_t n)
{
    unsigned long c = 0xffffffffUL;
    for (size_t i = 0; i < n; i++)
        c = crc_table[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffUL;
}

void be32(unsigned char* p, unsigned long v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

} /* namespace */

void encode_init()
{
    if (crc_ready)
        return;
    for (unsigned long n = 0; n < 256; n++) {
        unsigned long c = n;
        for (int k = 0; k < 8; k++)
            c = c & 1 ? 0xedb88320UL ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
    crc_ready = true;
}

bool encode_png(const Image& im, const char* const* text, unsigned char** out, size_t* len)
{
    *out = 0;
    *len = 0;
    if (im.empty())
        return false;
    encode_init();
    Buf png = { 0, 0, 0, false };
    if (!ti_stbiw_png(write_fn, &png, im.w, im.h, im.rgba) || png.failed || png.n < 33) {
        free(png.d);
        return false;
    }
    /* The signature and IHDR (8 + 25 bytes), the text chunks, the rest. */
    Buf o = { 0, 0, 0, false };
    put(&o, png.d, 33);
    for (int i = 0; text && text[i] && text[i + 1]; i += 2) {
        size_t kl = strlen(text[i]), vl = strlen(text[i + 1]);
        if (kl == 0 || kl > 79)
            continue;
        size_t dl = kl + 1 + vl;
        unsigned char hdr[8];
        be32(hdr, (unsigned long)dl);
        memcpy(hdr + 4, "tEXt", 4);
        size_t start = o.n + 4;
        put(&o, hdr, 8);
        put(&o, text[i], kl + 1);
        put(&o, text[i + 1], vl);
        unsigned char c[4];
        if (!o.failed)
            be32(c, crc(o.d + start, 4 + dl));
        put(&o, c, 4);
    }
    put(&o, png.d + 33, png.n - 33);
    free(png.d);
    if (o.failed) {
        free(o.d);
        return false;
    }
    *out = o.d;
    *len = o.n;
    return true;
}

bool png_text(const unsigned char* d, size_t n, const char* key, char* value, size_t len)
{
    if (n < 8 || memcmp(d, "\x89PNG\r\n\x1a\n", 8) != 0 || !len)
        return false;
    size_t at = 8, kl = strlen(key);
    while (at + 12 <= n) {
        unsigned long cl = ((unsigned long)d[at] << 24) | ((unsigned long)d[at + 1] << 16) |
                           ((unsigned long)d[at + 2] << 8) | d[at + 3];
        if (cl > n - at - 12)
            return false;
        const unsigned char* t = d + at + 4;
        if (memcmp(t, "IDAT", 4) == 0 || memcmp(t, "IEND", 4) == 0)
            return false;
        if (memcmp(t, "tEXt", 4) == 0 && cl > kl && memcmp(t + 4, key, kl) == 0 && t[4 + kl] == 0) {
            size_t vl = cl - kl - 1;
            if (vl >= len)
                vl = len - 1;
            memcpy(value, t + 4 + kl + 1, vl);
            value[vl] = 0;
            return true;
        }
        at += 12 + cl;
    }
    return false;
}

} /* namespace ti */
