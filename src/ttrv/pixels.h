/*
 * ttrv's pixels: RGBA images into a visual's pixel format (ttrv/DESIGN.md
 * section 5.1). TrueColor and DirectColor of any channel layout, 16, 24
 * or 32 bits a pixel, in the server's byte order; PseudoColor and
 * GrayScale (the 8-bit Indy) through a colour cube allocated read-only
 * in the colormap, ordered-dithered.
 */
#ifndef TTRV_PIXELS_H
#define TTRV_PIXELS_H

#include "../../ti/ti.h"
#include "../../tw/ximage.h"

#include <X11/Xlib.h>

namespace ttrv {

class PixelFormat {
public:
    PixelFormat();
    ~PixelFormat();

    /* For windows of this visual and colormap. False if it is neither
     * kind (1-bit screens). */
    bool init(Display* dpy, Visual* visual, Colormap cmap, int depth);
    void release();

    /* 0xRRGGBB as a pixel. */
    unsigned long pixel(unsigned long rgb) const;
    /* im (RGBA; alpha blended over bg, 0xRRGGBB) into out at dx, dy,
     * clipped to it. out must be of this visual and depth. */
    void convert(const ti::Image& im, unsigned long bg, tw::ClientImage* out, int dx,
                 int dy) const;
    bool true_color() const { return true_color_; }

private:
    PixelFormat(const PixelFormat&);
    PixelFormat& operator=(const PixelFormat&);

    void store(char* row, int x, unsigned long p, int bpp, bool swap) const;

    Display* dpy_;
    Colormap cmap_;
    bool true_color_;
    int shift_[3], bits_[3];        /* TrueColor: each channel's place */
    int levels_;                    /* the cube: levels_^3 colours */
    unsigned long* cube_;           /* the pixels, r * L * L + g * L + b */
    int ncube_;                     /* allocated (to free) */
};

} /* namespace ttrv */

#endif
