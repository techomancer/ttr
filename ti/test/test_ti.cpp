/* Tests for ti: every fixture through every decoder the build has (the
 * system library's and the built-in one must agree), scaling, and the
 * terminal encodings. Run from the source tree's top; the fixtures come
 * from tools/make-ti-fixtures.sh. */
#include "../term.h"
#include "../ti.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks, g_failures;

#define CHECK(c) check((c), __FILE__, __LINE__, #c)

static void check(bool ok, const char* file, int line, const char* what)
{
    g_checks++;
    if (!ok) {
        g_failures++;
        printf("%s:%d: failed: %s\n", file, line, what);
    }
}

static unsigned char* read_file(const char* name, size_t* n)
{
    char path[300];
    sprintf(path, "ti/test/data/%s", name);
    FILE* f = fopen(path, "rb");
    if (!f)
        return 0;
    unsigned char* d = (unsigned char*)malloc(1 << 20);
    *n = d ? fread(d, 1, 1 << 20, f) : 0;
    fclose(f);
    return d;
}

/* The fixtures' patterns (tools/make-ti-fixtures.sh). */
enum Kind { K_RGB, K_RGBA, K_GREY, K_PAL, K_BLACK };

static void expected(Kind k, int x, int y, int* px)
{
    if (k == K_GREY) {
        px[0] = px[1] = px[2] = x * 30 + y * 12;
    } else if (k == K_BLACK) {
        px[0] = px[1] = px[2] = 0;
    } else {
        px[0] = x * 36;
        px[1] = y * 60;
        px[2] = 255 - x * 20 - y * 10;
    }
    px[3] = k == K_RGBA ? 255 - (x + y) * 20 : k == K_PAL && x == 0 && y == 0 ? 0 : 255;
}

struct Fixture {
    const char* file;
    ti::Format format;
    Kind kind;
    int tolerance;
};

static const Fixture k_fixtures[] = {
    { "rgb.png", ti::FMT_PNG, K_RGB, 0 },
    { "rgba.png", ti::FMT_PNG, K_RGBA, 0 },
    { "grey.png", ti::FMT_PNG, K_GREY, 0 },
    { "pal.png", ti::FMT_PNG, K_PAL, 8 },
    { "blank16.png", ti::FMT_PNG, K_BLACK, 0 },
    { "rgb_interlaced.png", ti::FMT_PNG, K_RGB, 0 },
    { "rgb.jpg", ti::FMT_JPEG, K_RGB, 16 },
    { "rgb_progressive.jpg", ti::FMT_JPEG, K_RGB, 16 },
    { "grey.jpg", ti::FMT_JPEG, K_GREY, 12 },
    { "cmyk.jpg", ti::FMT_JPEG, K_RGB, 24 },
    { "pal.gif", ti::FMT_GIF, K_PAL, 8 },
    { "pal_interlaced.gif", ti::FMT_GIF, K_PAL, 8 },
    { "rgb.tif", ti::FMT_TIFF, K_RGB, 0 },
    { "rgb_msb.tif", ti::FMT_TIFF, K_RGB, 0 },
    { "rgba_lzw.tif", ti::FMT_TIFF, K_RGBA, 4 },  /* premultiplied by libtiff */
    { "rgb.bmp", ti::FMT_BMP, K_RGB, 0 },
    { "rgb.ppm", ti::FMT_PNM, K_RGB, 0 },
    { "grey.pgm", ti::FMT_PNM, K_GREY, 0 },
    { "rgb.sgi", ti::FMT_SGI, K_RGB, 0 },
    { "rgba_rle.sgi", ti::FMT_SGI, K_RGBA, 0 },
};

static int g_decoded;

static bool matches(const ti::Image& im, Kind k, int tol, const char* what)
{
    if (im.w != 7 || im.h != 5) {
        printf("%s: %dx%d\n", what, im.w, im.h);
        return false;
    }
    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 7; x++) {
            int e[4];
            expected(k, x, y, e);
            const unsigned char* p = im.row(y) + x * 4;
            /* A transparent pixel's colour does not matter. */
            int n = e[3] == 0 ? 3 : 0;
            for (int c = n; c < 4; c++) {
                int d = (int)p[c] - e[c];
                if (d < -tol || d > tol) {
                    printf("%s: pixel %d,%d channel %d is %d, not %d\n", what, x, y, c, p[c], e[c]);
                    return false;
                }
            }
        }
    return true;
}

