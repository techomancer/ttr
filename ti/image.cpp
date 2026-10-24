#include "internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ti {

using namespace internal;

Image::Image() : w(0), h(0), rgba(0) {}

Image::~Image()
{
    free(rgba);
}

bool Image::alloc(int nw, int nh)
{
    clear();
    if (nw <= 0 || nh <= 0 || (double)nw * (double)nh > (double)k_max_pixels)
        return false;
    rgba = (unsigned char*)malloc((size_t)nw * (size_t)nh * 4);
    if (!rgba)
        return false;
    w = nw;
    h = nh;
    return true;
}

void Image::clear()
{
    free(rgba);
    rgba = 0;
    w = h = 0;
}

void Image::take(Image& o)
{
    if (&o == this)
        return;
    clear();
    w = o.w;
    h = o.h;
    rgba = o.rgba;
    o.w = o.h = 0;
    o.rgba = 0;
}

const char* format_name(Format f)
{
    static const char* const names[FMT_COUNT] = {
        "unknown", "PNG", "JPEG", "GIF", "TIFF", "BMP", "PNM", "SGI"
    };
    return f >= 0 && f < FMT_COUNT ? names[f] : names[0];
}

Format sniff(const unsigned char* d, size_t n)
{
    if (n >= 8 && memcmp(d, "\x89PNG\r\n\x1a\n", 8) == 0)
        return FMT_PNG;
    if (n >= 3 && d[0] == 0xff && d[1] == 0xd8 && d[2] == 0xff)
        return FMT_JPEG;
    if (n >= 6 && (memcmp(d, "GIF87a", 6) == 0 || memcmp(d, "GIF89a", 6) == 0))
        return FMT_GIF;
    if (n >= 4 && (memcmp(d, "II*\0", 4) == 0 || memcmp(d, "MM\0*", 4) == 0))
        return FMT_TIFF;
    if (n >= 18 && d[0] == 'B' && d[1] == 'M') {
        /* The info header's size: one of the known versions. */
        unsigned long hs = d[14] | (d[15] << 8) | ((unsigned long)d[16] << 16) |
                           ((unsigned long)d[17] << 24);
        if (hs == 12 || hs == 40 || hs == 52 || hs == 56 || hs == 64 || hs == 108 || hs == 124)
            return FMT_BMP;
    }
    if (n >= 3 && d[0] == 'P' && (d[1] == '5' || d[1] == '6') &&
        (d[2] == ' ' || d[2] == '\n' || d[2] == '\r' || d[2] == '\t'))
        return FMT_PNM;
    if (n >= 4 && d[0] == 0x01 && d[1] == 0xda && d[2] <= 1 && (d[3] == 1 || d[3] == 2))
        return FMT_SGI;
    return FMT_UNKNOWN;
}

namespace {

const char* system_name(Format f)
{
    switch (f) {
    case FMT_PNG: return png_system_name();
    case FMT_JPEG: return jpeg_system_name();
    case FMT_GIF: return gif_system_name();
    case FMT_TIFF: return tiff_system_name();
    default: return 0;
    }
}

bool system_decode(Ctx& c, Format f)
{
    switch (f) {
    case FMT_PNG: return png_system(c);
    case FMT_JPEG: return jpeg_system(c);
    case FMT_GIF: return gif_system(c);
    case FMT_TIFF: return tiff_system(c);
    default: return false;
    }
}

} /* namespace */

const char* decoder_name(Format f, Backend b)
{
    if (f == FMT_SGI)
        return b == BACKEND_SYSTEM ? 0 : "ti";
    const char* s = system_name(f);
    if (b == BACKEND_SYSTEM)
        return s;
    if (b == BACKEND_ANY && s)
        return s;
    return builtin_name(f);
}

namespace internal {

bool fail(Ctx& c, const char* fmt, ...)
{
    if (c.err && c.errlen) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(c.err, c.errlen, fmt, ap);
        va_end(ap);
    }
    return false;
}

bool check_size(Ctx& c, long w, long h)
{
    c.info->width = (int)w;
    c.info->height = (int)h;
    if (w <= 0 || h <= 0)
        return fail(c, "The image has no pixels.");
    if ((double)w * (double)h > (double)k_max_pixels)
        return fail(c, "The image is too big (%ld x %ld).", w, h);
    return true;
}

} /* namespace internal */

