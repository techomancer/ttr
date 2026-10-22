/*
 * ttrc-drive: runs a program (ttrc) on a pty and reads its screen through
 * a tl::Terminal, from a script on stdin (ttrc/DESIGN.md section 10).
 *
 *   ttrc-drive [-s COLSxROWS] program args... < script
 *
 * Script lines:
 *   send TEXT        type TEXT (C escapes: \r \n \t \e \\ \xHH)
 *   wait MS          let the program run
 *   expect TEXT      wait up to 8 s for TEXT on the screen; FAIL if not
 *   absent TEXT      FAIL if TEXT is on the screen
 *   screen           print the screen
 *   # ...            a comment
 * Exit status: the number of failed expectations.
 */
#include "../../../tl/terminal.h"
#include "../../../tp/tp.h"

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

namespace {

tp::Pty g_pty;
tl::Terminal g_term;

struct Answer : public tl::Host {
    void write_pty(const uint8_t* d, size_t n) { g_pty.write(d, n); }
};
Answer g_host;

long now_ms()
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

bool pump(int ms)
{
    long end = now_ms() + ms;
    for (;;) {
        long left = end - now_ms();
        if (left < 0)
            left = 0;
        int r = g_pty.wait_readable((int)left);
        if (r > 0) {
            char buf[8192];
            long n = g_pty.read(buf, sizeof(buf));
            if (n < 0)
                return false;
            if (n > 0)
                g_term.feed((const uint8_t*)buf, (size_t)n);
        }
        if (now_ms() >= end)
            return true;
    }
}

void screen_text(tl::Buffer& b)
{
    b.clear();
    g_term.screen_text(b);
}

size_t unescape(const char* s, char* out)
{
    size_t o = 0;
    while (*s) {
        if (*s == '\\' && s[1]) {
            s++;
            switch (*s) {
            case 'r': out[o++] = '\r'; break;
            case 'n': out[o++] = '\n'; break;
            case 't': out[o++] = '\t'; break;
            case 'e': out[o++] = 033; break;
            case 'x': {
                int v = 0;
                for (int k = 0; k < 2 && s[1]; k++) {
                    char c = *++s;
                    v = v * 16 + (c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0');
                }
                out[o++] = (char)v;
                break;
            }
            default: out[o++] = *s; break;
            }
            s++;
        } else {
            out[o++] = *s++;
        }
    }
    out[o] = 0;
    return o;
}

} /* namespace */

int main(int argc, char** argv)
{
    int cols = 100, rows = 30, a = 1;
    if (argc > 2 && strcmp(argv[1], "-s") == 0) {
        sscanf(argv[2], "%dx%d", &cols, &rows);
        a = 3;
    }
    if (a >= argc) {
        fprintf(stderr, "usage: ttrc-drive [-s COLSxROWS] program args... < script\n");
        return 2;
    }
    tp::WinSize ws;
    ws.cols = cols;
    ws.rows = rows;
    ws.width_px = ws.height_px = 0;
    if (!g_pty.open(ws)) {
        fprintf(stderr, "pty: %s\n", g_pty.error());
        return 2;
    }
    g_term.init(cols, rows, 0);
    g_term.set_host(&g_host);
    tp::Process proc;
    tp::SpawnOptions o;
    o.argv = (const char* const*)(argv + a);
    if (!proc.spawn(g_pty, o)) {
        fprintf(stderr, "spawn: %s\n", proc.error());
        return 2;
    }
    int failures = 0;
    char line[4096], text[4096];
    tl::Buffer b;
    while (fgets(line, sizeof(line), stdin)) {
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r'))
            line[--l] = 0;
        if (!l || line[0] == '#')
            continue;
        if (strncmp(line, "send ", 5) == 0) {
            size_t n = unescape(line + 5, text);
            g_pty.write(text, n);
            pump(30);
        } else if (strncmp(line, "wait ", 5) == 0) {
            pump(atoi(line + 5));
        } else if (strncmp(line, "expect ", 7) == 0 || strncmp(line, "absent ", 7) == 0) {
            bool want = line[0] == 'e';
            unescape(line + 7, text);
            long end = now_ms() + (want ? 8000 : 0);
            bool found = false;
            for (;;) {
                screen_text(b);
                found = strstr(b.c_str(), text) != 0;
                if (found == want || now_ms() >= end)
                    break;
                pump(50);
            }
            if (found != want) {
                failures++;
                printf("FAIL: %s \"%s\"\n--- screen ---\n%s\n--------------\n",
                       want ? "expected" : "unexpected", text, b.c_str());
            } else {
                printf("ok: %s \"%s\"\n", want ? "found" : "absent", text);
            }
        } else if (strcmp(line, "screen") == 0) {
            screen_text(b);
            printf("--- screen ---\n%s\n--------------\n", b.c_str());
        } else {
            fprintf(stderr, "drive: unknown line: %s\n", line);
        }
        fflush(stdout);
    }
    proc.signal_group(SIGHUP);
    proc.wait(1000);
    printf("drive: %d failure%s\n", failures, failures == 1 ? "" : "s");
    return failures;
}
