/* RGBA into a visual's pixels (pixels.h). */
#include "pixels.h"

#include <stdlib.h>
#include <string.h>
#include <X11/Xutil.h>

namespace ttrv {

namespace {

void mask_place(unsigned long m, int* shift, int* bits)
{
    int s = 0, b = 0;
    while (m && !(m & 1)) {
        m >>= 1;
        s++;
    }
    while (m & 1) {
        m >>= 1;
        b++;
    }
    *shift = s;
    *bits = b;
}

/* 4 x 4 Bayer matrix, 0..15. */
const int k_bayer[4][4] = {
    { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 }
};

} /* namespace */

PixelFormat::PixelFormat()
    : dpy_(0), cmap_(0), true_color_(false), levels_(0), cube_(0), ncube_(0)
{
    for (int i = 0; i < 3; i++)
        shift_[i] = bits_[i] = 0;
}

PixelFormat::~PixelFormat()
{
    release();
}

void PixelFormat::release()
{
    if (cube_ && ncube_ && dpy_)
        XFreeColors(dpy_, cmap_, cube_, ncube_, 0);
    free(cube_);
    cube_ = 0;
    ncube_ = 0;
    levels_ = 0;
}

bool PixelFormat::init(Display* dpy, Visual* visual, Colormap cmap, int depth)
{
    release();
    dpy_ = dpy;
    cmap_ = cmap;
    if (visual->c_class == TrueColor || visual->c_class == DirectColor) {
        true_color_ = true;
        mask_place(visual->red_mask, &shift_[0], &bits_[0]);
        mask_place(visual->green_mask, &shift_[1], &bits_[1]);
        mask_place(visual->blue_mask, &shift_[2], &bits_[2]);
        return bits_[0] && bits_[1] && bits_[2];
    }
    true_color_ = false;
    if (depth < 4)
        return false;
    /* The largest cube whose colours the colormap gives us, read-only so
     * other programs keep theirs: 6^3 = 216 on an 8-bit screen that is
     * not full, down to 2^3. */
    for (int L = depth >= 8 ? 6 : 3; L >= 2; L--) {
        int n = L * L * L;
        cube_ = (unsigned long*)malloc((size_t)n * sizeof(unsigned long));
        if (!cube_)
            return false;
        int got = 0;
        for (int i = 0; i < n; i++) {
            XColor c;
            int r = i / (L * L), g = (i / L) % L, b = i % L;
            c.red = (unsigned short)(r * 65535 / (L - 1));
            c.green = (unsigned short)(g * 65535 / (L - 1));
            c.blue = (unsigned short)(b * 65535 / (L - 1));
            c.flags = DoRed | DoGreen | DoBlue;
            if (!XAllocColor(dpy, cmap, &c))
                break;
            cube_[i] = c.pixel;
            got++;
        }
        if (got == n) {
            levels_ = L;
            ncube_ = n;
            return true;
        }
        if (got)
            XFreeColors(dpy, cmap, cube_, got, 0);
        free(cube_);
        cube_ = 0;
    }
    return false;
}

unsigned long PixelFormat::pixel(unsigned long rgb) const
{
    unsigned long c[3] = { (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255 };
    if (true_color_) {
        unsigned long p = 0;
        for (int i = 0; i < 3; i++)
            p |= (bits_[i] >= 8 ? c[i] << (bits_[i] - 8) : c[i] >> (8 - bits_[i])) << shift_[i];
        return p;
    }
    if (!cube_)
        return 0;
    int L = levels_, idx = 0;
    for (int i = 0; i < 3; i++)
        idx = idx * L + (int)((c[i] * (unsigned long)(L - 1) + 127) / 255);
    return cube_[idx];
}

void PixelFormat::store(char* row, int x, unsigned long p, int bpp, bool swap) const
{
    switch (bpp) {
    case 8:
        ((unsigned char*)row)[x] = (unsigned char)p;
        break;
    case 16: {
        unsigned short v = (unsigned short)p;
        if (swap)
            v = (unsigned short)((v >> 8) | (v << 8));
        ((unsigned short*)row)[x] = v;
        break;
    }
    case 24: {
        /* Packed: in the server's order, byte by byte. */
        unsigned char* o = (unsigned char*)row + x * 3;
        bool lsb = ImageByteOrder(dpy_) == LSBFirst;
        o[lsb ? 0 : 2] = (unsigned char)p;
        o[1] = (unsigned char)(p >> 8);
        o[lsb ? 2 : 0] = (unsigned char)(p >> 16);
        break;
    }
    case 32: {
        unsigned int v = (unsigned int)p;
        if (swap)
            v = (v >> 24) | ((v >> 8) & 0xff00) | ((v << 8) & 0xff0000) | (v << 24);
        ((unsigned int*)row)[x] = v;
        break;
    }
    default:
        break;
    }
}

void PixelFormat::convert(const ti::Image& im, unsigned long bg, tw::ClientImage* out, int dx,
                          int dy) const
{
    if (im.empty() || !out->image())
        return;
    int x0 = dx < 0 ? -dx : 0, y0 = dy < 0 ? -dy : 0;
    int x1 = im.w, y1 = im.h;
    if (dx + x1 > out->width())
        x1 = out->width() - dx;
    if (dy + y1 > out->height())
        y1 = out->height() - dy;
    if (x0 >= x1 || y0 >= y1)
        return;
    int bpp = out->bits_per_pixel();
    bool swap = out->swapped();
    int bgc[3] = { (int)((bg >> 16) & 255), (int)((bg >> 8) & 255), (int)(bg & 255) };
    int L = levels_;
    for (int y = y0; y < y1; y++) {
        const unsigned char* s = im.row(y) + x0 * 4;
        char* row = out->row(dy + y);
        for (int x = x0; x < x1; x++, s += 4) {
            int c[3];
            int a = s[3];
            for (int k = 0; k < 3; k++)
                c[k] = a == 255 ? s[k] : (s[k] * a + bgc[k] * (255 - a) + 127) / 255;
            unsigned long p;
            if (true_color_) {
                p = 0;
                for (int k = 0; k < 3; k++) {
                    unsigned long v = (unsigned long)c[k];
                    int b = bits_[k];
                    p |= (b >= 8 ? v << (b - 8) : v >> (8 - b)) << shift_[k];
                }
            } else {
                /* Ordered dither between the cube's levels. */
                int t = k_bayer[(dy + y) & 3][(dx + x) & 3];
                int idx = 0;
                for (int k = 0; k < 3; k++) {
                    int v = c[k] * (L - 1);             /* 0 .. 255 * (L - 1) */
                    int lo = v / 255, frac = v % 255;
                    if (lo < L - 1 && frac * 16 > t * 255 + 127)
                        lo++;
                    idx = idx * L + lo;
                }
                p = cube_ ? cube_[idx] : 0;
            }
            store(row, dx + x, p, bpp, swap);
        }
    }
}

} /* namespace ttrv */
