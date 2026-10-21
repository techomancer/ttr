#include "source.h"

namespace ttrt {

static uint32_t rgb(tl::Rgb c)
{
    return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
}

void TermSource::scroll(int i, int* top, int* bottom, int* count) const
{
    const tl::ScrollRecord& r = v_.scroll_record(i);
    *top = r.top;
    *bottom = r.bottom;
    *count = r.count;
}

void TermSource::cells(int y, int lo, int hi, tw::CellData* out) const
{
    const tl::Row& row = v_.row(y);
    for (int x = lo; x <= hi; x++) {
        tw::CellData& d = out[x - lo];
        const tl::Cell& c = row.cells[x];
        uint32_t cps[2];
        int n = v_.text(c, cps, 2);
        d.cp = n ? cps[0] : 0;
        d.cluster = c.is_grapheme() && n > 1 ? 1 : 0;
        switch (c.wide()) {
        case tl::CELL_WIDE: d.width = 2; break;
        case tl::CELL_SPACER_TAIL: d.width = 0; break;
        case tl::CELL_SPACER_HEAD: d.width = 1; d.cp = 0; break;
        default: d.width = 1; break;
        }
        tl::ResolvedColors rc = v_.colors(c);
        const tl::Style& s = v_.style(c);
        d.style.fg = rgb(rc.fg);
        d.style.bg = rgb(rc.bg);
        d.style.deco = rgb(rc.ul);
        uint16_t f = 0;
        if (s.attrs & tl::ATTR_BOLD) f |= tw::S_BOLD;
        if (s.attrs & tl::ATTR_ITALIC) f |= tw::S_ITALIC;
        if (s.attrs & tl::ATTR_BLINK) f |= tw::S_BLINK;
        if (s.attrs & tl::ATTR_INVISIBLE) f |= tw::S_INVISIBLE;
        if (s.attrs & tl::ATTR_STRIKE) f |= tw::S_STRIKE;
        if (s.attrs & tl::ATTR_OVERLINE) f |= tw::S_OVERLINE;
        switch (s.underline()) {
        case tl::UL_NONE: break;
        case tl::UL_DOUBLE: f |= tw::S_DOUBLE_UNDERLINE; break;
        case tl::UL_CURLY: f |= tw::S_CURLY_UNDERLINE; break;
        default: f |= tw::S_UNDERLINE; break;
        }
        if (rc.bg_default) f |= tw::S_DEFAULT_BG;
        if (v_.selected(x, y)) {
            /* Selected text is drawn inverted. */
            uint32_t t = d.style.fg;
            d.style.fg = d.style.bg;
            d.style.bg = t;
            f &= (uint16_t)~(tw::S_DEFAULT_BG | tw::S_INVISIBLE);
        }
        d.style.flags = f;
    }
}

int TermSource::cluster(int y, int x, uint32_t* cps, int max) const
{
    return v_.text(v_.cell(x, y), cps, max);
}

tw::LineAttr TermSource::line_attr(int y) const
{
    switch (v_.row(y).line_attr) {
    case tl::LINE_DOUBLE_WIDTH: return tw::LINE_DOUBLE_WIDTH;
    case tl::LINE_DOUBLE_TOP: return tw::LINE_DOUBLE_TOP;
    case tl::LINE_DOUBLE_BOTTOM: return tw::LINE_DOUBLE_BOTTOM;
    default: return tw::LINE_SINGLE;
    }
}

tw::Cursor TermSource::cursor() const
{
    tl::ViewCursor c = v_.cursor();
    tw::Cursor o;
    o.x = c.x;
    o.y = c.y;
    o.visible = c.visible;
    o.blink = c.blink;
    o.wide = c.wide;
    o.shape = c.shape == tl::CURSOR_BAR ? tw::CURSOR_BAR
            : c.shape == tl::CURSOR_UNDERLINE ? tw::CURSOR_UNDERLINE : tw::CURSOR_BLOCK;
    return o;
}

uint32_t TermSource::background() const
{
    return rgb(t_.modes().reverse ? t_.default_fg() : t_.default_bg());
}

uint32_t TermSource::cursor_color() const
{
    if (have_cursor_)
        return cursor_;
    return rgb(t_.modes().reverse ? t_.default_bg() : t_.default_fg());
}

} /* namespace ttrt */
