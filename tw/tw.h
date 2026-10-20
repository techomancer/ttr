/*
 * tw: the text window drawing engine. See tw/DESIGN.md.
 *
 * A tw::Source describes a grid of cells and what changed; a tw::Surface
 * draws it into an X window with one of the renderers. The TwGrid Motif
 * widget (grid_widget.h) wraps a Surface.
 */
#ifndef TW_TW_H
#define TW_TW_H

#if defined(__sgi)
#include <inttypes.h>
#else
#include <stdint.h>
#endif
#include <stddef.h>

namespace tw {

enum StyleFlag {
    S_BOLD = 1,
    S_ITALIC = 2,
    S_UNDERLINE = 4,
    S_DOUBLE_UNDERLINE = 8,
    S_CURLY_UNDERLINE = 16,
    S_STRIKE = 32,
    S_OVERLINE = 64,
    S_INVISIBLE = 128,
    S_BLINK = 256,
    S_DEFAULT_BG = 512      /* bg is the window's background */
};

/* Colours are resolved 0xRRGGBB; the model owns palettes and inversion. */
struct CellStyle {
    uint32_t fg, bg, deco;  /* deco: underline/strike colour */
    uint16_t flags;

    bool operator==(const CellStyle& o) const
    {
        return fg == o.fg && bg == o.bg && deco == o.deco && flags == o.flags;
    }
    bool operator!=(const CellStyle& o) const { return !(*this == o); }
};

struct CellData {
    uint32_t cp;            /* first code point, 0 for an empty cell */
    uint8_t width;          /* 1, 2 (first half of a wide char), 0 (second half) */
    uint8_t cluster;        /* more code points follow; see Source::cluster */
    CellStyle style;
};

enum CursorStyle {
    CURSOR_BLOCK = 0,
    CURSOR_UNDERLINE,
    CURSOR_BAR
};

struct Cursor {
    int x, y;
    bool visible;
    bool blink;
    bool wide;
    CursorStyle shape;
};

enum LineAttr {
    LINE_SINGLE = 0,
    LINE_DOUBLE_WIDTH,
    LINE_DOUBLE_TOP,
    LINE_DOUBLE_BOTTOM
};

class Source {
public:
    virtual ~Source() {}

    /* A frame is about to be drawn: a good moment to take a snapshot of
     * the model that the calls below then read. */
    virtual void frame_begin() {}
    virtual int cols() const = 0;
    virtual int rows() const = 0;
    /* Synchronized output: do not present a frame now. */
    virtual bool held() const = 0;
    /* Damage since the last frame_done(). */
    virtual bool full_damage() const = 0;
    virtual int scroll_count() const = 0;
    /* Rows top..bottom moved up by count (down when negative). */
    virtual void scroll(int i, int* top, int* bottom, int* count) const = 0;
    virtual bool row_dirty(int y, int* lo, int* hi) const = 0;
    /* Cells lo..hi of row y into out[0..hi-lo]. */
    virtual void cells(int y, int lo, int hi, CellData* out) const = 0;
    /* All code points of a cell whose CellData::cluster is set. */
    virtual int cluster(int y, int x, uint32_t* cps, int max) const = 0;
    virtual LineAttr line_attr(int y) const = 0;
    virtual Cursor cursor() const = 0;
    virtual uint32_t background() const = 0;       /* margins, default bg */
    virtual uint32_t cursor_color() const = 0;
    /* Everything reported has been drawn. */
    virtual void frame_done() = 0;
};

} /* namespace tw */

#endif
