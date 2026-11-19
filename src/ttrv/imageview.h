/*
 * ttrv's image view: an image centred on a background, in the best
 * visual for images (ttrv/DESIGN.md section 5): the deepest TrueColor
 * one, else the default visual through a colour cube. When that visual
 * is not Motif's, the pixels go to a child window of it inside the
 * drawing area, registered with Xt so its events reach the widget, and
 * on the shell's WM_COLORMAP_WINDOWS.
 *
 * The view shows an image it does not own (the cache does), already
 * fitted by the loader; larger ones are cropped around their centre.
 */
#ifndef TTRV_IMAGEVIEW_H
#define TTRV_IMAGEVIEW_H

#include "pixels.h"

#include "core/str.h"

#include <Xm/Xm.h>

namespace ttrv {

class ImageView;

class ImageViewHost {
public:
    virtual ~ImageViewHost() {}
    /* The window's size changed: the image may be wanted at another. */
    virtual void view_resized(ImageView* v) { (void)v; }
};

class ImageView {
public:
    ImageView(Widget parent, const char* name, ImageViewHost* host);
    ~ImageView();

    Widget widget() const { return da_; }
    /* Show im (0: none); it must stay until the next show(). */
    void show(const ti::Image* im);
    const ti::Image* shown() const { return im_; }
    /* A line of text in the middle (when there is no image) or at the
     * bottom left (over one): "Decoding...", why it failed. */
    void set_text(const char* text);
    void set_background(unsigned long rgb);
    /* The drawable area in pixels. */
    void size(int* w, int* h) const;

    /* Callbacks (public for the C trampolines). */
    void exposed();
    void resized();

private:
    ImageView(const ImageView&);
    ImageView& operator=(const ImageView&);

    bool ensure_window();
    void rebuild();
    void paint();

    Widget da_;
    ImageViewHost* host_;
    Display* dpy_;
    Window win_;            /* the drawing area's, or our child */
    bool own_window_;
    Visual* visual_;
    int depth_;
    Colormap cmap_;
    bool own_cmap_;
    GC gc_;
    XFontStruct* font_;
    PixelFormat fmt_;
    tw::ClientImage frame_; /* the whole window, ready to put */
    bool dirty_;            /* frame_ needs rebuilding */
    const ti::Image* im_;
    ttr::Str text_;
    unsigned long bg_;
};

} /* namespace ttrv */

#endif
