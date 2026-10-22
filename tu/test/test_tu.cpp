/*
 * Tests for tu: terminfo reading, tparm, key decoding, and the screen
 * diff, checked by feeding tu's output into tl's terminal emulator.
 */
#include "../input.h"
#include "../screen.h"
#include "../term.h"
#include "../terminfo.h"
#include "../tparm.h"

#include "../../tl/terminal.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_checks, g_failures;

#define CHECK(c) check((c), __FILE__, __LINE__, #c)
#define CHECK_STR(a, b) check_str((a), (b), __FILE__, __LINE__)

static void check(bool ok, const char* file, int line, const char* what)
{
    g_checks++;
    if (!ok) {
        g_failures++;
        printf("%s:%d: failed: %s\n", file, line, what);
    }
}

/* putenv keeps the pointer: the strings must live on. */
static void env(const char* kv)
{
    putenv((char*)kv);
}

static void show(const char* s, char* out, size_t n)
{
    size_t o = 0;
    for (; s && *s && o + 5 < n; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c == 0x7f)
            o += (size_t)sprintf(out + o, "\\x%02x", c);
        else
            out[o++] = (char)c;
    }
    out[o] = 0;
}

static void check_str(const char* got, const char* want, const char* file, int line)
{
    g_checks++;
    if (!got || strcmp(got, want) != 0) {
        g_failures++;
        char a[400], b[400];
        show(got, a, sizeof(a));
        show(want, b, sizeof(b));
        printf("%s:%d: got \"%s\", want \"%s\"\n", file, line, got ? a : "(null)", b);
    }
}

/* ------------------------------------------------------------------ */

static void test_tparm()
{
    char b[128];
    tu::tparm2(b, sizeof(b), "\033[%i%p1%d;%p2%dH", 2, 4);
    CHECK_STR(b, "\033[3;5H");
    const char* setaf =
        "\033[%?%p1%{8}%<%t3%p1%d%e%p1%{16}%<%t9%p1%{8}%-%d%e38;5;%p1%d%;m";
    tu::tparm1(b, sizeof(b), setaf, 1);
    CHECK_STR(b, "\033[31m");
    tu::tparm1(b, sizeof(b), setaf, 9);
    CHECK_STR(b, "\033[91m");
    tu::tparm1(b, sizeof(b), setaf, 200);
    CHECK_STR(b, "\033[38;5;200m");
    tu::tparm2(b, sizeof(b), "%p1%02d:%p2%3d|%p1%x", 7, 5);
    CHECK_STR(b, "07:  5|7");
    tu::tparm1(b, sizeof(b), "\033[%p1%dm$<5/>x", 4);
    CHECK_STR(b, "\033[4mx");
    tu::tparm1(b, sizeof(b), "%p1%c%{65}%c%'B'%c", 'Z');
    CHECK_STR(b, "ZAB");
    tu::tparm2(b, sizeof(b), "%p1%p2%>%tbig%esmall%;", 3, 2);
    CHECK_STR(b, "big");
    tu::tparm2(b, sizeof(b), "%p1%p2%>%tbig%esmall%;", 1, 2);
    CHECK_STR(b, "small");
    tu::tparm1(b, sizeof(b), "%p1%Pa%ga%ga%*%d", 6);
    CHECK_STR(b, "36");
}

static void test_terminfo_source()
{
    tu::Terminfo ti;
    CHECK(ti.load_source("test|a test, am, cols#80, lines#0x18, cup=\\E[%i%p1%d;%p2%dH,"
                         " kbs=^?, x=a\\,b\\072^A\\s, RGB, Sync=\\E[?2026%?%p1%{1}%-%tl%eh%;,"));
    CHECK_STR(ti.name(), "test");
    CHECK(ti.flag("am"));
    CHECK(!ti.flag("bce"));
    CHECK(ti.num("cols") == 80);
    CHECK(ti.num("lines") == 24);
    CHECK(ti.num("colors") == -1);
    CHECK_STR(ti.str("cup"), "\033[%i%p1%d;%p2%dH");
    CHECK_STR(ti.str("kbs"), "\x7f");
    CHECK_STR(ti.str("x"), "a,b:\x01 ");
    CHECK(ti.flag("RGB"));
    ti.supplement("colors#8, cup=nope, setaf=\\E[3%p1%dm,");
    CHECK(ti.num("colors") == 8);
    CHECK_STR(ti.str("cup"), "\033[%i%p1%d;%p2%dH");
    CHECK_STR(ti.str("setaf"), "\033[3%p1%dm");

    CHECK(ti.load_builtin("iris-ansi"));
    CHECK_STR(ti.str("kf1"), "\033[001q");
    CHECK_STR(ti.str("kdch1"), "\x7f");
    CHECK(ti.num("lines") == 40);
    CHECK(!ti.load_builtin("no-such-terminal"));
}

