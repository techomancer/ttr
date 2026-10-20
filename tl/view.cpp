#include "view.h"
#include "terminal.h"

namespace tl {

const Row& View::row(int y) const
{
    return grid_->abs_row(top_ + (uint64_t)y);
}

size_t View::history() const
{
    return grid_->history();
}

int View::text(const Cell& c, uint32_t* out, int max) const
{
    return term_->cell_text(c, out, max);
}

const Style& View::style(const Cell& c) const
{
    return term_->style(c);
}

static void widen(int* lo, int* hi, bool* any, int a, int b)
{
    if (!*any) {
        *lo = a;
        *hi = b;
        *any = true;
        return;
    }
    if (a < *lo) *lo = a;
    if (b > *hi) *hi = b;
}

bool View::row_dirty(int y, int* lo, int* hi) const
{
    if (y < 0 || y >= rows_)
        return false;
    if (full_) {
        *lo = 0;
        *hi = cols_ - 1;
        return true;
    }
    bool any = false;
    const Row& r = row(y);
    if (r.dirty())
        widen(lo, hi, &any, r.dirty_lo, r.dirty_hi < cols_ ? r.dirty_hi : cols_ - 1);
    /* The cell under the old cursor and the cell under the new one. */
    if (prev_cursor_y_ == y && prev_cursor_x_ < cols_) {
        int a = prev_cursor_x_;
        int b = prev_cursor_wide_ && a + 1 < cols_ ? a + 1 : a;
        widen(lo, hi, &any, a, b);
    }
    if (cursor_.y == y) {
        int b = cursor_.wide ? cursor_.x + 1 : cursor_.x;
        widen(lo, hi, &any, cursor_.x, b);
    }
    return any;
}

static Rgb mix(Rgb a, Rgb b)
{
    Rgb m;
    m.r = (uint8_t)(((int)a.r + b.r) / 2);
    m.g = (uint8_t)(((int)a.g + b.g) / 2);
    m.b = (uint8_t)(((int)a.b + b.b) / 2);
    return m;
}

ResolvedColors View::colors(const Cell& c) const
{
    const Style& s = style(c);
    const Terminal& t = *term_;
    bool reverse_screen = t.modes().reverse;
    Rgb dfg = reverse_screen ? t.default_bg() : t.default_fg();
    Rgb dbg = reverse_screen ? t.default_fg() : t.default_bg();

    ResolvedColors rc;
    bool bold = (s.attrs & ATTR_BOLD) != 0;
    switch (color_kind(s.fg)) {
    case COLOR_PALETTE: {
        int i = color_index(s.fg);
        if (bold && i < 8 && t.bold_is_bright())
            i += 8;
        rc.fg = t.palette(i);
        break;
    }
    case COLOR_RGB:
        rc.fg.r = (uint8_t)color_r(s.fg);
        rc.fg.g = (uint8_t)color_g(s.fg);
        rc.fg.b = (uint8_t)color_b(s.fg);
        break;
    default:
        rc.fg = dfg;
        break;
    }
    rc.bg_default = false;
    switch (color_kind(s.bg)) {
    case COLOR_PALETTE:
        rc.bg = t.palette(color_index(s.bg));
        break;
    case COLOR_RGB:
        rc.bg.r = (uint8_t)color_r(s.bg);
        rc.bg.g = (uint8_t)color_g(s.bg);
        rc.bg.b = (uint8_t)color_b(s.bg);
        break;
    default:
        rc.bg = dbg;
        rc.bg_default = !reverse_screen;
        break;
    }
    if (s.attrs & ATTR_FAINT)
        rc.fg = mix(rc.fg, rc.bg);
    if (s.attrs & ATTR_INVERSE) {
        Rgb tmp = rc.fg;
        rc.fg = rc.bg;
        rc.bg = tmp;
        rc.bg_default = false;
    }
    if (s.attrs & ATTR_INVISIBLE)
        rc.fg = rc.bg;
    switch (color_kind(s.ul)) {
    case COLOR_PALETTE:
        rc.ul = t.palette(color_index(s.ul));
        break;
    case COLOR_RGB:
        rc.ul.r = (uint8_t)color_r(s.ul);
        rc.ul.g = (uint8_t)color_g(s.ul);
        rc.ul.b = (uint8_t)color_b(s.ul);
        break;
    default:
        rc.ul = rc.fg;
        break;
    }
    return rc;
}

} /* namespace tl */