static void test_decoders()
{
    for (size_t i = 0; i < sizeof(k_fixtures) / sizeof(k_fixtures[0]); i++) {
        const Fixture& f = k_fixtures[i];
        size_t n = 0;
        unsigned char* d = read_file(f.file, &n);
        CHECK(d && n > 0);
        if (!d)
            continue;
        CHECK(ti::sniff(d, n) == f.format);
        ti::Image both[2];
        for (int b = 0; b < 2; b++) {
            ti::DecodeOptions o;
            o.backend = b ? ti::BACKEND_BUILTIN : ti::BACKEND_SYSTEM;
            if (!ti::decoder_name(f.format, o.backend))
                continue;
            char err[200], what[200];
            ti::Info info;
            bool ok = ti::decode(d, n, &both[b], &info, err, sizeof(err), o);
            sprintf(what, "%s with %s", f.file, ti::decoder_name(f.format, o.backend));
            if (!ok)
                printf("%s: %s\n", what, err);
            CHECK(ok);
            CHECK(info.format == f.format && info.width == 7 && info.height == 5);
            CHECK(info.decoder && strcmp(info.decoder, ti::decoder_name(f.format, o.backend)) == 0);
            CHECK(matches(both[b], f.kind, f.tolerance, what));
            g_decoded++;
        }
        /* The two decoders agree (JPEG within rounding). */
        if (!both[0].empty() && !both[1].empty()) {
            int worst = 0;
            for (size_t k = 0; k < 7 * 5 * 4; k++) {
                int dd = (int)both[0].rgba[k] - both[1].rgba[k];
                if (dd < 0)
                    dd = -dd;
                if (dd > worst && !(f.kind == K_PAL && k < 3))
                    worst = dd;
            }
            CHECK(worst <= (f.format == ti::FMT_JPEG ? 8 : 0));
        }
        free(d);
    }
    /* Every fixture was decoded at least once. */
    CHECK(g_decoded >= (int)(sizeof(k_fixtures) / sizeof(k_fixtures[0])));
}

static void test_damage()
{
    size_t n;
    unsigned char* d = read_file("short.png", &n);
    CHECK(d != 0);
    for (int b = 1; d && b < 3; b++) {
        ti::DecodeOptions o;
        o.backend = (ti::Backend)b;
        if (!ti::decoder_name(ti::FMT_PNG, o.backend))
            continue;
        ti::Image im;
        char err[200];
        CHECK(!ti::decode(d, n, &im, 0, err, sizeof(err), o));
        CHECK(err[0] && im.empty());
    }
    free(d);
    ti::Image im;
    char err[200];
    const unsigned char text[] = "hello, world\n";
    CHECK(ti::sniff(text, sizeof(text) - 1) == ti::FMT_UNKNOWN);
    CHECK(!ti::decode(text, sizeof(text) - 1, &im, 0, err, sizeof(err)));
    CHECK(strstr(err, "Not an image") != 0);
    d = read_file("wide.tif", &n);
    if (d && ti::decoder_name(ti::FMT_TIFF, ti::BACKEND_SYSTEM)) {
        CHECK(ti::decode(d, n, &im, 0, err, sizeof(err)));
        CHECK(im.w == 3000 && im.h == 2 && im.rgba[0] == 10 && im.rgba[5998 * 4 + 2] == 30);
    }
    free(d);
    /* A header claiming a huge SGI image. */
    unsigned char hdr[512];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 0x01;
    hdr[1] = 0xda;
    hdr[3] = 1;
    hdr[5] = 3;
    hdr[6] = hdr[8] = 0xff;
    hdr[11] = 3;
    CHECK(!ti::decode(hdr, sizeof(hdr), &im, 0, err, sizeof(err)));
    CHECK(strstr(err, "too big") != 0 || strstr(err, "cut short") != 0);
}

/* Every fixture cut at every length, and with bytes changed: decoders
 * may fail but never crash (the sanitizer build shows reads past the
 * end). */
/* TI_TEST_DUMP=path: each damaged input is written there before it is
 * decoded, to find the one a library crashes on. */
static void dump(const unsigned char* d, size_t n)
{
    const char* p = getenv("TI_TEST_DUMP");
    FILE* f = p ? fopen(p, "wb") : 0;
    if (f) {
        fwrite(d, 1, n, f);
        fclose(f);
    }
}