bool decode(const unsigned char* d, size_t n, Image* out, Info* info, char* err, size_t errlen,
            const DecodeOptions& opt)
{
    Info dummy;
    if (!info)
        info = &dummy;
    *info = Info();
    out->clear();
    if (err && errlen)
        err[0] = 0;
    Ctx c;
    c.d = d;
    c.n = n;
    c.out = out;
    c.info = info;
    c.err = err;
    c.errlen = errlen;
    c.reduce_to = opt.reduce_to;
    Format f = sniff(d, n);
    info->format = f;
    if (f == FMT_UNKNOWN)
        return fail(c, "Not an image ti knows.");
    const char* name = decoder_name(f, opt.backend);
    if (!name)
        return fail(c, "No %s decoder in this build.", format_name(f));
    info->decoder = name;
    bool ok;
    if (f == FMT_SGI)
        ok = sgi_decode(c);
    else if (opt.backend != BACKEND_BUILTIN && system_name(f))
        ok = system_decode(c, f);
    else
        ok = builtin_decode(c, f);
    if (!ok) {
        out->clear();
        if (err && errlen && !err[0])
            fail(c, "The %s file is damaged.", format_name(f));
    }
    return ok;
}

bool scale(const Image& in, int w, int h, Image* out)
{
    if (in.empty() || !out->alloc(w, h))
        return false;
    for (int ty = 0; ty < h; ty++) {
        int y0 = (int)((double)ty * in.h / h);
        int y1 = (int)((double)(ty + 1) * in.h / h);
        if (y1 <= y0)
            y1 = y0 + 1;
        if (y1 > in.h)
            y1 = in.h;
        unsigned char* o = out->row(ty);
        for (int tx = 0; tx < w; tx++, o += 4) {
            int x0 = (int)((double)tx * in.w / w);
            int x1 = (int)((double)(tx + 1) * in.w / w);
            if (x1 <= x0)
                x1 = x0 + 1;
            if (x1 > in.w)
                x1 = in.w;
            if (x1 - x0 == 1 && y1 - y0 == 1) {
                memcpy(o, in.row(y0) + x0 * 4, 4);
                continue;
            }
            /* Weighted by alpha, so transparent pixels lend no colour. */
            unsigned long r = 0, g = 0, b = 0, a = 0, cnt = 0;
            for (int y = y0; y < y1; y++) {
                const unsigned char* p = in.row(y) + x0 * 4;
                for (int x = x0; x < x1; x++, p += 4) {
                    r += (unsigned long)p[0] * p[3];
                    g += (unsigned long)p[1] * p[3];
                    b += (unsigned long)p[2] * p[3];
                    a += p[3];
                    cnt++;
                }
            }
            if (a) {
                o[0] = (unsigned char)(r / a);
                o[1] = (unsigned char)(g / a);
                o[2] = (unsigned char)(b / a);
            } else {
                o[0] = o[1] = o[2] = 0;
            }
            o[3] = (unsigned char)(a / cnt);
        }
    }
    return true;
}

void fit(int iw, int ih, int max_w, int max_h, double aspect, bool grow, int* w, int* h)
{
    if (iw <= 0 || ih <= 0 || max_w <= 0 || max_h <= 0 || aspect <= 0) {
        *w = *h = 0;
        return;
    }
    /* w * aspect / h == iw / ih */
    double ratio = (double)iw / ((double)ih * aspect);
    double th = max_h, tw = max_h * ratio;
    if (tw > max_w) {
        tw = max_w;
        th = max_w / ratio;
    }
    if (!grow && th > ih) {
        th = ih;
        tw = ih * ratio;
    }
    *w = (int)(tw + 0.5);
    *h = (int)(th + 0.5);
    if (*w < 1)
        *w = 1;
    if (*h < 1)
        *h = 1;
    if (*w > max_w)
        *w = max_w;
    if (*h > max_h)
        *h = max_h;
}

void flatten(Image* im, unsigned long bg)
{
    unsigned br = (unsigned)(bg >> 16) & 255, bgc = (unsigned)(bg >> 8) & 255, bb = (unsigned)bg & 255;
    size_t n = (size_t)im->w * (size_t)im->h;
    unsigned char* p = im->rgba;
    for (size_t i = 0; i < n; i++, p += 4) {
        unsigned a = p[3];
        if (a == 255)
            continue;
        p[0] = (unsigned char)((p[0] * a + br * (255 - a) + 127) / 255);
        p[1] = (unsigned char)((p[1] * a + bgc * (255 - a) + 127) / 255);
        p[2] = (unsigned char)((p[2] * a + bb * (255 - a) + 127) / 255);
        p[3] = 255;
    }
}

} /* namespace ti */