/* The real database, where this system has one. */
static void test_terminfo_files()
{
    tu::Terminfo ti;
    const char* p256 = "/usr/share/terminfo/x/xterm-256color";
    if (access(p256, R_OK) == 0) {
        CHECK(ti.load_file(p256));
        CHECK_STR(ti.name(), "xterm-256color");
        CHECK(ti.num("colors") == 256);
        CHECK(ti.flag("am"));
        CHECK(ti.flag("xenl"));
        CHECK_STR(ti.str("cup"), "\033[%i%p1%d;%p2%dH");
        CHECK_STR(ti.str("kcuu1"), "\033OA");
        CHECK(ti.str("smcup") != 0);
        /* ncurses' extended capabilities */
        CHECK(ti.str("kUP5") != 0);
    }
    const char* direct = "/usr/share/terminfo/x/xterm-direct";
    if (access(direct, R_OK) == 0) {
        CHECK(ti.load_file(direct));
        CHECK(ti.flag("RGB"));
        CHECK(ti.num("colors") == 0x1000000);
    }
    /* IRIX's own database (run in the guest). */
    const char* iris = "/usr/share/lib/terminfo/i/iris-ansi";
    if (access(iris, R_OK) == 0) {
        CHECK(ti.load_file(iris));
        CHECK_STR(ti.name(), "iris-ansi");
        CHECK(ti.num("lines") == 40);
        CHECK_STR(ti.str("kf1"), "\033[001q");
        CHECK_STR(ti.str("cup"), "\033[%i%p1%d;%p2%dH");
        CHECK_STR(ti.str("kpp"), "\033[150q");
        CHECK_STR(ti.str("cvvis"), "\033[10/y\033[=1h\033[=2l\033[=6h");
        CHECK(ti.flag("am"));
    }
    CHECK(ti.load("vt100"));
    CHECK(ti.str("cup") != 0);
}

/* ------------------------------------------------------------------ */

static tu::Event key(tu::Input& in, const char* bytes, bool timed_out = false)
{
    in.feed(bytes, strlen(bytes));
    tu::Event ev;
    if (!in.next(&ev, timed_out))
        ev.key = -1;
    return ev;
}

