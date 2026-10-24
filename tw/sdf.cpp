#include "sdf.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xutil.h>

namespace tw {

namespace {

/* EPX / Scale2x of a w x h 0/1 image into 2w x 2h. */
void scale2x(const unsigned char* in, int w, int h, unsigned char* o)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int P = in[y * w + x];
            int A = y > 0 ? in[(y - 1) * w + x] : 0;
            int B = x < w - 1 ? in[y * w + x + 1] : 0;
            int C = x > 0 ? in[y * w + x - 1] : 0;
            int D = y < h - 1 ? in[(y + 1) * w + x] : 0;
            int e0 = P, e1 = P, e2 = P, e3 = P;
            if (C == A && C != D && A != B)
                e0 = A;
            if (A == B && A != C && B != D)
                e1 = B;
            if (D == C && D != B && C != A)
                e2 = C;
            if (B == D && B != A && D != C)
                e3 = D;
            o[(2 * y) * 2 * w + 2 * x] = (unsigned char)e0;
            o[(2 * y) * 2 * w + 2 * x + 1] = (unsigned char)e1;
            o[(2 * y + 1) * 2 * w + 2 * x] = (unsigned char)e2;
            o[(2 * y + 1) * 2 * w + 2 * x + 1] = (unsigned char)e3;
        }
}

/* Field n (from 0) of an XLFD, as [*start, *end). False if it has fewer. */
bool xlfd_field(const char* x, int n, const char** start, const char** end)
{
    const char* p = x;
    for (int i = 0; i <= n; i++) {
        p = strchr(p, '-');
        if (!p)
            return false;
        p++;
    }
    *start = p;
    const char* e = strchr(p, '-');
    *end = e ? e : p + strlen(p);
    return true;
}

} /* namespace */

SdfMaker::SdfMaker()
    : dpy_(0), drawable_(0), fonts_(0), cw_(0), ch_(0), ascent_(0), pm_(0), gc_(0), pm_w_(0),
      pm_h_(0), work_(0), work_cap_(0)
{
    for (int f = 0; f < FACE_COUNT; f++)
        big_ascent_[f] = big_dx_[f] = 0;
}

SdfMaker::~SdfMaker()
{
    release();
    free(work_);
}

void SdfMaker::set_display(Display* dpy, Drawable d)
{
    dpy_ = dpy;
    drawable_ = d;
}

void SdfMaker::release()
{
    for (int f = 0; f < FACE_COUNT; f++)
        big_[f].unload();
    if (gc_)
        XFreeGC(dpy_, gc_);
    if (pm_)
        XFreePixmap(dpy_, pm_);
    gc_ = 0;
    pm_ = 0;
}

void SdfMaker::configure(const Fonts* fonts, int cell_w, int cell_h, int ascent)
{
    release();
    fonts_ = fonts;
    cw_ = cell_w;
    ch_ = cell_h;
    ascent_ = ascent;
    if (!dpy_ || !fonts)
        return;
    for (int f = 0; f < FACE_COUNT; f++) {
        if (!fonts->has_face(f))
            continue;
        /* The face's XLFD with K times the pixel size, any point size and
         * width: a scalable face comes back K times larger. */
        const char* x = fonts->face(f).xlfd();
        const char *ps, *pe, *ts, *te, *ws, *we;
        if (!xlfd_field(x, 6, &ps, &pe) || !xlfd_field(x, 7, &ts, &te) ||
            !xlfd_field(x, 11, &ws, &we))
            continue;
        int px = atoi(ps);
        if (px <= 0)
            continue;
        char name[400];
        snprintf(name, sizeof(name), "%.*s%d-*%.*s*%s", (int)(ps - x), x, px * K,
                 (int)(ws - te), te, we);
        if (!big_[f].load(dpy_, name))
            continue;
        /* A bitmap font answers with its nearest size: not ours. */
        int a = big_[f].ascent();
        if (a < (ascent * K) * 3 / 4 || a > (ascent * K) * 5 / 4) {
            big_[f].unload();
            continue;
        }
        big_ascent_[f] = a;
        big_dx_[f] = (cw_ * K - big_[f].width()) / 2;
    }
}

