#include "terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace tl {

void Modes::reset()
{
    insert = newline = app_cursor = col132 = reverse = origin = false;
    autowrap = true;
    autorepeat = true;
    cursor_blink = false;
    cursor_visible = true;
    allow_132 = false;
    reverse_wrap = false;
    app_keypad = false;
    focus_events = alt_scroll = bracketed_paste = sync_output = false;
    graphemes = false;
    mouse = 0;
    mouse_encoding = 0;
    sixel_display = false;
    sixel_right = false;
}

static void cursor_reset(Cursor& c)
{
    c.x = c.y = 0;
    c.pending_wrap = false;
    c.style.clear();
    c.style_id = 0;
    c.protect = false;
    c.origin = false;
    c.cs.reset();
}

Terminal::Terminal()
    : host_(&null_host_), alt_(false), cols_(0), rows_(0), scrollback_(0),
      cursor_shape_(CURSOR_BLOCK), top_(0), bot_(0), tabs_(0),
      ambiguous_wide_(false), reflow_(true), backspace_del_(true), mouse_buttons_(0),
      mouse_last_col_(-1), mouse_last_row_(-1), sel_anchor_(0), sel_point_(0), sel_alt_(false),
      sel_mode_(SELECT_CHAR), sel_shown_(false), sel_shown_r0_(0), sel_shown_r1_(0),
      sel_shown_c0_(0), sel_shown_c1_(0), bold_bright_(true), full_damage_(true),
      vp_anchor_(0), follow_(true), last_valid_(false), last_at_bottom_(true), last_alt_(false),
      last_top_(0), last_cx_(0), last_cy_(0), last_cwide_(false), prev_valid_(false), prev_x_(0), prev_y_(0), last_cp_(0),
      in_sixel_(false), cell_w_(0), cell_h_(0), n_kplace_(0), kloading_(false), kfailed_(false),
      kdata_(0), kdata_n_(0), kdata_cap_(0)
{
    parser_.set_sink(this);
    /* xterm's default palette. */
    static const uint8_t base16[16][3] = {
        {0x00, 0x00, 0x00}, {0xcd, 0x00, 0x00}, {0x00, 0xcd, 0x00}, {0xcd, 0xcd, 0x00},
        {0x00, 0x00, 0xee}, {0xcd, 0x00, 0xcd}, {0x00, 0xcd, 0xcd}, {0xe5, 0xe5, 0xe5},
        {0x7f, 0x7f, 0x7f}, {0xff, 0x00, 0x00}, {0x00, 0xff, 0x00}, {0xff, 0xff, 0x00},
        {0x5c, 0x5c, 0xff}, {0xff, 0x00, 0xff}, {0x00, 0xff, 0xff}, {0xff, 0xff, 0xff}
    };
    for (int i = 0; i < 16; i++) {
        palette_[i].r = base16[i][0];
        palette_[i].g = base16[i][1];
        palette_[i].b = base16[i][2];
    }
    static const uint8_t level[6] = { 0, 95, 135, 175, 215, 255 };
    for (int i = 0; i < 216; i++) {
        palette_[16 + i].r = level[i / 36];
        palette_[16 + i].g = level[(i / 6) % 6];
        palette_[16 + i].b = level[i % 6];
    }
    for (int i = 0; i < 24; i++) {
        uint8_t v = (uint8_t)(8 + 10 * i);
        palette_[232 + i].r = palette_[232 + i].g = palette_[232 + i].b = v;
    }
    fg_ = palette_[7];
    bg_ = palette_[0];
    modes_.reset();
    cursor_reset(cur_);
    gstate_.reset();
    cursor_reset(primary_.saved);
    cursor_reset(alt_screen_.saved);
    primary_.has_saved = alt_screen_.has_saved = false;
}

Terminal::~Terminal()
{
    free(kdata_);
    free(tabs_);
}

bool Terminal::init(int cols, int rows, size_t scrollback)
{
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    if (cols > 4096) cols = 4096;
    cols_ = cols;
    rows_ = rows;
    scrollback_ = scrollback;
    if (!primary_.grid.init(cols, rows, scrollback))
        return false;
    if (!alt_screen_.grid.init(cols, rows, 0))
        return false;
    if (!vp_anchor_)
        vp_anchor_ = primary_.grid.anchor_new(0, 0);
    free(tabs_);
    tabs_ = (bool*)calloc((size_t)cols, sizeof(bool));
    if (!tabs_)
        return false;
    reset();
    return true;
}

void Terminal::reset()
{
    /* RIS leaves the input encoding alone, as in xterm. */
    parser_.reset();
    alt_ = false;
    modes_.reset();
    cursor_reset(cur_);
    cursor_shape_ = CURSOR_BLOCK;
    top_ = 0;
    bot_ = rows_ - 1;
    for (int x = 0; x < cols_; x++)
        tabs_[x] = x > 0 && x % 8 == 0;
    cursor_reset(primary_.saved);
    cursor_reset(alt_screen_.saved);
    primary_.has_saved = alt_screen_.has_saved = false;
    Cell b = Cell();
    for (int y = 0; y < rows_; y++) {
        primary_.grid.clear_row(primary_.grid.row(y), b);
        alt_screen_.grid.clear_row(alt_screen_.grid.row(y), b);
    }
    primary_.grid.clear_history();
    primary_.grid.full_damage = true;
    images_.clear();
    in_sixel_ = false;
    kitty_.clear();
    n_kplace_ = 0;
    kloading_ = false;
    kdata_n_ = 0;
    title_.clear();
    select_clear();
    mouse_buttons_ = 0;
    follow_ = true;
    full_damage_ = true;
    prev_valid_ = false;
    last_cp_ = 0;
    gstate_.reset();
}

void Terminal::clear_scrollback()
{
    primary_.grid.clear_history();
    collect_images();
    follow_ = true;
    full_damage_ = true;
}

void Terminal::feed(const uint8_t* data, size_t n)
{
    parser_.feed(data, n);
    if (sel_anchor_ || sel_shown_)
        sel_refresh();
}

void Terminal::feed(const char* s)
{
    parser_.feed((const uint8_t*)s, strlen(s));
}

bool Terminal::resize(int cols, int rows)
{
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    if (cols > 4096) cols = 4096;
    if (cols == cols_ && rows == rows_)
        return true;
    /* The primary screen: reflow it around its cursor, which is the live
     * one or, while the alternate screen is up, the one 1049 saved. */
    bool did_reflow = false;
    if (reflow_ && cols != cols_) {
        Cursor& pc = alt_ ? primary_.saved : cur_;
        int x = pc.x, y = pc.y;
        bool pend = pc.pending_wrap;
        if (!primary_.grid.reflow(cols, rows, &x, &y, &pend))
            return false;
        pc.x = x;
        pc.y = y;
        pc.pending_wrap = pend;
        did_reflow = true;
    }
    int cy = cur_.y;
    int pcy = alt_ ? primary_.saved.y : cy, acy = cy;
    if (!did_reflow && !primary_.grid.resize(cols, rows, alt_ ? &pcy : &cy))
        return false;
    if (!did_reflow && alt_)
        primary_.saved.y = pcy;
    if (!alt_screen_.grid.resize(cols, rows, alt_ ? &cy : &acy))
        return false;
    if (did_reflow && !alt_)
        cy = cur_.y;
    bool* nt = (bool*)calloc((size_t)cols, sizeof(bool));
    if (!nt)
        return false;
    for (int x = 0; x < cols; x++)
        nt[x] = x < cols_ ? tabs_[x] : (x > 0 && x % 8 == 0);
    free(tabs_);
    tabs_ = nt;
    cols_ = cols;
    rows_ = rows;
    top_ = 0;
    bot_ = rows - 1;
    cur_.y = cy;
    if (!(did_reflow && !alt_)) {
        /* Growing wider lets a pending wrap continue on the same line. */
        if (cur_.pending_wrap && cols > cur_.x + 1)
            cur_.x++;
        cur_.pending_wrap = false;
    }
    if (cur_.x >= cols) cur_.x = cols - 1;
    if (cur_.y >= rows) cur_.y = rows - 1;
    if (cur_.y < 0) cur_.y = 0;
    Screen* scr[2] = { &primary_, &alt_screen_ };
    for (int i = 0; i < 2; i++) {
        Cursor& s = scr[i]->saved;
        if (s.x >= cols) s.x = cols - 1;
        if (s.y >= rows) s.y = rows - 1;
    }
    prev_valid_ = false;
    if (sel_anchor_ || sel_shown_)
        sel_refresh();
    return true;
}

