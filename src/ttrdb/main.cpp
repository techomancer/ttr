/*
 * ttrdb - td's command-line front end, for trying td out and for tests
 * (td/DESIGN.md section 8.1).
 *
 *   ttrdb program [args...]      start it, stopped at its first instruction
 *   ttrdb -p PID                 attach to a running process
 *   ttrdb -x SCRIPT program ...  run the commands in SCRIPT (a test)
 *
 * In a script, "expect TEXT" checks that the previous command printed
 * TEXT, "absent TEXT" that it did not; "#" starts a comment. ttrdb then
 * exits with the number of failed checks.
 */
#include "../../td/session.h"
#include "../../td/source.h"

#include <ctype.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace {

td::Session* g_s;
td::Sources g_src;      /* "dir" adds to where sources are looked for */
char** g_argv;          /* the program's, for "r" after it ended */
bool g_script;
/* What the last command printed, for "expect". */
char* g_out;
size_t g_out_len, g_out_cap;

void out(const char* fmt, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fputs(buf, stdout);
    size_t n = strlen(buf);
    if (g_out_len + n + 1 > g_out_cap) {
        size_t cap = (g_out_len + n + 1) * 2;
        char* p = (char*)realloc(g_out, cap);
        if (!p)
            return;
        g_out = p;
        g_out_cap = cap;
    }
    memcpy(g_out + g_out_len, buf, n + 1);
    g_out_len += n;
}

const char* signal_name(int sig)
{
    switch (sig) {
    case SIGINT: return "SIGINT";
    case SIGQUIT: return "SIGQUIT";
    case SIGILL: return "SIGILL";
    case SIGTRAP: return "SIGTRAP";
    case SIGABRT: return "SIGABRT";
    case SIGFPE: return "SIGFPE";
    case SIGKILL: return "SIGKILL";
    case SIGBUS: return "SIGBUS";
    case SIGSEGV: return "SIGSEGV";
    case SIGPIPE: return "SIGPIPE";
    case SIGTERM: return "SIGTERM";
    default: return "signal";
    }
}

bool source_line(const char* file, int line, char* buf, size_t n)
{
    return g_src.line(file, line, buf, n);
}

void show_line(const td::Frame& f)
{
    char text[1024];
    if (f.has_place && f.place.file && source_line(f.place.file, f.place.line, text, sizeof(text)))
        out("%d\t%s\n", f.place.line, text);
}

void report(const td::Stop& s)
{
    td::Frame f;
    switch (s.kind) {
    case td::EV_EXITED:
        out("Program exited with code %d.\n", s.status);
        return;
    case td::EV_KILLED:
        out("Program killed by %s (%d).\n", signal_name(s.sig), s.sig);
        return;
    case td::EV_ERROR:
        out("The program is not running.\n");
        return;
    default:
        break;
    }
    g_s->describe(s.pc, false, &f);
    ttr::Str where = td::format_place(f);
    if (!f.func || (f.has_place && !f.place.exact)) {
        /* Not at the start of a line: say where exactly. */
        char h[24], b[1600];
        snprintf(b, sizeof(b), "%s in %s", td::hex(s.pc, h), where.c_str());
        where = b;
    }
    if (s.kind == td::EV_BREAK && s.bp)
        out("Breakpoint %d, %s\n", s.bp, where.c_str());
    else if (s.kind == td::EV_SIGNAL)
        out("Program received %s (%d) in %s\n", signal_name(s.sig), s.sig, where.c_str());
    else
        out("%s\n", where.c_str());
    show_line(f);
}

void cmd_bt()
{
    ttr::Vector<td::Frame> frames;
    int n = g_s->backtrace(&frames, 64);
    int i;
    for (i = 0; i < n; i++) {
        ttr::Str w = td::format_place(frames[i]);
        char h[24];
        out("#%d  %s in %s\n", i, td::hex(frames[i].pc, h), w.c_str());
    }
    if (!n)
        out("No stack.\n");
}

void cmd_regs()
{
    td::Regs r;
    if (!g_s->regs(&r)) {
        out("The program is not running.\n");
        return;
    }
    const td::Arch* a = g_s->target()->arch();
    int i;
    for (i = 0; i < a->nregs; i++) {
        const char* n = a->reg_name(i);
        char h[24];
        if (n && r.valid[i])
            out("%-5s %s\n", n, td::hex(r.r[i], h));
    }
    char h[24];
    out("pc    %s\n", td::hex(r.pc, h));
}

void cmd_x(const char* args)
{
    const char* end = 0;
    td::Addr a = td::parse_number(args, &end);
    int n = end && *end ? atoi(end) : 16;
    if (n <= 0 || n > 4096)
        n = 16;
    unsigned char buf[4096];
    if (!g_s->target() || !g_s->target()->read((td::Addr)a, buf, (size_t)n)) {
        char h[24];
        out("Cannot read %s.\n", td::hex(a, h));
        return;
    }
    int i;
    for (i = 0; i < n; i += 16) {
        char h[24];
        out("%s:", td::hex(a + (td::Addr)i, h));
        int k;
        for (k = i; k < n && k < i + 16; k++)
            out(" %02x", buf[k]);
        out("\n");
    }
}

