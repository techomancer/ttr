/*
 * tp tests. The binary is also its own child program: "tp_test --child
 * WHAT" runs one probe on the terminal it was started on, so the checks
 * do not depend on what stty or the shell print on each system.
 */
#include "../tp.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

static int failures = 0;
static int checks = 0;
static char self[1024];

#define CHECK(cond)                                                           \
    do {                                                                      \
        checks++;                                                             \
        if (!(cond)) {                                                        \
            failures++;                                                       \
            fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                     \
    } while (0)

/* ---- the child side ------------------------------------------------------ */

static volatile sig_atomic_t got_winch = 0;
static void on_winch(int) { got_winch = 1; }

static int child_main(const char* what, const char* arg)
{
    struct winsize ws;
    if (strcmp(what, "size") == 0) {
        memset(&ws, 0, sizeof(ws));
        ioctl(0, TIOCGWINSZ, &ws);
        printf("size %d %d %d %d\n", ws.ws_col, ws.ws_row, ws.ws_xpixel, ws.ws_ypixel);
    } else if (strcmp(what, "ctty") == 0) {
        int fd = open("/dev/tty", O_RDWR);
        printf("ctty %s\n", fd >= 0 ? "ok" : "none");
        printf("isatty %d %d %d\n", isatty(0), isatty(1), isatty(2));
        printf("leader %d\n", getsid(0) == getpid());
        printf("fg %d\n", tcgetpgrp(0) == getpgrp());
    } else if (strcmp(what, "modes") == 0) {
        struct termios t;
        tcgetattr(0, &t);
        printf("cc intr %d quit %d erase %d kill %d eof %d susp %d start %d stop %d\n",
               t.c_cc[VINTR], t.c_cc[VQUIT], t.c_cc[VERASE], t.c_cc[VKILL],
               t.c_cc[VEOF], t.c_cc[VSUSP], t.c_cc[VSTART], t.c_cc[VSTOP]);
        printf("lflag icanon %d echo %d isig %d\n", (t.c_lflag & ICANON) != 0,
               (t.c_lflag & ECHO) != 0, (t.c_lflag & ISIG) != 0);
        printf("iflag icrnl %d ixon %d\n", (t.c_iflag & ICRNL) != 0, (t.c_iflag & IXON) != 0);
        printf("oflag onlcr %d\n", (t.c_oflag & ONLCR) != 0);
    } else if (strcmp(what, "env") == 0) {
        const char* v = getenv(arg);
        printf("env %s=%s\n", arg, v ? v : "(unset)");
    } else if (strcmp(what, "cwd") == 0) {
        char buf[1024];
        printf("cwd %s\n", getcwd(buf, sizeof(buf)) ? buf : "?");
    } else if (strcmp(what, "argv0") == 0) {
        printf("argv0 %s\n", arg);
    } else if (strcmp(what, "winch") == 0) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = on_winch;
        sigaction(SIGWINCH, &sa, 0);
        printf("ready\n");
        fflush(stdout);
        for (int i = 0; i < 500 && !got_winch; i++)
            usleep(10000);
        memset(&ws, 0, sizeof(ws));
        ioctl(0, TIOCGWINSZ, &ws);
        printf("winch %d size %d %d\n", (int)got_winch, ws.ws_col, ws.ws_row);
    } else if (strcmp(what, "exit") == 0) {
        return atoi(arg);
    } else if (strcmp(what, "sleep") == 0) {
        printf("sleeping\n");
        fflush(stdout);
        sleep(30);
    } else if (strcmp(what, "cat") == 0) {
        char line[256];
        while (fgets(line, sizeof(line), stdin))
            printf("got %s", line);
        printf("eof\n");
    } else if (strcmp(what, "fds") == 0) {
        int open_fds = 0;
        for (int fd = 3; fd < 256; fd++)
            if (fcntl(fd, F_GETFD) >= 0)
                open_fds++;
        printf("extra fds %d\n", open_fds);
    }
    fflush(stdout);
    return 0;
}

/* ---- the parent side ----------------------------------------------------- */

struct Run {
    tp::Pty pty;
    tp::Process proc;
    char out[8192];
    size_t len;

    Run() : len(0) { out[0] = 0; }