int Terminal::cell_text(const Cell& c, uint32_t* out, int max) const
{
    if (max <= 0)
        return 0;
    if (c.is_image()) {
        out[0] = ' ';
        return 1;
    }
    if (c.is_grapheme()) {
        const uint32_t* cps;
        int n = screen().grid.graphemes().get(c.cp(), &cps);
        if (n > max) n = max;
        for (int i = 0; i < n; i++)
            out[i] = cps[i];
        return n;
    }
    out[0] = c.cp();
    return c.cp() ? 1 : 0;
}

void Terminal::row_text(int y, Buffer& out) const
{
    const Row& r = screen().grid.row(y);
    append_cells(r, 0, r.cols - 1, out, false);
}

/* Cells lo..hi of a row as UTF-8, trailing empty cells dropped. */
void Terminal::append_cells(const Row& r, int lo, int hi, Buffer& out, bool trim_spaces) const
{
    if (hi >= r.cols)
        hi = r.cols - 1;
    int last = hi;
    while (last >= lo && (r.cells[last].is_empty() ||
                          (trim_spaces && r.cells[last].cp() == ' ' && !r.cells[last].is_grapheme())))
        last--;
    for (int x = lo; x <= last; x++) {
        const Cell& c = r.cells[x];
        if (c.wide() == CELL_SPACER_TAIL)
            continue;
        if (c.wide() == CELL_SPACER_HEAD || c.is_empty()) {
            out.append_byte(' ');
            continue;
        }
        uint32_t cps[TL_GRAPHEME_MAX];
        int n = cell_text(c, cps, TL_GRAPHEME_MAX);
        for (int i = 0; i < n; i++)
            out.append_utf8(cps[i]);
    }
}

void Terminal::screen_text(Buffer& out) const
{
    int last = rows_ - 1;
    Buffer line;
    while (last >= 0) {
        line.clear();
        row_text(last, line);
        if (line.size())
            break;
        last--;
    }
    for (int y = 0; y <= last; y++) {
        if (y)
            out.append_byte('\n');
        row_text(y, out);
    }
}

/* ---- presenting -------------------------------------------------------- */

uint64_t Terminal::viewport_top() const
{
    const Grid& g = screen().grid;
    if (alt_ || follow_)
        return g.active_top();
    const Anchor* a = primary_.grid.anchor(vp_anchor_);
    if (!a || a->lost || a->row < g.base())
        return g.base();
    if (a->row > g.active_top())
        return g.active_top();
    return a->row;
}

void Terminal::scroll_viewport(long n)
{
    if (alt_ || n == 0)
        return;
    const Grid& g = primary_.grid;
    uint64_t top = viewport_top();
    uint64_t lo = g.base(), hi = g.active_top();
    uint64_t t;
    if (n < 0)
        t = (uint64_t)(-n) > top - lo ? lo : top - (uint64_t)(-n);
    else
        t = (uint64_t)n > hi - top ? hi : top + (uint64_t)n;
    if (t == top)
        return;
    follow_ = t == hi;
    primary_.grid.anchor_set(vp_anchor_, t, 0);
    full_damage_ = true;
}

void Terminal::scroll_to_bottom()
{
    if (!follow_) {
        follow_ = true;
        full_damage_ = true;
    }
}

void Terminal::scroll_to_top()
{
    if (alt_)
        return;
    scroll_viewport(-(long)(viewport_top() - primary_.grid.base()));
}

void Terminal::set_default_colors(Rgb fg, Rgb bg)
{
    fg_ = fg;
    bg_ = bg;
    full_damage_ = true;
}

void Terminal::set_palette(int index, Rgb c)
{
    palette_[index & 255] = c;
    full_damage_ = true;
}

View Terminal::view() const
{
    View v;
    const Grid& g = screen().grid;
    v.term_ = this;
    v.grid_ = &g;
    v.cols_ = cols_;
    v.rows_ = rows_;
    v.top_ = viewport_top();
    v.at_bottom_ = v.top_ == g.active_top();
    v.held_ = modes_.sync_output;
    v.n_scroll_ = 0;
    bool full = g.full_damage || full_damage_ || !last_valid_ || alt_ != last_alt_;
    if (!full) {
        if (v.at_bottom_ && last_at_bottom_) {
            /* Following the output: the grid's records are viewport moves. */
            v.n_scroll_ = g.scroll_record_count();
            for (int i = 0; i < v.n_scroll_; i++)
                v.scroll_[i] = g.scroll_record(i);
        } else if (v.top_ != last_top_ || v.at_bottom_ != last_at_bottom_) {
            full = true;
        } else if (g.scroll_record_count() > 0 && v.top_ + (uint64_t)rows_ > g.active_top()) {
            /* Scrolled back, but part of the active area is visible and
             * moved under the viewport. */
            full = true;
        }
    }
    v.full_ = full;
    v.sel_ = selection_range(&v.sel_r0_, &v.sel_c0_, &v.sel_r1_, &v.sel_c1_);

    ViewCursor& c = v.cursor_;
    uint64_t abs = g.active_top() + (uint64_t)cur_.y;
    c.x = cur_.x;
    c.y = abs >= v.top_ && abs < v.top_ + (uint64_t)rows_ ? (int)(abs - v.top_) : -1;
    c.visible = modes_.cursor_visible && c.y >= 0;
    c.blink = modes_.cursor_blink;
    c.shape = cursor_shape_;
    c.wide = false;
    if (c.y >= 0) {
        const Row& r = g.abs_row(abs);
        c.wide = r.cells[c.x].wide() == CELL_WIDE && c.x + 1 < cols_;
        if (c.x > 0 && r.cells[c.x].wide() == CELL_SPACER_TAIL) {
            c.x--;
            c.wide = true;
        }
    }

    /* The cell the cursor was drawn on last time, moved along with any
     * scrolled rows, must be repainted to remove it. */
    v.prev_cursor_x_ = last_cx_;
    v.prev_cursor_wide_ = last_cwide_;
    v.prev_cursor_y_ = -1;
    if (!full && last_cy_ >= 0) {
        int y = last_cy_;
        for (int i = 0; i < v.n_scroll_ && y >= 0; i++) {
            const ScrollRecord& s = v.scroll_[i];
            if (y < s.top || y > s.bottom)
                continue;
            y -= s.count;
            if (y < s.top || y > s.bottom)
                y = -1;
        }
        v.prev_cursor_y_ = y;
    }
    return v;
}

