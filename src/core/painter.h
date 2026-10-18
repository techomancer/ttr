/*
 * Drawing interface for our own decorations. Implemented by XPainter in
 * X11 mode (and later by a pixel buffer painter in Wayland mode).
 */
#ifndef TTR_CORE_PAINTER_H
#define TTR_CORE_PAINTER_H

#include "core/color.h"
#include "core/geom.h"
#include "core/str.h"
#include "core/vector.h"

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

/* Another painter with its origin moved to (dx, dy), for drawing a part
 * (a frame, say) somewhere inside a bigger picture. */
class OffsetPainter : public Painter {
public:
    OffsetPainter(Painter& p, int dx, int dy) : p_(p), dx_(dx), dy_(dy) {}
    void fill(const Rect& r, const Rgb& c)
    {
        p_.fill(Rect(r.x + dx_, r.y + dy_, r.w, r.h), c);
    }
    int text_width(const char* s, int len) { return p_.text_width(s, len); }
    int text_ascent() { return p_.text_ascent(); }
    int text_descent() { return p_.text_descent(); }
    void text(int x, int y, const char* s, int len, const Rgb& c)
    {
        p_.text(x + dx_, y + dy_, s, len, c);
    }
    void icon(const Icon& ic, int x, int y, int size, const Rgb& bg)
    {
        p_.icon(ic, x + dx_, y + dy_, size, bg);
    }

private:
    Painter& p_;
    int dx_, dy_;
};

/* Draw 'ic', or a generic window glyph if it is null. */
void draw_app_icon(Painter& p, const Icon* ic, int x, int y, int size,
                   const ColorSet& cs);

/* A loudspeaker with 'level' sound waves (0..3), struck out if muted. */
void draw_speaker_icon(Painter& p, int x, int y, int size, int level,
                       bool muted, const ColorSet& cs);

/* Network state: kind 0 offline, 1 wired, 2 wireless with 'level' 0..4
 * signal bars. */
void draw_network_icon(Painter& p, int x, int y, int size, int kind,
                       int level, const ColorSet& cs);

/* A USB stick, for removable storage. */
void draw_storage_icon(Painter& p, int x, int y, int size, const ColorSet& cs);

/* A closed folder, for menus. */
void draw_folder_icon(Painter& p, int x, int y, int size);

/* Word-wrap 'text' (with its own newlines) to 'width' pixels, at most
 * 'max_lines' lines, the last ending in "..." if text was left over. */
void wrap_text(Painter& p, const char* text, int width, int max_lines,
               Vector<Str>* out);

/* Longest prefix of s that fits in 'width' pixels, with "..." appended if
 * it had to be cut. Writes into buf (size bytes); returns its length. */
int fit_text(Painter& p, const char* s, int width, char* buf, int size);

/* Motif-style bevel: top/left in 'top', bottom/right in 'bottom'. Pass the
 * colors swapped for a sunken bevel. */
void draw_bevel(Painter& p, const Rect& r, int thickness,
                const Rgb& top, const Rgb& bottom);

}

#endif
