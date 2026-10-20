/* tw tests that need no X display: BDF reading, glyph composition,
 * builtin glyphs. */
#include "../bdf.h"
#include "../fonts.h"
#include "../glyphs.h"

#include <stdio.h>
#include <string.h>

using namespace tw;

static int checks = 0, failures = 0;
#define CHECK(c)                                                                \
    do {                                                                        \
        checks++;                                                               \
        if (!(c)) {                                                             \
            failures++;                                                         \
            fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #c);       \
        }                                                                       \
    } while (0)

static const char* g_fonts = "third_party/misc-misc";

static bool pixel(const GlyphCache& c, const CachedGlyph* g, int x, int y)
{
    return (c.bits(g)[y * g->stride + x / 8] & (0x80 >> (x % 8))) != 0;
}

static int ink(const GlyphCache& c, const CachedGlyph* g)
{
    int n = 0;
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++)
            n += pixel(c, g, x, y);
    return n;
}

static void test_bdf()
{
    char path[600];
    sprintf(path, "%s/9x15.bdf", g_fonts);
    BdfFont f;
    CHECK(f.load(path));
    CHECK(f.count() > 4000);
    CHECK(f.ascent() == 12 && f.descent() == 3);
    CHECK(f.cell_width() == 9 && f.cell_height() == 15);
    CHECK(!f.bold());
    const BdfGlyph* a = f.find('A');
    CHECK(a && a->dwidth == 9);
    CHECK(f.has(0x3b1));                /* alpha */
    CHECK(f.has(0x2264));               /* less or equal */
    CHECK(f.has(0x301));                /* combining acute */
    CHECK(!f.has(0x4e2d));              /* no CJK in misc-fixed */
    sprintf(path, "%s/9x15B.bdf", g_fonts);
    BdfFont b;
    CHECK(b.load(path) && b.bold());
    CHECK(!f.load("/nonexistent.bdf"));
}

static void test_glyphs()
{
    Fonts fonts;
    fonts.set_cell(9, 15, 12);
    CHECK(fonts.add_fallback_dir(g_fonts));
    CHECK(fonts.has_fallback());
    GlyphCache c;
    c.configure(&fonts, 9, 15, 12);
    uint32_t cp = 0x3b1;
    CachedGlyph* alpha = c.get(FACE_REGULAR, &cp, 1, false);
    CHECK(alpha && alpha->found && alpha->w == 9 && alpha->h == 15);
    CHECK(ink(c, alpha) > 10);
    CHECK(c.get(FACE_REGULAR, &cp, 1, false) == alpha);         /* cached */
    /* Bold comes from 9x15B and has more ink. */
    CachedGlyph* balpha = c.get(FACE_BOLD, &cp, 1, false);
    CHECK(balpha && balpha->found && ink(c, balpha) > ink(c, alpha));
    /* A combining mark adds to its base. */
    uint32_t e[2] = { 'e', 0x301 };
    CachedGlyph* plain = c.get(FACE_REGULAR, e, 1, false);
    CachedGlyph* acute = c.get(FACE_REGULAR, e, 2, false);
    CHECK(plain && acute && plain != acute && ink(c, acute) > ink(c, plain));
    /* Nothing for CJK: the renderer draws a box. */
    uint32_t cjk = 0x4e2d;
    CachedGlyph* none = c.get(FACE_REGULAR, &cjk, 1, true);
    CHECK(none && !none->found);
    /* A smaller font centred in a larger cell. */
    Fonts big;
    big.set_cell(11, 22, 17);
    CHECK(big.add_fallback_dir(g_fonts));
    CHECK(big.fallback(FACE_REGULAR).cell_width() == 10);       /* 10x20 fits */
    GlyphCache bc;
    bc.configure(&big, 11, 22, 17);
    uint32_t bar = '|';
    CachedGlyph* g = bc.get(FACE_REGULAR, &bar, 1, false);
    CHECK(g && g->found && g->w == 11);
    /* Synthetic bold when the size has no bold file (10x20). */
    CachedGlyph* gb = bc.get(FACE_BOLD, &bar, 1, false);
    CHECK(gb && gb->found && ink(bc, gb) > ink(bc, g));
    /* Nothing fits a tiny cell. */
    Fonts tiny;
    tiny.set_cell(3, 5, 4);
    CHECK(!tiny.add_fallback_dir(g_fonts));
}

static void test_builtin()
{
    GlyphRect r[16];
    int shade;
    /* Horizontal line: one rectangle across the cell. */
    int n = builtin_rects(0x2500, 9, 15, r, 16, &shade);
    CHECK(n == 2 && shade == 0);
    int minx = 99, maxx = -1;
    for (int i = 0; i < n; i++) {
        if (r[i].x < minx) minx = r[i].x;
        if (r[i].x + r[i].w > maxx) maxx = r[i].x + r[i].w;
        CHECK(r[i].h == 1);
    }
    CHECK(minx == 0 && maxx == 9);
    /* A cross reaches all four edges. */
    n = builtin_rects(0x253c, 9, 15, r, 16, &shade);
    bool top = false, bottom = false, left = false, right = false;
    for (int i = 0; i < n; i++) {
        top |= r[i].y == 0;
        bottom |= r[i].y + r[i].h == 15;
        left |= r[i].x == 0;
        right |= r[i].x + r[i].w == 9;
    }
    CHECK(top && bottom && left && right);
    /* Shades and blocks. */
    CHECK(builtin_rects(0x2592, 9, 15, r, 16, &shade) == 0 && shade == 2);
    n = builtin_rects(0x2584, 9, 15, r, 16, &shade);            /* lower half */
    CHECK(n == 1 && r[0].y + r[0].h == 15 && r[0].h == 7);
    CHECK(builtin_has(0x2588) && !builtin_has(0x2571) && !builtin_has('A'));
}

int main(int argc, char** argv)
{
    if (argc > 1)
        g_fonts = argv[1];
    test_bdf();
    test_glyphs();
    test_builtin();
    printf("tw tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
