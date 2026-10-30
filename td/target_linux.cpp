/*
 * td/target_linux.cpp - Target on Linux x86-64 through ptrace and /proc
 * (td/DESIGN.md section 3.1).
 */
#include "target.h"

#include <dirent.h>
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
#include <sys/stat.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace td {

namespace {

class LinuxTarget : public Target {
public:
    LinuxTarget()
        : pid_(-1), mem_(-1), alive_(false), attached_(false), cur_(-1), all_running_(false),
          interrupting_(false), next_watch_(1)
    {
        memset(slots_, 0, sizeof(slots_));
    }
    ~LinuxTarget();

    bool launch(char* const* argv, const LaunchOptions& o, ttr::Str* err);
    bool attach(int pid, ttr::Str* err);
    const Arch* arch() const { return arch_x86_64(); }
    int pid() const { return pid_; }
    ttr::Str exe_path();
    Addr exe_bias(Addr elf_entry);
    bool shared_objects(Addr dynamic, ttr::Vector<SharedObject>* out);
    bool loader_hook(Addr dynamic, SharedObject* loader, Addr* addr, ttr::Str* symbol);
    int threads(ttr::Vector<int>* ids);
    int current_thread() const { return cur_; }
    bool select_thread(int id);
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
    int add_watch(Addr a, size_t n, ttr::Str* err);
    void remove_watch(int handle);

private:
    /* One traced thread. All of them are stopped whenever the debugger
     * looks at the program. */
    struct Thread {
        pid_t tid;
        bool stopped;       /* in a ptrace stop */
        bool starting;      /* new: its first stop not seen yet */
        bool exiting;       /* past PTRACE_EVENT_EXIT: on its way out */
        bool has_pending;   /* stopped for something of its own while the */
        int pending;        /* others were being stopped: reported later */
        int sig;            /* a signal it gets when it runs again */
    };

    bool open_mem(pid_t tid);
    bool set_debugreg(pid_t tid, int i, unsigned long v);
    bool write_dr7(pid_t tid);
    void set_watches(pid_t tid);
    void gone();
    Thread* find(pid_t tid);
    Thread* add(pid_t tid, bool starting);
    void drop(pid_t tid);
    bool ours(pid_t tid);
    int next_status(bool block, pid_t* tid, int* st);
    int next(bool block, Event* ev);
    bool run_thread(Thread* t, bool step, int sig);
    void stop_others(pid_t except);
    bool trap_was_breakpoint(pid_t tid);
    void decode(Thread* t, int st, Event* ev);

