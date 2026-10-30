/*
 * ttrdb - td's command-line front end, for trying td out and for tests
 * (td/DESIGN.md section 8.1).
 *
 *   ttrdb program [args...]      start it, stopped at its first instruction
 *   ttrdb -p PID                 attach to a running process
 *   ttrdb -c CORE [program]      a core file (Linux's name their program)
 *   ttrdb -x SCRIPT program ...  run the commands in SCRIPT (a test)
 *   -d DIR                       look for sources under DIR too (as "dir")
 *
 * In a script, "expect TEXT" checks that the previous command printed
 * TEXT, "absent TEXT" that it did not; "#" starts a comment. ttrdb then
 * exits with the number of failed checks.
 */
#include "../../td/core.h"
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
int g_frame;            /* "fr N": the frame p and locals look in */
char** g_argv;          /* the program's, for "r" after it ended */
int g_last_thread = -1; /* the thread of the last stop reported */
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

void report(const td::Stop& s, bool core = false)
{
    td::Frame f;
    g_frame = 0;
    switch (s.kind) {
    case td::EV_EXITED:
        out("Program exited with code %d.\n", s.status);
        return;
    case td::EV_KILLED:
        out("Program killed by %s (%d).\n", signal_name(s.sig), s.sig);
        return;
    case td::EV_ERROR:
        out(g_s->is_core() ? "A core file cannot run.\n" : "The program is not running.\n");
        return;
    case td::EV_WATCH_ENDED:
        out("Watchpoint %d ended: its frame returned.\n", s.watch);
        break;
    default:
        break;
    }
    /* Which thread, when there are several and it is another one. */
    ttr::Vector<int> ids;
    if (s.thread != g_last_thread && g_s->threads(&ids) > 1)
        out("[Thread %d (%d)]\n", g_s->thread_number(s.thread), s.thread);
    g_last_thread = s.thread;
    g_s->describe(s.pc, false, &f);
    ttr::Str where = td::format_place(f);
    if (!f.func || (f.has_place && !f.place.exact)) {
        /* Not at the start of a line: say where exactly. */
        char h[24], b[1600];
        snprintf(b, sizeof(b), "%s in %s", td::hex(s.pc, h), where.c_str());
        where = b;
    }
    if (s.kind == td::EV_BREAK && s.bp) {
        out("Breakpoint %d, %s\n", s.bp, where.c_str());
    } else if (s.kind == td::EV_WATCH) {
        td::Value before, now;
        int i;
        const ttr::Vector<td::Watchpoint>& w = g_s->watchpoints();
        for (i = 0; i < w.size() && w[i].id != s.watch; i++)
            ;
        out("Watchpoint %d: %s\n", s.watch, i < w.size() ? w[i].expr.c_str() : "");
        if (g_s->watch_values(s.watch, &before, &now)) {
            out("Old value = %s\n", g_s->format(before).c_str());
            out("New value = %s\n", g_s->format(now).c_str());
        }
        out("%s\n", where.c_str());
    } else if (s.kind == td::EV_SIGNAL && core) {
        out("Core file of %s: %s (%d) in %s\n", g_s->core()->program_name(),
            signal_name(s.sig), s.sig, where.c_str());
    } else if (s.kind == td::EV_SIGNAL)
        out("Program received %s (%d) in %s\n", signal_name(s.sig), s.sig, where.c_str());
    else
        out("%s\n", where.c_str());
    show_line(f);
}