bool SdfMaker::big_glyph(int face, uint32_t cp, int w, unsigned char* big)
{
    const CoreFont& f = big_[face];
    if (!f.loaded() || !f.has(cp))
        return false;
    int bw = w * K, bh = ch_ * K;
    if (!pm_ || pm_w_ < bw || pm_h_ < bh) {
        if (gc_)
            XFreeGC(dpy_, gc_);
        if (pm_)
            XFreePixmap(dpy_, pm_);
        pm_w_ = 2 * cw_ * K;
        pm_h_ = bh;
        pm_ = XCreatePixmap(dpy_, drawable_, (unsigned)pm_w_, (unsigned)pm_h_, 1);
        XGCValues v;
        v.graphics_exposures = False;
        gc_ = XCreateGC(dpy_, pm_, GCGraphicsExposures, &v);
    }
    XSetForeground(dpy_, gc_, 0);
    XFillRectangle(dpy_, pm_, gc_, 0, 0, (unsigned)pm_w_, (unsigned)pm_h_);
    XSetForeground(dpy_, gc_, 1);
    XSetFont(dpy_, gc_, f.fs()->fid);
    /* On the cell's baseline, scaled. */
    int x = big_dx_[face] * (w / cw_), y = ascent_ * K;
    if (f.two_byte()) {
        XChar2b c;
        c.byte1 = (unsigned char)(cp >> 8);
        c.byte2 = (unsigned char)(cp & 255);
        XDrawString16(dpy_, pm_, gc_, x, y, &c, 1);
    } else {
        char c = (char)cp;
        XDrawString(dpy_, pm_, gc_, x, y, &c, 1);
    }
    XImage* im = XGetImage(dpy_, pm_, 0, 0, (unsigned)bw, (unsigned)bh, 1, XYPixmap);
    if (!im)
        return false;
    for (int yy = 0; yy < bh; yy++)
        for (int xx = 0; xx < bw; xx++)
            big[yy * bw + xx] = XGetPixel(im, xx, yy) ? 1 : 0;
    XDestroyImage(im);
    return true;
}

/* The field of a K-times 0/1 image at 1x: from each texel's centre, the
 * nearest pixel of the other kind within SPREAD pixels. */
void SdfMaker::field(const unsigned char* big, int bw, int bh, int w, int h, uint8_t* out)
{
    const int r = SPREAD * K;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            double cx = (x + 0.5) * K, cy = (y + 0.5) * K;
            int icx = (int)cx, icy = (int)cy;
            bool inside = big[icy * bw + icx] != 0;
            double best2 = (double)r * r;
            int y0 = icy - r < 0 ? 0 : icy - r, y1 = icy + r >= bh ? bh - 1 : icy + r;
            int x0 = icx - r < 0 ? 0 : icx - r, x1 = icx + r >= bw ? bw - 1 : icx + r;
            for (int sy = y0; sy <= y1; sy++) {
                double ddy = sy + 0.5 - cy;
                const unsigned char* row = big + sy * bw;
                for (int sx = x0; sx <= x1; sx++) {
                    if ((row[sx] != 0) == inside)
                        continue;
                    double ddx = sx + 0.5 - cx;
                    double d2 = ddx * ddx + ddy * ddy;
                    if (d2 < best2)
                        best2 = d2;
                }
            }
            /* The outline lies half a big pixel from the nearest pixel of
             * the other kind. */
            double d = (sqrt(best2) - 0.5) / K;
            if (d < 0)
                d = 0;
            if (!inside)
                d = -d;
            int v = (int)floor(128.0 + d * 127.0 / SPREAD + 0.5);
            out[y * w + x] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
}

void SdfMaker::make(int face, const uint32_t* cps, int n, bool wide, const uint8_t* mask,
                    int stride, uint8_t* out)
{
    int w = wide ? 2 * cw_ : cw_, h = ch_;
    int bw = w * K, bh = h * K;
    size_t need = (size_t)bw * (size_t)bh * 2 + (size_t)w * (size_t)h;
    if (need > work_cap_) {
        unsigned char* p = (unsigned char*)realloc(work_, need);
        if (!p) {
            memset(out, 0, (size_t)w * (size_t)h);
            return;
        }
        work_ = p;
        work_cap_ = need;
    }
    unsigned char* big = work_;
    /* Bold without a bold face: the regular outline grown a little,
     * which a distance field does by moving its threshold. */
    int src = face;
    double grow = 0;
    if (face == FACE_BOLD && !big_[FACE_BOLD].loaded() && big_[FACE_REGULAR].loaded()) {
        src = FACE_REGULAR;
        grow = 0.4;
    }
    if (src < 0 || src >= FACE_COUNT || n != 1 || !big_glyph(src, cps[0], w, big)) {
        grow = 0;
        /* The cell mask, EPX twice: K = 4. */
        unsigned char* small = work_ + (size_t)bw * (size_t)bh * 2;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                small[y * w + x] = (mask[y * stride + x / 8] & (0x80 >> (x % 8))) ? 1 : 0;
        unsigned char* mid = work_ + (size_t)bw * (size_t)bh;     /* 2x */
        scale2x(small, w, h, mid);
        scale2x(mid, 2 * w, 2 * h, big);
    }
    field(big, bw, bh, w, h, out);
    if (grow > 0) {
        int add = (int)(grow * 127.0 / SPREAD + 0.5);
        for (int i = 0; i < w * h; i++)
            out[i] = (uint8_t)(out[i] + add > 255 ? 255 : out[i] + add);
    }
}

uint8_t SdfMaker::coverage(uint8_t v, double edge, double weight)
{
    double d = ((int)v - 128) * (double)SPREAD / 127.0 + weight;
    double t = (d + edge) / (2 * edge);
    if (t < 0)
        t = 0;
    if (t > 1)
        t = 1;
    return (uint8_t)(255.0 * t * t * (3 - 2 * t) + 0.5);
}

} /* namespace tw */
