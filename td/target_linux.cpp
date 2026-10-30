/*
 * td/target_linux.cpp - Target on Linux x86-64 through ptrace and /proc
 * (td/DESIGN.md section 3.1).
 */
#include "target.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/personality.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

namespace td {

namespace {

class LinuxTarget : public Target {
public:
    LinuxTarget() : pid_(-1), mem_(-1), alive_(false), attached_(false) {}
    ~LinuxTarget();

    bool launch(char* const* argv, const LaunchOptions& o, ttr::Str* err);
    bool attach(int pid, ttr::Str* err);
    const Arch* arch() const { return arch_x86_64(); }
    int pid() const { return pid_; }
    ttr::Str exe_path();
    Addr exe_bias(Addr elf_entry);
    bool shared_objects(Addr dynamic, ttr::Vector<SharedObject>* out);
    bool loader_hook(Addr dynamic, SharedObject* loader, Addr* addr, ttr::Str* symbol);
    bool resume(bool step, int sig);
    bool wait(Event* ev);
    bool poll(Event* ev, bool* got);
    bool interrupt();
    void kill();
    void detach();
    bool get_regs(Regs* r);
    bool set_pc(Addr pc);
    bool read(Addr a, void* buf, size_t n);
    bool write(Addr a, const void* buf, size_t n);

private:
    bool open_mem();
    void gone();
    bool decode(int st, Event* ev);

