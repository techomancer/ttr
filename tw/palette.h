/*
 * tw/palette.h - RGB colours to pixel values for any X visual.
 *
 * TrueColor and DirectColor: computed from the channel masks.
 * PseudoColor and GrayScale (CI8 on an Indy, 12-bit CI): shared read-only
 * cells allocated with XAllocColor while the budget lasts, then the
 * nearest colour already in the colormap. StaticColor and friends: what
 * XAllocColor answers (always the closest).
 */
#ifndef TW_PALETTE_H
#define TW_PALETTE_H

#include "tw.h"

#include <X11/Xlib.h>

namespace tw {

class Palette {
public:
    Palette();
    ~Palette();

    void init(Display* dpy, Visual* visual, Colormap cmap, int depth);
    void release();             /* free allocated cells */
    unsigned long pixel(uint32_t rgb);
    bool true_color() const { return true_; }
    int allocated() const { return n_alloc_; }

    /* Most exact allocations on a PseudoColor colormap before falling back
     * to the nearest colour (the desktop and other clients need cells too). */
    void set_budget(int cells) { budget_ = cells; }

private:
    struct Slot {
        uint32_t rgb;
        unsigned long pixel;
        bool used;
    };
    enum { CACHE = 1024 };

    unsigned long nearest(uint32_t rgb);
    void load_colormap();

    Display* dpy_;
    Colormap cmap_;
    Visual* visual_;
    bool true_;
    bool dynamic_;              /* PseudoColor or GrayScale: may allocate */
    int shift_[3];
    int bits_[3];
    unsigned long mask_[3];
    Slot cache_[CACHE];
    unsigned long* alloc_;      /* pixels we own */
    int n_alloc_, alloc_cap_;
    int budget_;
    XColor* cmap_colors_;       /* snapshot of the colormap for nearest() */
    int cmap_size_;
    bool cmap_loaded_;
};

} /* namespace tw */

#endif