    pid_t pid_;
    int mem_;           /* /proc/<pid>/mem */
    bool alive_;
    bool attached_;     /* attached rather than started: detach on close */
    ttr::Vector<Thread> threads_;
    pid_t cur_;
    bool all_running_;  /* resumed all; else stepping cur_ alone */
    bool interrupting_; /* interrupt() asked: the next PTRACE_EVENT_STOP is its */
    /* The debug registers DR0 to DR3: each watches 1, 2, 4 or 8 bytes,
     * aligned to their size. An area takes one or more of them. Every
     * thread has its own: each gets the same. */
    struct Slot {
        Addr addr;
        int len;
        int handle;     /* 0: free */
    };
    Slot slots_[4];
    int next_watch_;
};

/* ptrace's options for every thread: new threads traced too (the same
 * way, PTRACE_SEIZE), their ends seen, and the program killed if the
 * debugger dies. */
const long k_options = PTRACE_O_EXITKILL | PTRACE_O_TRACECLONE | PTRACE_O_TRACEEXIT;

void set_err(ttr::Str* err, const char* what)
{
    if (!err)
        return;
    char m[300];
    snprintf(m, sizeof(m), "%s: %s", what, strerror(errno));
    *err = m;
}

int event_of(int st)
{
    return (st >> 16) & 0xff;
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

/* Any thread's mem file is the process's memory; the first thread's goes
 * away if it ends before the others. */
bool LinuxTarget::open_mem(pid_t tid)
{
    char p[64];
    if (mem_ >= 0)
        close(mem_);
    snprintf(p, sizeof(p), "/proc/%d/task/%d/mem", (int)pid_, (int)tid);
    mem_ = open(p, O_RDWR);
    return mem_ >= 0;
}

LinuxTarget::Thread* LinuxTarget::find(pid_t tid)
{
    int i;
    for (i = 0; i < threads_.size(); i++)
        if (threads_[i].tid == tid)
            return &threads_[i];
    return 0;
}

LinuxTarget::Thread* LinuxTarget::add(pid_t tid, bool starting)
{
    Thread t;
    t.tid = tid;
    t.stopped = false;
    t.starting = starting;
    t.exiting = false;
    t.has_pending = false;
    t.pending = 0;
    t.sig = 0;
    threads_.push_back(t);
    return &threads_[threads_.size() - 1];
}

void LinuxTarget::drop(pid_t tid)
{
    int i;
    for (i = 0; i < threads_.size(); i++)
        if (threads_[i].tid == tid) {
            threads_.remove_at(i);
            break;
        }
    if (cur_ == tid)
        cur_ = threads_.empty() ? pid_ : threads_[0].tid;
}

/* A thread of the program we have not heard of yet (its first stop came
 * before its creator's PTRACE_EVENT_CLONE). */
bool LinuxTarget::ours(pid_t tid)
{
    char p[64], line[128];
    snprintf(p, sizeof(p), "/proc/%d/status", (int)tid);
    FILE* f = fopen(p, "r");
    if (!f)
        return false;
    bool yes = false;
    while (fgets(line, sizeof(line), f))
        if (!strncmp(line, "Tgid:", 5)) {
            yes = atoi(line + 5) == (int)pid_;
            break;
        }
    fclose(f);
    return yes;
}

bool LinuxTarget::launch(char* const* argv, const LaunchOptions& o, ttr::Str* err)
{
    /* The child waits on a pipe until it is traced (PTRACE_SEIZE, which
     * PTRACE_INTERRUPT and following threads need), then execs: the
     * exec's stop is at the program's first instruction. */
    int p[2];
    if (pipe(p) != 0) {
        set_err(err, "pipe");
        return false;
    }
    pid_t c = fork();
    if (c < 0) {
        set_err(err, "fork");
        close(p[0]);
        close(p[1]);
        return false;
    }
    if (c == 0) {
        char x;
        close(p[1]);
        if (::read(p[0], &x, 1) != 1)
            _exit(127);
        close(p[0]);
        setup_child(o);
        /* The same addresses every run, as gdb does. */
        personality(ADDR_NO_RANDOMIZE);
        execvp(argv[0], argv);
        fprintf(stderr, "ttrdb: cannot run %s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    close(p[0]);
    pid_ = c;
    if (ptrace(PTRACE_SEIZE, c, 0, (void*)(k_options | PTRACE_O_TRACEEXEC)) != 0) {
        set_err(err, "ptrace");
        close(p[1]);
        ::kill(c, SIGKILL);
        waitpid(c, 0, 0);
        pid_ = -1;
        return false;
    }
    if (::write(p[1], "x", 1) != 1) {
        set_err(err, "starting the program");
        close(p[1]);
        return false;
    }
    close(p[1]);
    int st = 0;
    for (;;) {
        if (waitpid(c, &st, __WALL) != c) {
            if (errno == EINTR)
                continue;
            set_err(err, "waitpid");
            pid_ = -1;
            return false;
        }
        if (!WIFSTOPPED(st)) {
            if (err)
                *err = "the program did not start";
            pid_ = -1;
            return false;
        }
        if (event_of(st) == PTRACE_EVENT_EXEC)
            break;
        /* A signal before the exec: let it have it. */
        ptrace(PTRACE_CONT, c, 0, (void*)(long)(event_of(st) ? 0 : WSTOPSIG(st)));
    }
    /* Later execs are not followed. */
    ptrace(PTRACE_SETOPTIONS, c, 0, (void*)k_options);
    alive_ = true;
    add(c, false)->stopped = true;
    cur_ = c;
    if (!open_mem(c)) {
        set_err(err, "/proc/<pid>/mem");
        return false;
    }
    return true;
}

/* Every thread: those in /proc/<pid>/task, again until no new one has
 * appeared (one may start another before it is traced), each stopped. */
bool LinuxTarget::attach(int pid, ttr::Str* err)
{
    if (ptrace(PTRACE_SEIZE, pid, 0, (void*)k_options) != 0) {
        set_err(err, errno == EPERM
                         ? "attach (see /proc/sys/kernel/yama/ptrace_scope)"
                         : "attach");
        return false;
    }
    pid_ = pid;
    alive_ = true;
    attached_ = true;
    add(pid, false);
    cur_ = pid;
    bool more = true;
    while (more) {
        more = false;
        char p[64];
        snprintf(p, sizeof(p), "/proc/%d/task", pid);
        DIR* d = opendir(p);
        if (!d)
            break;
        struct dirent* e;
        while ((e = readdir(d)) != 0) {
            pid_t t = (pid_t)atoi(e->d_name);
            if (t <= 0 || find(t))
                continue;
            if (ptrace(PTRACE_SEIZE, t, 0, (void*)k_options) == 0) {
                add(t, false);
                more = true;
            }
        }
        closedir(d);
    }
    int i;
    for (i = 0; i < threads_.size(); i++)
        ptrace(PTRACE_INTERRUPT, threads_[i].tid, 0, 0);
    stop_others(-1);
    if (!find(pid)) {
        if (err)
            *err = "it ended";
        return false;
    }
    if (!open_mem(pid)) {
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

bool LinuxTarget::shared_objects(Addr dynamic, ttr::Vector<SharedObject>* out)
{
    return glibc_objects(dynamic, out);
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

int LinuxTarget::threads(ttr::Vector<int>* ids)
{
    int i;
    for (i = 0; i < threads_.size(); i++)
        if (!threads_[i].exiting)
            ids->push_back((int)threads_[i].tid);
    return ids->size();
}

bool LinuxTarget::select_thread(int id)
{
    Thread* t = find((pid_t)id);
    if (!t || t->exiting)
        return false;
    cur_ = t->tid;
    return true;
}

bool LinuxTarget::run_thread(Thread* t, bool step, int sig)
{
    if (ptrace(step ? PTRACE_SINGLESTEP : PTRACE_CONT, t->tid, 0, (void*)(long)sig) != 0)
        return false;
    t->stopped = false;
    return true;
}

bool LinuxTarget::resume(bool step, int sig)
{
    if (!alive_)
        return false;
    Thread* c = find(cur_);
    if (!c)
        return false;
    int i;
    if (step) {
        if (c->has_pending) {
            c->sig = sig;
            return true;        /* its own stop first */
        }
        all_running_ = false;
        return run_thread(c, true, sig);
    }
    /* A stop another thread had first, without running (next() reports
     * it); the signal waits for the next run. */
    for (i = 0; i < threads_.size(); i++)
        if (threads_[i].has_pending) {
            c->sig = sig;
            return true;
        }
    all_running_ = true;
    bool ok = true;
    for (i = 0; i < threads_.size(); i++) {
        Thread* t = &threads_[i];
        if (!t->stopped)
            continue;
        int s = t->tid == cur_ ? sig : t->sig;
        t->sig = 0;
        if (!run_thread(t, false, s) && t->tid == cur_)
            ok = false;
    }
    return ok;
}

void LinuxTarget::gone()
{
    alive_ = false;
    threads_.clear();
    if (mem_ >= 0)
        close(mem_);
    mem_ = -1;
}

/* The next wait status of one of the program's threads: 1 one, 0 none
 * (not blocking), -1 none left. Not waitpid(-1): the debugger's own
 * other children (ttrcode's terminals) are not ours to reap. waitid with
 * WNOWAIT says who has one without taking it. */
int LinuxTarget::next_status(bool block, pid_t* tid, int* st)
{
    for (;;) {
        int i;
        for (i = 0; i < threads_.size(); i++) {
            pid_t r = waitpid(threads_[i].tid, st, __WALL | WNOHANG);
            if (r == threads_[i].tid) {
                *tid = r;
                return 1;
            }
        }
        if (threads_.empty())
            return -1;
        siginfo_t si;
        memset(&si, 0, sizeof(si));
        int r = waitid(P_ALL, 0, &si, WEXITED | WSTOPPED | WNOWAIT | __WALL | WNOHANG);
        if (r == 0 && si.si_pid && !find(si.si_pid) && ours(si.si_pid)) {
            /* A new thread whose creator has not said so yet. */
            if (waitpid(si.si_pid, st, __WALL | WNOHANG) == si.si_pid) {
                *tid = si.si_pid;
                return 1;
            }
            continue;
        }
        if (!block)
            return 0;
        if (r == 0 && si.si_pid) {
            /* Someone else's event is waiting: it would wake us at once. */
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 200000;
            nanosleep(&ts, 0);
            continue;
        }
        memset(&si, 0, sizeof(si));
        r = waitid(P_ALL, 0, &si, WEXITED | WSTOPPED | WNOWAIT | __WALL);
        if (r != 0 && errno == ECHILD)
            return -1;
    }
}

/* Was this thread's SIGTRAP an int3 (and not a single step or a debug
 * register)? */
bool LinuxTarget::trap_was_breakpoint(pid_t tid)
{
    siginfo_t si;
    memset(&si, 0, sizeof(si));
    if (ptrace(PTRACE_GETSIGINFO, tid, 0, &si) != 0)
        return false;
    if (si.si_code != SI_KERNEL && si.si_code != TRAP_BRKPT)
        return false;
    errno = 0;
    unsigned long dr6 = (unsigned long)ptrace(
        PTRACE_PEEKUSER, tid, (void*)(offsetof(struct user, u_debugreg) + 6 * sizeof(long)), 0);
    return errno || !(dr6 & 0xf);
}

/* Stop every thread but one, keeping what each stopped for if it was
 * not the stop asked for. A breakpoint another thread reached is not
 * kept: its pc goes back to the int3, which it reaches again if the
 * breakpoint is still there when it runs. */
void LinuxTarget::stop_others(pid_t except)
{
    int i;
    for (i = 0; i < threads_.size(); i++) {
        Thread* t = &threads_[i];
        if (t->tid != except && !t->stopped && !t->starting && !t->exiting)
            ptrace(PTRACE_INTERRUPT, t->tid, 0, 0);
    }
    for (i = 0; i < threads_.size(); i++) {
        Thread* t = &threads_[i];
        if (t->tid == except || t->stopped || t->exiting)
            continue;
        pid_t tid = t->tid;
        int st = 0;
        pid_t r;
        do
            r = waitpid(tid, &st, __WALL);
        while (r < 0 && errno == EINTR);
        if (r != tid || WIFEXITED(st) || WIFSIGNALED(st)) {
            if (tid != pid_) {
                drop(tid);
                i--;
            }
            continue;
        }
        t->stopped = true;
        int ev = event_of(st);
        if (t->starting) {
            t->starting = false;
            set_watches(tid);
            if (ev == PTRACE_EVENT_STOP)
                continue;
        }
        if (ev == PTRACE_EVENT_STOP)
            continue;
        if (ev == PTRACE_EVENT_CLONE) {
            unsigned long nt = 0;
            ptrace(PTRACE_GETEVENTMSG, tid, 0, &nt);
            if (nt && !find((pid_t)nt)) {
                add((pid_t)nt, true);
                t = &threads_[i];
            }
            continue;
        }
        if (ev == PTRACE_EVENT_EXIT) {
            t->exiting = true;
            continue;
        }
        if (ev)
            continue;
        if (WSTOPSIG(st) == SIGTRAP && trap_was_breakpoint(tid)) {
            struct user_regs_struct u;
            if (ptrace(PTRACE_GETREGS, tid, 0, &u) == 0) {
                u.rip -= (unsigned long)arch()->pc_after_bp;
                ptrace(PTRACE_SETREGS, tid, 0, &u);
            }
            continue;
        }
        t->has_pending = true;
        t->pending = st;
    }
    /* New threads the loop above added: their first stops. */
    for (i = 0; i < threads_.size(); i++) {
        Thread* t = &threads_[i];
        if (!t->starting)
            continue;
        int st = 0;
        pid_t r;
        do
            r = waitpid(t->tid, &st, __WALL);
        while (r < 0 && errno == EINTR);
        if (r != t->tid || !WIFSTOPPED(st)) {
            drop(t->tid);
            i--;
            continue;
        }
        t->starting = false;
        t->stopped = true;
        set_watches(t->tid);
    }
}

/* The next stop of the program, all its threads stopped: 1 one (in *ev),
 * 0 none yet (not blocking), -1 it is gone. Thread starts and ends, and
 * stops nobody asked for, are handled here. */
int LinuxTarget::next(bool block, Event* ev)
{
    memset(ev, 0, sizeof(*ev));
    int i;
    for (i = 0; i < threads_.size(); i++) {
        Thread* t = &threads_[i];
        if (!t->has_pending || (!all_running_ && t->tid != cur_))
            continue;
        /* Nothing runs: resume() saw this one waiting. */
        t->has_pending = false;
        cur_ = t->tid;
        decode(t, t->pending, ev);
        return 1;
    }
    for (;;) {
        pid_t tid = 0;
        int st = 0;
        int r = next_status(block, &tid, &st);
        if (r == 0)
            return 0;
        if (r < 0) {
            ev->kind = EV_ERROR;
            gone();
            return -1;
        }
        Thread* t = find(tid);
        if (WIFEXITED(st) || WIFSIGNALED(st)) {
            if (tid != pid_) {
                bool was_cur = tid == cur_;
                drop(tid);
                if (was_cur && !all_running_) {
                    /* The thread being stepped ended: the rest go on. */
                    resume(false, 0);
                }
                continue;
            }
            if (WIFEXITED(st)) {
                ev->kind = EV_EXITED;
                ev->status = WEXITSTATUS(st);
            } else {
                ev->kind = EV_KILLED;
                ev->sig = WTERMSIG(st);
            }
            ev->thread = tid;
            gone();
            return 1;
        }
        if (!WIFSTOPPED(st))
            continue;
        if (!t)
            t = add(tid, true);
        t->stopped = true;
        int e = event_of(st);
        if (t->starting) {
            /* A new thread's first stop: it runs if the others do. */
            t->starting = false;
            set_watches(tid);
            if (all_running_)
                run_thread(t, false, 0);
            continue;
        }
        if (e == PTRACE_EVENT_CLONE) {
            unsigned long nt = 0;
            ptrace(PTRACE_GETEVENTMSG, tid, 0, &nt);
            if (nt && !find((pid_t)nt)) {
                add((pid_t)nt, true);
                t = find(tid);
            }
            run_thread(t, !all_running_ && tid == cur_, 0);
            continue;
        }
        if (e == PTRACE_EVENT_EXIT) {
            t->exiting = true;
            if (tid == pid_) {
                /* The first thread ended before the others: memory
                 * through another's file. */
                for (i = 0; i < threads_.size(); i++)
                    if (!threads_[i].exiting) {
                        open_mem(threads_[i].tid);
                        break;
                    }
            }
            bool was_cur = tid == cur_;
            if (was_cur) {
                for (i = 0; i < threads_.size(); i++)
                    if (!threads_[i].exiting) {
                        cur_ = threads_[i].tid;
                        break;
                    }
            }
            t = find(tid);
            run_thread(t, false, 0);
            if (was_cur && !all_running_)
                resume(false, 0);       /* the stepped thread is ending */
            continue;
        }
        if (e == PTRACE_EVENT_STOP) {
            if (interrupting_ && WSTOPSIG(st) == SIGTRAP) {
                interrupting_ = false;
                stop_others(tid);
                cur_ = tid;
                Regs regs;
                get_regs(&regs);
                ev->kind = EV_PAUSED;
                ev->pc = regs.pc;
                ev->thread = tid;
                return 1;
            }
            /* An interrupt left over from stopping it before, or a group
             * stop: on it goes. */
            run_thread(t, !all_running_ && tid == cur_, 0);
            continue;
        }
        if (e) {
            run_thread(t, !all_running_ && tid == cur_, 0);
            continue;
        }
        if (all_running_)
            stop_others(tid);
        interrupting_ = false;
        cur_ = tid;
        decode(find(tid), st, ev);
        return 1;
    }
}

bool LinuxTarget::wait(Event* ev)
{
    if (!alive_) {
        memset(ev, 0, sizeof(*ev));
        ev->kind = EV_ERROR;
        return false;
    }
    return next(true, ev) > 0;
}

bool LinuxTarget::poll(Event* ev, bool* got)
{
    *got = true;
    if (!alive_) {
        memset(ev, 0, sizeof(*ev));
        ev->kind = EV_ERROR;
        return false;
    }
    int r = next(false, ev);
    if (r == 0) {
        *got = false;
        return true;
    }
    return r > 0;
}

/* PTRACE_INTERRUPT: one thread stops (PTRACE_EVENT_STOP), then the
 * others with it. */
bool LinuxTarget::interrupt()
{
    if (!alive_)
        return false;
    interrupting_ = true;
    Thread* t = find(cur_);
    if (!t || t->exiting)
        t = find(pid_);
    if (!t)
        return false;
    return ptrace(PTRACE_INTERRUPT, t->tid, 0, 0) == 0;
}

void LinuxTarget::decode(Thread* t, int st, Event* ev)
{
    pid_t tid = t->tid;
    ev->thread = tid;
    int sig = WSTOPSIG(st);
    Regs regs;
    get_regs(&regs);
    ev->pc = regs.pc;
    if (sig != SIGTRAP) {
        ev->kind = EV_SIGNAL;
        ev->sig = sig;
        return;
    }
    /* DR6 says which debug registers fired (bits 0 to 3), the step too
     * if it was one (the write was its instruction). */
    int i;
    unsigned long dr6 = 0;
    for (i = 0; i < 4 && !slots_[i].handle; i++)
        ;
    if (i < 4) {
        errno = 0;
        dr6 = (unsigned long)ptrace(PTRACE_PEEKUSER, tid,
                                    (void*)(offsetof(struct user, u_debugreg) + 6 * sizeof(long)),
                                    0);
        if (errno)
            dr6 = 0;
        set_debugreg(tid, 6, 0);
    }
    for (i = 0; i < 4; i++)
        if ((dr6 & (1ul << i)) && slots_[i].handle) {
            ev->kind = EV_WATCH;
            ev->watch = slots_[i].handle;
            return;
        }
    siginfo_t si;
    memset(&si, 0, sizeof(si));
    ptrace(PTRACE_GETSIGINFO, tid, 0, &si);
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
}

void LinuxTarget::kill()
{
    if (!alive_)
        return;
    ::kill(pid_, SIGKILL);
    /* Every thread ends (one stopped at its exit is let go); the first
     * one's end comes last. */
    int i, pass;
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < threads_.size(); i++) {
            pid_t tid = threads_[i].tid;
            if ((tid == pid_) != (pass == 1))
                continue;
            int st = 0;
            for (;;) {
                pid_t r = waitpid(tid, &st, __WALL);
                if (r < 0 && errno == EINTR)
                    continue;
                if (r != tid || WIFEXITED(st) || WIFSIGNALED(st))
                    break;
                ptrace(PTRACE_CONT, tid, 0, 0);
            }
        }
    gone();
}

/* Each thread goes with its debug registers cleared and the signal it
 * had for its next run. */
void LinuxTarget::detach()
{
    if (!alive_)
        return;
    int i;
    for (i = 0; i < threads_.size(); i++) {
        Thread* t = &threads_[i];
        if (t->exiting)
            continue;
        set_debugreg(t->tid, 7, 0);
        int sig = t->sig;
        if (t->has_pending && !event_of(t->pending) && WSTOPSIG(t->pending) != SIGTRAP)
            sig = WSTOPSIG(t->pending);
        ptrace(PTRACE_DETACH, t->tid, 0, (void*)(long)sig);
    }
    gone();
}

bool LinuxTarget::get_regs(Regs* r)
{
    r->clear();
    struct user_regs_struct u;
    if (ptrace(PTRACE_GETREGS, cur_, 0, &u) != 0)
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
    if (ptrace(PTRACE_GETREGS, cur_, 0, &u) != 0)
        return false;
    u.rip = pc;
    return ptrace(PTRACE_SETREGS, cur_, 0, &u) == 0;
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

bool LinuxTarget::set_debugreg(pid_t tid, int i, unsigned long v)
{
    return ptrace(PTRACE_POKEUSER, tid,
                  (void*)(offsetof(struct user, u_debugreg) + (size_t)i * sizeof(long)),
                  (void*)v) == 0;
}

/* DR7: for each register in use its local enable bit (2i), and at
 * 16 + 4i the condition (01: writes) and the length (00: 1 byte, 01: 2,
 * 11: 4, 10: 8). */
bool LinuxTarget::write_dr7(pid_t tid)
{
    unsigned long dr7 = 0;
    int i;
    for (i = 0; i < 4; i++) {
        if (!slots_[i].handle)
            continue;
        unsigned long len = slots_[i].len == 1 ? 0 : slots_[i].len == 2 ? 1 :
                            slots_[i].len == 4 ? 3 : 2;
        dr7 |= 1ul << (2 * i);
        dr7 |= (1ul | (len << 2)) << (16 + 4 * i);
    }
    return set_debugreg(tid, 7, dr7);
}

/* A thread's debug registers as slots_ says (a new thread's are clear). */
void LinuxTarget::set_watches(pid_t tid)
{
    int i;
    for (i = 0; i < 4; i++)
        if (slots_[i].handle)
            set_debugreg(tid, i, (unsigned long)slots_[i].addr);
    write_dr7(tid);
}

int LinuxTarget::add_watch(Addr a, size_t n, ttr::Str* err)
{
    /* Into aligned pieces of 8, 4, 2 and 1 bytes. */
    Slot want[4];
    int pieces = 0;
    Addr p = a, end = a + n;
    while (p < end) {
        int len = 8;
        while (len > 1 && ((p % (Addr)len) != 0 || p + (Addr)len > end))
            len /= 2;
        if (pieces == 4) {
            if (err)
                *err = "too big: the processor watches 32 bytes at most, in 4 aligned pieces";
            return -1;
        }
        want[pieces].addr = p;
        want[pieces].len = len;
        pieces++;
        p += (Addr)len;
    }
    int i, k, free = 0;
    for (i = 0; i < 4; i++)
        if (!slots_[i].handle)
            free++;
    if (pieces > free) {
        if (err)
            *err = "the processor has no more debug registers (4, of up to 8 bytes each)";
        return -1;
    }
    int handle = next_watch_++;
    for (i = 0, k = 0; k < pieces; i++) {
        if (slots_[i].handle)
            continue;
        slots_[i] = want[k++];
        slots_[i].handle = handle;
    }
    for (i = 0; i < threads_.size(); i++) {
        pid_t tid = threads_[i].tid;
        if (threads_[i].exiting || threads_[i].starting)
            continue;
        for (k = 0; k < 4; k++)
            if (slots_[k].handle == handle && !set_debugreg(tid, k, (unsigned long)slots_[k].addr)) {
                set_err(err, "setting a debug register");
                remove_watch(handle);
                return -1;
            }
        if (!write_dr7(tid)) {
            set_err(err, "setting the debug registers");
            remove_watch(handle);
            return -1;
        }
    }
    return handle;
}

void LinuxTarget::remove_watch(int handle)
{
    int i;
    for (i = 0; i < 4; i++)
        if (slots_[i].handle == handle)
            slots_[i].handle = 0;
    if (!alive_)
        return;
    for (i = 0; i < threads_.size(); i++)
        if (!threads_[i].exiting && !threads_[i].starting)
            write_dr7(threads_[i].tid);
}

} /* namespace */

Target* Target::create()
{
    return new LinuxTarget;
}

/* /proc/<pid>: owned by the user; its comm and cmdline. */
int Target::processes(ttr::Vector<ProcessInfo>* out)
{
    DIR* d = opendir("/proc");
    if (!d)
        return 0;
    ttr::Vector<int> pids;
    struct dirent* e;
    while ((e = readdir(d)) != 0) {
        int pid = atoi(e->d_name);
        if (pid > 0 && pid != (int)getpid())
            pids.push_back(pid);
    }
    closedir(d);
    int i;
    for (i = 0; i < pids.size(); i++) {
        char p[64], buf[4096];
        struct stat st;
        snprintf(p, sizeof(p), "/proc/%d", pids[i]);
        if (stat(p, &st) != 0 || st.st_uid != getuid())
            continue;
        ProcessInfo pi;
        pi.pid = pids[i];
        snprintf(p, sizeof(p), "/proc/%d/comm", pids[i]);
        int fd = open(p, O_RDONLY);
        ssize_t n = fd >= 0 ? ::read(fd, buf, sizeof(buf) - 1) : -1;
        if (fd >= 0)
            close(fd);
        if (n <= 0)
            continue;       /* gone */
        buf[n] = 0;
        if (n && buf[n - 1] == '\n')
            buf[n - 1] = 0;
        pi.name = buf;
        /* stat: its start time is the 22nd field, the 20th after the
         * name's closing parenthesis. */
        pi.start = 0;
        snprintf(p, sizeof(p), "/proc/%d/stat", pids[i]);
        fd = open(p, O_RDONLY);
        n = fd >= 0 ? ::read(fd, buf, sizeof(buf) - 1) : -1;
        if (fd >= 0)
            close(fd);
        if (n > 0) {
            buf[n] = 0;
            char* q = strrchr(buf, ')');
            int f;
            for (f = 0; q && f < 20; f++)
                q = strchr(q + 1, ' ');
            if (q)
                pi.start = (uint64_t)strtoull(q + 1, 0, 10);
        }
        snprintf(p, sizeof(p), "/proc/%d/cmdline", pids[i]);
        fd = open(p, O_RDONLY);
        n = fd >= 0 ? ::read(fd, buf, sizeof(buf) - 1) : -1;
        if (fd >= 0)
            close(fd);
        if (n > 0) {
            /* Its arguments, NUL-separated: spaces between. */
            ssize_t j;
            for (j = 0; j < n; j++)
                if (!buf[j])
                    buf[j] = ' ';
            while (n > 0 && buf[n - 1] == ' ')
                n--;
            buf[n] = 0;
            pi.args = buf;
        }
        out->push_back(pi);
    }
    sort_processes(out);
    return out->size();
}

} /* namespace td */
