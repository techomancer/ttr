/*
 * tl/grid.h - cells, rows and the ring of rows that holds the active area
 * and the scrollback. See DESIGN.md section 5.
 */
#ifndef TL_GRID_H
#define TL_GRID_H

#include "port.h"

namespace tl {

enum WideKind {
    CELL_NARROW = 0,
    CELL_WIDE = 1,          /* first column of a two-column character */
    CELL_SPACER_TAIL = 2,   /* second column of a wide character; not drawn */
    CELL_SPACER_HEAD = 3    /* last column of a row whose wide character wrapped */
};

enum { TL_GRAPHEME_MAX = 32 };

/* 8 bytes; a zero cell is an empty default cell. */
struct Cell {
    uint32_t ch;            /* bits 0-20 code point or grapheme index; 21 grapheme;
                               22-23 WideKind; 24 protected (DECSCA) */
    uint16_t style;         /* StyleTable id, 0 = default */
    uint16_t link;          /* hyperlink id, 0 = none */

    enum {
        CP_MASK = 0x1fffff,
        F_GRAPHEME = 1u << 21,
        WIDE_SHIFT = 22,
        WIDE_MASK = 3u << 22,
        F_PROTECTED = 1u << 24
    };

    uint32_t cp() const { return ch & CP_MASK; }
    bool is_grapheme() const { return (ch & F_GRAPHEME) != 0; }
    WideKind wide() const { return (WideKind)((ch & WIDE_MASK) >> WIDE_SHIFT); }
    bool is_protected() const { return (ch & F_PROTECTED) != 0; }
    bool is_empty() const { return cp() == 0 && !is_grapheme(); }

    void set(uint32_t codepoint, WideKind w, uint16_t style_id, uint16_t link_id, bool prot)
    {
        ch = (codepoint & CP_MASK) | ((uint32_t)w << WIDE_SHIFT) | (prot ? (uint32_t)F_PROTECTED : 0u);
        style = style_id;
        link = link_id;
    }
    void set_grapheme(uint32_t index) { ch = (ch & ~(uint32_t)CP_MASK) | F_GRAPHEME | (index & CP_MASK); }
};

enum RowFlag {
    ROW_WRAPPED = 1,        /* soft wrap: the logical line continues on the next row */
    ROW_HAS_GRAPHEMES = 2,
    ROW_HAS_LINKS = 4,
    ROW_HAS_STYLES = 8
};

enum LineAttr {
    LINE_SINGLE = 0,
    LINE_DOUBLE_WIDTH = 1,  /* DECDWL */
    LINE_DOUBLE_TOP = 2,    /* DECDHL top half */
    LINE_DOUBLE_BOTTOM = 3  /* DECDHL bottom half */
};

enum SemanticKind {         /* OSC 133 */
    SEM_OUTPUT = 0,
    SEM_PROMPT,
    SEM_INPUT,
    SEM_PROMPT_CONTINUATION
};

struct Row {
    Cell* cells;
    uint16_t cols;          /* cells allocated */
    uint16_t dirty_lo;      /* damaged columns, dirty_lo > dirty_hi: clean */
    uint16_t dirty_hi;
    uint8_t flags;
    uint8_t line_attr;
    uint8_t semantic;

    bool dirty() const { return dirty_lo <= dirty_hi; }
    void clean() { dirty_lo = 0xffff; dirty_hi = 0; }
    void damage(int lo, int hi)
    {
        if (lo < dirty_lo) dirty_lo = (uint16_t)lo;
        if (hi > dirty_hi) dirty_hi = (uint16_t)hi;
    }
    void damage_all() { dirty_lo = 0; dirty_hi = cols ? (uint16_t)(cols - 1) : 0; }
};

/* Code point sequences for cells with more than one code point. Entries
 * are append-only; compact() rebuilds the pool from the cells in use. */
class GraphemePool {
public:
    GraphemePool();
    ~GraphemePool();
    /* Add a sequence; returns its index, or 0 on failure (0 is never a
     * valid index: the pool starts with a dummy entry). */
    uint32_t add(const uint32_t* cps, int n);
    int get(uint32_t index, const uint32_t** cps) const;
    size_t size() const { return len_; }
    bool needs_compaction() const { return len_ > (1u << 18); }
    void clear();
    /* Used by Grid::compact_graphemes(). */
    void swap(GraphemePool& o);

private:
    uint32_t* data_;
    size_t len_;
    size_t cap_;

