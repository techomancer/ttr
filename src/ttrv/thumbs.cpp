/* ttrv's thumbnails on disk (thumbs.h). */
#include "thumbs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace ttrv {

namespace {

char g_base[1024];

/* MD5, RFC 1321. */
typedef unsigned int U32;

struct Md5 {
    U32 a, b, c, d;
    U32 lo, hi;
    unsigned char buf[64];
};

#define MD5_F(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define MD5_G(x, y, z) ((y) ^ ((z) & ((x) ^ (y))))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | ~(z)))
#define MD5_STEP(f, a, b, c, d, x, t, s) \
    (a) += f((b), (c), (d)) + (x) + (t); \
    (a) = (((a) << (s)) | (((a) & 0xffffffffU) >> (32 - (s)))); \
    (a) += (b);

const unsigned char* md5_body(Md5* ctx, const unsigned char* p, size_t size)
{
    U32 a = ctx->a, b = ctx->b, c = ctx->c, d = ctx->d;
    U32 x[16];
    do {
        U32 sa = a, sb = b, sc = c, sd = d;
        for (int i = 0; i < 16; i++)
            x[i] = (U32)p[i * 4] | ((U32)p[i * 4 + 1] << 8) | ((U32)p[i * 4 + 2] << 16) |
                   ((U32)p[i * 4 + 3] << 24);
        MD5_STEP(MD5_F, a, b, c, d, x[0], 0xd76aa478, 7)
        MD5_STEP(MD5_F, d, a, b, c, x[1], 0xe8c7b756, 12)
        MD5_STEP(MD5_F, c, d, a, b, x[2], 0x242070db, 17)
        MD5_STEP(MD5_F, b, c, d, a, x[3], 0xc1bdceee, 22)
        MD5_STEP(MD5_F, a, b, c, d, x[4], 0xf57c0faf, 7)
        MD5_STEP(MD5_F, d, a, b, c, x[5], 0x4787c62a, 12)
        MD5_STEP(MD5_F, c, d, a, b, x[6], 0xa8304613, 17)
        MD5_STEP(MD5_F, b, c, d, a, x[7], 0xfd469501, 22)
        MD5_STEP(MD5_F, a, b, c, d, x[8], 0x698098d8, 7)
        MD5_STEP(MD5_F, d, a, b, c, x[9], 0x8b44f7af, 12)
        MD5_STEP(MD5_F, c, d, a, b, x[10], 0xffff5bb1, 17)
        MD5_STEP(MD5_F, b, c, d, a, x[11], 0x895cd7be, 22)
        MD5_STEP(MD5_F, a, b, c, d, x[12], 0x6b901122, 7)
        MD5_STEP(MD5_F, d, a, b, c, x[13], 0xfd987193, 12)
        MD5_STEP(MD5_F, c, d, a, b, x[14], 0xa679438e, 17)
        MD5_STEP(MD5_F, b, c, d, a, x[15], 0x49b40821, 22)
        MD5_STEP(MD5_G, a, b, c, d, x[1], 0xf61e2562, 5)
        MD5_STEP(MD5_G, d, a, b, c, x[6], 0xc040b340, 9)
        MD5_STEP(MD5_G, c, d, a, b, x[11], 0x265e5a51, 14)
        MD5_STEP(MD5_G, b, c, d, a, x[0], 0xe9b6c7aa, 20)
        MD5_STEP(MD5_G, a, b, c, d, x[5], 0xd62f105d, 5)
        MD5_STEP(MD5_G, d, a, b, c, x[10], 0x02441453, 9)
        MD5_STEP(MD5_G, c, d, a, b, x[15], 0xd8a1e681, 14)
        MD5_STEP(MD5_G, b, c, d, a, x[4], 0xe7d3fbc8, 20)
        MD5_STEP(MD5_G, a, b, c, d, x[9], 0x21e1cde6, 5)
        MD5_STEP(MD5_G, d, a, b, c, x[14], 0xc33707d6, 9)
        MD5_STEP(MD5_G, c, d, a, b, x[3], 0xf4d50d87, 14)
        MD5_STEP(MD5_G, b, c, d, a, x[8], 0x455a14ed, 20)
        MD5_STEP(MD5_G, a, b, c, d, x[13], 0xa9e3e905, 5)
        MD5_STEP(MD5_G, d, a, b, c, x[2], 0xfcefa3f8, 9)
        MD5_STEP(MD5_G, c, d, a, b, x[7], 0x676f02d9, 14)
        MD5_STEP(MD5_G, b, c, d, a, x[12], 0x8d2a4c8a, 20)
        MD5_STEP(MD5_H, a, b, c, d, x[5], 0xfffa3942, 4)
        MD5_STEP(MD5_H, d, a, b, c, x[8], 0x8771f681, 11)
        MD5_STEP(MD5_H, c, d, a, b, x[11], 0x6d9d6122, 16)
        MD5_STEP(MD5_H, b, c, d, a, x[14], 0xfde5380c, 23)
        MD5_STEP(MD5_H, a, b, c, d, x[1], 0xa4beea44, 4)
        MD5_STEP(MD5_H, d, a, b, c, x[4], 0x4bdecfa9, 11)
        MD5_STEP(MD5_H, c, d, a, b, x[7], 0xf6bb4b60, 16)
        MD5_STEP(MD5_H, b, c, d, a, x[10], 0xbebfbc70, 23)
        MD5_STEP(MD5_H, a, b, c, d, x[13], 0x289b7ec6, 4)
        MD5_STEP(MD5_H, d, a, b, c, x[0], 0xeaa127fa, 11)
        MD5_STEP(MD5_H, c, d, a, b, x[3], 0xd4ef3085, 16)
        MD5_STEP(MD5_H, b, c, d, a, x[6], 0x04881d05, 23)
        MD5_STEP(MD5_H, a, b, c, d, x[9], 0xd9d4d039, 4)
        MD5_STEP(MD5_H, d, a, b, c, x[12], 0xe6db99e5, 11)
        MD5_STEP(MD5_H, c, d, a, b, x[15], 0x1fa27cf8, 16)
        MD5_STEP(MD5_H, b, c, d, a, x[2], 0xc4ac5665, 23)
        MD5_STEP(MD5_I, a, b, c, d, x[0], 0xf4292244, 6)
        MD5_STEP(MD5_I, d, a, b, c, x[7], 0x432aff97, 10)
        MD5_STEP(MD5_I, c, d, a, b, x[14], 0xab9423a7, 15)
        MD5_STEP(MD5_I, b, c, d, a, x[5], 0xfc93a039, 21)
        MD5_STEP(MD5_I, a, b, c, d, x[12], 0x655b59c3, 6)
        MD5_STEP(MD5_I, d, a, b, c, x[3], 0x8f0ccc92, 10)
        MD5_STEP(MD5_I, c, d, a, b, x[10], 0xffeff47d, 15)
        MD5_STEP(MD5_I, b, c, d, a, x[1], 0x85845dd1, 21)
        MD5_STEP(MD5_I, a, b, c, d, x[8], 0x6fa87e4f, 6)
        MD5_STEP(MD5_I, d, a, b, c, x[15], 0xfe2ce6e0, 10)
        MD5_STEP(MD5_I, c, d, a, b, x[6], 0xa3014314, 15)
        MD5_STEP(MD5_I, b, c, d, a, x[13], 0x4e0811a1, 21)
        MD5_STEP(MD5_I, a, b, c, d, x[4], 0xf7537e82, 6)
        MD5_STEP(MD5_I, d, a, b, c, x[11], 0xbd3af235, 10)
        MD5_STEP(MD5_I, c, d, a, b, x[2], 0x2ad7d2bb, 15)
        MD5_STEP(MD5_I, b, c, d, a, x[9], 0xeb86d391, 21)
        a += sa;
        b += sb;
        c += sc;
        d += sd;
        p += 64;
    } while (size -= 64);
    ctx->a = a;
    ctx->b = b;
    ctx->c = c;
    ctx->d = d;
    return p;
}

void md5_update(Md5* ctx, const unsigned char* data, size_t size)
{
    U32 saved = ctx->lo;
    if ((ctx->lo = (saved + (U32)size) & 0x1fffffff) < saved)
        ctx->hi++;
    ctx->hi += (U32)(size >> 29);
    size_t used = saved & 0x3f;
    if (used) {
        size_t avail = 64 - used;
        if (size < avail) {
            memcpy(&ctx->buf[used], data, size);
            return;
        }
        memcpy(&ctx->buf[used], data, avail);
        data += avail;
        size -= avail;
        md5_body(ctx, ctx->buf, 64);
    }
    if (size >= 64) {
        data = md5_body(ctx, data, size & ~(size_t)0x3f);
        size &= 0x3f;
    }
    memcpy(ctx->buf, data, size);
}

void md5_final(Md5* ctx, unsigned char out[16])
{
    size_t used = ctx->lo & 0x3f;
    ctx->buf[used++] = 0x80;
    size_t avail = 64 - used;
    if (avail < 8) {
        memset(&ctx->buf[used], 0, avail);
        md5_body(ctx, ctx->buf, 64);
        used = 0;
        avail = 64;
    }
    memset(&ctx->buf[used], 0, avail - 8);
    ctx->lo <<= 3;
    U32 v[2] = { ctx->lo, ctx->hi };
    for (int k = 0; k < 2; k++)
        for (int i = 0; i < 4; i++)
            ctx->buf[56 + k * 4 + i] = (unsigned char)(v[k] >> (8 * i));
    md5_body(ctx, ctx->buf, 64);
    U32 r[4] = { ctx->a, ctx->b, ctx->c, ctx->d };
    for (int k = 0; k < 4; k++)
        for (int i = 0; i < 4; i++)
            out[k * 4 + i] = (unsigned char)(r[k] >> (8 * i));
}

bool make_dir(const char* p)
{
    return mkdir(p, 0700) == 0 || errno == EEXIST;
}

} /* namespace */