static void test_robust()
{
    unsigned long seed = 12345;
    int decoded = 0;
    for (size_t i = 0; i < sizeof(k_fixtures) / sizeof(k_fixtures[0]); i++) {
        size_t n;
        unsigned char* d = read_file(k_fixtures[i].file, &n);
        if (!d)
            continue;
        unsigned char* m = (unsigned char*)malloc(n);
        for (int b = 1; b < 3; b++) {
            ti::DecodeOptions o;
            o.backend = (ti::Backend)b;
            if (!ti::decoder_name(k_fixtures[i].format, o.backend))
                continue;
            char err[200];
            if (getenv("TI_TEST_VERBOSE"))
                fprintf(stderr, "robust: %s with %s\n", k_fixtures[i].file,
                        ti::decoder_name(k_fixtures[i].format, o.backend));
            /* Every length through the headers, then a sample (the
             * emulated IRIX machine is slow). */
            for (size_t len = 0; len < n; len += len < 64 ? 1 : n / 97 + 1) {
                /* A copy of just that length, so overreads are caught. */
                unsigned char* cut = (unsigned char*)malloc(len ? len : 1);
                memcpy(cut, d, len);
                ti::Image im;
                dump(cut, len);
                if (ti::decode(cut, len, &im, 0, err, sizeof(err), o))
                    decoded++;
                free(cut);
            }
            for (int k = 0; k < 100; k++) {
                memcpy(m, d, n);
                for (int j = 0; j < 3; j++) {
                    seed = seed * 1103515245UL + 12345UL;
                    size_t at = (size_t)((seed >> 8) % n);
                    m[at] = (unsigned char)(seed >> 20);
                }
                ti::Image im;
                dump(m, n);
                ti::decode(m, n, &im, 0, err, sizeof(err), o);
            }
        }
        free(m);
        free(d);
    }
    CHECK(decoded >= 0);
}

static void test_scale()
{
    ti::Image a;
    CHECK(a.alloc(2, 2));
    memset(a.rgba, 0, 16);
    /* White opaque, black opaque, and two transparent red ones. */
    unsigned char px[16] = { 255, 255, 255, 255, 0, 0, 0, 255, 255, 0, 0, 0, 255, 0, 0, 0 };
    memcpy(a.rgba, px, 16);
    ti::Image b;
    CHECK(ti::scale(a, 1, 1, &b));
    CHECK(b.rgba[0] == 127 && b.rgba[1] == 127 && b.rgba[2] == 127 && b.rgba[3] == 127);
    CHECK(ti::scale(a, 4, 4, &b));
    CHECK(b.w == 4 && memcmp(b.row(0), px, 4) == 0 && memcmp(b.row(0) + 4, px, 4) == 0);
    ti::flatten(&a, 0x0000ffUL);
    CHECK(a.rgba[8] == 0 && a.rgba[9] == 0 && a.rgba[10] == 255 && a.rgba[11] == 255);
    int w, h;
    ti::fit(1000, 500, 80, 40, 1.0, false, &w, &h);
    CHECK(w == 80 && h == 40);
    ti::fit(1000, 500, 80, 80, 1.0, false, &w, &h);
    CHECK(w == 80 && h == 40);
    /* Cells half as wide as tall: twice as many columns. */
    ti::fit(100, 100, 200, 50, 0.5, true, &w, &h);
    CHECK(w == 100 && h == 50);
    ti::fit(10, 10, 200, 50, 1.0, false, &w, &h);
    CHECK(w == 10 && h == 10);
    ti::Image big;
    CHECK(!big.alloc(1 << 14, 1 << 14));
}

static bool starts(const ti::Bytes& b, const char* s)
{
    return b.n >= strlen(s) && memcmp(b.d, s, strlen(s)) == 0;
}

static bool ends(const ti::Bytes& b, const char* s)
{
    size_t l = strlen(s);
    return b.n >= l && memcmp(b.d + b.n - l, s, l) == 0;
}

static bool contains(const ti::Bytes& b, const char* s)
{
    size_t l = strlen(s);
    for (size_t i = 0; i + l <= b.n; i++)
        if (memcmp(b.d + i, s, l) == 0)
            return true;
    return false;
}