    GraphemePool(const GraphemePool&);
    GraphemePool& operator=(const GraphemePool&);
};

/* The rows of one screen: a ring holding scrollback (oldest first) and
 * then the active area, which is always the last rows() entries. Every
 * row has an absolute number; base() is the oldest row still kept. */
class Grid {
public:
    Grid();
    ~Grid();

    bool init(int cols, int rows, size_t max_history);
    void destroy();

    int cols() const { return cols_; }
    int rows() const { return rows_; }
    size_t history() const { return len_ - (size_t)rows_; }
    size_t max_history() const { return max_history_; }
    uint64_t base() const { return base_; }
    uint64_t active_top() const { return base_ + history(); }
    uint64_t total_rows() const { return (uint64_t)len_; }

    Row& row(int y) { return ring_[(start_ + history() + (size_t)y) % cap_]; }
    const Row& row(int y) const { return ring_[(start_ + history() + (size_t)y) % cap_]; }
    /* A row by absolute number, base() <= a < base() + total_rows(). */
    Row& abs_row(uint64_t a) { return ring_[(start_ + (size_t)(a - base_)) % cap_]; }
    const Row& abs_row(uint64_t a) const { return ring_[(start_ + (size_t)(a - base_)) % cap_]; }
    Cell& cell(int x, int y) { return row(y).cells[x]; }
    const Cell& cell(int x, int y) const { return row(y).cells[x]; }

    GraphemePool& graphemes() { return graphemes_; }
    const GraphemePool& graphemes() const { return graphemes_; }

    /* Fill cells lo..hi of a row and damage them. */
    void fill(Row& r, int lo, int hi, const Cell& fill);
    void clear_row(Row& r, const Cell& fill) { fill_row(r, fill); }
    /* Scroll rows top..bot (inclusive, active coordinates) up by n: the top
     * n rows leave, n blank rows enter at the bottom. With to_history and
     * a region covering the whole screen, the leaving rows become
     * scrollback (or are dropped when there is no room for any). */
    void scroll_up(int top, int bot, int n, const Cell& fill, bool to_history);
    void scroll_down(int top, int bot, int n, const Cell& fill);
    /* Drop the scrollback. */
    void clear_history();
    /* Change the size without reflow: rows are cut or padded, the active
     * area keeps its bottom rows (cursor_y tells which rows must stay). */
    bool resize(int cols, int rows, int* cursor_y);
    /* Change the size and re-wrap soft-wrapped lines to the new width.
     * The cursor (active coordinates; pending: a wrap is pending) keeps
     * its character and is updated. Blank rows below the cursor are
     * dropped before re-wrapping; the last rows of the result become the
     * active area, so a narrower screen pushes rows into the scrollback
     * and a wider one pulls them back. */
    bool reflow(int cols, int rows, int* cursor_x, int* cursor_y, bool* pending);
    /* Rebuild the grapheme pool from the cells that use it. */
    void compact_graphemes();

    /* Set when every row must be repainted. */
    bool full_damage;

private:
    void fill_row(Row& r, const Cell& fill);
    bool grow_ring(size_t need);
    void trim_history();
    bool row_blank(const Row& r) const;
    bool alloc_row(Row& r, int cols);
    void push_history_row(const Cell& fill);

    Row* ring_;
    size_t cap_;            /* slots in ring_ */
    size_t start_;          /* slot of the oldest row */
    size_t len_;            /* rows in use: history + rows_ */
    int cols_;
    int rows_;
    size_t max_history_;
    uint64_t base_;
    GraphemePool graphemes_;

    Grid(const Grid&);
    Grid& operator=(const Grid&);
};

} /* namespace tl */

#endif
