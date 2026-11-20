/*
 * ttrv's image view: an image centred on a background, in the best
 * visual for images (ttrv/DESIGN.md section 5): the deepest TrueColor
 * one, else the default visual through a colour cube. When that visual
 * is not Motif's, the pixels go to a child window of it inside the
 * drawing area, registered with Xt so its events reach the widget, and
 * on the shell's WM_COLORMAP_WINDOWS.
 *
 * The view shows an image it does not own (the cache does): as it is,
 * centred (the loader fitted it), or at a zoom and position (the
 * viewer's). The viewer's views may draw with GL (glpaint.h), which
 * keeps the image in textures, so moving it costs nothing; the soft
 * renderer computes the window's pixels each time (render.h).
 */
#ifndef TTRV_IMAGEVIEW_H
#define TTRV_IMAGEVIEW_H

#include "glpaint.h"
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
    /* "auto": the deepest TrueColor visual; "default": Motif's own
     * (on an 8-bit screen, the colour cube). Before the first view. */
    static void set_visual_choice(const char* choice);
    /* "auto": GL where it is direct and textures are in hardware, else
     * soft; "gl" or "soft" force one (gl falls back to soft, saying why,
     * if there is none). Texture memory in megabytes, 0 for the board's
     * default. Before the first view. */
    static void set_renderer(const char* renderer, int texture_mb);

    /* gl: this view may use GL (the viewer's; the preview is soft). */
    ImageView(Widget parent, const char* name, ImageViewHost* host, bool gl = false);
    ~ImageView();

    Widget widget() const { return da_; }
    /* Show im (0: none); it must stay until the next show(). */
    void show(const ti::Image* im, const char* key = "");
    /* im at 'zoom' window pixels per image pixel, the window's top left
     * at image position (x0, y0). 'key' names im for GL's textures (the
     * same key, the same pixels). */
    void show_at(const ti::Image* im, const char* key, double zoom, double x0, double y0);
    const ti::Image* shown() const { return im_; }
    /* A line of text in the middle (when there is no image) or at the
     * bottom left (over one): "Decoding...", why it failed. */
    void set_text(const char* text);
    void set_background(unsigned long rgb);
    /* The drawable area in pixels. */
    void size(int* w, int* h) const;
    /* "soft", or the GL board's name; 0 until it has a window. */
    const char* renderer() const;

    /* Callbacks (public for the C trampolines). */
    void exposed();
    void resized();

private:
    ImageView(const ImageView&);
    ImageView& operator=(const ImageView&);

    bool ensure_window();
    bool make_gl_window();
    void rebuild();
    void paint();
    void paint_gl();
    void text_at(int w, int h, int* x, int* y, unsigned long* fg) const;

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
    bool at_;               /* shown at zoom_, x0_, y0_ (else centred) */
    double zoom_, x0_, y0_;
    ttr::Str key_;
    ti::Image scaled_;      /* the soft renderer's frame */
    bool may_gl_;
    GLPaint* gl_;
    ttr::Str text_;
    unsigned long bg_;
};

} /* namespace ttrv */

#endif
