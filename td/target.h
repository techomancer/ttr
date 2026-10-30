/*
 * td/target.h - one process under the debugger (td/DESIGN.md section 3):
 * start or attach, run, stop, registers, memory. The only part of td that
 * differs between systems: target_linux.cpp (ptrace) and target_irix.cpp
 * (/proc ioctls), one of which the Makefile links (TD_SYS).
 *
 * The prototype follows one thread, the first.
 */
#ifndef TD_TARGET_H
#define TD_TARGET_H

#include "td.h"

#include "core/str.h"
#include "core/vector.h"

namespace td {

enum EventKind {
    EV_BREAK,       /* hit a breakpoint instruction */
    EV_STEP,        /* a single step finished */
    EV_SIGNAL,      /* stopped with a signal (sig) */
    EV_EXITED,      /* exited (status) */
    EV_KILLED,      /* killed by a signal (sig) */
    EV_PAUSED,      /* stopped because the debugger asked (Session::interrupt) */
    EV_ERROR        /* the target is gone or cannot be waited for */
};

struct Event {
    EventKind kind;
    int sig;
    int status;
    Addr pc;        /* for stops: where (breakpoints: already moved back) */
};

/* A shared object the dynamic linker loaded: its file, and how far it
 * was moved from its link addresses. */
/* How to start a program. */
struct LaunchOptions {
    const char* cwd;    /* 0: the debugger's */
    const char* tty;    /* a terminal for stdin, stdout and stderr; 0: the debugger's */
    LaunchOptions() : cwd(0), tty(0) {}
};

struct SharedObject {
    ttr::Str path;
    Addr bias;
};

class Target {
public:
    /* The implementation for this system. */
    static Target* create();
    virtual ~Target() {}

    /* Start argv[0] (searched in PATH) stopped at its first instruction,
     * or attach to a running process and stop it. */
    virtual bool launch(char* const* argv, const LaunchOptions& o, ttr::Str* err) = 0;
    virtual bool attach(int pid, ttr::Str* err) = 0;

    virtual const Arch* arch() const = 0;
    virtual int pid() const = 0;
    /* The executable's file, and how far it was moved from its link
     * address (position-independent executables). */
    virtual ttr::Str exe_path() = 0;
    virtual Addr exe_bias(Addr elf_entry) = 0;
    /* The shared objects loaded now (not the executable), from the
     * dynamic linker's list (td/DESIGN.md section 4). dynamic: where the
     * executable's dynamic section is in memory. */
    virtual bool shared_objects(Addr dynamic, ttr::Vector<SharedObject>* out) = 0;
    /* Where the dynamic linker calls each time its list changes (dlopen,
     * dlclose): an address, or a symbol in the linker's own file, which
     * *loader names when it is not on the list itself. False if not
     * known (yet). */
    virtual bool loader_hook(Addr dynamic, SharedObject* loader, Addr* addr,
                             ttr::Str* symbol) = 0;

    /* Let it run, or run one instruction, passing sig (0: none). */
    virtual bool resume(bool step, int sig) = 0;
    /* Wait until it stops or ends. */
    virtual bool wait(Event* ev) = 0;
    /* The same without waiting: *got false if it is still running. */
    virtual bool poll(Event* ev, bool* got) = 0;
    /* Ask a running program to stop; wait() or poll() then report it as
     * a signal (SIGSTOP on Linux, none on IRIX). */
    virtual bool interrupt() = 0;
    virtual void kill() = 0;
    virtual void detach() = 0;

    virtual bool get_regs(Regs* r) = 0;
    virtual bool set_pc(Addr pc) = 0;
    virtual bool read(Addr a, void* buf, size_t n) = 0;
    /* Also into read-only text (breakpoints). */
    virtual bool write(Addr a, const void* buf, size_t n) = 0;

    /* An unsigned value of n bytes (1, 2, 4, 8) in the target's order. */
    bool read_uint(Addr a, int n, uint64_t* v);
    /* In the child, before exec: LaunchOptions' directory and terminal. */
    static void setup_child(const LaunchOptions& o);
    /* A NUL-terminated string, at most n - 1 bytes. */
    bool read_string(Addr a, char* buf, size_t n);
    /* A dynamic section entry's value by tag (one of a pointer size's
     * pairs), from the section in memory. */
    bool dynamic_entry(Addr dynamic, uint64_t tag, uint64_t* value);
};

} /* namespace td */

#endif