void Terminal::clear_damage()
{
    Grid& g = screen().grid;
    g.clear_damage();
    uint64_t top = viewport_top();
    if (top < g.active_top()) {
        for (int y = 0; y < rows_; y++) {
            uint64_t a = top + (uint64_t)y;
            if (a >= g.active_top())
                break;
            g.abs_row(a).clean();
        }
    }
    full_damage_ = false;
    View v = view();
    last_valid_ = true;
    last_alt_ = alt_;
    last_top_ = v.top_;
    last_at_bottom_ = v.at_bottom_;
    last_cx_ = v.cursor_.x;
    last_cy_ = v.cursor_.y;
    last_cwide_ = v.cursor_.wide;
}

void Terminal::text(uint64_t r0, int c0, uint64_t r1, int c1, Buffer& out) const
{
    const Grid& g = screen().grid;
    uint64_t lo = g.base(), hi = g.base() + g.total_rows() - 1;
    if (r0 > r1 || (r0 == r1 && c0 > c1)) {
        uint64_t tr = r0; r0 = r1; r1 = tr;
        int tc = c0; c0 = c1; c1 = tc;
    }
    if (r1 < lo || r0 > hi)
        return;
    if (r0 < lo) { r0 = lo; c0 = 0; }
    if (r1 > hi) { r1 = hi; c1 = cols_ - 1; }
    for (uint64_t r = r0; r <= r1; r++) {
        const Row& row = g.abs_row(r);
        int from = r == r0 ? c0 : 0;
        int to = r == r1 ? c1 : row.cols - 1;
        bool joined = (row.flags & ROW_WRAPPED) && r < r1;
        if (joined) {
            /* Keep interior blanks of a soft-wrapped row. */
            int last = to;
            if (last >= 0 && row.cells[last].wide() == CELL_SPACER_HEAD)
                last--;
            for (int x = from; x <= last; x++) {
                const Cell& c = row.cells[x];
                if (c.wide() == CELL_SPACER_TAIL)
                    continue;
                if (c.is_empty()) {
                    out.append_byte(' ');
                    continue;
                }
                uint32_t cps[TL_GRAPHEME_MAX];
                int n = cell_text(c, cps, TL_GRAPHEME_MAX);
                for (int i = 0; i < n; i++)
                    out.append_utf8(cps[i]);
            }
        } else {
            append_cells(row, from, to, out, true);
            if (r < r1)
                out.append_byte('\n');
        }
    }
}

/* ---- styles ------------------------------------------------------------ */

void Terminal::collect_styles()
{
    Screen& s = screen();
    s.styles.mark_begin();
    Grid& g = s.grid;
    for (uint64_t a = g.base(); a < g.base() + g.total_rows(); a++) {
        const Row& r = g.abs_row(a);
        if (!(r.flags & ROW_HAS_STYLES))
            continue;
        for (int x = 0; x < r.cols; x++)
            s.styles.mark(r.cells[x].style);
    }
    s.styles.mark(cur_.style_id);
    if (s.has_saved)
        s.styles.mark(s.saved.style_id);
    s.styles.sweep();
}

uint16_t Terminal::intern(const Style& st)
{
    if (st.is_default())
        return 0;
    StyleTable& t = screen().styles;
    uint16_t id = t.intern(st);
    if (id == 0) {
        collect_styles();
        id = t.intern(st);
    }
    return id;
}

void Terminal::update_style_id()
{
    cur_.style_id = intern(cur_.style);
}

Cell Terminal::blank()
{
    Cell c = Cell();
    if (cur_.style.bg) {
        Style s;
        s.clear();
        s.bg = cur_.style.bg;
        c.style = intern(s);
    }
    return c;
}

/* ---- cursor ------------------------------------------------------------ */

/* The View repaints the cells under the old and the new cursor itself,
 * so moving the cursor damages nothing. */
void Terminal::damage_cursor()
{
}

void Terminal::set_cursor(int x, int y)
{
    damage_cursor();
    if (x < 0) x = 0;
    if (x >= cols_) x = cols_ - 1;
    if (y < 0) y = 0;
    if (y >= rows_) y = rows_ - 1;
    cur_.x = x;
    cur_.y = y;
    cur_.pending_wrap = false;
    damage_cursor();
}

void Terminal::move_to(int x, int y)
{
    if (modes_.origin) {
        y += top_;
        if (y > bot_) y = bot_;
        if (y < top_) y = top_;
    }
    set_cursor(x, y);
}

void Terminal::save_cursor()
{
    Screen& s = screen();
    s.saved = cur_;
    s.saved.origin = modes_.origin;
    s.has_saved = true;
}

void Terminal::restore_cursor()
{
    Screen& s = screen();
    if (!s.has_saved) {
        /* Like xterm: home, default attributes, origin off. */
        Cursor c;
        cursor_reset(c);
        damage_cursor();
        cur_ = c;
        modes_.origin = false;
        damage_cursor();
        return;
    }
    damage_cursor();
    bool wrap = s.saved.pending_wrap;
    cur_ = s.saved;
    modes_.origin = s.saved.origin;
    if (cur_.x >= cols_) cur_.x = cols_ - 1;
    if (cur_.y >= rows_) cur_.y = rows_ - 1;
    cur_.pending_wrap = wrap && cur_.x == cols_ - 1;
    update_style_id();
    damage_cursor();
}

/* ---- printing ---------------------------------------------------------- */

void Terminal::print(uint32_t cp)
{
    print_char(cp);
}

void Terminal::print_run(const uint8_t* s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        print_char(s[i]);
}

/* A wide character's other half is about to be overwritten or erased:
 * blank the half that would be left behind. */
void Terminal::fix_wide_left(Row& r, int x)
{
    if (x > 0 && x < r.cols && r.cells[x].wide() == CELL_SPACER_TAIL) {
        r.cells[x - 1] = blank();
        r.damage(x - 1, x - 1);
    }
}

void Terminal::fix_wide_right(Row& r, int x)
{
    if (x >= 0 && x + 1 < r.cols && r.cells[x].wide() == CELL_WIDE) {
        r.cells[x + 1] = blank();
        r.damage(x + 1, x + 1);
    }
}

void Terminal::put_cell(int x, int y, uint32_t cp, WideKind w)
{
    sel_touch(y);
    Row& r = row(y);
    Cell& c = r.cells[x];
    c.set(cp, w, cur_.style_id, 0, cur_.protect);
    if (cur_.style_id)
        r.flags |= ROW_HAS_STYLES;
    r.damage(x, x);
}

void Terminal::wrap_line()
{
    row(cur_.y).flags |= ROW_WRAPPED;
    cur_.pending_wrap = false;
    cur_.x = 0;
    index();
}

void Terminal::append_to_previous(uint32_t cp)
{
    if (!prev_valid_ || prev_y_ >= rows_ || prev_x_ >= cols_)
        return;
    Row& r = row(prev_y_);
    Cell& c = r.cells[prev_x_];
    if (c.is_empty())
        return;
    uint32_t cps[TL_GRAPHEME_MAX];
    int n = cell_text(c, cps, TL_GRAPHEME_MAX);
    if (n >= TL_GRAPHEME_MAX)
        return;
    cps[n++] = cp;
    GraphemePool& pool = screen().grid.graphemes();
    if (pool.needs_compaction())
        screen().grid.compact_graphemes();
    uint32_t idx = pool.add(cps, n);
    if (!idx)
        return;
    c.set_grapheme(idx);
    r.flags |= ROW_HAS_GRAPHEMES;
    r.damage(prev_x_, prev_x_);
}

