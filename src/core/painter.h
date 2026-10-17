/*
 * Drawing interface for our own decorations. Implemented by XPainter in
 * X11 mode (and later by a pixel buffer painter in Wayland mode).
 */
#ifndef TTR_CORE_PAINTER_H
#define TTR_CORE_PAINTER_H

#include "core/color.h"
#include "core/geom.h"

namespace ttr {

class Icon;

class Painter {
public:
    virtual ~Painter() {}

    virtual void fill(const Rect& r, const Rgb& c) = 0;

    virtual int text_width(const char* s, int len) = 0;
    virtual int text_ascent() = 0;
    virtual int text_descent() = 0;
    /* y is the baseline. */
    virtual void text(int x, int y, const char* s, int len, const Rgb& c) = 0;

    /* Draw 'ic' scaled to size x size, blended onto the solid color 'bg'. */
    virtual void icon(const Icon& ic, int x, int y, int size,
                      const Rgb& bg) = 0;
};

/* Draw 'ic', or a generic window glyph if it is null. */
void draw_app_icon(Painter& p, const Icon* ic, int x, int y, int size,
                   const ColorSet& cs);

/* A closed folder, for menus. */
void draw_folder_icon(Painter& p, int x, int y, int size);

/* Longest prefix of s that fits in 'width' pixels, with "..." appended if
 * it had to be cut. Writes into buf (size bytes); returns its length. */
int fit_text(Painter& p, const char* s, int width, char* buf, int size);

/* Motif-style bevel: top/left in 'top', bottom/right in 'bottom'. Pass the
 * colors swapped for a sunken bevel. */
void draw_bevel(Painter& p, const Rect& r, int thickness,
                const Rgb& top, const Rgb& bottom);

}

#endif