    bool start(const char* what, const char* arg = 0, const char* const* env = 0,
               int cols = 80, int rows = 24)
    {
        tp::WinSize ws;
        ws.cols = cols;
        ws.rows = rows;
        ws.width_px = cols * 8;
        ws.height_px = rows * 16;
        if (!pty.open(ws)) {
            fprintf(stderr, "pty: %s\n", pty.error());
            return false;
        }
        const char* argv[5] = { self, "--child", what, arg, 0 };
        tp::SpawnOptions o;
        o.argv = argv;
        o.env = env;
        if (!proc.spawn(pty, o)) {
            fprintf(stderr, "spawn: %s\n", proc.error());
            return false;
        }
        return true;
    }
    /* Read until the text appears or the other side closes. */
    bool read_until(const char* text, int timeout_ms = 10000)
    {
        int waited = 0;
        while (!text || !strstr(out, text)) {
            int r = pty.wait_readable(100);
            if (r == 0) {
                waited += 100;
                if (waited >= timeout_ms)
                    return false;
                continue;
            }
            char buf[1024];
            long n = pty.read(buf, sizeof(buf));
            if (n < 0)
                return text == 0;
            for (long i = 0; i < n && len + 1 < sizeof(out); i++)
                if (buf[i] != '\r')
                    out[len++] = buf[i];
            out[len] = 0;
        }
        return true;
    }
    bool has(const char* s) const { return strstr(out, s) != 0; }
};

static void test_basics()
{
    Run r;
    CHECK(r.start("size", 0, 0, 100, 30));
    CHECK(r.read_until(0));
    CHECK(r.has("size 100 30 800 480"));
    CHECK(r.proc.wait(5000));
    CHECK(r.proc.exited() && r.proc.exit_code() == 0);
    CHECK(r.pty.slave_name()[0] == '/');
    if (!r.has("size 100 30"))
        fprintf(stderr, "output: %s\n", r.out);
}

static void test_controlling_tty()
{
    Run r;
    CHECK(r.start("ctty"));
    CHECK(r.read_until(0));
    CHECK(r.has("ctty ok"));
    CHECK(r.has("isatty 1 1 1"));
    CHECK(r.has("leader 1"));
    CHECK(r.has("fg 1"));
    if (!r.has("ctty ok") || !r.has("fg 1"))
        fprintf(stderr, "output: %s\n", r.out);
}

static void test_modes()
{
    Run r;
    CHECK(r.start("modes"));
    CHECK(r.read_until(0));
    CHECK(r.has("cc intr 3 quit 28 erase 127 kill 21 eof 4 susp 26 start 17 stop 19"));
    CHECK(r.has("lflag icanon 1 echo 1 isig 1"));
    CHECK(r.has("iflag icrnl 1 ixon 1"));
    CHECK(r.has("oflag onlcr 1"));
    if (!r.has("cc intr 3 quit 28 erase 127"))
        fprintf(stderr, "output: %s\n", r.out);
}

static void test_environment()
{
    /* TP_GONE is in our environment and must not reach the child. */
    static char gone[] = "TP_GONE=still here";
    putenv(gone);
    const char* env[] = { "TERM=tp-test", "TP_GONE", 0 };
    Run a;
    CHECK(a.start("env", "TERM", env));
    CHECK(a.read_until(0));
    CHECK(a.has("env TERM=tp-test"));
    Run b;
    CHECK(b.start("env", "TP_GONE", env));
    CHECK(b.read_until(0));
    CHECK(b.has("env TP_GONE=(unset)"));
    Run c;
    CHECK(c.start("env", "PATH", env));
    CHECK(c.read_until(0));
    CHECK(!c.has("(unset)"));
}

static void test_descriptors()
{
    /* A stray descriptor in the parent must not reach the child. */
    int leak = open("/dev/null", O_RDONLY);
    Run r;
    CHECK(r.start("fds"));
    CHECK(r.read_until(0));
    CHECK(r.has("extra fds 0"));
    if (!r.has("extra fds 0"))
        fprintf(stderr, "output: %s\n", r.out);
    close(leak);
}

static void test_resize()
{
    Run r;
    CHECK(r.start("winch"));
    CHECK(r.read_until("ready"));
    tp::WinSize ws;
    ws.cols = 132;
    ws.rows = 50;
    ws.width_px = ws.height_px = 0;
    CHECK(r.pty.resize(ws));
    CHECK(r.read_until(0));
    CHECK(r.has("winch 1 size 132 50"));
    if (!r.has("winch 1"))
        fprintf(stderr, "output: %s\n", r.out);
}

static void test_io()
{
    Run r;
    CHECK(r.start("cat"));
    const char line[] = "hello pty\n";
    CHECK(r.pty.write(line, sizeof(line) - 1) == (long)(sizeof(line) - 1));
    CHECK(r.read_until("got hello pty"));
    CHECK(r.has("hello pty\ngot hello pty"));      /* echo, then the reply */
    const char eof = 0x04;                           /* VEOF, ^D */
    CHECK(r.pty.write(&eof, 1) == 1);
    CHECK(r.read_until(0));
    CHECK(r.has("eof"));
    CHECK(r.proc.wait(5000));
    CHECK(r.proc.exited());

    /* ^C from the terminal interrupts the foreground job. */
    Run i;
    CHECK(i.start("sleep"));
    CHECK(i.read_until("sleeping"));
    const char intr = 0x03;
    CHECK(i.pty.write(&intr, 1) == 1);
    CHECK(i.proc.wait(5000));
    CHECK(!i.proc.exited() && i.proc.term_signal() == SIGINT);
}