void Terminal::print_char(uint32_t cp)
{
    if (cp < 0x80 || !parser_.utf8())
        cp = cur_.cs.map(cp);
    else
        cur_.cs.single_shift = 0;

    int w = unicode_width(cp, ambiguous_wide_);
    bool boundary = grapheme_boundary(gstate_, cp);
    if (modes_.graphemes) {
        if (!boundary && prev_valid_) {
            append_to_previous(cp);
            return;
        }
        if (w == 0)
            w = 1;                  /* a lone mark starts its own cluster */
    } else if (w == 0) {
        append_to_previous(cp);
        return;
    }

    if (cur_.pending_wrap && modes_.autowrap)
        wrap_line();

    /* A double-width line holds half as many characters. */
    int right = row(cur_.y).line_attr != LINE_SINGLE && cols_ > 1 ? cols_ / 2 : cols_;
    if (cur_.x >= right)
        cur_.x = right - 1;

    if (w == 2 && cur_.x == right - 1) {
        if (right < 2) {
            w = 1;
        } else if (modes_.autowrap) {
            Row& r = row(cur_.y);
            fix_wide_left(r, cur_.x);
            r.cells[cur_.x] = blank();
            r.cells[cur_.x].ch |= (uint32_t)CELL_SPACER_HEAD << Cell::WIDE_SHIFT;
            r.damage(cur_.x, cur_.x);
            wrap_line();
        } else {
            cur_.x = right - 2;     /* no wrap: the last two columns */
        }
    }

    Row& r = row(cur_.y);
    if (modes_.insert) {
        int n = w;
        int x = cur_.x;
        fix_wide_left(r, x);
        if (cols_ - n - 1 >= x)
            fix_wide_right(r, cols_ - n - 1);
        memmove(r.cells + x + n, r.cells + x, (size_t)(cols_ - x - n) * sizeof(Cell));
        r.damage(x, cols_ - 1);
    }

    fix_wide_left(r, cur_.x);
    fix_wide_right(r, cur_.x + w - 1);
    put_cell(cur_.x, cur_.y, cp, w == 2 ? CELL_WIDE : CELL_NARROW);
    if (w == 2)
        put_cell(cur_.x + 1, cur_.y, 0, CELL_SPACER_TAIL);

    prev_valid_ = true;
    prev_x_ = cur_.x;
    prev_y_ = cur_.y;
    last_cp_ = cp;

    if (cur_.x + w >= right) {
        cur_.x = right - 1;
        if (modes_.autowrap)
            cur_.pending_wrap = true;
    } else {
        cur_.x += w;
    }
    damage_cursor();
}

/* ---- scrolling and editing --------------------------------------------- */

void Terminal::scroll_up(int n)
{
    Grid& g = screen().grid;
    Cell b = blank();
    /* Lines scrolled off the full-screen region of the primary screen go
     * to the scrollback; a partial region just loses them. */
    /* Only a region scroll moves text under fixed row numbers. */
    if (alt_ || top_ != 0 || bot_ != rows_ - 1)
        sel_rows_moving(top_, bot_);
    g.scroll_up(top_, bot_, n, b, !alt_);
    prev_valid_ = false;
}

void Terminal::scroll_down(int n)
{
    sel_rows_moving(top_, bot_);
    screen().grid.scroll_down(top_, bot_, n, blank());
    prev_valid_ = false;
}

void Terminal::index()
{
    if (cur_.y == bot_) {
        scroll_up(1);
    } else if (cur_.y < rows_ - 1) {
        damage_cursor();
        cur_.y++;
        damage_cursor();
    }
}

void Terminal::reverse_index()
{
    if (cur_.y == top_) {
        scroll_down(1);
    } else if (cur_.y > 0) {
        damage_cursor();
        cur_.y--;
        damage_cursor();
    }
}

void Terminal::erase_cells(int y, int lo, int hi, bool selective)
{
    sel_touch(y);
    if (lo < 0) lo = 0;
    if (hi >= cols_) hi = cols_ - 1;
    if (lo > hi)
        return;
    Row& r = row(y);
    Cell b = blank();
    if (!selective) {
        fix_wide_left(r, lo);
        fix_wide_right(r, hi);
        screen().grid.fill(r, lo, hi, b);
        return;
    }
    for (int x = lo; x <= hi; x++) {
        if (r.cells[x].is_protected())
            continue;
        fix_wide_left(r, x);
        fix_wide_right(r, x);
        r.cells[x] = b;
    }
    r.damage(lo, hi);
}

void Terminal::erase_display(int mode, bool selective)
{
    switch (mode) {
    case 0:
        erase_cells(cur_.y, cur_.x, cols_ - 1, selective);
        for (int y = cur_.y + 1; y < rows_; y++)
            erase_cells(y, 0, cols_ - 1, selective);
        break;
    case 1:
        for (int y = 0; y < cur_.y; y++)
            erase_cells(y, 0, cols_ - 1, selective);
        erase_cells(cur_.y, 0, cur_.x, selective);
        break;
    case 2:
        for (int y = 0; y < rows_; y++) {
            erase_cells(y, 0, cols_ - 1, selective);
            if (!selective)
                row(y).flags &= (uint8_t)~ROW_WRAPPED;
        }
        break;
    case 3:
        if (!selective)
            screen().grid.clear_history();
        break;
    }
    cur_.pending_wrap = false;
    prev_valid_ = false;
}

void Terminal::erase_line(int mode, bool selective)
{
    switch (mode) {
    case 0: erase_cells(cur_.y, cur_.x, cols_ - 1, selective); break;
    case 1: erase_cells(cur_.y, 0, cur_.x, selective); break;
    case 2: erase_cells(cur_.y, 0, cols_ - 1, selective); break;
    }
    cur_.pending_wrap = false;
    prev_valid_ = false;
}

void Terminal::insert_chars(int n)
{
    sel_touch(cur_.y);
    Row& r = row(cur_.y);
    int x = cur_.x;
    if (n > cols_ - x)
        n = cols_ - x;
    fix_wide_left(r, x);
    if (cols_ - n - 1 >= x)
        fix_wide_right(r, cols_ - n - 1);
    memmove(r.cells + x + n, r.cells + x, (size_t)(cols_ - x - n) * sizeof(Cell));
    Cell b = blank();
    for (int i = 0; i < n; i++)
        r.cells[x + i] = b;
    r.damage(x, cols_ - 1);
    cur_.pending_wrap = false;
}

void Terminal::delete_chars(int n)
{
    sel_touch(cur_.y);
    Row& r = row(cur_.y);
    int x = cur_.x;
    if (n > cols_ - x)
        n = cols_ - x;
    fix_wide_left(r, x);
    fix_wide_right(r, x + n - 1);
    memmove(r.cells + x, r.cells + x + n, (size_t)(cols_ - x - n) * sizeof(Cell));
    Cell b = blank();
    for (int i = cols_ - n; i < cols_; i++)
        r.cells[i] = b;
    r.damage(x, cols_ - 1);
    cur_.pending_wrap = false;
}

