/*
 * Drawing interface for our own decorations. Implemented by XPainter in
 * X11 mode (and later by a pixel buffer painter in Wayland mode).
 */
#ifndef TTR_CORE_PAINTER_H
#define TTR_CORE_PAINTER_H

#include "core/color.h"
#include "core/geom.h"

namespace ttr {

class Painter {
public:
    virtual ~Painter() {}

    virtual void fill(const Rect& r, const Rgb& c) = 0;

    virtual int text_width(const char* s, int len) = 0;
    virtual int text_ascent() = 0;
    virtual int text_descent() = 0;
    /* y is the baseline. */
    virtual void text(int x, int y, const char* s, int len, const Rgb& c) = 0;
};

/* Motif-style bevel: top/left in 'top', bottom/right in 'bottom'. Pass the
 * colors swapped for a sunken bevel. */
void draw_bevel(Painter& p, const Rect& r, int thickness,
                const Rgb& top, const Rgb& bottom);

}

#endif
