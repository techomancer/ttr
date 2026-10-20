#include "tltest.h"
#include "../terminal.h"

#include <stdio.h>
#include <string.h>

using namespace tl;

namespace {

class TestHost : public Host {
public:
    Buffer out;
    Buffer title;
    int bells;
    TestHost() : bells(0) {}
    virtual void write_pty(const uint8_t* d, size_t n) { out.append(d, n); }
    virtual void bell() { bells++; }
    virtual void title_changed(TitleKind, const char* t) { title.clear(); title.append(t); }
};

struct T {
    Terminal term;
    TestHost host;
    Buffer buf;

    T(int cols, int rows, size_t sb = 100)
    {
        term.init(cols, rows, sb);
        term.set_host(&host);
    }
    T& feed(const char* s) { term.feed(s); return *this; }
    const char* screen()
    {
        buf.clear();
        term.screen_text(buf);
        return buf.c_str();
    }
    const char* row(int y)
    {
        buf.clear();
        term.row_text(y, buf);
        return buf.c_str();
    }
    const char* reply()
    {
        buf.clear();
        buf.append(host.out.c_str());
        host.out.clear();
        return buf.c_str();
    }
    bool at(int x, int y) const { return term.cursor().x == x && term.cursor().y == y; }
};

#define CHECK_CURSOR(t, cx, cy)                                                 \
    do {                                                                        \
        tl_test_checks++;                                                       \
        if (!(t).at(cx, cy)) {                                                  \
            char m_[96];                                                        \
            sprintf(m_, "cursor at %d,%d, want %d,%d", (t).term.cursor().x,     \
                    (t).term.cursor().y, cx, cy);                               \
            tl_test_fail(__FILE__, __LINE__, m_);                               \
        }                                                                       \
    } while (0)

void text_and_wrap()
{
    T t(10, 3);
    t.feed("hello");
    TL_CHECK_STR(t.screen(), "hello");
    CHECK_CURSOR(t, 5, 0);

    /* Pending wrap: the cursor stays on the last column. */
    T w(5, 3);
    w.feed("abcde");
    CHECK_CURSOR(w, 4, 0);
    TL_CHECK(w.term.cursor().pending_wrap);
    w.feed("f");
    TL_CHECK_STR(w.screen(), "abcde\nf");
    CHECK_CURSOR(w, 1, 1);
    TL_CHECK((w.term.grid().row(0).flags & ROW_WRAPPED) != 0);
    TL_CHECK((w.term.grid().row(1).flags & ROW_WRAPPED) == 0);
    /* CR clears the pending wrap. */
    T c(5, 3);
    c.feed("abcde\rX");
    TL_CHECK_STR(c.screen(), "Xbcde");
    /* No autowrap: overwrite the last column. */
    T n(5, 3);
    n.feed("\x1b[?7labcdefg");
    TL_CHECK_STR(n.screen(), "abcdg");
    CHECK_CURSOR(n, 4, 0);

    /* Scrolling into history. */
    T s(5, 2, 10);
    s.feed("1\r\n2\r\n3\r\n4");
    TL_CHECK_STR(s.screen(), "3\n4");
    TL_CHECK(s.term.grid().history() == 2);
    /* No history for the alternate screen. */
    T a(5, 2, 10);
    a.feed("\x1b[?1049h1\r\n2\r\n3");
    TL_CHECK(a.term.grid().history() == 0);
    TL_CHECK_STR(a.screen(), "2\n3");
    a.feed("\x1b[?1049l");
    TL_CHECK_STR(a.screen(), "");
    /* History is capped. */
    T cap(5, 2, 3);
    for (int i = 0; i < 20; i++)
        cap.feed("x\r\n");
    TL_CHECK(cap.term.grid().history() == 3);
    TL_CHECK(cap.term.grid().base() == 16);    /* 21 rows written, 2 + 3 kept */
}

void cursor_motion()
{
    T t(10, 5);
    t.feed("\x1b[3;4H");
    CHECK_CURSOR(t, 3, 2);
    t.feed("\x1b[A");
    CHECK_CURSOR(t, 3, 1);
    t.feed("\x1b[10A");
    CHECK_CURSOR(t, 3, 0);
    t.feed("\x1b[2B\x1b[3C");
    CHECK_CURSOR(t, 6, 2);
    t.feed("\x1b[20C");
    CHECK_CURSOR(t, 9, 2);
    t.feed("\x1b[2D");
    CHECK_CURSOR(t, 7, 2);
    t.feed("\x1b[E");
    CHECK_CURSOR(t, 0, 3);
    t.feed("\x1b[5G");
    CHECK_CURSOR(t, 4, 3);
    t.feed("\x1b[2d");
    CHECK_CURSOR(t, 4, 1);
    t.feed("\x1b[H");
    CHECK_CURSOR(t, 0, 0);
    t.feed("\x1b[99;99H");
    CHECK_CURSOR(t, 9, 4);
    t.feed("\x1b[;3H");
    CHECK_CURSOR(t, 2, 0);
    t.feed("\b\b\b");
    CHECK_CURSOR(t, 0, 0);

    /* Tabs. */
    T tab(30, 2);
    tab.feed("\t");
    CHECK_CURSOR(tab, 8, 0);
    tab.feed("\t\t\t");
    CHECK_CURSOR(tab, 29, 0);
    tab.feed("\x1b[Z");
    CHECK_CURSOR(tab, 24, 0);
    tab.feed("\x1b[3g\r\t");
    CHECK_CURSOR(tab, 29, 0);
    tab.feed("\x1b[5G\x1bH\r\t");
    CHECK_CURSOR(tab, 4, 0);

    /* Save and restore. */
    T sc(10, 5);
    sc.feed("\x1b[2;3H\x1b" "7\x1b[5;5H\x1b" "8");
    CHECK_CURSOR(sc, 2, 1);
    sc.feed("\x1b[4;4H\x1b[s\x1b[H\x1b[u");
    CHECK_CURSOR(sc, 3, 3);

    /* Scroll region and origin mode. */
    T r(10, 6);
    r.feed("\x1b[2;4r");
    CHECK_CURSOR(r, 0, 0);
    TL_CHECK(r.term.scroll_top() == 1 && r.term.scroll_bottom() == 3);
    r.feed("\x1b[?6h");
    CHECK_CURSOR(r, 0, 1);
    r.feed("\x1b[9;1H");
    CHECK_CURSOR(r, 0, 3);
    r.feed("\x1b[6n");
    TL_CHECK_STR(r.reply(), "\x1b[3;1R");
    /* CUD stops at the bottom margin unless the cursor starts below it. */
    r.feed("\x1b[?6l\x1b[1;1H\x1b[10B");
    CHECK_CURSOR(r, 0, 3);
    r.feed("\x1b[5;1H\x1b[10B");
    CHECK_CURSOR(r, 0, 5);
    r.feed("\x1b[3;1H\x1b[10B");
    CHECK_CURSOR(r, 0, 3);
}

void editing()
{
    T t(10, 4);
    t.feed("abcdefghij\x1b[1;4H\x1b[2@");
    TL_CHECK_STR(t.row(0), "abc  defgh");
    t.feed("\x1b[3P");
    TL_CHECK_STR(t.row(0), "abcefgh");
    t.feed("\x1b[2X");
    TL_CHECK_STR(t.row(0), "abc  gh");
    t.feed("\x1b[K");
    TL_CHECK_STR(t.row(0), "abc");
    t.feed("\x1b[1K");
    TL_CHECK_STR(t.row(0), "");
    t.feed("\x1b[Hxyz\x1b[2K");
    TL_CHECK_STR(t.row(0), "");

    T ed(5, 3);
    ed.feed("aaaaa\r\nbbbbb\r\nccccc\x1b[2;3H\x1b[J");
    TL_CHECK_STR(ed.screen(), "aaaaa\nbb");
    ed.feed("\x1b[H\x1b[2J");
    TL_CHECK_STR(ed.screen(), "");
    ed.feed("aaaaa\r\nbbbbb\r\nccccc\x1b[2;3H\x1b[1J");
    TL_CHECK_STR(ed.screen(), "\n   bb\nccccc");

    /* Insert and delete lines. */
    T il(3, 4);
    il.feed("1\r\n2\r\n3\r\n4\x1b[2;1H\x1b[L");
    TL_CHECK_STR(il.screen(), "1\n\n2\n3");
    il.feed("\x1b[2M");
    TL_CHECK_STR(il.screen(), "1\n3");
    /* Not in the scrollback. */
    TL_CHECK(il.term.grid().history() == 0);
    /* Delete lines at the top of the full screen do not go to history. */
    il.feed("\x1b[H\x1b[M");
    TL_CHECK(il.term.grid().history() == 0);

    /* Region scroll. */
    T rs(3, 5);
    rs.feed("1\r\n2\r\n3\r\n4\r\n5\x1b[2;4r\x1b[4;1H\n");
    TL_CHECK_STR(rs.screen(), "1\n3\n4\n\n5");
    TL_CHECK(rs.term.grid().history() == 0);
    rs.feed("\x1b[2;1H\x1bM");
    TL_CHECK_STR(rs.screen(), "1\n\n3\n4\n5");
    rs.feed("\x1b[S");
    TL_CHECK_STR(rs.screen(), "1\n3\n4\n\n5");
    rs.feed("\x1b[2T");
    TL_CHECK_STR(rs.screen(), "1\n\n\n3\n5");

    /* Insert mode. */
    T im(6, 2);
    im.feed("abcd\r\x1b[4hXY\x1b[4l");
    TL_CHECK_STR(im.row(0), "XYabcd");

    /* REP */
    T rep(10, 2);
    rep.feed("a\x1b[3b");
    TL_CHECK_STR(rep.row(0), "aaaa");

    /* DECALN */
    T al(3, 2);
    al.feed("\x1b#8");
    TL_CHECK_STR(al.screen(), "EEE\nEEE");

    /* Linefeed/new line mode. */
    T lnm(5, 3);
    lnm.feed("\x1b[20hab\ncd");
    TL_CHECK_STR(lnm.screen(), "ab\ncd");
}

void wide_and_combining()
{
    T t(6, 3);
    t.feed("\xe4\xb8\xad" "a");                    /* U+4E2D */
    TL_CHECK_STR(t.row(0), "\xe4\xb8\xad" "a");
    CHECK_CURSOR(t, 3, 0);
    TL_CHECK(t.term.grid().row(0).cells[0].wide() == CELL_WIDE);
    TL_CHECK(t.term.grid().row(0).cells[1].wide() == CELL_SPACER_TAIL);
    /* Overwriting the tail blanks the head. */
    t.feed("\x1b[1;2Hx");
    TL_CHECK_STR(t.row(0), " xa");
    /* Wide character at the last column wraps, leaving a spacer head. */
    T w(5, 3);
    w.feed("abcd\xe4\xb8\xad");
    TL_CHECK_STR(w.screen(), "abcd\n\xe4\xb8\xad");
    TL_CHECK(w.term.grid().row(0).cells[4].wide() == CELL_SPACER_HEAD);
    CHECK_CURSOR(w, 2, 1);
    /* Combining marks join the previous cell. */
    T c(5, 2);
    c.feed("e\xcc\x81x");                          /* e + U+0301 */
    TL_CHECK_STR(c.row(0), "e\xcc\x81x");
    CHECK_CURSOR(c, 2, 0);
    TL_CHECK(c.term.grid().row(0).cells[0].is_grapheme());
    /* ...also at the right margin with a pending wrap. */
    T m(3, 2);
    m.feed("abc\xcc\x81");
    TL_CHECK_STR(m.row(0), "abc\xcc\x81");
    TL_CHECK(m.term.cursor().pending_wrap);
    /* Erasing half a wide character erases both. */
    T e(6, 2);
    e.feed("\xe4\xb8\xad\xe4\xb8\xad\x1b[1;2H\x1b[1X");
    TL_CHECK_STR(e.row(0), "  \xe4\xb8\xad");
    /* Legacy (wcwidth) clustering: man ZWJ woman is two cells of two. */
    T z(10, 2);
    z.feed("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9");
    CHECK_CURSOR(z, 4, 0);
    /* Mode 2027: one cluster. */
    T g(10, 2);
    g.feed("\x1b[?2027h\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9");
    CHECK_CURSOR(g, 2, 0);
    TL_CHECK_STR(g.row(0), "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9");
}

void charsets()
{
    T t(10, 2);
    t.feed("\x1b(0qx\x1b(Bq");
    TL_CHECK_STR(t.row(0), "\xe2\x94\x80\xe2\x94\x82q");
    T so(10, 2);
    so.feed("\x1b)0a\x0eq\x0fq");
    TL_CHECK_STR(so.row(0), "a\xe2\x94\x80q");
    T uk(10, 2);
    uk.feed("\x1b(A#");
    TL_CHECK_STR(uk.row(0), "\xc2\xa3");
    /* 8-bit mode: GR is Latin-1. */
    T l1(10, 2);
    l1.feed("\x1b%@\xe9\x1b%G\xc3\xa9");
    TL_CHECK_STR(l1.row(0), "\xc3\xa9\xc3\xa9");
}

void attributes()
{
    T t(10, 2);
    t.feed("\x1b[1;31mA\x1b[0mB\x1b[38;2;1;2;3;48;5;200mC\x1b[38:2::4:5:6;4:3mD");
    const Row& r = t.term.grid().row(0);
    const Style& a = t.term.style(r.cells[0]);
    TL_CHECK((a.attrs & ATTR_BOLD) != 0);
    TL_CHECK(a.fg == color_palette(1));
    TL_CHECK(r.cells[1].style == 0);
    const Style& c = t.term.style(r.cells[2]);
    TL_CHECK(c.fg == color_rgb(1, 2, 3));
    TL_CHECK(c.bg == color_palette(200));
    const Style& d = t.term.style(r.cells[3]);
    TL_CHECK(d.fg == color_rgb(4, 5, 6));
    TL_CHECK(d.underline() == UL_CURLY);
    TL_CHECK(d.bg == color_palette(200));
    /* Same style, same id. */
    t.feed("\x1b[0;1;31mE");
    TL_CHECK(r.cells[4].style == r.cells[0].style);
    /* Background colour erase. */
    t.feed("\x1b[0;44m\x1b[2;1H\x1b[K");
    const Style& bce = t.term.style(t.term.grid().row(1).cells[5]);
    TL_CHECK(bce.bg == color_palette(4));
    /* Bright colours. */
    t.feed("\x1b[0;97;101m\x1b[HF");
    const Style& br = t.term.style(r.cells[0]);
    TL_CHECK(br.fg == color_palette(15) && br.bg == color_palette(9));

    /* Many distinct styles: the table is swept when it fills. */
    T many(2, 2, 0);
    char b[64];
    for (int i = 0; i < 70000; i++) {
        sprintf(b, "\x1b[38;2;%d;%d;%dmx\r", i & 255, (i >> 8) & 255, (i >> 16) & 255);
        many.feed(b);
    }
    const Style& last = many.term.style(many.term.grid().row(0).cells[0]);
    TL_CHECK(last.fg == color_rgb(69999 & 255, (69999 >> 8) & 255, 69999 >> 16));
}

void reports_and_modes()
{
    T t(10, 5);
    t.feed("\x1b[c");
    TL_CHECK_STR(t.reply(), "\x1b[?64;1;6;22c");
    t.feed("\x1b[>c");
    TL_CHECK_STR(t.reply(), "\x1b[>41;1;0c");
    t.feed("\x1b[5n");
    TL_CHECK_STR(t.reply(), "\x1b[0n");
    t.feed("\x1b[2;3H\x1b[6n");
    TL_CHECK_STR(t.reply(), "\x1b[2;3R");
    t.feed("\x1b[?2004h\x1b[?2004$p");
    TL_CHECK_STR(t.reply(), "\x1b[?2004;1$y");
    t.feed("\x1b[?2026$p");
    TL_CHECK_STR(t.reply(), "\x1b[?2026;2$y");
    t.feed("\x1b[?9999$p");
    TL_CHECK_STR(t.reply(), "\x1b[?9999;0$y");
    t.feed("\x1b[4$p");
    TL_CHECK_STR(t.reply(), "\x1b[4;2$y");
    t.feed("\x1b[?25l");
    TL_CHECK(!t.term.modes().cursor_visible);
    t.feed("\x1b[5 q");
    TL_CHECK(t.term.cursor_shape() == CURSOR_BAR);
    t.feed("\x1b]2;hello\x07");
    TL_CHECK_STR(t.host.title.c_str(), "hello");
    TL_CHECK_STR(t.term.title(), "hello");
    t.feed("\x07");
    TL_CHECK(t.host.bells == 1);
    t.feed("\x1b" "c");
    TL_CHECK(t.term.modes().cursor_visible);
    TL_CHECK_STR(t.screen(), "");
}

void resizing()
{
    T t(10, 4, 100);
    t.feed("1\r\n2\r\n3\r\n4");
    t.term.resize(10, 2);
    TL_CHECK_STR(t.screen(), "3\n4");
    TL_CHECK(t.term.grid().history() == 2);
    CHECK_CURSOR(t, 1, 1);
    t.term.resize(10, 4);
    TL_CHECK_STR(t.screen(), "1\n2\n3\n4");
    CHECK_CURSOR(t, 1, 3);
    t.term.resize(4, 6);
    TL_CHECK_STR(t.screen(), "1\n2\n3\n4");
    t.feed("abcdefg");
    TL_CHECK_STR(t.row(3), "4abc");
    /* Cursor near the top: the blank rows below it go. */
    T b(5, 5);
    b.feed("x");
    b.term.resize(5, 2);
    TL_CHECK_STR(b.screen(), "x");
    CHECK_CURSOR(b, 1, 0);
    TL_CHECK(b.term.grid().history() == 0);
    /* Alternate screen keeps no history while shrinking. */
    T a(5, 4, 100);
    a.feed("\x1b[?1049h1\r\n2\r\n3\r\n4");
    a.term.resize(5, 2);
    TL_CHECK_STR(a.screen(), "3\n4");
    TL_CHECK(a.term.grid().history() == 0);
    a.term.resize(5, 5);
    TL_CHECK(a.term.rows() == 5);
}

} /* namespace */

void tl_test_terminal()
{
    text_and_wrap();
    cursor_motion();
    editing();
    wide_and_combining();
    charsets();
    attributes();
    reports_and_modes();
    resizing();
}