void Terminal::insert_lines(int n)
{
    if (cur_.y < top_ || cur_.y > bot_)
        return;
    sel_rows_moving(cur_.y, bot_);
    screen().grid.scroll_down(cur_.y, bot_, n, blank());
    /* The cursor stays put: the VT102 guide does not move it and neither
     * does xterm (ECMA-48 would move it to the line home). */
    cur_.pending_wrap = false;
}

void Terminal::delete_lines(int n)
{
    if (cur_.y < top_ || cur_.y > bot_)
        return;
    sel_rows_moving(cur_.y, bot_);
    screen().grid.scroll_up(cur_.y, bot_, n, blank(), false);
    cur_.pending_wrap = false;
}

void Terminal::tab_forward(int n)
{
    int x = cur_.x;
    while (n-- > 0) {
        x++;
        while (x < cols_ - 1 && !tabs_[x])
            x++;
        if (x >= cols_ - 1) {
            x = cols_ - 1;
            break;
        }
    }
    damage_cursor();
    cur_.x = x;
    cur_.pending_wrap = false;
    damage_cursor();
}

void Terminal::tab_backward(int n)
{
    int x = cur_.x;
    while (n-- > 0 && x > 0) {
        x--;
        while (x > 0 && !tabs_[x])
            x--;
    }
    set_cursor(x, cur_.y);
}

void Terminal::decaln()
{
    select_clear();
    top_ = 0;
    bot_ = rows_ - 1;
    modes_.origin = false;
    Cell e = Cell();
    e.set('E', CELL_NARROW, 0, 0, false);
    for (int y = 0; y < rows_; y++) {
        Row& r = row(y);
        screen().grid.fill(r, 0, cols_ - 1, e);
        r.flags &= (uint8_t)~ROW_WRAPPED;
        r.line_attr = LINE_SINGLE;
    }
    set_cursor(0, 0);
}

/* ---- controls ---------------------------------------------------------- */

void Terminal::execute(uint8_t c)
{
    gstate_.reset();
    switch (c) {
    case 0x05:                          /* ENQ: empty answerback */
        break;
    case 0x07:
        host_->bell();
        break;
    case 0x08:                          /* BS */
        if (cur_.x > 0)
            set_cursor(cur_.x - 1, cur_.y);
        else
            cur_.pending_wrap = false;
        prev_valid_ = false;
        break;
    case 0x09:
        tab_forward(1);
        prev_valid_ = false;
        break;
    case 0x0a: case 0x0b: case 0x0c:    /* LF, VT, FF */
        cur_.pending_wrap = false;
        index();
        if (modes_.newline)
            set_cursor(0, cur_.y);
        prev_valid_ = false;
        break;
    case 0x0d:
        set_cursor(0, cur_.y);
        prev_valid_ = false;
        break;
    case 0x0e:                          /* SO: G1 into GL */
        cur_.cs.gl = 1;
        break;
    case 0x0f:                          /* SI: G0 into GL */
        cur_.cs.gl = 0;
        break;
    case 0x84:                          /* IND */
        cur_.pending_wrap = false;
        index();
        prev_valid_ = false;
        break;
    case 0x85:                          /* NEL */
        cur_.pending_wrap = false;
        index();
        set_cursor(0, cur_.y);
        prev_valid_ = false;
        break;
    case 0x88:                          /* HTS */
        tabs_[cur_.x] = true;
        break;
    case 0x8d:                          /* RI */
        cur_.pending_wrap = false;
        reverse_index();
        prev_valid_ = false;
        break;
    case 0x8e:
        cur_.cs.single_shift = 2;
        break;
    case 0x8f:
        cur_.cs.single_shift = 3;
        break;
    case 0x9a: {                        /* DECID */
        Seq q;
        q.clear();
        q.final_byte = 'c';
        device_attributes(q);
        break;
    }
    default:
        break;
    }
}

void Terminal::esc_dispatch(const Seq& q)
{
    gstate_.reset();
    uint8_t f = q.final_byte;
    if (q.n_intermediates == 0) {
        switch (f) {
        case '7': save_cursor(); break;
        case '8': restore_cursor(); prev_valid_ = false; break;
        case 'D': execute(0x84); break;
        case 'E': execute(0x85); break;
        case 'H': execute(0x88); break;
        case 'M': execute(0x8d); break;
        case 'N': execute(0x8e); break;
        case 'O': execute(0x8f); break;
        case 'Z': execute(0x9a); break;
        case 'c': reset(); break;
        case '=': modes_.app_keypad = true; break;
        case '>': modes_.app_keypad = false; break;
        case 'n': cur_.cs.gl = 2; break;      /* LS2 */
        case 'o': cur_.cs.gl = 3; break;      /* LS3 */
        case '~': cur_.cs.gr = 1; break;      /* LS1R */
        case '}': cur_.cs.gr = 2; break;      /* LS2R */
        case '|': cur_.cs.gr = 3; break;      /* LS3R */
        default: break;                        /* includes ST, ESC \ */
        }
        return;
    }
    if (q.n_intermediates != 1)
        return;
    uint8_t i = q.intermediates[0];
    switch (i) {
    case '(': case ')': case '*': case '+': {
        int cs = CharsetState::from_final(f, false);
        if (cs >= 0)
            cur_.cs.g[i - '('] = (uint8_t)cs;
        break;
    }
    case '-': case '.': case '/': {
        int cs = CharsetState::from_final(f, true);
        if (cs >= 0)
            cur_.cs.g[i - ',' ] = (uint8_t)cs;
        break;
    }
    case '#':
        if (f == '8') {
            decaln();
        } else if (f >= '3' && f <= '6') {
            Row& r = row(cur_.y);
            r.line_attr = f == '3' ? LINE_DOUBLE_TOP : f == '4' ? LINE_DOUBLE_BOTTOM
                        : f == '5' ? LINE_SINGLE : LINE_DOUBLE_WIDTH;
            r.damage_all();
            if (r.line_attr != LINE_SINGLE && cur_.x >= cols_ / 2)
                set_cursor(cols_ / 2 - 1, cur_.y);
        }
        break;
    case '%':
        if (f == 'G')
            parser_.set_utf8(true);
        else if (f == '@')
            parser_.set_utf8(false);
        break;
    default:
        break;
    }
}

/* ---- CSI --------------------------------------------------------------- */