/* "t": the threads, where each is; "t N": switch to thread N. */
void cmd_threads(const char* args)
{
    ttr::Vector<int> ids;
    if (!g_s->threads(&ids)) {
        out("The program is not running.\n");
        return;
    }
    int cur = g_s->current_thread(), i;
    if (*args) {
        int n = atoi(args);
        for (i = 0; i < ids.size() && g_s->thread_number(ids[i]) != n; i++)
            ;
        if (i == ids.size() || !g_s->select_thread(ids[i])) {
            out("No thread %s.\n", args);
            return;
        }
        g_frame = 0;
        g_last_thread = ids[i];
        td::Regs r;
        td::Frame f;
        out("[Thread %d (%d)]\n", n, ids[i]);
        if (g_s->regs(&r) && g_s->describe(r.pc, false, &f)) {
            ttr::Str w = td::format_place(f);
            out("%s\n", w.c_str());
            show_line(f);
        }
        return;
    }
    for (i = 0; i < ids.size(); i++) {
        td::Regs r;
        td::Frame f;
        ttr::Str w("??");
        if (g_s->select_thread(ids[i]) && g_s->regs(&r) && g_s->describe(r.pc, false, &f))
            w = td::format_place(f);
        out("%c %d  Thread %d  %s\n", ids[i] == cur ? '*' : ' ', g_s->thread_number(ids[i]),
            ids[i], w.c_str());
    }
    g_s->select_thread(cur);
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

/* The frame chosen with "fr" (0 after every run). */
bool current_frame(td::Frame* f)
{
    ttr::Vector<td::Frame> frames;
    int n = g_s->backtrace(&frames, g_frame + 1);
    if (n <= 0) {
        out("The program is not running.\n");
        return false;
    }
    if (g_frame >= n)
        g_frame = n - 1;
    *f = frames[g_frame];
    return true;
}

void cmd_frame(const char* args)
{
    if (*args)
        g_frame = atoi(args);
    td::Frame f;
    if (!current_frame(&f))
        return;
    ttr::Str w = td::format_place(f);
    out("#%d  %s\n", g_frame, w.c_str());
    show_line(f);
}

/* "w EXPR": stop when it changes. */
void cmd_watch(const char* args)
{
    td::Frame f;
    if (!*args) {
        out("w EXPR\n");
        return;
    }
    if (!current_frame(&f))
        return;
    ttr::Str err;
    int id = g_s->add_watch(f, args, &err);
    if (!id) {
        out("Cannot watch %s: %s\n", args, err.c_str());
        return;
    }
    const td::Watchpoint& w = g_s->watchpoints()[g_s->watchpoints().size() - 1];
    char h[24];
    out("Watchpoint %d: %s (%d bytes at %s%s)\n", id, args, (int)w.size, td::hex(w.addr, h),
        w.scope_sp ? ", until its frame returns" : "");
}

void print_value(const td::Value& v, const char* prefix)
{
    ttr::Str t = td::type_name(v.type);
    ttr::Str text = g_s->format(v);
    out("%s%s %s = %s\n", prefix, t.c_str(), v.name.c_str(), text.c_str());
}

void cmd_print(const char* args)
{
    td::Frame f;
    if (!*args) {
        out("p EXPR\n");
        return;
    }
    if (!current_frame(&f))
        return;
    td::Value v = g_s->evaluate(f, args);
    out("%s = %s\n", v.name.c_str(), g_s->format(v).c_str());
}

void cmd_locals()
{
    td::Frame f;
    if (!current_frame(&f))
        return;
    ttr::Vector<td::Value> vs;
    if (!g_s->locals(f, &vs))
        out("No locals.\n");
    int i;
    for (i = 0; i < vs.size(); i++)
        print_value(vs[i], "");
}

/* "e EXPR": its members, elements or what it points to. */
void cmd_expand(const char* args)
{
    td::Frame f;
    if (!current_frame(&f))
        return;
    td::Value v = g_s->evaluate(f, args);
    out("%s = %s\n", v.name.c_str(), g_s->format(v).c_str());
    ttr::Vector<td::Value> kids;
    g_s->children(v, &kids, 100);
    int i;
    for (i = 0; i < kids.size(); i++) {
        ttr::Str t = td::type_name(kids[i].type);
        out("  %s %s = %s   (%s)\n", t.c_str(), kids[i].name.c_str(),
            g_s->format(kids[i]).c_str(), kids[i].expr.c_str());
    }
}

/* "pt EXPR": its type, a struct's members. */
void cmd_ptype(const char* args)
{
    td::Frame f;
    if (!current_frame(&f))
        return;
    td::Value v = g_s->evaluate(f, args);
    if (!v.ok()) {
        out("%s\n", v.error.c_str());
        return;
    }
    ttr::Str t = td::type_name(v.type);
    out("type = %s\n", t.c_str());
    const td::Type* s = td::strip(v.type);
    while (s && (s->kind == td::TK_POINTER || s->kind == td::TK_ARRAY))
        s = td::strip(s->target);
    if (s && (s->kind == td::TK_STRUCT || s->kind == td::TK_UNION || s->kind == td::TK_CLASS)) {
        int i;
        for (i = 0; i < s->nmembers; i++) {
            const td::Member& m = s->members[i];
            ttr::Str mt = td::type_name(m.type);
            if (m.bit_size)
                out("  %s %s : %d;  /* bit %d */\n", mt.c_str(), m.name ? m.name : "",
                    m.bit_size, (int)m.bit_offset);
            else
                out("  %s %s;  /* offset %d */\n", mt.c_str(), m.name ? m.name : "",
                    (int)m.offset);
        }
    } else if (s && s->kind == td::TK_ENUM) {
        int i;
        for (i = 0; i < s->nenums; i++)
            out("  %s = %d\n", s->enums[i].name, (int)s->enums[i].value);
    }
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
    const ttr::Vector<td::Watchpoint>& w = g_s->watchpoints();
    for (i = 0; i < w.size(); i++) {
        char h[24];
        out("%d  watch %s (%d bytes at %s), %d hits\n", w[i].id, w[i].expr.c_str(),
            (int)w[i].size, td::hex(w[i].addr, h), w[i].hits);
    }
    if (!last && w.empty())
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
    if (g_s->is_core() && (!strcmp(c, "r") || !strcmp(c, "run") || !strcmp(c, "c") ||
                           !strcmp(c, "s") || !strcmp(c, "n") || !strcmp(c, "si") ||
                           !strcmp(c, "f") || !strcmp(c, "w"))) {
        out("A core file cannot run.\n");
        return true;
    }
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
    } else if (!strcmp(c, "w") || !strcmp(c, "watch")) {
        cmd_watch(args);
    } else if (!strcmp(c, "d") || !strcmp(c, "delete")) {
        if (!g_s->remove_breakpoint(atoi(args)))
            out("No breakpoint %s.\n", args);
    } else if (!strcmp(c, "t") || !strcmp(c, "thread")) {
        cmd_threads(args);
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
    } else if (!strcmp(c, "p") || !strcmp(c, "print")) {
        cmd_print(args);
    } else if (!strcmp(c, "locals")) {
        cmd_locals();
    } else if (!strcmp(c, "e")) {
        cmd_expand(args);
    } else if (!strcmp(c, "pt")) {
        cmd_ptype(args);
    } else if (!strcmp(c, "fr") || !strcmp(c, "frame")) {
        cmd_frame(args);
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
            "b SPEC (file:line, function, *addr), b list, w EXPR watch, d N delete,\n"
            "bt backtrace, fr N frame, t threads, t N thread, where, l list, regs,\n"
            "x ADDR [N] memory, p EXPR print (EXPR,x hex; p,N N elements), locals,\n"
            "e EXPR expand, pt EXPR type, mods modules,\n"
            "dir DIR source directory, k kill, detach, q quit\n");
    } else {
        out("Unknown command \"%s\" (help lists them).\n", c);
    }
    return true;
}