static void test_input()
{
    tu::Terminfo ti;
    ti.load_builtin("xterm-256color");
    tu::Input in;
    in.init(ti, true);
    tu::Event e = key(in, "\033OA");
    CHECK(e.key == tu::KEY_UP && e.mods == 0);
    e = key(in, "\033[1;5C");
    CHECK(e.key == tu::KEY_RIGHT && e.mods == tu::MOD_CTRL);
    e = key(in, "\033[15~");
    CHECK(e.key == tu::KEY_F1 + 4);
    e = key(in, "\033[24;2~");
    CHECK(e.key == tu::KEY_F1 + 11 && e.mods == tu::MOD_SHIFT);
    e = key(in, "\033[<0;10;5M");
    CHECK(e.key == tu::KEY_MOUSE && e.x == 9 && e.y == 4 && e.button == 1 &&
          e.action == tu::MOUSE_PRESS);
    e = key(in, "\033[<64;1;1M");
    CHECK(e.key == tu::KEY_MOUSE && e.action == tu::MOUSE_WHEEL_UP);
    e = key(in, "a");
    CHECK(e.key == tu::KEY_CHAR && e.ch == 'a' && e.mods == 0);
    e = key(in, "\x01");
    CHECK(e.key == tu::KEY_CHAR && e.ch == 'a' && e.mods == tu::MOD_CTRL);
    e = key(in, "\r");
    CHECK(e.key == tu::KEY_ENTER);
    e = key(in, "\x7f");
    CHECK(e.key == tu::KEY_BACKSPACE);
    e = key(in, "\303\251");
    CHECK(e.key == tu::KEY_CHAR && e.ch == 0xe9);
    e = key(in, "\033x");
    CHECK(e.key == tu::KEY_CHAR && e.ch == 'x' && e.mods == tu::MOD_ALT);
    e = key(in, "\0331");
    CHECK(e.key == tu::KEY_CHAR && e.ch == '1' && e.mods == tu::MOD_ALT);
    /* A lone Esc needs the timeout. */
    e = key(in, "\033");
    CHECK(e.key == -1);
    CHECK(in.pending());
    CHECK(in.next(&e, true) && e.key == tu::KEY_ESC);
    CHECK(!in.pending());
    /* Half a sequence waits for the rest. */
    e = key(in, "\033[1;");
    CHECK(e.key == -1);
    e = key(in, "2A");
    CHECK(e.key == tu::KEY_UP && e.mods == tu::MOD_SHIFT);
    /* Several keys at once. */
    in.feed("\033[B\033[Dq", 7);
    CHECK(in.next(&e, false) && e.key == tu::KEY_DOWN);
    CHECK(in.next(&e, false) && e.key == tu::KEY_LEFT);
    CHECK(in.next(&e, false) && e.key == tu::KEY_CHAR && e.ch == 'q');
    e = key(in, "\033[200~");
    CHECK(e.key == tu::KEY_PASTE_BEGIN);
    e = key(in, "\033[97;5u");
    CHECK(e.key == tu::KEY_CHAR && e.ch == 'a' && e.mods == tu::MOD_CTRL);
    /* Terminal replies are swallowed. */
    in.feed("\033[?62;22cz", 10);
    CHECK(in.next(&e, false) && e.key == tu::KEY_CHAR && e.ch == 'z');

    /* iris-ansi */
    tu::Terminfo iris;
    iris.load_builtin("iris-ansi");
    tu::Input ii;
    ii.init(iris, false);
    e = key(ii, "\033[001q");
    CHECK(e.key == tu::KEY_F1);
    e = key(ii, "\033[008q");
    CHECK(e.key == tu::KEY_F1 + 7);
    e = key(ii, "\033[150q");
    CHECK(e.key == tu::KEY_PGUP);
    e = key(ii, "\x7f");
    CHECK(e.key == tu::KEY_DELETE);
    e = key(ii, "\b");
    CHECK(e.key == tu::KEY_BACKSPACE);
    e = key(ii, "\351");
    CHECK(e.key == tu::KEY_CHAR && e.ch == 0xe9);
    e = key(ii, "\033[H");
    CHECK(e.key == tu::KEY_HOME);
}

/* ------------------------------------------------------------------ */

struct Rig {
    int fds[2];
    int in[2];
    tu::Term term;
    tl::Terminal tl;
    char buf[65536];
    size_t got;

    Rig(const char* name, int cols, int rows, const char* lang)
    {
        if (pipe(fds) != 0 || pipe(in) != 0)
            abort();
        fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
        env(lang);
        env("LC_ALL=");
        env("LC_CTYPE=");
        env("COLORTERM=");
        env("TU_TIER=");
        term.open(in[0], fds[1], name);
        term.set_size(cols, rows);
        tl.init(cols, rows, 0);
        tl.set_utf8(term.utf8());
        got = 0;
    }
    ~Rig()
    {
        close(fds[0]);
        close(fds[1]);
        close(in[0]);
        close(in[1]);
    }
    /* What tu wrote since the last call, fed to tl; returns its length. */
    size_t pump()
    {
        ssize_t n = read(fds[0], buf, sizeof(buf) - 1);
        got = n > 0 ? (size_t)n : 0;
        buf[got] = 0;
        tl.feed((const uint8_t*)buf, got);
        return got;
    }
    const char* row(int y)
    {
        static tl::Buffer b;
        b.clear();
        tl.row_text(y, b);
        return b.c_str();
    }
};