void Terminal::csi_dispatch(const Seq& q)
{
    gstate_.reset();
    prev_valid_ = false;
    const Params& p = q.params;
    uint8_t f = q.final_byte;
    uint8_t im = q.intermediate();
    int n = p.top(0, 1);

    if (q.priv == 0 && im == 0) {
        switch (f) {
        case '@': insert_chars(n); return;
        case 'A': {
            int lim = cur_.y >= top_ ? top_ : 0;
            int y = cur_.y - n;
            set_cursor(cur_.x, y < lim ? lim : y);
            return;
        }
        case 'B': case 'e': {
            int lim = cur_.y <= bot_ ? bot_ : rows_ - 1;
            int y = cur_.y + n;
            set_cursor(cur_.x, y > lim ? lim : y);
            return;
        }
        case 'C': case 'a': set_cursor(cur_.x + n, cur_.y); return;
        case 'D': case 'j': set_cursor(cur_.x - n, cur_.y); return;
        case 'k': set_cursor(cur_.x, cur_.y - n); return;   /* VPB */
        case 'E': {
            int lim = cur_.y <= bot_ ? bot_ : rows_ - 1;
            int y = cur_.y + n;
            set_cursor(0, y > lim ? lim : y);
            return;
        }
        case 'F': {
            int lim = cur_.y >= top_ ? top_ : 0;
            int y = cur_.y - n;
            set_cursor(0, y < lim ? lim : y);
            return;
        }
        case 'G': case '`': set_cursor(n - 1, cur_.y); return;
        case 'H': case 'f':
            move_to(p.top(1, 1) - 1, p.top(0, 1) - 1);
            return;
        case 'I': tab_forward(n); return;
        case 'J': erase_display(p.top(0, 0), false); return;
        case 'K': erase_line(p.top(0, 0), false); return;
        case 'L': insert_lines(n); return;
        case 'M': delete_lines(n); return;
        case 'P': delete_chars(n); return;
        case 'S': scroll_up(n); return;
        case 'T':
            if (p.top_count() <= 1)
                scroll_down(n);
            return;
        case 'X': {
            int hi = cur_.x + n - 1;
            erase_cells(cur_.y, cur_.x, hi, false);
            cur_.pending_wrap = false;
            return;
        }
        case 'Z': tab_backward(n); return;
        case 'b':                           /* REP */
            if (last_cp_) {
                if (n > 65535) n = 65535;
                for (int i = 0; i < n; i++)
                    print_char(last_cp_);
            }
            return;
        case 'c':
            device_attributes(q);
            return;
        case 'd': move_to(cur_.x, n - 1); return;
        case 'g':
            if (p.top(0, 0) == 0)
                tabs_[cur_.x] = false;
            else if (p.top(0, 0) == 3)
                memset(tabs_, 0, (size_t)cols_ * sizeof(bool));
            return;
        case 'h': case 'l':
            for (int i = 0; i < p.top_count(); i++)
                set_mode(p.top(i, 0), false, f == 'h');
            return;
        case 'm': sgr(p); return;
        case 'n': device_status(q); return;
        case 'r': {
            int t = p.top(0, 1) - 1;
            int b = p.top(1, rows_) - 1;
            if (b >= rows_) b = rows_ - 1;
            if (t < b) {
                top_ = t;
                bot_ = b;
                move_to(0, 0);
            }
            return;
        }
        case 's': save_cursor(); return;
        case 'u': restore_cursor(); return;
        case 'x': {                         /* DECREQTPARM */
            int v = p.top(0, 0);
            if (v == 0 || v == 1) {
                char b[64];
                sprintf(b, "\x1b[%d;1;1;128;128;1;0x", v + 2);
                reply(b);
            }
            return;
        }
        case 't': window_ops(p); return;   /* XTWINOPS: size reports */
        default:
            return;
        }
    }

    if (q.priv == '?' && im == 0) {
        switch (f) {
        case 'h': case 'l':
            for (int i = 0; i < p.top_count(); i++)
                set_mode(p.top(i, 0), true, f == 'h');
            return;
        case 'J': erase_display(p.top(0, 0), true); return;
        case 'K': erase_line(p.top(0, 0), true); return;
        case 'n': device_status(q); return;
        case 'S': graphics_attributes(p); return;   /* XTSMGRAPHICS */
        default: return;
        }
    }

    if (q.priv == '>' && im == 0 && f == 'c') {
        device_attributes(q);
        return;
    }
    if (q.priv == '=' && im == 0 && f == 'c') {
        device_attributes(q);
        return;
    }

    if (q.priv == 0 && im == ' ' && f == 'q') {   /* DECSCUSR */
        int s = p.top(0, 0);
        modes_.cursor_blink = s == 0 || s % 2 == 1;
        cursor_shape_ = s <= 2 ? CURSOR_BLOCK : s <= 4 ? CURSOR_UNDERLINE : CURSOR_BAR;
        damage_cursor();
        return;
    }
    if (q.priv == 0 && im == '"' && f == 'q') {   /* DECSCA */
        cur_.protect = p.top(0, 0) == 1;
        return;
    }
    if (q.priv == 0 && im == '!' && f == 'p') {   /* DECSTR */
        soft_reset();
        return;
    }
    if (im == '$' && f == 'p') {                  /* DECRQM */
        int m = p.top(0, 0);
        char b[48];
        sprintf(b, "\x1b[%s%d;%d$y", q.priv == '?' ? "?" : "", m, mode_state(m, q.priv == '?'));
        reply(b);
        return;
    }
}

/* ---- modes ------------------------------------------------------------- */

void Terminal::switch_screen(bool alt, bool clear)
{
    if (alt == alt_)
        return;
    select_clear();
    alt_ = alt;
    if (alt && clear) {
        Cell b = Cell();
        for (int y = 0; y < rows_; y++)
            alt_screen_.grid.clear_row(alt_screen_.grid.row(y), b);
    }
    full_damage_ = true;
    update_style_id();
    prev_valid_ = false;
}

void Terminal::set_mode(int m, bool priv, bool on)
{
    if (!priv) {
        switch (m) {
        case 4: modes_.insert = on; break;
        case 20: modes_.newline = on; break;
        default: break;
        }
        return;
    }
    switch (m) {
    case 1: modes_.app_cursor = on; break;
    case 3:
        if (!modes_.allow_132)
            break;
        modes_.col132 = on;
        host_->request_resize(on ? 132 : 80, rows_);
        top_ = 0;
        bot_ = rows_ - 1;
        erase_display(2, false);
        set_cursor(0, 0);
        break;
    case 5:
        modes_.reverse = on;
        full_damage_ = true;
        break;
    case 6:
        modes_.origin = on;
        move_to(0, 0);
        break;
    case 7:
        modes_.autowrap = on;
        if (!on)
            cur_.pending_wrap = false;
        break;
    case 8: modes_.autorepeat = on; break;
    case 12: modes_.cursor_blink = on; break;
    case 25: modes_.cursor_visible = on; break;
    case 40: modes_.allow_132 = on; break;
    case 45: modes_.reverse_wrap = on; break;
    case 66: modes_.app_keypad = on; break;
    case 9: case 1000: case 1002: case 1003:
        modes_.mouse = on ? m : 0;
        break;
    case 1005: case 1006: case 1015: case 1016:
        if (on)
            modes_.mouse_encoding = m;
        else if (modes_.mouse_encoding == m)
            modes_.mouse_encoding = 0;
        break;
    case 1004: modes_.focus_events = on; break;
    case 1007: modes_.alt_scroll = on; break;
    case 2004: modes_.bracketed_paste = on; break;
    case 80: modes_.sixel_display = on; break;
    case 8452: modes_.sixel_right = on; break;
    case 2026: modes_.sync_output = on; break;
    case 2027: modes_.graphemes = on; break;
    case 47:
        switch_screen(on, false);
        break;
    case 1047:
        if (!on && alt_) {
            Cell b = Cell();
            for (int y = 0; y < rows_; y++)
                alt_screen_.grid.clear_row(alt_screen_.grid.row(y), b);
        }
        switch_screen(on, false);
        break;
    case 1048:
        if (on) save_cursor(); else restore_cursor();
        break;
    case 1049:
        if (on) {
            if (!alt_) {
                save_cursor();
                switch_screen(true, true);
            }
        } else if (alt_) {
            switch_screen(false, false);
            restore_cursor();
        }
        break;
    default:
        break;
    }
}

/* DECRPM values: 0 not recognized, 1 set, 2 reset, 3 permanently set,
 * 4 permanently reset. */
