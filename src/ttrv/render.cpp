/* The viewer's software rendering (render.h). */
#include "render.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace ttrv {

namespace {

void fill(ti::Image* out, unsigned long bg)
{
    unsigned char c[4] = { (unsigned char)(bg >> 16), (unsigned char)(bg >> 8), (unsigned char)bg,
                           255 };
    for (int y = 0; y < out->h; y++) {
        unsigned char* p = out->row(y);
        for (int x = 0; x < out->w; x++, p += 4)
            memcpy(p, c, 4);
    }
}

/* The source pixel over the background (alpha kept as 255 out). */
inline void put(unsigned char* o, const int* acc, int a, const int* bg)
{
    for (int k = 0; k < 3; k++)
        o[k] = (unsigned char)((acc[k] * a + bg[k] * (255 - a) + 127) / 255);
    o[3] = 255;
}

} /* namespace */

bool render_view(const ti::Image& src, double zoom, double x0, double y0, int w, int h,
                 unsigned long bg, ti::Image* out)
{
    if (src.empty() || zoom <= 0 || !out->alloc(w, h))
        return false;
    fill(out, bg);
    int bgc[3] = { (int)((bg >> 16) & 255), (int)((bg >> 8) & 255), (int)(bg & 255) };
    /* The window pixels the image covers. */
    int ox0 = (int)ceil(-x0 * zoom), oy0 = (int)ceil(-y0 * zoom);
    int ox1 = (int)floor((src.w - x0) * zoom), oy1 = (int)floor((src.h - y0) * zoom);
    if (ox0 < 0) ox0 = 0;
    if (oy0 < 0) oy0 = 0;
    if (ox1 > w) ox1 = w;
    if (oy1 > h) oy1 = h;
    if (ox0 >= ox1 || oy0 >= oy1)
        return true;
    double inv = 1.0 / zoom;
    if (zoom < 1.0) {
        /* Shrinking: each window pixel the average of the source pixels
         * it covers (whole pixels; the error is under one pixel). */
        int* cx0 = (int*)malloc(sizeof(int) * (size_t)(ox1 - ox0 + 1) * 2);
        if (!cx0)
            return false;
        int* cx1 = cx0 + (ox1 - ox0 + 1);
        for (int x = ox0; x < ox1; x++) {
            int a = (int)(x0 + x * inv), b = (int)(x0 + (x + 1) * inv);
            if (b <= a) b = a + 1;
            if (a < 0) a = 0;
            if (b > src.w) b = src.w;
            cx0[x - ox0] = a;
            cx1[x - ox0] = b;
        }
        for (int y = oy0; y < oy1; y++) {
            int a = (int)(y0 + y * inv), b = (int)(y0 + (y + 1) * inv);
            if (b <= a) b = a + 1;
            if (a < 0) a = 0;
            if (b > src.h) b = src.h;
            unsigned char* o = out->row(y) + ox0 * 4;
            for (int x = ox0; x < ox1; x++, o += 4) {
                long sum[4] = { 0, 0, 0, 0 };
                int xa = cx0[x - ox0], xb = cx1[x - ox0];
                for (int sy = a; sy < b; sy++) {
                    const unsigned char* s = src.row(sy) + xa * 4;
                    for (int sx = xa; sx < xb; sx++, s += 4) {
                        /* Weighted by alpha, so transparent pixels do not
                         * darken their neighbours. */
                        sum[0] += s[0] * s[3];
                        sum[1] += s[1] * s[3];
                        sum[2] += s[2] * s[3];
                        sum[3] += s[3];
                    }
                }
                long n = (long)(b - a) * (xb - xa);
                int acc[3] = { 0, 0, 0 };
                if (sum[3])
                    for (int k = 0; k < 3; k++)
                        acc[k] = (int)(sum[k] / sum[3]);
                put(o, acc, n ? (int)(sum[3] / n) : 0, bgc);
            }
        }
        free(cx0);
        return true;
    }
    if (zoom >= 2.0) {
        /* Nearest pixel: sharp when looked at closely. */
        for (int y = oy0; y < oy1; y++) {
            int sy = (int)(y0 + (y + 0.5) * inv);
            if (sy >= src.h) sy = src.h - 1;
            const unsigned char* row = src.row(sy);
            unsigned char* o = out->row(y) + ox0 * 4;
            for (int x = ox0; x < ox1; x++, o += 4) {
                int sx = (int)(x0 + (x + 0.5) * inv);
                if (sx >= src.w) sx = src.w - 1;
                const unsigned char* s = row + sx * 4;
                int acc[3] = { s[0], s[1], s[2] };
                put(o, acc, s[3], bgc);
            }
        }
        return true;
    }
    /* 1x to 2x: bilinear between the four nearest pixels. */
    for (int y = oy0; y < oy1; y++) {
        double fy = y0 + (y + 0.5) * inv - 0.5;
        int sy = (int)floor(fy);
        int wy = (int)((fy - sy) * 256);
        int sy1 = sy + 1;
        if (sy < 0) sy = 0;
        if (sy1 >= src.h) sy1 = src.h - 1;
        if (sy >= src.h) sy = src.h - 1;
        const unsigned char* r0 = src.row(sy);
        const unsigned char* r1 = src.row(sy1);
        unsigned char* o = out->row(y) + ox0 * 4;
        for (int x = ox0; x < ox1; x++, o += 4) {
            double fx = x0 + (x + 0.5) * inv - 0.5;
            int sx = (int)floor(fx);
            int wx = (int)((fx - sx) * 256);
            int sx1 = sx + 1;
            if (sx < 0) sx = 0;
            if (sx1 >= src.w) sx1 = src.w - 1;
            if (sx >= src.w) sx = src.w - 1;
            const unsigned char* p00 = r0 + sx * 4;
            const unsigned char* p01 = r0 + sx1 * 4;
            const unsigned char* p10 = r1 + sx * 4;
            const unsigned char* p11 = r1 + sx1 * 4;
            int v[4];
            for (int k = 0; k < 4; k++) {
                int top = p00[k] * (256 - wx) + p01[k] * wx;
                int bot = p10[k] * (256 - wx) + p11[k] * wx;
                v[k] = (top * (256 - wy) + bot * wy + 32768) >> 16;
            }
            put(o, v, v[3], bgc);
        }
    }
    return true;
}

bool rotate(const ti::Image& src, int q, ti::Image* out)
{
    q &= 3;
    int w = q & 1 ? src.h : src.w, h = q & 1 ? src.w : src.h;
    if (src.empty() || !out->alloc(w, h))
        return false;
    for (int y = 0; y < src.h; y++) {
        const unsigned char* s = src.row(y);
        for (int x = 0; x < src.w; x++, s += 4) {
            int dx, dy;
            switch (q) {
            case 1: dx = src.h - 1 - y; dy = x; break;
            case 2: dx = src.w - 1 - x; dy = src.h - 1 - y; break;
            case 3: dx = y; dy = src.w - 1 - x; break;
            default: dx = x; dy = y; break;
            }
            memcpy(out->row(dy) + dx * 4, s, 4);
        }
    }
    return true;
}

} /* namespace ttrv */
