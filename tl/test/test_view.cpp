#include "tltest.h"
#include "../terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace tl;

namespace {

struct T {
    Terminal term;
    Buffer buf;
    T(int cols, int rows, size_t sb = 100) { term.init(cols, rows, sb); }
    T& feed(const char* s) { term.feed(s); return *this; }
    /* Viewport row y as text. */
    const char* vrow(int y)
    {
        View v = term.view();
        buf.clear();
        const Row& r = v.row(y);
        int last = r.cols - 1;
        while (last >= 0 && r.cells[last].is_empty())
            last--;
        for (int x = 0; x <= last; x++) {
            uint32_t cps[TL_GRAPHEME_MAX];
            const Cell& c = r.cells[x];
            if (c.wide() == CELL_SPACER_TAIL)
                continue;
            int n = v.text(c, cps, TL_GRAPHEME_MAX);
            if (n == 0)
                buf.append_byte(' ');
            for (int i = 0; i < n; i++)
                buf.append_utf8(cps[i]);
        }
        return buf.c_str();
    }
    /* "y:lo-hi" for every dirty viewport row. */
    const char* dirty()
    {
        View v = term.view();
        buf.clear();
        if (v.full_damage()) {
            buf.append("full");
            return buf.c_str();
        }
        for (int y = 0; y < v.rows(); y++) {
            int lo, hi;
            if (!v.row_dirty(y, &lo, &hi))
                continue;
            if (buf.size())
                buf.append_byte(' ');
            buf.append_int(y);
            buf.append_byte(':');
            buf.append_int(lo);
            buf.append_byte('-');
            buf.append_int(hi);
        }
        return buf.c_str();
    }
    const char* records()
    {
        View v = term.view();
        buf.clear();
        for (int i = 0; i < v.scroll_record_count(); i++) {
            const ScrollRecord& r = v.scroll_record(i);
            if (i)
                buf.append_byte(' ');
            buf.append_int(r.top);
            buf.append_byte('-');
            buf.append_int(r.bottom);
            buf.append_byte(r.count < 0 ? 'v' : '^');
            buf.append_int(r.count < 0 ? -r.count : r.count);
        }
        return buf.c_str();
    }
};

void damage()
{
    T t(10, 4);
    TL_CHECK_STR(t.dirty(), "full");             /* first frame */
    t.term.clear_damage();
    TL_CHECK_STR(t.dirty(), "0:0-0");            /* just the cursor cell */
    t.feed("ab");
    TL_CHECK_STR(t.dirty(), "0:0-2");
    t.term.clear_damage();
    t.feed("\x1b[3;5H");
    TL_CHECK_STR(t.dirty(), "0:2-2 2:4-4");      /* old cursor cell, new one */
    t.term.clear_damage();
    t.feed("\x1b[?25l");
    TL_CHECK(!t.term.view().cursor().visible);

    /* Scrolling the whole screen: a record, and only the new row dirty. */
    T s(5, 3);
    s.feed("1\r\n2\r\n3");
    s.term.clear_damage();
    s.feed("\r\n4");
    TL_CHECK_STR(s.records(), "0-2^1");
    TL_CHECK_STR(s.dirty(), "1:1-1 2:0-4");      /* old cursor cell, moved up; new row */
    s.term.clear_damage();
    s.feed("\r\n5\r\n6");
    TL_CHECK_STR(s.records(), "0-2^2");           /* merged */
    s.term.clear_damage();
    /* More than the region holds: plain damage, no record. */
    s.feed("\r\n\r\n\r\n\r\n");
    TL_CHECK_STR(s.records(), "");
    TL_CHECK(!s.term.view().full_damage());

    /* Region scrolls and IL. */
    T r(5, 5);
    r.feed("\x1b[2;4r\x1b[4;1H");
    r.term.clear_damage();
    r.feed("\n");
    TL_CHECK_STR(r.records(), "1-3^1");
    r.term.clear_damage();
    r.feed("\x1b[2;1H\x1b[2L");
    TL_CHECK_STR(r.records(), "1-3v2");

    /* Screen switch and reverse video repaint everything. */
    T a(5, 3);
    a.term.clear_damage();
    a.feed("\x1b[?1049h");
    TL_CHECK_STR(a.dirty(), "full");
    a.term.clear_damage();
    a.feed("\x1b[?5h");
    TL_CHECK_STR(a.dirty(), "full");

    /* Synchronized output. */
    T h(5, 3);
    h.feed("\x1b[?2026h");
    TL_CHECK(h.term.view().held());
    h.feed("\x1b[?2026l");
    TL_CHECK(!h.term.view().held());
}

void viewport()
{
    T t(5, 3, 100);
    for (int i = 0; i < 10; i++) {
        char b[16];
        sprintf(b, "%d\r\n", i);
        t.feed(b);
    }
    /* Rows 0..9 written, cursor on a blank row 10. */
    TL_CHECK_STR(t.vrow(0), "8");
    TL_CHECK(t.term.view().at_bottom());
    t.term.clear_damage();
    t.term.scroll_viewport(-3);
    TL_CHECK(!t.term.view().at_bottom());
    TL_CHECK_STR(t.vrow(0), "5");
    TL_CHECK_STR(t.dirty(), "full");
    TL_CHECK(!t.term.view().cursor().visible);
    t.term.clear_damage();
    /* Output while scrolled back leaves the viewport on its text. */
    t.feed("a\r\nb\r\nc\r\n");
    TL_CHECK_STR(t.vrow(0), "5");
    TL_CHECK_STR(t.vrow(2), "7");
    TL_CHECK_STR(t.dirty(), "");
    TL_CHECK(t.term.view().scroll_record_count() == 0);
    /* Back to the bottom. */
    t.term.scroll_to_bottom();
    TL_CHECK(t.term.view().at_bottom());
    TL_CHECK_STR(t.vrow(1), "c");
    TL_CHECK_STR(t.dirty(), "full");
    /* Clamped at the top. */
    t.term.scroll_viewport(-1000);
    TL_CHECK(t.term.view().top_row() == t.term.grid().base());
    TL_CHECK_STR(t.vrow(0), "0");
    t.term.scroll_to_top();
    t.term.scroll_viewport(1000);
    TL_CHECK(t.term.view().at_bottom());

    /* A viewport whose rows are pruned from the history clamps to the
     * oldest row. */
    T p(5, 2, 4);
    for (int i = 0; i < 6; i++)
        p.feed("x\r\n");
    p.term.scroll_to_top();
    uint64_t top = p.term.viewport_top();
    for (int i = 0; i < 10; i++)
        p.feed("y\r\n");
    TL_CHECK(p.term.viewport_top() > top);
    TL_CHECK(p.term.viewport_top() == p.term.grid().base());

    /* The alternate screen has no viewport of its own. */
    T a(5, 3, 100);
    for (int i = 0; i < 6; i++)
        a.feed("x\r\n");
    a.feed("\x1b[?1049h");
    a.term.scroll_viewport(-2);
    TL_CHECK(a.term.view().at_bottom());

    /* The viewport survives a reflow on the same text. */
    T r(10, 3, 100);
    r.feed("aaaaaaaaaa\r\nbbbbbbbbbb\r\ncc\r\ndd\r\nee\r\nff");
    r.term.scroll_viewport(-2);
    TL_CHECK_STR(r.vrow(0), "bbbbbbbbbb");
    r.term.resize(5, 3);
    TL_CHECK_STR(r.vrow(0), "bbbbb");
    TL_CHECK_STR(r.vrow(1), "bbbbb");
}

void anchors()
{
    T t(10, 4, 100);
    t.feed("one\r\ntwo\r\nthree");
    Grid& g = t.term.grid();
    int a = g.anchor_new(g.active_top() + 1, 1);     /* the 'w' of two */
    for (int i = 0; i < 5; i++)
        t.feed("\r\nmore");
    const Anchor* an = g.anchor(a);
    TL_CHECK(an && !an->lost);
    TL_CHECK(g.abs_row(an->row).cells[an->col].cp() == 'w');

    /* Region moves carry anchors; deleted rows lose them. */
    T r(10, 5, 100);
    r.feed("a\r\nb\r\nc\r\nd");
    Grid& rg = r.term.grid();
    int ac = rg.anchor_new(rg.active_top() + 2, 0);  /* 'c' */
    int ab = rg.anchor_new(rg.active_top() + 1, 0);  /* 'b' */
    r.feed("\x1b[2;1H\x1b[M");                       /* delete the 'b' row */
    TL_CHECK(rg.anchor(ab)->lost);
    TL_CHECK(!rg.anchor(ac)->lost);
    TL_CHECK(rg.abs_row(rg.anchor(ac)->row).cells[0].cp() == 'c');
    r.feed("\x1b[1;1H\x1b[2L");
    TL_CHECK(rg.abs_row(rg.anchor(ac)->row).cells[0].cp() == 'c');
    rg.anchor_free(ac);
    TL_CHECK(rg.anchor(ac) == 0);

    /* Reflow keeps an anchor on its character. */
    T f(10, 3, 100);
    f.feed("0123456789abcdef");
    Grid& fg = f.term.grid();
    int ae = fg.anchor_new(fg.active_top() + 1, 4);  /* 'e' */
    f.term.resize(4, 3);
    const Anchor* e = f.term.grid().anchor(ae);
    TL_CHECK(e && !e->lost);
    TL_CHECK(f.term.grid().abs_row(e->row).cells[e->col].cp() == 'e');
    f.term.resize(20, 3);
    e = f.term.grid().anchor(ae);
    TL_CHECK(e->col == 14);
    TL_CHECK(f.term.grid().abs_row(e->row).cells[e->col].cp() == 'e');

    /* Pruned from the history: lost. */
    T p(5, 2, 2);
    Grid& pg = p.term.grid();
    int ap = pg.anchor_new(pg.active_top(), 0);
    for (int i = 0; i < 6; i++)
        p.feed("x\r\n");
    TL_CHECK(pg.anchor(ap)->lost);
}

void text_extraction()
{
    T t(5, 5, 100);
    t.feed("abcdefg\r\nhi  \r\n\r\nxyz");
    Grid& g = t.term.grid();
    uint64_t top = g.active_top();
    Buffer b;
    t.term.text(top, 0, top + 4, 4, b);
    TL_CHECK_STR(b.c_str(), "abcdefg\nhi\n\nxyz");
    b.clear();
    t.term.text(top, 2, top + 2, 1, b);               /* across the soft wrap */
    TL_CHECK_STR(b.c_str(), "cdefg\nhi");
    b.clear();
    t.term.text(top + 2, 1, top, 3, b);               /* reversed ends */
    TL_CHECK_STR(b.c_str(), "defg\nhi");
    b.clear();
    T w(6, 2);
    w.feed("a\xe4\xb8\xad" "b");
    w.term.text(w.term.grid().active_top(), 0, w.term.grid().active_top(), 5, b);
    TL_CHECK_STR(b.c_str(), "a\xe4\xb8\xad" "b");
}

void selection()
{
    T t(20, 4, 100);
    t.feed("hello world foo\r\nsecond line here");
    Grid& g = t.term.grid();
    uint64_t top = g.active_top();
    Buffer b;
    TL_CHECK(!t.term.has_selection());
    t.term.select_start(top, 2, SELECT_CHAR);
    t.term.select_extend(top, 8);
    TL_CHECK(t.term.selection_text(b));
    TL_CHECK_STR(b.c_str(), "llo wor");
    View v = t.term.view();
    TL_CHECK(v.selected(2, 0) && v.selected(8, 0) && !v.selected(9, 0) && !v.selected(1, 0));
    /* Backwards and across lines. */
    t.term.select_start(top + 1, 3, SELECT_CHAR);
    t.term.select_extend(top, 12);
    b.clear();
    t.term.selection_text(b);
    TL_CHECK_STR(b.c_str(), "foo\nseco");
    /* Words: double click on "world", drag to "line". */
    t.term.select_start(top, 7, SELECT_WORD);
    b.clear();
    t.term.selection_text(b);
    TL_CHECK_STR(b.c_str(), "world");
    t.term.select_extend(top + 1, 8);
    b.clear();
    t.term.selection_text(b);
    TL_CHECK_STR(b.c_str(), "world foo\nsecond line");
    /* A run of spaces is a word of its own class; punctuation too. */
    T p(30, 2);
    p.feed("a   b /usr/local/bin:x !!! z");
    uint64_t pt = p.term.grid().active_top();
    p.term.select_start(pt, 2, SELECT_WORD);
    b.clear();
    p.term.selection_text(b);
    TL_CHECK_STR(b.c_str(), "");                           /* blanks trim to nothing */
    p.term.select_start(pt, 10, SELECT_WORD);
    b.clear();
    p.term.selection_text(b);
    TL_CHECK_STR(b.c_str(), "/usr/local/bin:x");
    p.term.select_start(pt, 24, SELECT_WORD);
    b.clear();
    p.term.selection_text(b);
    TL_CHECK_STR(b.c_str(), "!!!");
    /* Lines, across soft wraps. */
    T l(10, 4, 100);
    l.feed("0123456789abcdef\r\nnext");
    uint64_t lt = l.term.grid().active_top();
    l.term.select_start(lt + 1, 2, SELECT_LINE);
    b.clear();
    l.term.selection_text(b);
    TL_CHECK_STR(b.c_str(), "0123456789abcdef");
    /* A word across the soft wrap. */
    l.term.select_start(lt, 8, SELECT_WORD);
    b.clear();
    l.term.selection_text(b);
    TL_CHECK_STR(b.c_str(), "0123456789abcdef");
    /* The selection stays on its text while output scrolls... */
    t.term.select_start(top, 6, SELECT_WORD);
    for (int i = 0; i < 6; i++)
        t.feed("\r\nmore");
    b.clear();
    TL_CHECK(t.term.selection_text(b));
    TL_CHECK_STR(b.c_str(), "world");
    /* ...and through a reflow. */
    t.term.resize(8, 4);
    b.clear();
    TL_CHECK(t.term.selection_text(b));
    TL_CHECK_STR(b.c_str(), "world");
    /* Gone with its text, and when the screen switches. */
    T s(10, 2, 0);
    s.feed("abc");
    s.term.select_start(s.term.grid().active_top(), 0, SELECT_WORD);
    s.feed("\r\n1\r\n2\r\n3");
    TL_CHECK(!s.term.has_selection());
    T a(10, 2, 10);
    a.feed("abc");
    a.term.select_start(a.term.grid().active_top(), 0, SELECT_WORD);
    a.feed("\x1b[?1049h");
    TL_CHECK(!a.term.has_selection());
    /* Writing into the selected text drops the selection; writing
     * elsewhere does not. */
    T o(20, 4);
    o.feed("one two\r\nthree");
    uint64_t ot = o.term.grid().active_top();
    o.term.select_start(ot, 4, SELECT_WORD);
    o.feed("\x1b[3;1Hx");
    TL_CHECK(o.term.has_selection());
    o.feed("\x1b[1;10Hy");
    TL_CHECK(!o.term.has_selection());
    o.term.select_start(ot, 4, SELECT_WORD);
    o.feed("\x1b[2J");
    TL_CHECK(!o.term.has_selection());
    /* Select all. */
    T al(10, 3, 10);
    al.feed("x\r\ny");
    al.term.select_all();
    b.clear();
    al.term.selection_text(b);
    TL_CHECK_STR(b.c_str(), "x\ny\n");
    /* Changing the selection damages its rows. */
    T d(10, 4);
    d.feed("aa\r\nbb\r\ncc");
    d.term.clear_damage();
    d.term.select_start(d.term.grid().active_top() + 1, 0, SELECT_CHAR);
    d.term.select_extend(d.term.grid().active_top() + 2, 1);
    View dv = d.term.view();
    int lo, hi;
    TL_CHECK(!dv.row_dirty(0, &lo, &hi) || (lo == hi));   /* only a cursor cell, if any */
    TL_CHECK(dv.row_dirty(1, &lo, &hi) && lo == 0 && hi == 9);
    TL_CHECK(dv.row_dirty(2, &lo, &hi) && lo == 0 && hi == 9);
    d.term.clear_damage();
    d.term.select_clear();
    dv = d.term.view();
    TL_CHECK(dv.row_dirty(1, &lo, &hi) && lo == 0 && hi == 9);
}

void colors()
{
    T t(10, 2);
    Rgb fg = { 10, 20, 30 }, bg = { 200, 210, 220 };
    t.term.set_default_colors(fg, bg);
    t.feed("a\x1b[31mb\x1b[1mc\x1b[0;7md\x1b[0;38;2;1;2;3;48;5;21me\x1b[0;2mf\x1b[0;8mg");
    View v = t.term.view();
    ResolvedColors c = v.colors(v.cell(0, 0));
    TL_CHECK(c.fg.r == 10 && c.bg.r == 200 && c.bg_default);
    c = v.colors(v.cell(1, 0));
    TL_CHECK(c.fg.r == 0xcd && c.fg.g == 0);
    c = v.colors(v.cell(2, 0));                      /* bold red is bright red */
    TL_CHECK(c.fg.r == 0xff && c.fg.g == 0);
    c = v.colors(v.cell(3, 0));                      /* inverse */
    TL_CHECK(c.fg.r == 200 && c.bg.r == 10 && !c.bg_default);
    c = v.colors(v.cell(4, 0));
    TL_CHECK(c.fg.r == 1 && c.fg.g == 2 && c.fg.b == 3);
    TL_CHECK(c.bg.r == 0 && c.bg.g == 0 && c.bg.b == 255);   /* cube 21 */
    TL_CHECK(c.ul.r == 1);
    c = v.colors(v.cell(5, 0));                      /* faint */
    TL_CHECK(c.fg.r == 105);
    c = v.colors(v.cell(6, 0));                      /* invisible */
    TL_CHECK(c.fg.r == c.bg.r);
    t.term.set_bold_is_bright(false);
    c = t.term.view().colors(v.cell(2, 0));
    TL_CHECK(c.fg.r == 0xcd);
    /* Reverse video swaps the defaults. */
    t.feed("\x1b[?5h");
    c = t.term.view().colors(v.cell(0, 0));
    TL_CHECK(c.fg.r == 200 && c.bg.r == 10 && !c.bg_default);
    /* Grey ramp. */
    TL_CHECK(t.term.palette(232).r == 8 && t.term.palette(255).r == 238);
}

void cursor_info()
{
    T t(6, 2);
    t.feed("\xe4\xb8\xad\x1b[1;1H");
    ViewCursor c = t.term.view().cursor();
    TL_CHECK(c.wide && c.x == 0 && c.y == 0);
    t.feed("\x1b[4 q");
    TL_CHECK(t.term.view().cursor().shape == CURSOR_UNDERLINE);
}

/* ---- damage model check -------------------------------------------------
 * A simulated host keeps a shadow of what it drew and updates it only as
 * the View says (scroll records, then dirty cells). After every frame the
 * shadow must equal a full repaint, or the damage under-reported. */

struct ShadowCell {
    uint32_t cp;
    Style style;
    bool cursor;
    bool sel;
};

struct Shadow {
    ShadowCell* cells;
    int cols, rows;
    Shadow() : cells(0), cols(0), rows(0) {}
    ~Shadow() { delete[] cells; }
    void size(int c, int r)
    {
        delete[] cells;
        cells = new ShadowCell[c * r];
        cols = c;
        rows = r;
    }
    ShadowCell& at(int x, int y) { return cells[y * cols + x]; }
};

static ShadowCell render(const View& v, int x, int y)
{
    ShadowCell s;
    const Cell& c = v.cell(x, y);
    uint32_t cps[TL_GRAPHEME_MAX];
    int n = v.text(c, cps, TL_GRAPHEME_MAX);
    s.cp = n ? cps[0] + (uint32_t)n * 0x01000000u : 0;
    s.style = v.style(c);
    ViewCursor vc = v.cursor();
    s.cursor = vc.visible && vc.y == y && (vc.x == x || (vc.wide && vc.x + 1 == x));
    s.sel = v.selected(x, y);
    return s;
}

static bool same(const ShadowCell& a, const ShadowCell& b)
{
    return a.cp == b.cp && a.style == b.style && a.cursor == b.cursor && a.sel == b.sel;
}

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed) {}
    uint32_t next() { s = s * 1103515245u + 12345u; return (s >> 8) & 0xffffff; }
    int below(int n) { return (int)(next() % (uint32_t)n); }
};

