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
#include <X11/Xutil.h>

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

/* How glyphs are drawn: sharp (the font's own 1-bit pixels), smooth
 * (antialiased from distance fields, DESIGN.md section 4.6), or chosen:
 * smooth where the face is scalable and at least 18 pixels tall, where it
 * looks better than the hinted bitmap. Renderers that cannot draw smooth
 * text draw sharp. */
enum TextRendering {
    TEXT_SHARP,
    TEXT_SMOOTH,
    TEXT_AUTO
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
    /* A run of image cells: tiles image_col.. of tile row image_row of
     * one image, from column x of row y. The default draws them as the
     * blank cells they also are: their background. */
    virtual void draw_image(int x, int y, const CellData* cells, int n, const ImageData& img)
    {
        (void)img;
        draw_run(x, y, cells, n, 0);
    }
    /* The cursor over the cell under it (already drawn plainly). */
    virtual void draw_cursor(const Cursor& c, const CellData& under, uint32_t color,
                             bool focused, const ClusterLookup* clusters) = 0;
    virtual void flush() = 0;

    /* A frame starts; bg is the window's background colour. */
    virtual void begin_frame(uint32_t bg) { (void)bg; }
    /* Every frame must redraw every cell (double buffering, or an
     * animated background behind the text). */
    virtual bool full_frames() const { return false; }
    /* Retained: the renderer keeps every row it was given and draws all
     * of them itself each frame, so the Surface sends only changed rows,
     * whole, between row_begin and row_end (and the scroll records),
     * even when full_frames() is true. */
    virtual bool retained() const { return false; }
    virtual void row_begin(int y) { (void)y; }
    virtual void row_end(int y) { (void)y; }
    /* The renderer lost what it kept (its glyph atlas filled up): the
     * Surface sends every row next frame. Asking clears it. */
    virtual bool take_full_request() { return false; }
    /* Above 0: draw a frame this often even without damage (animation). */
    virtual int animation_ms() const { return 0; }
    virtual void set_text_rendering(TextRendering t) { (void)t; }
    /* Smooth text is what it draws now. */
    virtual bool smooth_text() const { return false; }
    /* An animated background by name ("aurora", ...); false if this
     * renderer cannot draw it. */
    virtual bool set_background(const char* name) { return !name || !*name; }
    virtual const char* name() const = 0;
};

/* One row of pixels of an image run, dy pixels down from the top of the
 * cells, scaled from the image's tile size to cw x ch: n cells starting
 * at tile column col of tile row row, into out[n * cw] as 0xAARRGGBB
 * (A 0 where the image has no pixel). */
void image_row(const ImageData& img, int col, int row, int n, int cw, int ch, int dy,
               uint32_t* out);
/* The same, put over a background: 0xRRGGBB. */
void image_row_over(const ImageData& img, int col, int row, int n, int cw, int ch, int dy,
                    uint32_t bg, uint32_t* out);

/* The renderers. A kind the build or the display lacks is 0. */
Renderer* create_core_renderer();
Renderer* create_soft_renderer();
Renderer* create_gl_renderer();

/* The visual a renderer kind wants for its window ("core": the parent's,
 * "soft": TrueColor, "gl": a GLX visual); false if the display has none.
 */
bool choose_visual(const char* kind, Display* dpy, int screen, XVisualInfo* out);

} /* namespace tw */

#endif