int Terminal::mode_state(int m, bool priv) const
{
    if (!priv) {
        switch (m) {
        case 4: return modes_.insert ? 1 : 2;
        case 20: return modes_.newline ? 1 : 2;
        default: return 0;
        }
    }
    bool v;
    switch (m) {
    case 1: v = modes_.app_cursor; break;
    case 3: v = modes_.col132; break;
    case 5: v = modes_.reverse; break;
    case 6: v = modes_.origin; break;
    case 7: v = modes_.autowrap; break;
    case 8: v = modes_.autorepeat; break;
    case 12: v = modes_.cursor_blink; break;
    case 25: v = modes_.cursor_visible; break;
    case 40: v = modes_.allow_132; break;
    case 45: v = modes_.reverse_wrap; break;
    case 66: v = modes_.app_keypad; break;
    case 9: case 1000: case 1002: case 1003: v = modes_.mouse == m; break;
    case 1005: case 1006: case 1015: case 1016: v = modes_.mouse_encoding == m; break;
    case 1004: v = modes_.focus_events; break;
    case 1007: v = modes_.alt_scroll; break;
    case 2004: v = modes_.bracketed_paste; break;
    case 80: v = modes_.sixel_display; break;
    case 8452: v = modes_.sixel_right; break;
    case 2026: v = modes_.sync_output; break;
    case 2027: v = modes_.graphemes; break;
    case 47: case 1047: case 1049: v = alt_; break;
    case 1048: v = false; break;
    default: return 0;
    }
    return v ? 1 : 2;
}

void Terminal::soft_reset()
{
    modes_.insert = false;
    modes_.origin = false;
    modes_.autowrap = true;
    modes_.cursor_visible = true;
    modes_.app_cursor = false;
    modes_.app_keypad = false;
    top_ = 0;
    bot_ = rows_ - 1;
    cur_.style.clear();
    cur_.style_id = 0;
    cur_.protect = false;
    cur_.pending_wrap = false;
    cur_.cs.reset();
    Screen& s = screen();
    s.saved = cur_;
    s.saved.x = s.saved.y = 0;
    s.has_saved = true;
}

/* ---- SGR --------------------------------------------------------------- */

/* Parse an extended colour starting at entry e (38, 48 or 58). Returns the
 * index of the last entry consumed. *out is left alone on bad input. */
int Terminal::sgr_color(const Params& p, int e, uint32_t* out)
{
    int subs = p.subs_of(e);
    if (subs > 0) {
        /* Colon form: 38:5:n or 38:2:[cs]:r:g:b */
        int kind = p.value[e + 1];
        if (kind == 5 && subs >= 2) {
            *out = color_palette(p.value[e + 2]);
        } else if (kind == 2 && subs >= 4) {
            int o = subs >= 5 ? e + 3 : e + 2;
            *out = color_rgb(p.value[o], p.value[o + 1], p.value[o + 2]);
        }
        return e + subs;
    }
    /* Semicolon form: 38;5;n or 38;2;r;g;b */
    if (e + 1 >= p.count)
        return e;
    int kind = p.value[e + 1];
    if (kind == 5) {
        if (e + 2 < p.count)
            *out = color_palette(p.value[e + 2]);
        return e + 2;
    }
    if (kind == 2) {
        if (e + 4 < p.count)
            *out = color_rgb(p.value[e + 2], p.value[e + 3], p.value[e + 4]);
        return e + 4;
    }
    return e + 1;
}

void Terminal::sgr(const Params& p)
{
    Style& s = cur_.style;
    if (p.count == 0) {
        s.clear();
        update_style_id();
        return;
    }
    for (int e = 0; e < p.count; e++) {
        if (p.is_sub[e])
            continue;
        int v = p.value[e];
        switch (v) {
        case 0: s.clear(); break;
        case 1: s.attrs |= ATTR_BOLD; break;
        case 2: s.attrs |= ATTR_FAINT; break;
        case 3: s.attrs |= ATTR_ITALIC; break;
        case 4:
            if (p.subs_of(e) > 0) {
                int u = p.value[e + 1];
                s.set_underline(u <= UL_DASHED ? (Underline)u : UL_SINGLE);
            } else {
                s.set_underline(UL_SINGLE);
            }
            break;
        case 5: case 6: s.attrs |= ATTR_BLINK; break;
        case 7: s.attrs |= ATTR_INVERSE; break;
        case 8: s.attrs |= ATTR_INVISIBLE; break;
        case 9: s.attrs |= ATTR_STRIKE; break;
        case 21: s.set_underline(UL_DOUBLE); break;
        case 22: s.attrs &= (uint16_t)~(ATTR_BOLD | ATTR_FAINT); break;
        case 23: s.attrs &= (uint16_t)~ATTR_ITALIC; break;
        case 24: s.set_underline(UL_NONE); break;
        case 25: s.attrs &= (uint16_t)~ATTR_BLINK; break;
        case 27: s.attrs &= (uint16_t)~ATTR_INVERSE; break;
        case 28: s.attrs &= (uint16_t)~ATTR_INVISIBLE; break;
        case 29: s.attrs &= (uint16_t)~ATTR_STRIKE; break;
        case 38: e = sgr_color(p, e, &s.fg); break;
        case 39: s.fg = color_default(); break;
        case 48: e = sgr_color(p, e, &s.bg); break;
        case 49: s.bg = color_default(); break;
        case 53: s.attrs |= ATTR_OVERLINE; break;
        case 55: s.attrs &= (uint16_t)~ATTR_OVERLINE; break;
        case 58: e = sgr_color(p, e, &s.ul); break;
        case 59: s.ul = color_default(); break;
        default:
            if (v >= 30 && v <= 37)
                s.fg = color_palette(v - 30);
            else if (v >= 40 && v <= 47)
                s.bg = color_palette(v - 40);
            else if (v >= 90 && v <= 97)
                s.fg = color_palette(v - 90 + 8);
            else if (v >= 100 && v <= 107)
                s.bg = color_palette(v - 100 + 8);
            break;
        }
        /* Skip any sub-parameters we did not consume. */
        while (e + 1 < p.count && p.is_sub[e + 1])
            e++;
    }
    update_style_id();
}

/* ---- reports ----------------------------------------------------------- */

void Terminal::reply(const char* s)
{
    host_->write_pty((const uint8_t*)s, strlen(s));
}

void Terminal::device_status(const Seq& q)
{
    int v = q.params.top(0, 0);
    char b[64];
    if (q.priv == 0) {
        if (v == 5) {
            reply("\x1b[0n");
        } else if (v == 6) {
            int y = cur_.y - (modes_.origin ? top_ : 0);
            sprintf(b, "\x1b[%d;%dR", y + 1, cur_.x + 1);
            reply(b);
        }
        return;
    }
    switch (v) {
    case 6: {                               /* DECXCPR */
        int y = cur_.y - (modes_.origin ? top_ : 0);
        sprintf(b, "\x1b[?%d;%d;1R", y + 1, cur_.x + 1);
        reply(b);
        break;
    }
    case 15: reply("\x1b[?13n"); break;     /* no printer */
    case 25: reply("\x1b[?21n"); break;     /* UDKs locked */
    case 26: reply("\x1b[?27;1;0;0n"); break; /* North American keyboard */
    default: break;
    }
}