static void random_chunk(Rng& r, Buffer& b, int cols, int rows)
{
    static const char* const fixed[] = {
        "\r\n", "\n", "\r", "\b", "\t", "\x1bM", "\x1b" "D", "\x1b" "E",
        "\x1b[K", "\x1b[1K", "\x1b[2K", "\x1b[J", "\x1b[1J", "\x1b[2J",
        "\x1b[L", "\x1b[2L", "\x1b[M", "\x1b[3M", "\x1b[S", "\x1b[2T",
        "\x1b[@", "\x1b[3P", "\x1b[2X", "\x1b[r", "\x1b[?1049h", "\x1b[?1049l",
        "\x1b[?25l", "\x1b[?25h", "\x1b[7m", "\x1b[0m", "\x1b[1;31m", "\x1b[44m",
        "\x1b[4h", "\x1b[4l", "\x1b[?7l", "\x1b[?7h", "\x1b#8", "\x1b[?5h", "\x1b[?5l",
        "\xe4\xb8\xad", "e\xcc\x81", "\x1b#6", "\x1b[2b", "\x1b[?6h", "\x1b[?6l"
    };
    int k = r.below(10);
    char tmp[64];
    if (k < 4) {
        int n = 1 + r.below(12);
        for (int i = 0; i < n; i++)
            b.append_byte((uint8_t)('a' + r.below(26)));
    } else if (k < 6) {
        /* One draw per statement: argument order is unspecified. */
        int y = 1 + r.below(rows + 1);
        int x = 1 + r.below(cols + 1);
        sprintf(tmp, "\x1b[%d;%dH", y, x);
        b.append(tmp);
    } else if (k < 7) {
        int t = 1 + r.below(rows);
        int bt = t + r.below(rows - t + 1);
        sprintf(tmp, "\x1b[%d;%dr", t, bt);
        b.append(tmp);
    } else {
        b.append(fixed[r.below((int)(sizeof(fixed) / sizeof(fixed[0])))]);
    }
}