static void test_screen()
{
    {
        Rig r("xterm-256color", 20, 5, "LANG=C.UTF-8");
        tu::Screen s(r.term);
        tu::Style st(0xffffff, 0x0000aa);
        s.clear(st);
        s.frame(0, 0, 20, 5, tu::FRAME_DOUBLE, st);
        s.text(2, 1, "hello", st);
        s.text(2, 2, "wide \xe4\xb8\x96\xe7\x95\x8c!", st);
        s.text(2, 3, "na\xefve", st);          /* Latin-1 byte in a UTF-8 world */
        s.flush();
        CHECK(r.pump() > 0);
        CHECK_STR(r.row(0), "\xe2\x95\x94\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
                            "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
                            "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
                            "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x97");
        CHECK_STR(r.row(1), "\xe2\x95\x91 hello            \xe2\x95\x91");
        CHECK_STR(r.row(2), "\xe2\x95\x91 wide \xe4\xb8\x96\xe7\x95\x8c!       \xe2\x95\x91");
        CHECK_STR(r.row(3), "\xe2\x95\x91 na\xc3\xafve            \xe2\x95\x91");
        /* One changed cell: a short update. */
        s.text(3, 1, "E", st);
        s.flush();
        size_t n = r.pump();
        CHECK(n > 0 && n < 24);
        CHECK_STR(r.row(1), "\xe2\x95\x91 hEllo            \xe2\x95\x91");
        /* Nothing changed: nothing sent (cursor hidden stays hidden). */
        s.flush();
        CHECK(r.pump() == 0);
        /* Colours: the 256 tier maps 0x0000aa to an index. */
        CHECK(r.term.tier() == tu::TIER_256);
    }
    {
        /* vt100: no UTF-8, lines from the alternate character set. */
        Rig r("vt100", 10, 3, "LANG=C");
        r.term.start();         /* enacs: G1 is the line drawing set */
        tu::Screen s(r.term);
        s.clear(tu::Style());
        s.frame(0, 0, 10, 3, tu::FRAME_DOUBLE, tu::Style());
        s.text(1, 1, "caf\xc3\xa9", tu::Style());
        s.flush();
        r.pump();
        CHECK(r.term.tier() == tu::TIER_BASIC);
        CHECK(strstr(r.buf, "\033[") != 0);
        /* tl shows DEC special graphics as Unicode lines. */
        CHECK_STR(r.row(0), "\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
                            "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x90");
        CHECK_STR(r.row(1), "\xe2\x94\x82" "caf\xc3\xa9    " "\xe2\x94\x82");
    }
    {
        /* iris-ansi: colours and lines come from the gap table. */
        Rig r("iris-ansi", 10, 3, "LANG=C");
        CHECK(r.term.tier() == tu::TIER_ANSI);
        CHECK(r.term.has_acs());
        tu::Screen s(r.term);
        tu::Style st(0xffff55, 0x0000aa);
        s.clear(st);
        s.frame(0, 0, 10, 3, tu::FRAME_SINGLE, st);
        s.text(1, 1, "caf\xe9", st);            /* Latin-1 text */
        s.flush();
        r.pump();
        CHECK(strstr(r.buf, "\033[44m") != 0);  /* blue background */
        CHECK(strstr(r.buf, "\033[33m") != 0);  /* bright yellow: 3 and bold */
        CHECK(strstr(r.buf, "\033(0") != 0);
        CHECK_STR(r.row(1), "\xe2\x94\x82" "caf\xc3\xa9    " "\xe2\x94\x82");
    }
    {
        /* Modern: true colour SGR. */
        Rig r("xterm-256color", 10, 3, "LANG=en_US.UTF-8");
        r.term.set_tier(tu::TIER_MODERN);
        tu::Screen s(r.term);
        s.clear(tu::Style(0x123456, 0x654321));
        s.flush();
        r.pump();
        CHECK(strstr(r.buf, "38;2;18;52;86") != 0);
        CHECK(strstr(r.buf, "48;2;101;67;33") != 0);
    }
}

int main()
{
    test_tparm();
    test_terminfo_source();
    test_terminfo_files();
    test_input();
    test_screen();
    printf("tu tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
