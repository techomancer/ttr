/*
 * tx/appicon.cpp - a program's icon on its window (tx/appicon.h).
 */
#include "appicon.h"

#include "core/port.h"
#include "x11/svg.h"

#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <Xm/Xm.h>

#include <stdlib.h>
#include <string.h>

namespace tx {

namespace {

const int k_sizes[] = { 16, 32, 48 };
const int k_nsizes = 3;
const int k_pixmap_size = 48;

/* The shift that puts an 8 bit channel under 'mask', and its width. */
void channel(unsigned long mask, int* shift, int* bits)
{
    *shift = 0;
    *bits = 0;
    if (!mask)
        return;
    while (!(mask & 1)) {
        mask >>= 1;
        (*shift)++;
    }
    while (mask & 1) {
        mask >>= 1;
        (*bits)++;
    }
}

/* Colours allocated on a colormap visual (8 bit IRIX screens): the icons
 * have only a few. */
struct Allocated {
    uint32_t rgb;
    unsigned long pixel;
};

unsigned long pixel_of(Display* dpy, Screen* scr, Visual* vis, uint32_t rgb,
                       Allocated* cache, int* ncache)
{
    if (vis->c_class == TrueColor || vis->c_class == DirectColor) {
        int rs, rb, gs, gb, bs, bb;
        channel(vis->red_mask, &rs, &rb);
        channel(vis->green_mask, &gs, &gb);
        channel(vis->blue_mask, &bs, &bb);
        unsigned long r = (rgb >> 16) & 0xff, g = (rgb >> 8) & 0xff, b = rgb & 0xff;
        return ((r >> (8 - rb)) << rs) | ((g >> (8 - gb)) << gs) | ((b >> (8 - bb)) << bs);
    }
    int i;
    for (i = 0; i < *ncache; i++)
        if (cache[i].rgb == rgb)
            return cache[i].pixel;
    XColor c;
    c.red = (unsigned short)(((rgb >> 16) & 0xff) * 257);
    c.green = (unsigned short)(((rgb >> 8) & 0xff) * 257);
    c.blue = (unsigned short)((rgb & 0xff) * 257);
    c.flags = DoRed | DoGreen | DoBlue;
    unsigned long p;
    if (XAllocColor(dpy, DefaultColormapOfScreen(scr), &c))
        p = c.pixel;
    else
        p = (c.red + c.green + c.blue) / 3 > 0x8000 ? WhitePixelOfScreen(scr)
                                                    : BlackPixelOfScreen(scr);
    if (*ncache < 64) {
        cache[*ncache].rgb = rgb;
        cache[*ncache].pixel = p;
        (*ncache)++;
    }
    return p;
}

/* The icon pixmap and its mask, for window managers without
 * _NET_WM_ICON. */
void set_pixmap(Widget shell, const uint32_t* argb, int size)
{
    Display* dpy = XtDisplay(shell);
    Screen* scr = XtScreen(shell);
    Visual* vis = DefaultVisualOfScreen(scr);
    int depth = DefaultDepthOfScreen(scr);
    Window root = RootWindowOfScreen(scr);

    XImage* img = XCreateImage(dpy, vis, (unsigned)depth, ZPixmap, 0, 0, (unsigned)size,
                               (unsigned)size, 32, 0);
    if (!img)
        return;
    img->data = (char*)malloc((size_t)img->bytes_per_line * size);
    if (!img->data) {
        XDestroyImage(img);
        return;
    }
    int bpl = (size + 7) / 8;
    char* bits = (char*)calloc((size_t)bpl * size, 1);
    Allocated cache[64];
    int ncache = 0;
    int x, y;
    for (y = 0; y < size; y++) {
        for (x = 0; x < size; x++) {
            uint32_t p = argb[y * size + x];
            bool opaque = (p >> 24) >= 0x80;
            XPutPixel(img, x, y,
                      opaque ? pixel_of(dpy, scr, vis, p & 0xffffff, cache, &ncache) : 0);
            if (opaque && bits)
                bits[y * bpl + x / 8] |= (char)(1 << (x % 8));
        }
    }
    Pixmap pm = XCreatePixmap(dpy, root, (unsigned)size, (unsigned)size, (unsigned)depth);
    GC gc = XCreateGC(dpy, pm, 0, 0);
    XPutImage(dpy, pm, gc, img, 0, 0, 0, 0, (unsigned)size, (unsigned)size);
    XFreeGC(dpy, gc);
    XDestroyImage(img);
    Pixmap mask = None;
    if (bits) {
        mask = XCreateBitmapFromData(dpy, root, bits, (unsigned)size, (unsigned)size);
        free(bits);
    }
    XtVaSetValues(shell, XmNiconPixmap, pm, XmNiconMask, mask, (char*)0);
}

} /* namespace */

void set_app_icon(Widget shell, const char* svg)
{
    if (!XtIsRealized(shell))
        return;
    /* _NET_WM_ICON: width, height and the pixels for each size; format
     * 32 is a long per item whatever its size. */
    int total = 0, i;
    for (i = 0; i < k_nsizes; i++)
        total += 2 + k_sizes[i] * k_sizes[i];
    long* prop = new long[total];
    uint32_t* argb = new uint32_t[k_pixmap_size * k_pixmap_size];
    int n = 0;
    bool any = false;
    for (i = 0; i < k_nsizes; i++) {
        int s = k_sizes[i];
        if (!ttr::svg_render_text(svg, s, argb))
            continue;
        any = true;
        prop[n++] = s;
        prop[n++] = s;
        for (int j = 0; j < s * s; j++)
            prop[n++] = (long)argb[j];
        if (s == k_pixmap_size)
            set_pixmap(shell, argb, s);
    }
    if (any) {
        Display* dpy = XtDisplay(shell);
        XChangeProperty(dpy, XtWindow(shell), XInternAtom(dpy, "_NET_WM_ICON", False),
                        XA_CARDINAL, 32, PropModeReplace, (unsigned char*)prop, n);
    }
    delete [] argb;
    delete [] prop;
}

} /* namespace tx */