static void test_exit_and_signals()
{
    Run r;
    CHECK(r.start("exit", "7"));
    CHECK(r.read_until(0));
    CHECK(r.proc.wait(5000));
    CHECK(r.proc.exited() && r.proc.exit_code() == 7);
    CHECK(!r.proc.running());

    Run s;
    CHECK(s.start("sleep"));
    CHECK(s.read_until("sleeping"));
    CHECK(s.proc.running());
    CHECK(s.proc.signal_group(SIGTERM));
    CHECK(s.proc.wait(5000));
    CHECK(!s.proc.exited() && s.proc.term_signal() == SIGTERM);
    /* The master sees the hangup. */
    CHECK(s.read_until(0));
}

static void test_spawn_errors()
{
    tp::Pty pty;
    tp::WinSize ws = { 80, 24, 0, 0 };
    CHECK(pty.open(ws));
    tp::Process p;
    const char* missing[] = { "tp-no-such-program-anywhere", 0 };
    tp::SpawnOptions o;
    o.argv = missing;
    CHECK(!p.spawn(pty, o));
    CHECK(strstr(p.error(), "not found") != 0);
    const char* notexec[] = { "/dev/null", 0 };
    o.argv = notexec;
    CHECK(!p.spawn(pty, o));
    CHECK(strstr(p.error(), "cannot run") != 0);
    const char* ok[] = { self, "--child", "cwd", 0 };
    o.argv = ok;
    o.cwd = "/nonexistent-tp-dir";
    CHECK(!p.spawn(pty, o));
    CHECK(strstr(p.error(), "directory") != 0);
}

static void test_cwd()
{
    tp::Pty pty;
    tp::WinSize ws = { 80, 24, 0, 0 };
    CHECK(pty.open(ws));
    tp::Process p;
    const char* argv[] = { self, "--child", "cwd", 0 };
    tp::SpawnOptions o;
    o.argv = argv;
    o.cwd = "/";
    CHECK(p.spawn(pty, o));
    char out[512];
    size_t len = 0;
    while (len + 1 < sizeof(out) && pty.wait_readable(5000) > 0) {
        long n = pty.read(out + len, sizeof(out) - 1 - len);
        if (n < 0)
            break;
        len += (size_t)n;
    }
    out[len] = 0;
    CHECK(strstr(out, "cwd /\r\n") != 0);
    p.wait(5000);
}

/* Another process's directory, children and the terminal's foreground
 * job, read from outside (what ttrc does with its shell). */
static void test_process_queries()
{
    tp::Pty pty;
    tp::Process proc;
    tp::WinSize ws;
    ws.cols = 80;
    ws.rows = 24;
    ws.width_px = ws.height_px = 0;
    CHECK(pty.open(ws));
    /* "; true" keeps sh from exec'ing sleep in its place. */
    const char* argv[] = { "/bin/sh", "-c", "cd /usr && sleep 3; true", 0 };
    tp::SpawnOptions o;
    o.argv = argv;
    CHECK(proc.spawn(pty, o));
    char cwd[512] = "";
    bool got = false;
    for (int i = 0; i < 40 && !got; i++) {
        usleep(50000);
        got = tp::process_cwd(proc.pid(), cwd, sizeof(cwd)) && strcmp(cwd, "/usr") == 0;
    }
    CHECK(got);
    CHECK(tp::process_has_children(proc.pid()) == 1);
    /* sh -c has no job control: its own group stays in the foreground. */
    int fg = pty.foreground_group();
    CHECK(fg == -1 || fg == proc.pid());
    proc.signal_group(SIGTERM);
    CHECK(proc.wait(5000));
    CHECK(!tp::process_cwd(proc.pid(), cwd, sizeof(cwd)));
}

int main(int argc, char** argv)
{
    if (argc >= 3 && strcmp(argv[1], "--child") == 0)
        return child_main(argv[2], argc > 3 ? argv[3] : "");

    if (argv[0][0] == '/') {
        strncpy(self, argv[0], sizeof(self) - 1);
    } else {
        if (!getcwd(self, sizeof(self) - 256))
            return 2;
        strcat(self, "/");
        strncat(self, argv[0], 200);
    }

    test_basics();
    test_controlling_tty();
    test_modes();
    test_environment();
    test_descriptors();
    test_resize();
    test_io();
    test_exit_and_signals();
    test_spawn_errors();
    test_cwd();
    test_process_queries();

    printf("tp tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
