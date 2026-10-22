#include "screen.h"

#include "../tl/unicode.h"

#include <stdlib.h>
#include <string.h>

namespace tu {

namespace {

const uint32_t k_unknown = 0xffffffffu;

/* h, v, top left, top right, bottom left, bottom right */
const uint32_t k_frames[4][6] = {
    { 0x2500, 0x2502, 0x250c, 0x2510, 0x2514, 0x2518 },   /* single */
    { 0x2550, 0x2551, 0x2554, 0x2557, 0x255a, 0x255d },   /* double */
    { 0x2501, 0x2503, 0x250f, 0x2513, 0x2517, 0x251b },   /* heavy */
    { 0x2500, 0x2502, 0x256d, 0x256e, 0x2570, 0x256f }    /* rounded */
};

/* A single line meeting a frame of each style: left, right, top, bottom */
const uint32_t k_joins[4][4] = {
    { 0x251c, 0x2524, 0x252c, 0x2534 },
    { 0x255f, 0x2562, 0x2564, 0x2567 },
    { 0x2520, 0x2528, 0x252f, 0x2537 },
    { 0x251c, 0x2524, 0x252c, 0x2534 }
};

int char_width(uint32_t cp)
{
    if (cp < 0x20 || cp == 0x7f)
        return 1;           /* shown as '?' */
    if (cp < 0x7f)
        return 1;
    return tl::unicode_width(cp, false);
}

Color darken(Color c, Color dflt)
{
    if (c == COLOR_DEFAULT)
        c = dflt;
    unsigned r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    return ((Color)(r * 2 / 5) << 16) | ((Color)(g * 2 / 5) << 8) | (Color)(b * 2 / 5);
}

} /* namespace */

int decode_char(const char* s, uint32_t* cp)
{
    const unsigned char* p = (const unsigned char*)s;
    if (!p[0])
        return 0;
    if (p[0] < 0x80) {
        *cp = p[0];
        return 1;
    }
    int n = (p[0] & 0xe0) == 0xc0 ? 2 : (p[0] & 0xf0) == 0xe0 ? 3 : (p[0] & 0xf8) == 0xf0 ? 4 : 0;
    uint32_t v = n == 2 ? p[0] & 0x1f : n == 3 ? p[0] & 0x0f : p[0] & 0x07;
    for (int i = 1; i < n; i++) {
        if ((p[i] & 0xc0) != 0x80) {
            n = 0;
            break;
        }
        v = (v << 6) | (p[i] & 0x3f);
    }
    /* Overlong forms and surrogates are not UTF-8 either. */
    if (n == 0 || (n == 2 && v < 0x80) || (n == 3 && v < 0x800) || (n == 4 && v < 0x10000) ||
        (v >= 0xd800 && v < 0xe000) || v > 0x10ffff) {
        *cp = p[0];             /* Latin-1 */
        return 1;
    }
    *cp = v;
    return n;
}

Screen::Screen(Term& term)
    : term_(term), cols_(0), rows_(0), back_(0), front_(0), full_(true), cur_x_(0), cur_y_(0),
      cur_visible_(false)
{
    resize();
}

Screen::~Screen()
{
    delete [] back_;
    delete [] front_;
}

void Screen::resize()
{
    delete [] back_;
    delete [] front_;
    cols_ = term_.cols();
    rows_ = term_.rows();
    back_ = new Cell[(size_t)cols_ * (size_t)rows_];
    front_ = new Cell[(size_t)cols_ * (size_t)rows_];
    clear(Style());
    full_ = true;
}

void Screen::clear(const Style& s)
{
    fill(0, 0, cols_, rows_, ' ', s);
}

void Screen::fill(int x, int y, int w, int h, uint32_t ch, const Style& s)
{
    int x1 = x + w, y1 = y + h;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x1 > cols_)
        x1 = cols_;
    if (y1 > rows_)
        y1 = rows_;
    for (int j = y; j < y1; j++)
        for (int i = x; i < x1; i++) {
            Cell* c = back(i, j);
            c->ch = ch;
            c->style = s;
        }
}

void Screen::put(int x, int y, uint32_t ch, const Style& s)
{
    if (x < 0 || y < 0 || x >= cols_ || y >= rows_)
        return;
    Cell* c = back(x, y);
    /* Overwriting half of a wide character blanks the other half. */
    if (c->ch == 0 && x > 0)
        back(x - 1, y)->ch = ' ';
    if (x + 1 < cols_ && back(x + 1, y)->ch == 0)
        back(x + 1, y)->ch = ' ';
    c->ch = ch;
    c->style = s;
}

int Screen::text(int x, int y, const char* s, const Style& st, int max_cols)
{
    if (y < 0 || y >= rows_ || !s)
        return 0;
    int limit = max_cols < 0 ? cols_ - x : max_cols;
    if (x + limit > cols_)
        limit = cols_ - x;
    int used = 0;
    while (*s && used < limit) {
        uint32_t cp;
        int n = decode_char(s, &cp);
        s += n;
        int w = char_width(cp);
        if (w == 0)
            continue;           /* combining marks: not yet */
        if (w == 2 && used + 2 > limit) {
            put(x + used, y, ' ', st);
            used++;
            break;
        }
        put(x + used, y, cp < 0x20 || cp == 0x7f ? '?' : cp, st);
        if (w == 2 && x + used + 1 < cols_) {
            Cell* c = back(x + used + 1, y);
            c->ch = 0;
            c->style = st;
        }
        used += w;
    }
    return used;
}

