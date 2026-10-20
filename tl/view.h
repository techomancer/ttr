/*
 * tl/view.h - what a presenter reads to draw a frame. See DESIGN.md
 * section 6.
 *
 * A View is a thin read-only window over the terminal, valid until the
 * next feed(), resize() or viewport change. Typical frame:
 *
 *     tl::View v = term.view();
 *     if (v.held()) return;                  // synchronized output (2026)
 *     if (v.full_damage()) repaint everything;
 *     else {
 *         for each v.scroll_record(i): copy pixels of rows top..bottom
 *             up by count rows (down when negative);
 *         for each y with v.row_dirty(y, &lo, &hi): repaint lo..hi;
 *     }
 *     draw v.cursor();
 *     term.clear_damage();
 */
#ifndef TL_VIEW_H
#define TL_VIEW_H

#include "grid.h"
#include "style.h"

namespace tl {

class Terminal;

struct Rgb {
    uint8_t r, g, b;
};

enum CursorShape {
    CURSOR_BLOCK = 0,
    CURSOR_UNDERLINE,
    CURSOR_BAR
};

struct ViewCursor {
    int x, y;               /* viewport cell */
    bool visible;           /* shown and inside the viewport */
    bool blink;
    bool wide;              /* on a two-column character */
    CursorShape shape;
};

struct ResolvedColors {
    Rgb fg;
    Rgb bg;
    Rgb ul;                 /* underline colour */
    bool bg_default;        /* the background is the default one: a host
                               with a translucent or patterned window
                               background may skip painting it */
};

class View {
public:
    int cols() const { return cols_; }
    int rows() const { return rows_; }

    bool held() const { return held_; }
    bool full_damage() const { return full_; }
    int scroll_record_count() const { return full_ ? 0 : n_scroll_; }
    const ScrollRecord& scroll_record(int i) const { return scroll_[i]; }
    /* Columns lo..hi of viewport row y need repainting. */
    bool row_dirty(int y, int* lo, int* hi) const;

    const Row& row(int y) const;
    const Cell& cell(int x, int y) const { return row(y).cells[x]; }
    /* The code points of a cell (one, or a grapheme cluster). */
    int text(const Cell& c, uint32_t* out, int max) const;
    const Style& style(const Cell& c) const;
    ResolvedColors colors(const Cell& c) const;
    ViewCursor cursor() const { return cursor_; }

    /* Scrollback position. */
    uint64_t top_row() const { return top_; }     /* absolute row of viewport row 0 */
    bool at_bottom() const { return at_bottom_; }
    size_t history() const;

private:
    friend class Terminal;
    View() {}

    const Terminal* term_;
    const Grid* grid_;
    int cols_, rows_;
    uint64_t top_;
    bool at_bottom_;
    bool held_;
    bool full_;
    ScrollRecord scroll_[TL_MAX_SCROLL_RECORDS];
    int n_scroll_;
    ViewCursor cursor_;
    int prev_cursor_x_, prev_cursor_y_;   /* old cursor cell to repaint, y -1: none */
    bool prev_cursor_wide_;
};

} /* namespace tl */

#endif