void cmd_list()
{
    td::Regs r;
    td::Frame f;
    if (!g_s->regs(&r) || !g_s->describe(r.pc, false, &f) || !f.has_place || !f.place.file) {
        out("No source here.\n");
        return;
    }
    ttr::Str path;
    if (!g_src.find(f.place.file, &path)) {
        out("%s:%d: the source file is not here (dir DIR says where to look).\n",
            f.place.file, f.place.line);
        return;
    }
    int from = f.place.line - 4 < 1 ? 1 : f.place.line - 4;
    int line;
    for (line = from; line < from + 9; line++) {
        char text[1024];
        if (!source_line(f.place.file, line, text, sizeof(text)))
            break;
        out("%s%d\t%s\n", line == f.place.line ? "=>" : "  ", line, text);
    }
}

void cmd_modules()
{
    int i;
    for (i = 0; i < g_s->module_count(); i++) {
        const td::Module* m = g_s->module(i);
        char h[24], l[24];
        out("%s-%s  %s%s\n", td::hex(m->elf().load_low() + m->bias(), l),
            td::hex(m->elf().load_high() + m->bias(), h), m->path(),
            m->has_lines() ? "  (lines)" : "");
    }
}

void cmd_breakpoints()
{
    const ttr::Vector<td::Breakpoint>& b = g_s->breakpoints();
    int i, last = 0;
    for (i = 0; i < b.size(); i++) {
        if (b[i].id == last || b[i].id <= 0)
            continue;
        last = b[i].id;
        if (!b[i].addr) {
            out("%d  %s (pending)\n", b[i].id, b[i].where.c_str());
            continue;
        }
        td::Frame f;
        g_s->describe(b[i].addr, false, &f);
        ttr::Str w = td::format_place(f);
        char h[24];
        out("%d  %s at %s: %s\n", b[i].id, b[i].where.c_str(), td::hex(b[i].addr, h),
            w.c_str());
    }
    if (!last)
        out("No breakpoints.\n");
}

bool start_program()
{
    delete g_s;
    g_s = new td::Session;
    ttr::Str err;
    if (!g_s->start(g_argv, &err)) {
        out("Cannot start %s: %s\n", g_argv[0], err.c_str());
        return false;
    }
    return true;
}

/* One command. Returns false for quit. */
bool command(char* line)
{
    while (*line && isspace((unsigned char)*line))
        line++;
    size_t l = strlen(line);
    while (l && isspace((unsigned char)line[l - 1]))
        line[--l] = 0;
    if (!*line)
        return true;
    char* args = line;
    while (*args && !isspace((unsigned char)*args))
        args++;
    if (*args) {
        *args++ = 0;
        while (*args && isspace((unsigned char)*args))
            args++;
    }
    const char* c = line;
    if (!strcmp(c, "q") || !strcmp(c, "quit"))
        return false;
    if (!strcmp(c, "r") || !strcmp(c, "run")) {
        if (!g_s->running()) {
            /* Again from the start, keeping the breakpoints. */
            ttr::Vector<ttr::Str> specs;
            const ttr::Vector<td::Breakpoint>& b = g_s->breakpoints();
            int i, last = 0;
            for (i = 0; i < b.size(); i++)
                if (b[i].id > 0 && b[i].id != last) {
                    specs.push_back(b[i].where);
                    last = b[i].id;
                }
            if (!g_argv || !start_program())
                return true;
            ttr::Str err;
            for (i = 0; i < specs.size(); i++)
                g_s->add_breakpoint(specs[i].c_str(), &err);
        }
        report(g_s->cont());
    } else if (!strcmp(c, "c") || !strcmp(c, "continue")) {
        report(g_s->cont());
    } else if (!strcmp(c, "s") || !strcmp(c, "step")) {
        report(g_s->step_line(false));
    } else if (!strcmp(c, "n") || !strcmp(c, "next")) {
        report(g_s->step_line(true));
    } else if (!strcmp(c, "si")) {
        report(g_s->step_instruction());
    } else if (!strcmp(c, "f") || !strcmp(c, "finish")) {
        report(g_s->finish());
    } else if (!strcmp(c, "b") || !strcmp(c, "break")) {
        if (!*args) {
            cmd_breakpoints();
        } else {
            ttr::Str err;
            bool pending = false;
            int id = g_s->add_breakpoint(args, &err, &pending);
            if (!id) {
                out("Cannot set a breakpoint at %s: %s\n", args, err.c_str());
            } else if (pending) {
                out("Breakpoint %d (pending): %s, when the shared objects load\n", id, args);
            } else {
                const ttr::Vector<td::Breakpoint>& b = g_s->breakpoints();
                td::Frame f;
                g_s->describe(b[b.size() - 1].addr, false, &f);
                ttr::Str w = td::format_place(f);
                char h[24];
                out("Breakpoint %d at %s: %s\n", id, td::hex(b[b.size() - 1].addr, h),
                    w.c_str());
            }
        }
    } else if (!strcmp(c, "d") || !strcmp(c, "delete")) {
        if (!g_s->remove_breakpoint(atoi(args)))
            out("No breakpoint %s.\n", args);
    } else if (!strcmp(c, "bt") || !strcmp(c, "backtrace")) {
        cmd_bt();
    } else if (!strcmp(c, "dir")) {
        if (*args) {
            g_src.add_dir(args);
        } else {
            int i;
            for (i = 0; i < g_src.dir_count(); i++)
                out("%s\n", g_src.dir(i));
            if (!g_src.dir_count())
                out("Sources are read where the compiler recorded them.\n");
        }
    } else if (!strcmp(c, "mods")) {
        cmd_modules();
    } else if (!strcmp(c, "regs")) {
        cmd_regs();
    } else if (!strcmp(c, "x")) {
        cmd_x(args);
    } else if (!strcmp(c, "l") || !strcmp(c, "list")) {
        cmd_list();
    } else if (!strcmp(c, "detach")) {
        g_s->detach();
        out("Detached.\n");
    } else if (!strcmp(c, "k") || !strcmp(c, "kill")) {
        g_s->kill();
        out("Killed.\n");
    } else if (!strcmp(c, "where")) {
        td::Regs r;
        td::Frame f;
        if (g_s->regs(&r) && g_s->describe(r.pc, false, &f)) {
            ttr::Str w = td::format_place(f);
            out("%s\n", w.c_str());
            show_line(f);
        } else {
            out("The program is not running.\n");
        }
    } else if (!strcmp(c, "help") || !strcmp(c, "h")) {
        out("r run, c continue, s step, n next, si step instruction, f finish,\n"
            "b SPEC (file:line, function, *addr), b list, d N delete,\n"
            "bt backtrace, where, l list, regs, x ADDR [N] memory, mods modules,\n"
            "dir DIR source directory, k kill, detach, q quit\n");
    } else {
        out("Unknown command \"%s\" (help lists them).\n", c);
    }
    return true;
}

