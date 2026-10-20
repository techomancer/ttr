/* Selection on the active screen, held by two grid anchors. */
#include "terminal.h"

#include <string.h>

namespace tl {

bool Terminal::is_word_char(uint32_t cp) const
{
    if (cp == 0 || cp == ' ' || cp == '\t')
        return false;
    if (cp >= 0x80)
        return true;                    /* letters of every script */
    if ((cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z'))
        return true;
    /* Like xterm's default classes, plus what paths and URLs are made of. */
    return strchr("_-./~:@?&=%+#", (int)cp) != 0;
}

/* The class of a cell for word selection: 0 blank, 1 word, else the
 * character itself (a run of the same punctuation). */
static uint32_t cell_class(const Terminal& t, const Cell& c)
{
    uint32_t cps[1];
    int n = t.cell_text(c, cps, 1);
    uint32_t cp = n ? cps[0] : 0;
    if (cp == 0 || cp == ' ')
        return 0;
    return cp;
}

void Terminal::word_bounds(uint64_t row, int col, uint64_t* r0, int* c0, uint64_t* r1,
                           int* c1) const
{
    const Grid& g = screen().grid;
    uint64_t first = g.base(), last = g.base() + g.total_rows() - 1;
    uint32_t cls = cell_class(*this, g.abs_row(row).cells[col]);
    bool word = cls && is_word_char(cls);
    /* Left, across soft wraps. */
    uint64_t r = row;
    int c = col;
    for (;;) {
        uint64_t prow;
        int pc;
        if (c > 0) {
            prow = r;
            pc = c - 1;
        } else {
            if (r == first || !(g.abs_row(r - 1).flags & ROW_WRAPPED))
                break;
            prow = r - 1;
            pc = cols_ - 1;
        }
        const Cell& pcell = g.abs_row(prow).cells[pc];
        if (pcell.wide() == CELL_SPACER_TAIL && pc > 0) {
            /* Step over the tail onto its head. */
            const Cell& head = g.abs_row(prow).cells[pc - 1];
            uint32_t k = cell_class(*this, head);
            if (word ? !(k && is_word_char(k)) : k != cls)
                break;
            r = prow;
            c = pc - 1;
            continue;
        }
        uint32_t k = cell_class(*this, pcell);
        if (word ? !(k && is_word_char(k)) : k != cls)
            break;
        r = prow;
        c = pc;
    }
    *r0 = r;
    *c0 = c;
    /* Right. */
    r = row;
    c = col;
    for (;;) {
        bool at_end = c >= cols_ - 1;
        if (at_end && (r == last || !(g.abs_row(r).flags & ROW_WRAPPED)))
            break;
        uint64_t nrow = at_end ? r + 1 : r;
        int nc = at_end ? 0 : c + 1;
        const Cell& ncell = g.abs_row(nrow).cells[nc];
        if (ncell.wide() == CELL_SPACER_TAIL) {
            r = nrow;
            c = nc;
            continue;
        }
        uint32_t k = cell_class(*this, ncell);
        if (word ? !(k && is_word_char(k)) : k != cls)
            break;
        r = nrow;
        c = nc;
    }
    *r1 = r;
    *c1 = c;
}

void Terminal::line_bounds(uint64_t row, uint64_t* r0, uint64_t* r1) const
{
    const Grid& g = screen().grid;
    uint64_t first = g.base(), last = g.base() + g.total_rows() - 1;
    uint64_t r = row;
    while (r > first && (g.abs_row(r - 1).flags & ROW_WRAPPED))
        r--;
    *r0 = r;
    r = row;
    while (r < last && (g.abs_row(r).flags & ROW_WRAPPED))
        r++;
    *r1 = r;
}

bool Terminal::selection_range(uint64_t* r0, int* c0, uint64_t* r1, int* c1) const
{
    if (!sel_anchor_ || sel_alt_ != alt_)
        return false;
    const Grid& g = screen().grid;
    const Anchor* a = g.anchor(sel_anchor_);
    const Anchor* p = g.anchor(sel_point_);
    if (!a || !p || a->lost || p->lost)
        return false;
    uint64_t ar = a->row, pr = p->row;
    /* Anchors outside the grid would be a bookkeeping error elsewhere;
     * treat them as lost rather than read past the rows. */
    if (ar < g.base() || ar >= g.base() + g.total_rows() || pr < g.base() ||
        pr >= g.base() + g.total_rows())
        return false;
    int ac = a->col < cols_ ? a->col : cols_ - 1;
    int pc = p->col < cols_ ? p->col : cols_ - 1;
    bool swap = pr < ar || (pr == ar && pc < ac);
    uint64_t sr = swap ? pr : ar, er = swap ? ar : pr;
    int sc = swap ? pc : ac, ec = swap ? ac : pc;
    if (sel_mode_ == SELECT_WORD) {
        uint64_t x0, x1;
        int y0, y1;
        word_bounds(sr, sc, &x0, &y0, &x1, &y1);
        sr = x0;
        sc = y0;
        word_bounds(er, ec, &x0, &y0, &x1, &y1);
        er = x1;
        ec = y1;
    } else if (sel_mode_ == SELECT_LINE) {
        uint64_t x0, x1;
        line_bounds(sr, &x0, &x1);
        sr = x0;
        sc = 0;
        line_bounds(er, &x0, &x1);
        er = x1;
        ec = cols_ - 1;
    }
    /* Include the tail of a wide character at the end. */
    if (ec + 1 < cols_ && g.abs_row(er).cells[ec].wide() == CELL_WIDE)
        ec++;
    *r0 = sr;
    *c0 = sc;
    *r1 = er;
    *c1 = ec;
    return true;
}

bool Terminal::has_selection() const
{
    uint64_t a, b;
    int c, d;
    return selection_range(&a, &c, &b, &d);
}

void Terminal::sel_touch_slow(int y)
{
    if (sel_alt_ != alt_)
        return;
    uint64_t r0, r1;
    int c0, c1;
    if (!selection_range(&r0, &c0, &r1, &c1)) {
        select_clear();
        return;
    }
    uint64_t row = screen().grid.active_top() + (uint64_t)y;
    if (row >= r0 && row <= r1)
        select_clear();
}

void Terminal::sel_rows_moving_slow(int top, int bottom)
{
    if (sel_alt_ != alt_)
        return;
    uint64_t r0, r1;
    int c0, c1;
    if (!selection_range(&r0, &c0, &r1, &c1))
        return;
    uint64_t t = screen().grid.active_top() + (uint64_t)top;
    uint64_t b = screen().grid.active_top() + (uint64_t)bottom;
    if (r1 >= t && r0 <= b)
        select_clear();
}

void Terminal::sel_refresh()
{
    uint64_t r0 = 0, r1 = 0;
    int c0 = 0, c1 = 0;
    bool now = selection_range(&r0, &c0, &r1, &c1);
    if (now == sel_shown_ && (!now || (r0 == sel_shown_r0_ && r1 == sel_shown_r1_ &&
                                       c0 == sel_shown_c0_ && c1 == sel_shown_c1_)))
        return;
    if (sel_shown_)
        select_damage(sel_shown_r0_, sel_shown_r1_);
    if (now)
        select_damage(r0, r1);
    else if (sel_anchor_)
        select_clear();                 /* an end was lost: drop it all */
    sel_shown_ = now;
    sel_shown_r0_ = r0;
    sel_shown_r1_ = r1;
    sel_shown_c0_ = c0;
    sel_shown_c1_ = c1;
}

void Terminal::select_damage(uint64_t r0, uint64_t r1)
{
    Grid& g = screen().grid;
    uint64_t first = g.base(), last = g.base() + g.total_rows() - 1;
    if (r0 < first) r0 = first;
    if (r1 > last) r1 = last;
    for (uint64_t r = r0; r <= r1 && r >= r0; r++)
        g.abs_row(r).damage_all();
}

void Terminal::select_damage_current()
{
    uint64_t r0, r1;
    int c0, c1;
    if (selection_range(&r0, &c0, &r1, &c1))
        select_damage(r0, r1);
}

void Terminal::select_start(uint64_t row, int col, SelectMode mode)
{
    select_damage_current();
    select_clear();
    Grid& g = screen().grid;
    sel_alt_ = alt_;
    sel_mode_ = mode;
    sel_anchor_ = g.anchor_new(row, col);
    sel_point_ = g.anchor_new(row, col);
    select_damage_current();
    sel_refresh();
}

void Terminal::select_extend(uint64_t row, int col)
{
    if (!sel_anchor_ || sel_alt_ != alt_)
        return;
    uint64_t o0 = 0, o1 = 0;
    int c0, c1;
    bool had = selection_range(&o0, &c0, &o1, &c1);
    screen().grid.anchor_set(sel_point_, row, col);
    uint64_t n0, n1;
    if (selection_range(&n0, &c0, &n1, &c1)) {
        if (had) {
            if (o0 < n0) n0 = o0;
            if (o1 > n1) n1 = o1;
        }
        select_damage(n0, n1);
    }
    sel_refresh();
}

void Terminal::select_all()
{
    Grid& g = screen().grid;
    select_start(g.base(), 0, SELECT_CHAR);
    select_extend(g.base() + g.total_rows() - 1, cols_ - 1);
}

void Terminal::select_clear()
{
    if (!sel_anchor_)
        return;
    Grid& g = sel_alt_ ? alt_screen_.grid : primary_.grid;
    if (sel_alt_ == alt_)
        select_damage_current();
    g.anchor_free(sel_anchor_);
    g.anchor_free(sel_point_);
    sel_anchor_ = sel_point_ = 0;
    if (sel_shown_ && sel_alt_ == alt_)
        select_damage(sel_shown_r0_, sel_shown_r1_);
    sel_shown_ = false;
}

bool Terminal::selection_text(Buffer& out) const
{
    uint64_t r0, r1;
    int c0, c1;
    if (!selection_range(&r0, &c0, &r1, &c1))
        return false;
    text(r0, c0, r1, c1, out);
    return true;
}

} /* namespace tl */
