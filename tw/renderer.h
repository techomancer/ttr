/*
 * tw/renderer.h - what a renderer does for a Surface (DESIGN.md section 5).
 * Coordinates are cells unless a name says pixels; the Surface owns the
 * geometry and passes it in at init and resize.
 */
#ifndef TW_RENDERER_H
#define TW_RENDERER_H

#include "fonts.h"
#include "tw.h"

#include <X11/Xlib.h>

namespace tw {

struct Geometry {
    int cell_w, cell_h, ascent;
    int margin_x, margin_y;
    int width, height;          /* window, pixels */
    int cols, rows;
};

/* Looks up the code points of a cluster cell for a renderer. */
class ClusterLookup {
public:
    virtual ~ClusterLookup() {}
    virtual int cluster(int y, int x, uint32_t* cps, int max) const = 0;
};

class Renderer {
public:
    virtual ~Renderer() {}
    virtual bool init(Display* dpy, Window win, Visual* visual, Colormap cmap, int depth,
                      const Fonts* fonts) = 0;
    virtual void set_geometry(const Geometry& g) = 0;
    /* Fill a pixel rectangle. */
    virtual void fill_px(int x, int y, int w, int h, uint32_t rgb) = 0;
    /* Rows top..bottom moved up by count (down when negative). */
    virtual void scroll(int top, int bottom, int count) = 0;
    /* One run of cells with the same style, at row y from column x. */
    virtual void draw_run(int x, int y, const CellData* cells, int n,
                          const ClusterLookup* clusters) = 0;
    /* The cursor over the cell under it (already drawn plainly). */
    virtual void draw_cursor(const Cursor& c, const CellData& under, uint32_t color,
                             bool focused, const ClusterLookup* clusters) = 0;
    virtual void flush() = 0;
};

Renderer* create_core_renderer();

} /* namespace tw */

#endif