void usage()
{
    fprintf(stderr, "usage: ttrdb [-x script] program [args...]\n"
                    "       ttrdb -p pid\n");
    exit(2);
}

} /* namespace */

int main(int argc, char** argv)
{
    const char* script = 0;
    int pid = 0;
    int i = 1;
    while (i < argc && argv[i][0] == '-') {
        if (!strcmp(argv[i], "-x") && i + 1 < argc) {
            script = argv[i + 1];
            i += 2;
        } else if (!strcmp(argv[i], "-p") && i + 1 < argc) {
            pid = atoi(argv[i + 1]);
            i += 2;
        } else if (!strcmp(argv[i], "--version")) {
            printf("ttrdb (ttr). DWARF through libdwarf "
                   "(https://github.com/davea42/libdwarf-code), LGPL 2.1.\n");
            return 0;
        } else {
            usage();
        }
    }
    if (!pid && i >= argc)
        usage();
    setvbuf(stdout, 0, _IOLBF, 0);

    g_s = new td::Session;
    ttr::Str err;
    if (pid) {
        if (!g_s->attach(pid, &err)) {
            fprintf(stderr, "ttrdb: cannot attach to %d: %s\n", pid, err.c_str());
            return 1;
        }
    } else {
        g_argv = argv + i;
        if (!g_s->start(g_argv, &err)) {
            fprintf(stderr, "ttrdb: cannot start %s: %s\n", g_argv[0], err.c_str());
            return 1;
        }
    }

    FILE* in = stdin;
    if (script) {
        in = fopen(script, "r");
        if (!in) {
            fprintf(stderr, "ttrdb: cannot open %s\n", script);
            return 1;
        }
        g_script = true;
    }
    bool prompt = !script && isatty(0);
    int failed = 0;
    char line[2048];
    for (;;) {
        if (prompt) {
            fputs("(ttrdb) ", stdout);
            fflush(stdout);
        }
        if (!fgets(line, sizeof(line), in))
            break;
        if (g_script) {
            size_t l = strlen(line);
            while (l && (line[l - 1] == '\n' || line[l - 1] == '\r'))
                line[--l] = 0;
            if (!line[0] || line[0] == '#')
                continue;
            bool expect = !strncmp(line, "expect ", 7), absent = !strncmp(line, "absent ", 7);
            if (expect || absent) {
                bool there = g_out && strstr(g_out, line + 7) != 0;
                if (there != expect) {
                    printf("FAIL: %s\n", line);
                    failed++;
                } else {
                    printf("ok: %s\n", line);
                }
                continue;
            }
            printf("(ttrdb) %s\n", line);
        }
        g_out_len = 0;
        if (g_out)
            g_out[0] = 0;
        if (!command(line))
            break;
    }
    if (g_s->running())
        g_s->kill();
    delete g_s;
    if (g_script)
        printf("ttrdb: %d failed\n", failed);
    return failed > 255 ? 255 : failed;
}