void damage_model()
{
    int failures = 0;
    const char* env = getenv("TL_DAMAGE_SEEDS");
    uint32_t seeds = env ? (uint32_t)atoi(env) : 60;
    for (uint32_t seed = 1; seed <= seeds && failures == 0; seed++) {
        Rng r(seed);
        int cols = 4 + r.below(12);
        int rows = 3 + r.below(6);
        Terminal term;
        term.init(cols, rows, (size_t)r.below(3) * 5);
        Shadow sh;
        sh.size(cols, rows);
        for (int frame = 0; frame < 150 && failures == 0; frame++) {
            int chunks = 1 + r.below(4);
            Buffer b;
            for (int i = 0; i < chunks; i++)
                random_chunk(r, b, cols, rows);
            term.has_selection();
            if (getenv("TL_STEPS")) fprintf(stderr, "seed %u frame %d feed %s\n", (unsigned)seed, frame, b.c_str());
            term.feed(b.data(), b.size());
            term.has_selection();
            int act = r.below(40);
            if (getenv("TL_STEPS")) fprintf(stderr, "  act %d\n", act);
            bool trace = getenv("TL_DAMAGE_TRACE") && (uint32_t)atoi(getenv("TL_DAMAGE_TRACE")) == seed;
            if (trace) {
                fprintf(stderr, "frame %d: feed \"", frame);
                for (size_t i = 0; i < b.size(); i++) {
                    uint8_t ch = b.data()[i];
                    if (ch >= 0x20 && ch < 0x7f) fputc(ch, stderr);
                    else fprintf(stderr, "\\x%02x", ch);
                }
                fprintf(stderr, "\" act %d\n", act);
            }
            if (act == 0) {
                cols = 4 + r.below(12);
                rows = 3 + r.below(6);
                term.resize(cols, rows);
                if (trace)
                    fprintf(stderr, "  resize %dx%d\n", cols, rows);
            } else if (act == 1) {
                term.scroll_viewport(-(long)r.below(4));
            } else if (act == 2) {
                term.scroll_to_bottom();
            } else if (act >= 3 && act <= 6) {
                /* Selections in the viewport. */
                int sy = r.below(rows);
                int sx = r.below(cols);
                uint64_t row = term.viewport_top() + (uint64_t)sy;
                if (row < term.grid().base() + term.grid().total_rows()) {
                    if (act == 3)
                        term.select_start(row, sx, (SelectMode)r.below(3));
                    else if (act == 6)
                        term.select_clear();
                    else
                        term.select_extend(row, sx);
                }
            }

            term.has_selection();
            View v = term.view();
            if (v.held())
                continue;
            if (v.cols() != sh.cols || v.rows() != sh.rows) {
                TL_CHECK(v.full_damage());
                sh.size(v.cols(), v.rows());
            }
            if (trace) {
                uint64_t q0, q1;
                int k0, k1;
                if (term.selection_range(&q0, &k0, &q1, &k1))
                    fprintf(stderr, "  selection %lu,%d - %lu,%d (active top %lu)\n", (unsigned long)q0, k0,
                            (unsigned long)q1, k1, (unsigned long)term.grid().active_top());
                fprintf(stderr, "  full %d records %d top %lu bottom %d cursor %d,%d vis %d\n",
                        v.full_damage(), v.scroll_record_count(), (unsigned long)v.top_row(),
                        v.at_bottom(), v.cursor().x, v.cursor().y, v.cursor().visible);
                for (int i = 0; i < v.scroll_record_count(); i++)
                    fprintf(stderr, "  record %d-%d by %d\n", v.scroll_record(i).top,
                            v.scroll_record(i).bottom, v.scroll_record(i).count);
                for (int y = 0; y < v.rows(); y++) {
                    int lo, hi;
                    if (v.row_dirty(y, &lo, &hi))
                        fprintf(stderr, "  dirty %d: %d-%d\n", y, lo, hi);
                }
            }
            if (v.full_damage()) {
                for (int y = 0; y < sh.rows; y++)
                    for (int x = 0; x < sh.cols; x++)
                        sh.at(x, y) = render(v, x, y);
            } else {
                for (int i = 0; i < v.scroll_record_count(); i++) {
                    const ScrollRecord& sr = v.scroll_record(i);
                    if (sr.count > 0) {
                        for (int y = sr.top; y + sr.count <= sr.bottom; y++)
                            for (int x = 0; x < sh.cols; x++)
                                sh.at(x, y) = sh.at(x, y + sr.count);
                    } else {
                        for (int y = sr.bottom; y + sr.count >= sr.top; y--)
                            for (int x = 0; x < sh.cols; x++)
                                sh.at(x, y) = sh.at(x, y + sr.count);
                    }
                }
                for (int y = 0; y < sh.rows; y++) {
                    int lo, hi;
                    if (!v.row_dirty(y, &lo, &hi))
                        continue;
                    for (int x = lo; x <= hi; x++)
                        sh.at(x, y) = render(v, x, y);
                }
            }
            for (int y = 0; y < sh.rows && !failures; y++) {
                for (int x = 0; x < sh.cols; x++) {
                    if (!same(sh.at(x, y), render(v, x, y))) {
                        char m[160];
                        sprintf(m, "damage model: seed %u frame %d cell %d,%d stale after: %s",
                                (unsigned)seed, frame, x, y, b.c_str());
                        tl_test_fail(__FILE__, __LINE__, m);
                        const Row& fr = v.row(y);
                        fprintf(stderr, "  row %d: cols %d dirty %d-%d flags %x; grid %dx%d history %lu/%lu alt %d\n",
                                y, fr.cols, fr.dirty_lo, fr.dirty_hi, fr.flags, term.grid().cols(),
                                term.grid().rows(), (unsigned long)term.grid().history(),
                                (unsigned long)term.grid().max_history(), term.alt_screen());
                        failures++;
                        break;
                    }
                }
            }
            tl_test_checks++;
            term.clear_damage();
        }
    }
}

} /* namespace */

void tl_test_view()
{
    damage();
    viewport();
    anchors();
    text_extraction();
    selection();
    colors();
    cursor_info();
    damage_model();
}