void usage()
{
    fprintf(stderr, "usage: ttrdb [-d srcdir] [-x script] program [args...]\n"
                    "       ttrdb [-d srcdir] [-x script] -p pid\n"
                    "       ttrdb [-d srcdir] [-x script] -c core [program]\n");
    exit(2);
}

} /* namespace */

int main(int argc, char** argv)
{
    const char* script = 0;
    const char* core = 0;
    int pid = 0;
    int i = 1;
    /* Line by line, also into a file: a test killed for hanging shows
     * where. */
    setvbuf(stdout, 0, _IOLBF, BUFSIZ);
    while (i < argc && argv[i][0] == '-') {
        if (!strcmp(argv[i], "-x") && i + 1 < argc) {
            script = argv[i + 1];
            i += 2;
        } else if (!strcmp(argv[i], "-d") && i + 1 < argc) {
            g_src.add_dir(argv[i + 1]);
            i += 2;
        } else if (!strcmp(argv[i], "-c") && i + 1 < argc) {
            core = argv[i + 1];
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
    if (!pid && !core && i >= argc)
        usage();
    setvbuf(stdout, 0, _IOLBF, 0);

    g_s = new td::Session;
    ttr::Str err;
    td::Stop why;
    bool show_core = false;
    if (core) {
        if (!g_s->open_core(core, i < argc ? argv[i] : 0, &err, &why)) {
            fprintf(stderr, "ttrdb: cannot read %s: %s\n", core, err.c_str());
            return 1;
        }
        show_core = true;
    } else if (pid) {
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
    if (show_core)
        report(why, true);
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