static void test_encoders()
{
    size_t n;
    unsigned char* d = read_file("rgba.png", &n);
    ti::Image im;
    char err[200];
    CHECK(d && ti::decode(d, n, &im, 0, err, sizeof(err)));
    free(d);

    ti::Bytes k;
    ti::encode_kitty(im, 4, 2, 9, &k);
    CHECK(starts(k, "\033_Ga=T,f=32,s=7,v=5,c=4,r=2,i=9,q=2,C=1,m=0;"));
    CHECK(ends(k, "\033\\"));
    /* 140 bytes: 188 of base64. */
    CHECK(k.n == strlen("\033_Ga=T,f=32,s=7,v=5,c=4,r=2,i=9,q=2,C=1,m=0;") + 188 + 2);
    k.clear();
    ti::encode_kitty(im, 0, 0, 9, &k);
    CHECK(starts(k, "\033_Ga=T,f=32,s=7,v=5,i=9,q=2,C=1,m=0;"));
    k.clear();
    ti::encode_kitty(im, 4, 0, 9, &k);
    CHECK(starts(k, "\033_Ga=T,f=32,s=7,v=5,c=4,i=9,q=2,C=1,m=0;"));
    k.clear();
    ti::encode_kitty(im, 4, 2, 9, &k);
    ti::Image wide;
    CHECK(ti::scale(im, 70, 50, &wide));
    k.clear();
    ti::encode_kitty(wide, 4, 2, 9, &k);
    CHECK(starts(k, "\033_Ga=T,f=32,s=70,v=50,c=4,r=2,i=9,q=2,C=1,m=1;"));
    CHECK(contains(k, "\033\\\033_Gm=1;") && contains(k, "\033\\\033_Gm=0;"));
    k.clear();
    ti::kitty_delete(9, &k);
    CHECK(k.n == strlen("\033_Ga=d,d=I,i=9,q=2\033\\") && starts(k, "\033_Ga=d,d=I,i=9,q=2\033\\"));

    ti::Image flat;
    CHECK(ti::scale(im, 7, 5, &flat));
    ti::flatten(&flat, 0);
    ti::Bytes s;
    ti::encode_sixel(flat, &s);
    CHECK(starts(s, "\033P0;1;0q\"1;1;7;5#"));
    CHECK(s.n < 1200);
    CHECK(ends(s, "-\033\\"));
    /* One band of six rows: a single '-'. */
    size_t dashes = 0;
    for (size_t i = 0; i < s.n; i++)
        if (s.d[i] == '-')
            dashes++;
    CHECK(dashes == 1);

    /* The PNG ti writes reads back the same, with either decoder. */
    ti::Bytes p;
    ti::encode_png(im, &p);
    CHECK(!p.failed && starts(p, "\x89PNG\r\n\x1a\n"));
    for (int b = 1; b < 3; b++) {
        ti::DecodeOptions o;
        o.backend = (ti::Backend)b;
        if (!ti::decoder_name(ti::FMT_PNG, o.backend))
            continue;
        ti::Image back;
        CHECK(ti::decode((const unsigned char*)p.d, p.n, &back, 0, err, sizeof(err), o));
        CHECK(back.w == 7 && back.h == 5 && memcmp(back.rgba, im.rgba, 7 * 5 * 4) == 0);
    }
    /* Bigger than one stored block. */
    ti::Image large;
    CHECK(ti::scale(im, 300, 200, &large));
    p.clear();
    ti::encode_png(large, &p);
    ti::Image back;
    CHECK(ti::decode((const unsigned char*)p.d, p.n, &back, 0, err, sizeof(err)));
    CHECK(back.w == 300 && memcmp(back.rgba, large.rgba, 300 * 200 * 4) == 0);

    ti::Bytes it;
    ti::encode_iterm(im, 4, 2, &it);
    CHECK(starts(it, "\033]1337;File=inline=1;size="));
    CHECK(contains(it, ";width=4;height=2;preserveAspectRatio=1:iVBORw0KGgo"));
    CHECK(ends(it, "\a"));
}

static void test_protocols()
{
    CHECK(ti::protocol_from_setting("sixel") == ti::PROTO_SIXEL);
    CHECK(ti::protocol_from_setting("off") == ti::PROTO_OFF);
    CHECK(strcmp(ti::protocol_name(ti::PROTO_ITERM), "iterm") == 0);
    static char tmux[] = "TMUX=/tmp/tmux-1/default";
    static char kitty[] = "KITTY_WINDOW_ID=1";
    putenv(kitty);
    putenv(tmux);
    CHECK(ti::protocol_from_setting("auto") == ti::PROTO_BLOCKS);
    static char notmux[] = "TMUX";
    putenv(notmux);         /* glibc and IRIX: removes it */
    if (!getenv("TMUX"))
        CHECK(ti::detect_protocol() == ti::PROTO_KITTY);
}

int main()
{
    test_decoders();
    test_damage();
    test_robust();
    test_scale();
    test_encoders();
    test_protocols();
    printf("ti: %d checks, %d failures (decoders: PNG %s, JPEG %s, GIF %s, TIFF %s)\n", g_checks,
           g_failures, ti::decoder_name(ti::FMT_PNG), ti::decoder_name(ti::FMT_JPEG),
           ti::decoder_name(ti::FMT_GIF),
           ti::decoder_name(ti::FMT_TIFF) ? ti::decoder_name(ti::FMT_TIFF) : "none");
    return g_failures ? 1 : 0;
}