void Terminal::device_attributes(const Seq& q)
{
    if (q.params.top(0, 0) != 0)
        return;
    if (q.priv == 0)
        reply("\x1b[?64;1;4;6;22c");        /* VT420 class: 132 cols, sixel, selective erase, colour */
    else if (q.priv == '>')
        reply("\x1b[>41;1;0c");
    else if (q.priv == '=')
        reply("\x1bP!|00000000\x1b\\");
}

/* ---- strings ----------------------------------------------------------- */

/* XTWINOPS: only the reports (14 the text area, 16 a cell, 18 the size
 * in cells); moving and resizing the window is the window manager's. */
void Terminal::window_ops(const Params& p)
{
    char b[64];
    int cw = cell_w_ > 0 ? cell_w_ : 10, ch = cell_h_ > 0 ? cell_h_ : 20;
    switch (p.top(0, 0)) {
    case 14: sprintf(b, "\x1b[4;%d;%dt", rows_ * ch, cols_ * cw); reply(b); break;
    case 16: sprintf(b, "\x1b[6;%d;%dt", ch, cw); reply(b); break;
    case 18: sprintf(b, "\x1b[8;%d;%dt", rows_, cols_); reply(b); break;
    default: break;
    }
}

/* XTSMGRAPHICS: CSI ? Pi ; Pa S. Pi 1 colour registers, 2 sixel size;
 * Pa 1 read, 4 read the maximum. Setting is not supported (status 3). */
void Terminal::graphics_attributes(const Params& p)
{
    int item = p.top(0, 0), action = p.top(1, 0);
    char b[80];
    if (item != 1 && item != 2) {
        sprintf(b, "\x1b[?%d;1;0S", item);          /* unknown item */
    } else if (action != 1 && action != 4) {
        sprintf(b, "\x1b[?%d;3;0S", item);
    } else if (item == 1) {
        sprintf(b, "\x1b[?1;0;%dS", (int)SixelDecoder::REGISTERS);
    } else {
        int cw = cell_w_ > 0 ? cell_w_ : 10, ch = cell_h_ > 0 ? cell_h_ : 20;
        int w = action == 4 ? (int)SixelDecoder::MAX_SIDE : cols_ * cw;
        int h = action == 4 ? (int)SixelDecoder::MAX_SIDE : rows_ * ch;
        sprintf(b, "\x1b[?2;0;%d;%dS", w, h);
    }
    reply(b);
}

/* DCS P1 ; P2 ; P3 q: sixels. */
void Terminal::dcs_hook(const Seq& q)
{
    in_sixel_ = q.final_byte == 'q' && q.priv == 0 && q.n_intermediates == 0;
    if (in_sixel_)
        sixel_.begin(q.params.top(1, 0), (uint32_t)bg_.r << 16 | (uint32_t)bg_.g << 8 | bg_.b);
}

void Terminal::dcs_put(const uint8_t* s, size_t n)
{
    if (in_sixel_)
        sixel_.put(s, n);
}

void Terminal::dcs_unhook(bool aborted)
{
    if (!in_sixel_)
        return;
    in_sixel_ = false;
    uint32_t* px;
    int w, h;
    if (sixel_.finish(&px, &w, &h)) {
        if (aborted) {
            free(px);
            return;
        }
        int cw = cell_w_ > 0 ? cell_w_ : 10, ch = cell_h_ > 0 ? cell_h_ : 20;
        place_image(px, w, h, cw, ch, (w + cw - 1) / cw, (h + ch - 1) / ch,
                    modes_.sixel_display ? MOVE_TOP_LEFT : MOVE_SIXEL);
    }
}

void Terminal::collect_images()
{
    const Grid* grids[2] = { &primary_.grid, &alt_screen_.grid };
    images_.collect(grids, 2);
}

/* An image at the cursor as a block of cols x rows image cells of cw x ch
 * pixels each, down from the cursor row and scrolling at the bottom
 * margin. Then the cursor: MOVE_SIXEL on the line below in the image's
 * first column (xterm's sixel scrolling, or right of it on its last row
 * with mode 8452); MOVE_KITTY right of it on its last row; MOVE_NONE
 * where it was. MOVE_TOP_LEFT (DECSDM) puts it at the top left and
 * moves nothing. Takes px; returns the image's id, 0 if refused. */
int Terminal::place_image(uint32_t* px, int w, int h, int cw, int ch, int cols, int rows,
                          int move)
{
    /* Free what nothing shows before the store has to drop what does. */
    if (images_.count() >= ImageStore::MAX_ID - 64 || images_.count() % 64 == 63)
        collect_images();
    int id = images_.add(px, w, h, cw, ch);
    if (!id)
        return 0;
    bool top_left = move == MOVE_TOP_LEFT;
    int x0 = top_left ? 0 : cur_.x;
    int sx = cur_.x, sy = cur_.y;
    if (cols > cols_ - x0)
        cols = cols_ - x0;
    if (cols > Cell::IMAGE_COL_MAX + 1)
        cols = Cell::IMAGE_COL_MAX + 1;
    if (rows > 65535)
        rows = 65535;
    if (top_left && rows > rows_)
        rows = rows_;
    if (cols < 1 || rows < 1)
        return id;
    cur_.pending_wrap = false;
    damage_cursor();
    for (int r = 0; r < rows; r++) {
        int y;
        if (top_left) {
            y = r;
        } else {
            if (r > 0)
                index();
            y = cur_.y;
        }
        sel_touch(y);
        Row& rr = row(y);
        fix_wide_left(rr, x0);
        fix_wide_right(rr, x0 + cols - 1);
        /* Each cell keeps its style: transparent pixels show its
         * background, what was there. */
        for (int c = 0; c < cols; c++)
            rr.cells[x0 + c].set_image(id, c, r, rr.cells[x0 + c].style);
        rr.flags |= ROW_HAS_IMAGES;
        rr.damage(x0, x0 + cols - 1);
    }
    if (top_left)
        return id;
    if (move == MOVE_NONE) {
        /* Back where it was; if placing it scrolled, that row moved up. */
        int scrolled = sy + rows - 1 - cur_.y;
        cur_.x = sx;
        cur_.y = sy - scrolled < 0 ? 0 : sy - scrolled;
    } else if (move == MOVE_KITTY || modes_.sixel_right) {
        if (x0 + cols < cols_) {
            cur_.x = x0 + cols;
        } else {
            cur_.x = cols_ - 1;
            cur_.pending_wrap = move == MOVE_KITTY;
        }
    } else {
        index();
        cur_.x = x0;
    }
    damage_cursor();
    prev_valid_ = false;
    return id;
}

void Terminal::osc_dispatch(const uint8_t* s, size_t n, bool truncated)
{
    gstate_.reset();
    if (truncated)
        return;
    size_t i = 0;
    int code = 0;
    bool digits = false;
    while (i < n && s[i] >= '0' && s[i] <= '9') {
        code = code * 10 + (s[i] - '0');
        if (code > 100000)
            return;
        digits = true;
        i++;
    }
    if (!digits)
        return;
    if (i < n && s[i] == ';')
        i++;
    switch (code) {
    case 0: case 1: case 2: {
        Buffer t;
        if (i < n)
            t.append(s + i, n - i);
        if (code != 1) {
            title_.clear();
            title_.append(t.c_str());
        }
        host_->title_changed(code == 0 ? TITLE_BOTH : code == 1 ? TITLE_ICON : TITLE_WINDOW,
                             t.c_str());
        break;
    }
    default:
        break;
    }
}

} /* namespace tl */
