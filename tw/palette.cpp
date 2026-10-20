#include "palette.h"

#include <stdlib.h>
#include <string.h>
#include <X11/Xutil.h>

namespace tw {

static void mask_shift(unsigned long mask, int* shift, int* bits)
{
    int s = 0, b = 0;
    if (mask) {
        while (!(mask & 1)) {
            mask >>= 1;
            s++;
        }
        while (mask & 1) {
            mask >>= 1;
            b++;
        }
    }
    *shift = s;
    *bits = b;
}

Palette::Palette()
    : dpy_(0), cmap_(0), visual_(0), true_(false), dynamic_(false), alloc_(0),
      n_alloc_(0), alloc_cap_(0), budget_(48), cmap_colors_(0), cmap_size_(0),
      cmap_loaded_(false)
{
    memset(cache_, 0, sizeof(cache_));
}

Palette::~Palette()
{
    release();
    free(alloc_);
    free(cmap_colors_);
}

void Palette::init(Display* dpy, Visual* visual, Colormap cmap, int depth)
{
    release();
    dpy_ = dpy;
    visual_ = visual;
    cmap_ = cmap;
    memset(cache_, 0, sizeof(cache_));
    int cls = visual->c_class;          /* Xlib's name for "class" in C++ */
    true_ = cls == TrueColor || cls == DirectColor;
    dynamic_ = cls == PseudoColor || cls == GrayScale;
    if (true_) {
        mask_[0] = visual->red_mask;
        mask_[1] = visual->green_mask;
        mask_[2] = visual->blue_mask;
        for (int i = 0; i < 3; i++)
            mask_shift(mask_[i], &shift_[i], &bits_[i]);
    }
    cmap_size_ = visual->map_entries > 0 ? visual->map_entries : (1 << depth);
    if (cmap_size_ > 4096)
        cmap_size_ = 4096;
    /* A 12-bit colormap has room for exact colours. */
    if (cmap_size_ >= 4096)
        budget_ = 300;
    cmap_loaded_ = false;
}

void Palette::release()
{
    if (dpy_ && n_alloc_)
        XFreeColors(dpy_, cmap_, alloc_, n_alloc_, 0);
    n_alloc_ = 0;
    memset(cache_, 0, sizeof(cache_));
    cmap_loaded_ = false;
}

void Palette::load_colormap()
{
    free(cmap_colors_);
    cmap_colors_ = (XColor*)malloc((size_t)cmap_size_ * sizeof(XColor));
    if (!cmap_colors_)
        return;
    for (int i = 0; i < cmap_size_; i++) {
        cmap_colors_[i].pixel = (unsigned long)i;
        cmap_colors_[i].flags = DoRed | DoGreen | DoBlue;
    }
    XQueryColors(dpy_, cmap_, cmap_colors_, cmap_size_);
    cmap_loaded_ = true;
}

unsigned long Palette::nearest(uint32_t rgb)
{
    if (!cmap_loaded_)
        load_colormap();
    if (!cmap_colors_)
        return 0;
    int r = (int)((rgb >> 16) & 255), g = (int)((rgb >> 8) & 255), b = (int)(rgb & 255);
    long best = -1;
    unsigned long bp = 0;
    for (int i = 0; i < cmap_size_; i++) {
        int dr = (cmap_colors_[i].red >> 8) - r;
        int dg = (cmap_colors_[i].green >> 8) - g;
        int db = (cmap_colors_[i].blue >> 8) - b;
        /* Weighted for the eye: green matters most. */
        long d = 3L * dr * dr + 4L * dg * dg + 2L * db * db;
        if (best < 0 || d < best) {
            best = d;
            bp = cmap_colors_[i].pixel;
        }
    }
    return bp;
}

unsigned long Palette::pixel(uint32_t rgb)
{
    rgb &= 0xffffff;
    if (true_) {
        unsigned long p = 0;
        uint32_t ch[3] = { (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255 };
        for (int i = 0; i < 3; i++) {
            unsigned long v = bits_[i] >= 8 ? (unsigned long)ch[i] << (bits_[i] - 8)
                                            : (unsigned long)ch[i] >> (8 - bits_[i]);
            p |= (v << shift_[i]) & mask_[i];
        }
        return p;
    }
    unsigned h = (rgb * 2654435761u) >> 22;            /* 10 bits */
    for (int i = 0; i < 8; i++) {
        Slot& s = cache_[(h + (unsigned)i) & (CACHE - 1)];
        if (s.used && s.rgb == rgb)
            return s.pixel;
        if (!s.used)
            break;
    }
    unsigned long p;
    XColor c;
    c.red = (unsigned short)(((rgb >> 16) & 255) * 257);
    c.green = (unsigned short)(((rgb >> 8) & 255) * 257);
    c.blue = (unsigned short)((rgb & 255) * 257);
    c.flags = DoRed | DoGreen | DoBlue;
    if (!dynamic_) {
        p = XAllocColor(dpy_, cmap_, &c) ? c.pixel : nearest(rgb);
    } else if (n_alloc_ < budget_ && XAllocColor(dpy_, cmap_, &c)) {
        p = c.pixel;
        if (n_alloc_ == alloc_cap_) {
            int ncap = alloc_cap_ ? alloc_cap_ * 2 : 64;
            unsigned long* np = (unsigned long*)realloc(alloc_, (size_t)ncap * sizeof(unsigned long));
            if (np) {
                alloc_ = np;
                alloc_cap_ = ncap;
            }
        }
        if (n_alloc_ < alloc_cap_)
            alloc_[n_alloc_++] = p;
        cmap_loaded_ = false;                       /* the map changed */
    } else {
        p = nearest(rgb);
    }
    for (int i = 0; i < 8; i++) {
        Slot& s = cache_[(h + (unsigned)i) & (CACHE - 1)];
        if (!s.used || i == 7) {
            s.used = true;
            s.rgb = rgb;
            s.pixel = p;
            break;
        }
    }
    return p;
}

} /* namespace tw */