void md5_hex(const unsigned char* d, size_t n, char out[33])
{
    Md5 ctx;
    ctx.a = 0x67452301;
    ctx.b = 0xefcdab89;
    ctx.c = 0x98badcfe;
    ctx.d = 0x10325476;
    ctx.lo = ctx.hi = 0;
    md5_update(&ctx, d, n);
    unsigned char dig[16];
    md5_final(&ctx, dig);
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        out[i * 2] = hex[dig[i] >> 4];
        out[i * 2 + 1] = hex[dig[i] & 15];
    }
    out[32] = 0;
}

bool thumbs_init()
{
    const char* home = getenv("HOME");
    char p[1100];
#if defined(__sgi)
    /* IRIX: no XDG; ttrv's own place, the spec's layout. */
    snprintf(p, sizeof(p), "%.1000s/.ttrv", home ? home : "/tmp");
    make_dir(p);
    snprintf(g_base, sizeof(g_base), "%.1000s/.ttrv/thumbnails", home ? home : "/tmp");
#else
    const char* xdg = getenv("XDG_CACHE_HOME");
    if (xdg && *xdg && xdg[0] == '/') {
        snprintf(g_base, sizeof(g_base), "%.1000s/thumbnails", xdg);
        make_dir(xdg);
    } else {
        snprintf(p, sizeof(p), "%.1000s/.cache", home ? home : "/tmp");
        make_dir(p);
        snprintf(g_base, sizeof(g_base), "%.1000s/.cache/thumbnails", home ? home : "/tmp");
    }
#endif
    if (!make_dir(g_base)) {
        g_base[0] = 0;
        return false;
    }
    snprintf(p, sizeof(p), "%s/normal", g_base);
    bool ok = make_dir(p);
    snprintf(p, sizeof(p), "%s/large", g_base);
    ok = make_dir(p) && ok;
    if (!ok)
        g_base[0] = 0;
    return ok;
}

