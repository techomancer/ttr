/* Tests for th: colours, UI themes and text schemes. Run from the source
 * tree's top (data/ is found next to build/). */
#include "../scheme.h"
#include "../theme.h"

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

static void test_colors()
{
    th::Color c;
    CHECK(th::parse_hex("#0a246a", &c) && c == 0x0a246aUL);
    CHECK(th::parse_hex("  #fff", &c) && c == 0xffffffUL);
    CHECK(!th::parse_hex("0a246a", &c));
    CHECK(!th::parse_hex("#12345x", &c));
    char buf[8];
    th::format_hex(0x0a246a, buf);
    CHECK(strcmp(buf, "#0a246a") == 0);
    /* Motif's derivation: light grey gets black text, dark grey white. */
    CHECK(th::motif_colors(0xb8b8b8).fg == 0x000000UL);
    CHECK(th::motif_colors(0x404040).fg == 0xffffffUL);
    th::ColorSet cs = th::motif_colors(0xb8b8b8);
    CHECK(cs.top > cs.bg && cs.bottom < cs.bg);
    CHECK(th::mix(0x000000, 0xffffff, 50) == 0x7f7f7fUL);
    CHECK(th::brightness(0xffffff) == 255 && th::brightness(0) == 0);
}

static void test_themes()
{
    th::Theme t;
    CHECK(t.load("data/schemes/ttr/Windows2000.ad"));
    CHECK(strcmp(t.name, "Windows 2000") == 0);
    CHECK(t.get(th::ROLE_BACKGROUND) == 0xd4d0c8UL);
    CHECK(t.get(th::ROLE_ACTIVE) == 0x0a246aUL);
    CHECK(!t.dark);
    th::Theme g;
    CHECK(g.load("data/schemes/irix/Gotham.ad"));
    CHECK(g.dark);
#if !defined(__sgi)
    CHECK(g.get(th::ROLE_BACKGROUND) == 0x585858UL);   /* gamma1_0 */
#else
    CHECK(g.get(th::ROLE_BACKGROUND) != 0x585858UL);   /* the system gamma's */
#endif
    /* Derived roles follow the background. */
    th::Theme c = th::Theme::classic();
    CHECK(c.get(th::ROLE_LIST) == c.get(th::ROLE_BACKGROUND));
    th::List list;
    th::list_themes(&list);
    CHECK(list.count() > 30);
    CHECK(strcmp(list[0].name, "Classic") == 0);
    CHECK(list.has("Gotham") && list.has("Windows 2000"));
    th::Theme f;
    CHECK(th::find_theme("gotham", &f) && f.dark);
    CHECK(!th::find_theme("No Such Theme", &f));
    CHECK(strcmp(f.name, "Classic") == 0);
}

static void test_schemes()
{
    th::Scheme l = th::Scheme::light();
    CHECK(l.get(th::SC_BACKGROUND) == 0xffffffUL);
    CHECK(l.get(th::SC_CURSOR) == 0x000000UL);         /* derived: the text */
    CHECK(l.ansi(1) == 0xcd0000UL);      /* xterm's red */
    th::Scheme d = th::Scheme::dark_default();
    CHECK(d.dark);
    CHECK(d.get(th::SC_KEYWORD) != l.get(th::SC_KEYWORD));
    th::Scheme s;
    CHECK(s.load("data/schemes/text/SolarizedDark.scheme"));
    CHECK(strcmp(s.name, "Solarized Dark") == 0);
    CHECK(s.dark);
    CHECK(s.ansi(4) == 0x268bd2UL);
    CHECK(strcmp(th::scheme_key(th::SC_SELECTION_BG), "selectionBackground") == 0);
    CHECK(strcmp(th::scheme_key((th::SchemeColor)(th::SC_ANSI0 + 12)), "color12") == 0);
    th::List list;
    th::list_schemes(&list);
    CHECK(strcmp(list[0].name, "Light") == 0 && strcmp(list[1].name, "Dark") == 0);
    CHECK(list.has("Amber") && list.has("Solarized Light"));
    th::Scheme f;
    CHECK(th::find_scheme("dark", &f) && f.dark);
    CHECK(th::find_scheme("Green Screen", &f) && f.get(th::SC_FOREGROUND) == 0x33ff33UL);
    CHECK(!th::find_scheme("nothing", &f) && strcmp(f.name, "Light") == 0);
    /* A scheme with only a background derives the rest. */
    FILE* tmp = fopen("build/th-test.scheme", "w");
    CHECK(tmp != 0);
    if (tmp) {
        fputs("background: #102030\n", tmp);
        fclose(tmp);
        th::Scheme m;
        CHECK(m.load("build/th-test.scheme"));
        CHECK(m.dark);
        CHECK(m.get(th::SC_FOREGROUND) == 0xd4d4d4UL);
        CHECK(m.get(th::SC_GUTTER_BG) != m.get(th::SC_BACKGROUND));
        remove("build/th-test.scheme");
    }
}

int main()
{
    th::set_program_path("build/th_test");
    test_colors();
    test_themes();
    test_schemes();
    printf("th: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