int Screen::text_width(const char* s)
{
    int w = 0;
    while (s && *s) {
        uint32_t cp;
        s += decode_char(s, &cp);
        w += char_width(cp);
    }
    return w;
}

void Screen::restyle(int x, int y, int w, const Style& s)
{
    if (y < 0 || y >= rows_)
        return;
    for (int i = x < 0 ? 0 : x; i < x + w && i < cols_; i++)
        back(i, y)->style = s;
}

FrameStyle Screen::effective(FrameStyle f) const
{
    if (term_.tier() == TIER_MODERN)
        return f;
    /* Consoles and older fonts have single and double lines (code page
     * 437 did), not rounded or heavy ones. */
    if (f == FRAME_ROUNDED)
        return FRAME_SINGLE;
    if (f == FRAME_HEAVY)
        return FRAME_DOUBLE;
    return f;
}

void Screen::frame(int x, int y, int w, int h, FrameStyle f, const Style& s)
{
    if (w < 2 || h < 2)
        return;
    const uint32_t* c = k_frames[effective(f)];
    for (int i = x + 1; i < x + w - 1; i++) {
        put(i, y, c[0], s);
        put(i, y + h - 1, c[0], s);
    }
    for (int j = y + 1; j < y + h - 1; j++) {
        put(x, j, c[1], s);
        put(x + w - 1, j, c[1], s);
    }
    put(x, y, c[2], s);
    put(x + w - 1, y, c[3], s);
    put(x, y + h - 1, c[4], s);
    put(x + w - 1, y + h - 1, c[5], s);
}

void Screen::hline(int x, int y, int w, FrameStyle f, const Style& s)
{
    FrameStyle e = effective(f);
    for (int i = x + 1; i < x + w - 1; i++)
        put(i, y, 0x2500, s);
    put(x, y, k_joins[e][0], s);
    put(x + w - 1, y, k_joins[e][1], s);
}

void Screen::vline(int x, int y, int h, FrameStyle f, const Style& s)
{
    FrameStyle e = effective(f);
    for (int j = y + 1; j < y + h - 1; j++)
        put(x, j, 0x2502, s);
    put(x, y, k_joins[e][2], s);
    put(x, y + h - 1, k_joins[e][3], s);
}

void Screen::shadow(int x, int y, int w, int h)
{
    if (term_.tier() == TIER_BASIC)
        return;
    for (int j = y < 0 ? 0 : y; j < y + h && j < rows_; j++)
        for (int i = x < 0 ? 0 : x; i < x + w && i < cols_; i++) {
            Cell* c = back(i, j);
            if (term_.tier() == TIER_MODERN) {
                c->style.fg = darken(c->style.fg, 0xc0c0c0);
                c->style.bg = darken(c->style.bg, 0x000000);
            } else {
                c->style = Style(0x7f7f7f, 0x000000);
            }
            c->style.attrs &= ~(unsigned)(ATTR_BOLD | ATTR_REVERSE);
        }
}

void Screen::set_cursor(int x, int y, bool visible)
{
    cur_x_ = x;
    cur_y_ = y;
    cur_visible_ = visible;
}

void Screen::flush()
{
    Term& t = term_;
    t.begin_sync();
    if (full_) {
        t.show_cursor(false);
        t.clear_screen();
        /* What the terminal shows now: blanks in the default style. */
        for (int i = 0; i < cols_ * rows_; i++) {
            front_[i].ch = ' ';
            front_[i].style = Style();
        }
        full_ = false;
    }
    t.show_cursor(false);
    for (int y = 0; y < rows_; y++) {
        Cell* b = &back_[y * cols_];
        Cell* f = &front_[y * cols_];
        int x = 0;
        while (x < cols_) {
            if (b[x].ch == f[x].ch && b[x].style == f[x].style) {
                x++;
                continue;
            }
            if (b[x].ch == 0 && x > 0)
                x--;            /* redraw the whole wide character */
            /* Blanks to the end of the line: one clear-to-end. */
            int e = x;
            while (e < cols_ && b[e].ch == ' ' && b[e].style == b[x].style)
                e++;
            if (e == cols_ && cols_ - x > 3) {
                t.move(x, y);
                t.set_style(b[x].style);
                if (t.clear_eol()) {
                    for (int i = x; i < cols_; i++)
                        f[i] = b[i];
                    break;
                }
            }
            if (y == rows_ - 1 && x == cols_ - 1 && t.last_cell_scrolls()) {
                x++;
                continue;
            }
            uint32_t ch = b[x].ch ? b[x].ch : ' ';
            int w = x + 1 < cols_ && b[x + 1].ch == 0 ? 2 : 1;
            t.move(x, y);
            t.set_style(b[x].style);
            t.put_char(ch, w);
            f[x] = b[x];
            if (w == 2)
                f[x + 1] = b[x + 1];
            x += w;
        }
    }
    if (cur_visible_) {
        t.move(cur_x_, cur_y_);
        t.show_cursor(true);
    }
    t.end_sync();
    t.flush();
}

} /* namespace tu */