void file_uri(const char* path, char* out, size_t n)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    const char* pre = "file://";
    for (; *pre && o + 1 < n; pre++)
        out[o++] = *pre;
    for (const unsigned char* s = (const unsigned char*)path; *s && o + 4 < n; s++) {
        unsigned char c = *s;
        /* What glib leaves as it is in a path: unreserved and sub-delims. */
        bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                     strchr("-._~!$&'()*+,;=:@/", c) != 0;
        if (plain) {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }
    out[o] = 0;
}

int thumb_class(int cell)
{
    return cell <= 128 ? 128 : 256;
}

bool thumb_path(const char* path, int size, char* out, size_t n)
{
    if (!g_base[0])
        return false;
    char uri[4300];
    file_uri(path, uri, sizeof(uri));
    char md5[33];
    md5_hex((const unsigned char*)uri, strlen(uri), md5);
    snprintf(out, n, "%s/%s/%s.png", g_base, size > 128 ? "large" : "normal", md5);
    return true;
}

unsigned char* thumb_read(const char* path, long mtime, int size, size_t* len, int* w, int* h)
{
    *w = *h = 0;
    char tp[1300];
    if (!thumb_path(path, size, tp, sizeof(tp)))
        return 0;
    FILE* f = fopen(tp, "rb");
    if (!f)
        return 0;
    size_t cap = 65536, n = 0;
    unsigned char* d = (unsigned char*)malloc(cap);
    while (d) {
        if (n == cap) {
            if (cap > (8UL << 20))
                break;
            unsigned char* nd = (unsigned char*)realloc(d, cap * 2);
            if (!nd)
                break;
            d = nd;
            cap *= 2;
        }
        size_t got = fread(d + n, 1, cap - n, f);
        if (!got)
            break;
        n += got;
    }
    fclose(f);
    char v[64];
    if (!d || !ti::png_text(d, n, "Thumb::MTime", v, sizeof(v)) || atol(v) != mtime) {
        free(d);
        return 0;
    }
    if (ti::png_text(d, n, "Thumb::Image::Width", v, sizeof(v)))
        *w = atoi(v);
    if (ti::png_text(d, n, "Thumb::Image::Height", v, sizeof(v)))
        *h = atoi(v);
    *len = n;
    return d;
}

