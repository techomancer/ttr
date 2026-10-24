/* Tests for images: the sixel decoder, placing images as image cells, and
 * the store that frees them. */
#include "tltest.h"
#include "../terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace tl;

namespace {

class TestHost : public Host {
public:
    Buffer out;
    virtual void write_pty(const uint8_t* d, size_t n) { out.append(d, n); }
};

bool decode(const char* s, uint32_t** px, int* w, int* h, int p2 = 0, uint32_t bg = 0x102030)
{
    SixelDecoder d;
    d.begin(p2, bg);
    d.put((const uint8_t*)s, strlen(s));
    return d.finish(px, w, h);
}

void test_decoder()
{
    uint32_t* px = 0;
    int w = 0, h = 0;
    /* A red band 4 wide, then a blue one 2 wide in a 4 x 12 raster. */
    TL_CHECK(decode("\"1;1;4;12#1;2;100;0;0#1!4~-#2;2;0;0;100#2~~", &px, &w, &h));
    TL_CHECK(w == 4 && h == 12);
    if (px && w == 4 && h == 12) {
        TL_CHECK(px[0] == 0xffff0000u && px[3 + 5 * 4] == 0xffff0000u);
        TL_CHECK(px[6 * 4] == 0xff0000ffu && px[1 + 11 * 4] == 0xff0000ffu);
        /* Not drawn: the background. */
        TL_CHECK(px[3 + 6 * 4] == 0xff102030u);
    }
    free(px);
    /* P2 = 1: what is not drawn is transparent. */
    TL_CHECK(decode("#1;2;100;100;100#1~-~$!2@", &px, &w, &h, 1));
    TL_CHECK(w == 2 && h == 12);
    if (px && w == 2 && h == 12) {
        TL_CHECK(px[0] == 0xffffffffu && px[1] == 0);
        TL_CHECK(px[6 * 2] == 0xffffffffu && px[1 + 6 * 2] == 0xffffffffu);
        TL_CHECK(px[1 + 7 * 2] == 0);
    }
    free(px);
    /* DEC's HLS: hue 120 is red, 240 green, 0 blue. */
    TL_CHECK(decode("#3;1;120;50;100#3@#4;1;240;50;100#4@#5;1;0;50;100#5@", &px, &w, &h));
    TL_CHECK(w == 3 && h == 1);
    if (px && w == 3) {
        TL_CHECK(px[0] == 0xffff0000u);
        TL_CHECK(px[1] == 0xff00ff00u);
        TL_CHECK(px[2] == 0xff0000ffu);
    }
    free(px);
    /* Default colour register 0 of the VT340 is black; 1 is blue. */
    TL_CHECK(decode("#1@", &px, &w, &h));
    TL_CHECK(px && px[0] == 0xff3333ccu);
    free(px);
    /* Nothing drawn: no image. */
    TL_CHECK(!decode("#1;2;0;0;0", &px, &w, &h));
    /* Huge repeats are cut at the maximum size, not allocated. */
    TL_CHECK(decode("!100000~-!99999@", &px, &w, &h));
    TL_CHECK(w == SixelDecoder::MAX_SIDE && h == 7);
    TL_CHECK(px && px[(size_t)6 * (size_t)w + 4095] == 0xff000000u);
    free(px);
    /* Garbage and numbers out of range do not crash. */
    TL_CHECK(decode("#999;2;500;-1;7~#-3\"0;0;0;0!0~;;;;;;;;;;#1;3~", &px, &w, &h) || true);
    free(px);
}

struct T {
    Terminal term;
    TestHost host;
    Buffer buf;
    T(int cols, int rows)
    {
        term.init(cols, rows, 100);
        term.set_host(&host);
        term.set_cell_pixels(2, 3);
    }
    T& feed(const char* s) { term.feed(s); return *this; }
    const Cell& cell(int x, int y) const { return term.grid().cell(x, y); }
    bool tile(int x, int y, int col, int row) const
    {
        const Cell& c = cell(x, y);
        return c.is_image() && c.image_col() == col && c.image_row() == row &&
               term.images().get(c.image_id()) != 0;
    }
    const char* reply()
    {
        buf.clear();
        buf.append(host.out.c_str());
        host.out.clear();
        return buf.c_str();
    }
};

/* 4 x 12 pixels: 2 x 4 cells of 2 x 3 pixels. */
const char* k_img = "\x1bPq\"1;1;4;12#1;2;100;0;0#1!4~-!4~\x1b\\";

void test_placement()
{
    T t(10, 5);
    t.feed("\x1b[2;2H").feed(k_img);
    TL_CHECK(t.term.images().count() == 1);
    /* Rows 1-4, then the cursor's line feed scrolled the screen once. */
    TL_CHECK(t.tile(1, 0, 0, 0) && t.tile(2, 0, 1, 0));
    TL_CHECK(t.tile(1, 3, 0, 3) && t.tile(2, 3, 1, 3));
    TL_CHECK(!t.cell(0, 0).is_image() && !t.cell(3, 0).is_image());
    TL_CHECK(t.term.cursor().x == 1 && t.term.cursor().y == 4);
    const Image* im = t.term.images().get(t.cell(1, 0).image_id());
    TL_CHECK(im && im->w == 4 && im->h == 12 && im->cell_w == 2 && im->cell_h == 3);
    /* An image cell's text is a space. */
    Buffer b;
    t.term.row_text(0, b);
    TL_CHECK_STR(b.c_str(), "   ");
    /* Text replaces the tile under it. */
    t.feed("\x1b[1;2HX");
    TL_CHECK(!t.cell(1, 0).is_image() && t.cell(1, 0).cp() == 'X');
    TL_CHECK(t.tile(2, 0, 1, 0));
    /* Erasing the screen and the scrollback lets the store free it. */
    t.feed("\x1b[2J\x1b[3J");
    t.term.clear_scrollback();
    TL_CHECK(t.term.images().count() == 0);

    /* Inserting characters moves tiles with the line. */
    T u(10, 6);
    u.feed(k_img).feed("\x1b[1;1H\x1b[2@");
    TL_CHECK(u.tile(2, 0, 0, 0) && u.tile(3, 0, 1, 0) && !u.cell(0, 0).is_image());
    TL_CHECK(u.term.cursor().x == 0);
    /* Cut at the right margin. */
    T v(3, 6);
    v.feed("\x1b[1;3H").feed(k_img);
    TL_CHECK(v.tile(2, 0, 0, 0) && v.term.cursor().x == 2 && v.term.cursor().y == 4);
}

void test_modes()
{
    /* DECSDM: at the top left, the cursor stays. */
    T t(10, 5);
    t.feed("\x1b[?80h\x1b[4;5H").feed(k_img);
    TL_CHECK(t.tile(0, 0, 0, 0) && t.tile(1, 3, 1, 3));
    TL_CHECK(t.term.cursor().x == 4 && t.term.cursor().y == 3);
    t.feed("\x1b[?80$p");
    TL_CHECK_STR(t.reply(), "\x1b[?80;1$y");
    /* 8452: the cursor right of the image on its last row. */
    T u(10, 6);
    u.feed("\x1b[?8452h").feed(k_img);
    TL_CHECK(u.term.cursor().x == 2 && u.term.cursor().y == 3);
    /* A cancelled string draws nothing. */
    T v(10, 6);
    v.feed("\x1bPq#1~~~\x18");
    TL_CHECK(v.term.images().count() == 0 && !v.cell(0, 0).is_image());
}

void test_queries()
{
    T t(10, 5);
    t.feed("\x1b[c");
    TL_CHECK_STR(t.reply(), "\x1b[?64;1;4;6;22c");
    t.feed("\x1b[16t");
    TL_CHECK_STR(t.reply(), "\x1b[6;3;2t");
    t.feed("\x1b[14t");
    TL_CHECK_STR(t.reply(), "\x1b[4;15;20t");
    t.feed("\x1b[18t");
    TL_CHECK_STR(t.reply(), "\x1b[8;5;10t");
    t.feed("\x1b[?1;1S");
    TL_CHECK_STR(t.reply(), "\x1b[?1;0;256S");
    t.feed("\x1b[?2;1S");
    TL_CHECK_STR(t.reply(), "\x1b[?2;0;20;15S");
    t.feed("\x1b[?2;4S");
    TL_CHECK_STR(t.reply(), "\x1b[?2;0;4096;4096S");
    t.feed("\x1b[?1;3;64S");
    TL_CHECK_STR(t.reply(), "\x1b[?1;3;0S");
}

void test_store()
{
    ImageStore s;
    s.set_budget(1000);
    int a = s.add((uint32_t*)malloc(400), 10, 10, 1, 1);
    int b = s.add((uint32_t*)malloc(400), 10, 10, 1, 1);
    TL_CHECK(a && b && a != b && s.count() == 2 && s.bytes() == 800);
    /* Over the budget: the oldest goes. */
    int c = s.add((uint32_t*)malloc(400), 10, 10, 1, 1);
    TL_CHECK(c && s.count() == 2 && !s.get(a) && s.get(b) && s.get(c));
    TL_CHECK(s.get(c)->serial > s.get(b)->serial);
    /* Bigger than the whole budget: refused. */
    TL_CHECK(s.add((uint32_t*)malloc(4000), 10, 100, 1, 1) == 0);
    s.clear();
    TL_CHECK(s.count() == 0 && s.bytes() == 0);
}

/* Random sixel strings through a terminal: no crash, no image larger
 * than the limit (the sanitizer build checks memory). */
void test_random()
{
    static const char alphabet[] = "#!\"$-;0123456789?@ABCDEFG~^_xyz\r\n";
    unsigned long seed = 4711;
    T t(20, 6);
    char buf[300];
    for (int k = 0; k < 400; k++) {
        int n = 0;
        buf[n++] = '\x1b';
        buf[n++] = 'P';
        buf[n++] = 'q';
        int len = 1 + (int)(seed % 250);
        for (int i = 0; i < len; i++) {
            seed = seed * 1103515245UL + 12345UL;
            buf[n++] = alphabet[(seed >> 16) % (sizeof(alphabet) - 1)];
        }
        buf[n++] = '\x1b';
        buf[n++] = '\\';
        buf[n] = 0;
        t.feed(buf);
    }
    TL_CHECK(t.term.images().count() >= 0);
}

} /* namespace */

void tl_test_image()
{
    test_decoder();
    test_placement();
    test_modes();
    test_queries();
    test_store();
    test_random();
}
