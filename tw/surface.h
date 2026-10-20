/*
 * tw/surface.h - draws a tw::Source into an X window: geometry, frame
 * scheduling, damage to renderer calls (DESIGN.md sections 2 and 6).
 * Plain Xlib; the TwGrid widget wraps it, but it works on any window.
 */
#ifndef TW_SURFACE_H
#define TW_SURFACE_H

#include "fonts.h"
#include "renderer.h"
#include "tw.h"

#include <X11/Xlib.h>

namespace tw {

/* How a Surface asks for time: the host arms a timer and calls back. */
class Scheduler {
public:
    virtual ~Scheduler() {}
    /* Call Surface::frame() after delay_ms (replacing an earlier request
     * if this one is sooner). */
    virtual void schedule_frame(int delay_ms) = 0;
    /* Call Surface::blink() after delay_ms; 0 cancels. */
    virtual void schedule_blink(int delay_ms) = 0;
};

class Surface {
public:
    Surface();
    ~Surface();

    /* Fonts first: their cell size decides how many cells fit. */
    bool set_fonts(Display* dpy, const char* regular, const char* bold);
    int cell_width() const { return fonts_.cell_width(); }
    int cell_height() const { return fonts_.cell_height(); }

    bool attach(Display* dpy, Window win, Visual* visual, Colormap cmap, int depth,
                Scheduler* sched);
    void detach();
    void set_source(Source* src);
    void set_margins(int x, int y) { margin_x_ = x; margin_y_ = y; }
    void set_frame_delay(int ms) { frame_delay_ = ms; }
    void set_blink_interval(int ms) { blink_ms_ = ms; }

    /* The window is now width x height pixels. Returns the size in cells
     * that fits; the host resizes the model to it. */
    void resize(int width, int height, int* cols, int* rows);
    int fit_cols() const { return g_.cols; }
    int fit_rows() const { return g_.rows; }
    /* Pixel position to cell, clamped. */
    void cell_at(int px, int py, int* col, int* row) const;

    /* The model changed. */
    void damage();
    /* Part of the window was exposed. */
    void expose();
    void set_focus(bool in);
    bool focused() const { return focused_; }

    /* Called by the host's timers. */
    void frame();
    void blink();

    /* Statistics of the last frame, for -stats. */
    int last_frame_cells() const { return stat_cells_; }

private:
    class Lookup : public ClusterLookup {
    public:
        const Source* src;
        int cluster(int y, int x, uint32_t* cps, int max) const { return src->cluster(y, x, cps, max); }
    };

    void draw_row(int y, int lo, int hi);
    void draw_cursor();
    void clear_margins();
    long now_ms() const;

    Display* dpy_;
    Window win_;
    Scheduler* sched_;
    Renderer* r_;
    Source* src_;
    Fonts fonts_;
    Geometry g_;
    Lookup lookup_;
    int margin_x_, margin_y_;
    int frame_delay_;
    int blink_ms_;
    bool scheduled_;
    bool full_pending_;
    bool focused_;
    bool blink_on_;
    bool blink_armed_;
    long hold_since_;
    long last_frame_;
    CellData* row_buf_;
    int row_cap_;
    int stat_cells_;

    Surface(const Surface&);
    Surface& operator=(const Surface&);
};

} /* namespace tw */

#endif