bool thumb_write(const char* path, long mtime, int size, const ti::Image& im, int image_w,
                 int image_h)
{
    char tp[1300];
    if (!thumb_path(path, size, tp, sizeof(tp)))
        return false;
    char uri[4300], mt[32], iw[32], ih[32];
    file_uri(path, uri, sizeof(uri));
    sprintf(mt, "%ld", mtime);
    sprintf(iw, "%d", image_w);
    sprintf(ih, "%d", image_h);
    const char* text[] = { "Thumb::URI", uri, "Thumb::MTime", mt, "Thumb::Image::Width", iw,
                           "Thumb::Image::Height", ih, "Software", "ttrv", 0 };
    unsigned char* png = 0;
    size_t len = 0;
    if (!ti::encode_png(im, text, &png, &len))
        return false;
    /* Written whole under another name, then renamed: no one reads half. */
    char tmp[1400];
    /* The process and the image: two threads never share a name. */
    snprintf(tmp, sizeof(tmp), "%s.ttrv-%ld-%lx.tmp", tp, (long)getpid(), (unsigned long)&im);
    FILE* f = fopen(tmp, "wb");
    bool ok = f && fwrite(png, 1, len, f) == len;
    if (f)
        ok = fclose(f) == 0 && ok;
    free(png);
    if (ok) {
        chmod(tmp, 0600);
        ok = rename(tmp, tp) == 0;
    }
    if (!ok)
        remove(tmp);
    return ok;
}

} /* namespace ttrv */