    pid_t pid_;
    int mem_;           /* /proc/<pid>/mem */
    bool alive_;
    bool attached_;     /* attached rather than started: detach on close */
};

void set_err(ttr::Str* err, const char* what)
{
    if (!err)
        return;
    char m[300];
    snprintf(m, sizeof(m), "%s: %s", what, strerror(errno));
    *err = m;
}

LinuxTarget::~LinuxTarget()
{
    if (alive_) {
        if (attached_)
            detach();
        else
            kill();
    }
    if (mem_ >= 0)
        close(mem_);
}

bool LinuxTarget::open_mem()
{
    char p[64];
    snprintf(p, sizeof(p), "/proc/%d/mem", (int)pid_);
    mem_ = open(p, O_RDWR);
    return mem_ >= 0;
}

bool LinuxTarget::launch(char* const* argv, const LaunchOptions& o, ttr::Str* err)
{
    pid_t p = fork();
    if (p < 0) {
        set_err(err, "fork");
        return false;
    }
    if (p == 0) {
        ptrace(PTRACE_TRACEME, 0, 0, 0);
        setup_child(o);
        /* The same addresses every run, as gdb does. */
        personality(ADDR_NO_RANDOMIZE);
        execvp(argv[0], argv);
        fprintf(stderr, "ttrdb: cannot run %s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    pid_ = p;
    int st = 0;
    if (waitpid(pid_, &st, 0) != pid_ || !WIFSTOPPED(st)) {
        if (err)
            *err = WIFEXITED(st) ? "the program did not start" : "waitpid failed";
        pid_ = -1;
        return false;
    }
    /* Stopped after exec. The child dies with us. */
    ptrace(PTRACE_SETOPTIONS, pid_, 0, (void*)(long)PTRACE_O_EXITKILL);
    alive_ = true;
    if (!open_mem()) {
        set_err(err, "/proc/<pid>/mem");
        return false;
    }
    return true;
}

bool LinuxTarget::attach(int pid, ttr::Str* err)
{
    if (ptrace(PTRACE_SEIZE, pid, 0, 0) != 0) {
        set_err(err, errno == EPERM
                         ? "attach (see /proc/sys/kernel/yama/ptrace_scope)"
                         : "attach");
        return false;
    }
    pid_ = pid;
    alive_ = true;
    attached_ = true;
    ptrace(PTRACE_INTERRUPT, pid_, 0, 0);
    int st = 0;
    if (waitpid(pid_, &st, 0) != pid_) {
        set_err(err, "waitpid");
        return false;
    }
    if (!open_mem()) {
        set_err(err, "/proc/<pid>/mem");
        return false;
    }
    return true;
}

ttr::Str LinuxTarget::exe_path()
{
    char p[64], buf[4096];
    snprintf(p, sizeof(p), "/proc/%d/exe", (int)pid_);
    ssize_t n = readlink(p, buf, sizeof(buf) - 1);
    if (n <= 0)
        return ttr::Str("");
    buf[n] = 0;
    return ttr::Str(buf);
}

/* AT_ENTRY in the auxiliary vector is where the kernel put the entry
 * point; the difference to the file's is the load bias. */
Addr LinuxTarget::exe_bias(Addr elf_entry)
{
    char p[64];
    snprintf(p, sizeof(p), "/proc/%d/auxv", (int)pid_);
    int fd = open(p, O_RDONLY);
    if (fd < 0)
        return 0;
    Elf64_auxv_t av;
    Addr bias = 0;
    while (::read(fd, &av, sizeof(av)) == (ssize_t)sizeof(av) && av.a_type != AT_NULL)
        if (av.a_type == AT_ENTRY)
            bias = (Addr)av.a_un.a_val - elf_entry;
    close(fd);
    return bias;
}

/* glibc's list: DT_DEBUG points to struct r_debug once ld.so has started;
 * its r_map is the link_map list. x86-64 offsets: r_debug.r_map 8;
 * link_map l_addr 0, l_name 8, l_next 24. */
bool LinuxTarget::shared_objects(Addr dynamic, ttr::Vector<SharedObject>* out)
{
    uint64_t rdebug = 0, map = 0;
    if (!dynamic || !dynamic_entry(dynamic, 21 /* DT_DEBUG */, &rdebug) || !rdebug)
        return true;        /* not started yet, or linked statically */
    if (!read_uint(rdebug + 8, 8, &map))
        return false;
    int n;
    for (n = 0; map && n < 4096; n++) {
        uint64_t addr = 0, name = 0, next = 0;
        if (!read_uint(map, 8, &addr) || !read_uint(map + 8, 8, &name) ||
            !read_uint(map + 24, 8, &next))
            return false;
        char path[4096];
        /* The executable has an empty name; the vDSO has no file. */
        if (name && read_string(name, path, sizeof(path)) && path[0] == '/' &&
            access(path, R_OK) == 0) {
            SharedObject so;
            so.path = path;
            so.bias = addr;
            out->push_back(so);
        }
        map = next;
    }
    return true;
}

/* r_debug.r_brk (offset 16): ld.so calls it before and after each change
 * to the list. ld.so is on the list itself. */
bool LinuxTarget::loader_hook(Addr dynamic, SharedObject*, Addr* addr, ttr::Str*)
{
    uint64_t rdebug = 0, brk = 0;
    if (!dynamic || !dynamic_entry(dynamic, 21 /* DT_DEBUG */, &rdebug) || !rdebug ||
        !read_uint(rdebug + 16, 8, &brk) || !brk)
        return false;
    *addr = brk;
    return true;
}

bool LinuxTarget::resume(bool step, int sig)
{
    if (!alive_)
        return false;
    return ptrace(step ? PTRACE_SINGLESTEP : PTRACE_CONT, pid_, 0, (void*)(long)sig) == 0;
}

void LinuxTarget::gone()
{
    alive_ = false;
    if (mem_ >= 0)
        close(mem_);
    mem_ = -1;
}

bool LinuxTarget::wait(Event* ev)
{
    memset(ev, 0, sizeof(*ev));
    int st = 0;
    pid_t r;
    do
        r = waitpid(pid_, &st, __WALL);
    while (r < 0 && errno == EINTR);
    if (r != pid_) {
        ev->kind = EV_ERROR;
        gone();
        return false;
    }
    return decode(st, ev);
}

bool LinuxTarget::poll(Event* ev, bool* got)
{
    memset(ev, 0, sizeof(*ev));
    *got = false;
    if (!alive_) {
        ev->kind = EV_ERROR;
        *got = true;
        return false;
    }
    int st = 0;
    pid_t r = waitpid(pid_, &st, __WALL | WNOHANG);
    if (r == 0 || (r < 0 && errno == EINTR))
        return true;
    *got = true;
    if (r != pid_) {
        ev->kind = EV_ERROR;
        gone();
        return false;
    }
    return decode(st, ev);
}

bool LinuxTarget::interrupt()
{
    return alive_ && ::kill(pid_, SIGSTOP) == 0;
}

bool LinuxTarget::decode(int st, Event* ev)
{
    if (WIFEXITED(st)) {
        ev->kind = EV_EXITED;
        ev->status = WEXITSTATUS(st);
        gone();
        return true;
    }
    if (WIFSIGNALED(st)) {
        ev->kind = EV_KILLED;
        ev->sig = WTERMSIG(st);
        gone();
        return true;
    }
    int sig = WSTOPSIG(st);
    Regs regs;
    get_regs(&regs);
    ev->pc = regs.pc;
    if (sig != SIGTRAP) {
        ev->kind = EV_SIGNAL;
        ev->sig = sig;
        return true;
    }
    siginfo_t si;
    memset(&si, 0, sizeof(si));
    ptrace(PTRACE_GETSIGINFO, pid_, 0, &si);
    if (si.si_code == TRAP_TRACE) {
        ev->kind = EV_STEP;
    } else if (si.si_code == SI_KERNEL || si.si_code == TRAP_BRKPT) {
        /* int3 leaves the pc after itself. */
        ev->kind = EV_BREAK;
        ev->pc = regs.pc - (Addr)arch()->pc_after_bp;
        set_pc(ev->pc);
    } else {
        ev->kind = EV_SIGNAL;
        ev->sig = sig;
    }
    return true;
}

void LinuxTarget::kill()
{
    if (!alive_)
        return;
    ::kill(pid_, SIGKILL);
    int st;
    waitpid(pid_, &st, __WALL);
    gone();
}

void LinuxTarget::detach()
{
    if (!alive_)
        return;
    ptrace(PTRACE_DETACH, pid_, 0, 0);
    gone();
}

bool LinuxTarget::get_regs(Regs* r)
{
    r->clear();
    struct user_regs_struct u;
    if (ptrace(PTRACE_GETREGS, pid_, 0, &u) != 0)
        return false;
    /* DWARF order: rax rdx rcx rbx rsi rdi rbp rsp r8-r15 rip. */
    r->set(0, u.rax);
    r->set(1, u.rdx);
    r->set(2, u.rcx);
    r->set(3, u.rbx);
    r->set(4, u.rsi);
    r->set(5, u.rdi);
    r->set(6, u.rbp);
    r->set(7, u.rsp);
    r->set(8, u.r8);
    r->set(9, u.r9);
    r->set(10, u.r10);
    r->set(11, u.r11);
    r->set(12, u.r12);
    r->set(13, u.r13);
    r->set(14, u.r14);
    r->set(15, u.r15);
    r->set(16, u.rip);
    r->pc = u.rip;
    return true;
}

bool LinuxTarget::set_pc(Addr pc)
{
    struct user_regs_struct u;
    if (ptrace(PTRACE_GETREGS, pid_, 0, &u) != 0)
        return false;
    u.rip = pc;
    return ptrace(PTRACE_SETREGS, pid_, 0, &u) == 0;
}

bool LinuxTarget::read(Addr a, void* buf, size_t n)
{
    if (mem_ < 0)
        return false;
    return pread(mem_, buf, n, (off_t)a) == (ssize_t)n;
}

bool LinuxTarget::write(Addr a, const void* buf, size_t n)
{
    if (mem_ < 0)
        return false;
    /* /proc/<pid>/mem writes into read-only text too. */
    return pwrite(mem_, buf, n, (off_t)a) == (ssize_t)n;
}

} /* namespace */

Target* Target::create()
{
    return new LinuxTarget;
}

} /* namespace td */
