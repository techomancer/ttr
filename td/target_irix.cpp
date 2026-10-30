/*
 * td/target_irix.cpp - Target on IRIX through the /proc ioctls
 * (td/DESIGN.md section 3.2): PIOCSTOP, PIOCRUN, PIOCWSTOP, fault and
 * system call tracing, registers and memory through /proc/<pid>.
 *
 * Threads (pthreads, each a kernel uthread with ids from 0): PR_JOINTSTOP
 * stops all of them when one stops, the process's PIOCRUN runs all of
 * them again, and PIOCTHREAD issues an ioctl to one (its registers, a
 * single step). Checked on the Octane.
 */
#include "target.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fault.h>
#include <sys/procfs.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/ucontext.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace td {

namespace {

class IrixTarget : public Target {
public:
    IrixTarget()
        : pid_(-1), fd_(-1), exe_fd_(-1), alive_(false), child_(false), arch_(0),
          cur_(0), stepping_(false), held_(false), next_watch_(1)
    {
    }
    ~IrixTarget();

    bool launch(char* const* argv, const LaunchOptions& o, ttr::Str* err);
    bool attach(int pid, ttr::Str* err);
    const Arch* arch() const { return arch_ ? arch_ : arch_mips32(); }
    int pid() const { return pid_; }
    ttr::Str exe_path() { return path_; }
    Addr exe_bias(Addr) { return 0; }       /* IRIX executables are not moved */
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
    bool open_proc(ttr::Str* err);
    bool trace(bool on);
    bool joint(bool on);
    int tctl(int tid, int cmd, int flags, void* data);
    bool walk(prstatus_t* event);
    bool wait_all(prstatus_t* event);
    bool all_stopped();
    void hold_for_step();
    void unhold();
    void gone();
    void arch_from(const char* path);
    bool ended(Event* ev);
    bool reaped(Event* ev);
    bool decode(const prstatus_t& st, Event* ev);
    bool set_watch(Addr a, size_t n, int flags);
    bool store_conditional(Addr pc, Event* ev);
    bool run_through_sc(Addr ll, Addr sc, int rt, Event* ev);
    bool read_insn(Addr a, uint32_t* insn);
    int watch_at(Addr a) const;

    pid_t pid_;
    int fd_;            /* /proc/<pid> */
    int exe_fd_;        /* attach: the executable, from PIOCOPENM */
    bool alive_;
    bool child_;        /* started by us: waitpid gives its status */
    const Arch* arch_;
    ttr::Str path_;
    ttr::Vector<int> tids_;     /* the uthreads at the last stop */
    int cur_;
    bool stepping_;             /* cur_ alone runs, one instruction */
    bool held_;                 /* saved_hold_ is cur_'s held signals */
    sigset_t saved_hold_;
    /* Watched areas (PIOCSWATCH): any number and size, the kernel's. */
    struct Area {
        Addr addr;
        size_t size;
        int handle;
    };
    ttr::Vector<Area> areas_;
    int next_watch_;
};

void set_err(ttr::Str* err, const char* what)
{
    if (!err)
        return;
    char m[300];
    snprintf(m, sizeof(m), "%s: %s", what, strerror(errno));
    *err = m;
}

/* argv[0] as execvp would find it. */
ttr::Str find_program(const char* name)
{
    if (strchr(name, '/'))
        return ttr::Str(name);
    const char* path = getenv("PATH");
    if (!path)
        path = "/usr/bin:/bin";
    char buf[2048];
    while (*path) {
        const char* e = strchr(path, ':');
        size_t n = e ? (size_t)(e - path) : strlen(path);
        snprintf(buf, sizeof(buf), "%.*s/%s", (int)n, path, name);
        if (access(buf, X_OK) == 0)
            return ttr::Str(buf);
        if (!e)
            break;
        path = e + 1;
    }
    return ttr::Str(name);
}

IrixTarget::~IrixTarget()
{
    if (alive_) {
        if (child_)
            kill();
        else
            detach();
    }
    if (fd_ >= 0)
        close(fd_);
    if (exe_fd_ >= 0)
        close(exe_fd_);
}

/* 32 or 64 bit, from the file's ELF class. */
void IrixTarget::arch_from(const char* path)
{
    unsigned char h[5];
    int fd = open(path, O_RDONLY);
    arch_ = arch_mips32();
    if (fd < 0)
        return;
    if (::read(fd, h, 5) == 5 && h[4] == 2)
        arch_ = arch_mips64();
    close(fd);
}

bool IrixTarget::open_proc(ttr::Str* err)
{
    char p[64];
    sprintf(p, "/proc/%d", (int)pid_);
    fd_ = open(p, O_RDWR);
    if (fd_ < 0) {
        set_err(err, p);
        return false;
    }
    return true;
}

/* Stop on breakpoints and single steps (faults) and on signals. */
bool IrixTarget::trace(bool on)
{
    fltset_t f;
    premptyset(&f);
    sigset_t s;
    premptyset(&s);
    if (on) {
        praddset(&f, FLTBPT);
        praddset(&f, FLTTRACE);
        praddset(&f, FLTWATCH);
        praddset(&f, FLTKWATCH);
        praddset(&f, FLTSCWATCH);
        prfillset(&s);
        prdelset(&s, SIGCHLD);
#ifdef SIGPTRESCHED
        /* libpthread's own: its scheduler switching pthreads between
         * kernel threads. */
        prdelset(&s, SIGPTINTR);
        prdelset(&s, SIGPTRESCHED);
#endif
    }
    return ioctl(fd_, PIOCSFAULT, &f) == 0 && ioctl(fd_, PIOCSTRACE, &s) == 0;
}

/* All threads stop when one does (and poll says stopped only when all
 * have). */
bool IrixTarget::joint(bool on)
{
    long fl = PR_JOINTSTOP | PR_JOINTPOLL;
    return ioctl(fd_, on ? PIOCSET : PIOCRESET, &fl) == 0;
}

/* An ioctl on one thread. */
int IrixTarget::tctl(int tid, int cmd, int flags, void* data)
{
    prthreadctl_t c;
    memset(&c, 0, sizeof(c));
    c.pt_tid = (tid_t)tid;
    c.pt_cmd = cmd;
    c.pt_flags = flags;
    c.pt_data = (caddr_t)data;
    return ioctl(fd_, PIOCTHREAD, &c);
}

/* The threads now, in tids_, from PIOCTHREAD's walk (the first id at
 * least 0, then each next greater one); *event: the status of the first
 * one stopped for a reason of its own (a fault, a signal), else of
 * cur_. False if there are none. */
bool IrixTarget::walk(prstatus_t* event)
{
    tids_.clear();
    int tid = 0, flags = PTFD_GEQ;
    bool found = false, have_cur = false;
    prstatus_t cur;
    int n;
    for (n = 0; n < 4096; n++) {
        prstatus_t st;
        if (tctl(tid, PIOCSTATUS, flags, &st) != 0)
            break;
        tid = (int)st.pr_who;
        flags = PTFD_GTR;
        tids_.push_back(tid);
        if (!event)
            continue;
        if (!found && st.pr_why != PR_REQUESTED && (st.pr_flags & PR_STOPPED)) {
            *event = st;
            found = true;
        }
        if (tid == cur_) {
            cur = st;
            have_cur = true;
        }
    }
    if (tids_.empty())
        return false;
    if (tids_.find(cur_) < 0)
        cur_ = tids_[0];
    if (event && !found) {
        if (have_cur)
            *event = cur;
        else
            tctl(cur_, PIOCSTATUS, PTFD_EQL, event);
    }
    return true;
}

/* Every thread stopped (a joint stop takes them one by one). */
bool IrixTarget::all_stopped()
{
    int tid = 0, flags = PTFD_GEQ, n;
    for (n = 0; n < 4096; n++) {
        prstatus_t st;
        if (tctl(tid, PIOCSTATUS, flags, &st) != 0)
            break;
        if (!(st.pr_flags & PR_STOPPED))
            return false;
        tid = (int)st.pr_who;
        flags = PTFD_GTR;
    }
    return true;
}

/* After the process's PIOCWSTOP: until the others have stopped too, then
 * the thread that stopped for something. False: it is gone. */
bool IrixTarget::wait_all(prstatus_t* event)
{
    int tid = 0, flags = PTFD_GEQ, n;
    for (n = 0; n < 4096; n++) {
        prstatus_t st;
        if (tctl(tid, PIOCSTATUS, flags, &st) != 0)
            break;
        tid = (int)st.pr_who;
        flags = PTFD_GTR;
        if (!(st.pr_flags & PR_STOPPED)) {
            int r;
            do
                r = tctl(tid, PIOCWSTOP, PTFD_EQL, &st);
            while (r != 0 && errno == EINTR);
        }
    }
    return walk(event);
}

/* A single step with libpthread's signals held: delivered during the
 * step, they would run its scheduler's handler (and switch pthreads)
 * instead of the instruction. */
void IrixTarget::hold_for_step()
{
#ifdef SIGPTRESCHED
    sigset_t h;
    if (held_ || tctl(cur_, PIOCGHOLD, PTFD_EQL, &h) != 0)
        return;
    saved_hold_ = h;
    praddset(&h, SIGPTINTR);
    praddset(&h, SIGPTRESCHED);
    if (tctl(cur_, PIOCSHOLD, PTFD_EQL, &h) == 0)
        held_ = true;
#endif
}

void IrixTarget::unhold()
{
    if (!held_)
        return;
    tctl(cur_, PIOCSHOLD, PTFD_EQL, &saved_hold_);
    held_ = false;
}

int IrixTarget::threads(ttr::Vector<int>* ids)
{
    int i;
    for (i = 0; i < tids_.size(); i++)
        ids->push_back(tids_[i]);
    return ids->size();
}

bool IrixTarget::select_thread(int id)
{
    if (tids_.find(id) < 0)
        return false;
    cur_ = id;
    return true;
}

bool IrixTarget::launch(char* const* argv, const LaunchOptions& o, ttr::Str* err)
{
    path_ = find_program(argv[0]);
    arch_from(path_.c_str());
    /* The child waits on a pipe until it is stopped and traced, so the
     * stop after its exec is the first thing we see. */
    int p[2];
    if (pipe(p) != 0) {
        set_err(err, "pipe");
        return false;
    }
    pid_t c = fork();
    if (c < 0) {
        set_err(err, "fork");
        return false;
    }
    if (c == 0) {
        char x;
        close(p[1]);
        ::read(p[0], &x, 1);
        close(p[0]);
        setup_child(o);
        execvp(argv[0], argv);
        fprintf(stderr, "ttrdb: cannot run %s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    close(p[0]);
    pid_ = c;
    child_ = true;
    alive_ = true;
    prstatus_t st;
    if (!open_proc(err) || ioctl(fd_, PIOCSTOP, &st) != 0 || !trace(true)) {
        if (err && err->empty())
            set_err(err, "stopping the new process");
        close(p[1]);
        return false;
    }
    sysset_t sys;
    premptyset(&sys);
    praddset(&sys, SYS_execve);
    ioctl(fd_, PIOCSEXIT, &sys);
    prrun_t run;
    memset(&run, 0, sizeof(run));
    ioctl(fd_, PIOCRUN, &run);
    write(p[1], "x", 1);
    close(p[1]);
    /* The exec returns: stopped at the new program's first instruction
     * (in rld). */
    if (ioctl(fd_, PIOCWSTOP, &st) != 0 || st.pr_why != PR_SYSEXIT) {
        if (err)
            *err = "the program did not start";
        return false;
    }
    premptyset(&sys);
    ioctl(fd_, PIOCSEXIT, &sys);
    joint(true);
    /* If the debugger dies, so does the program (Linux: EXITKILL). */
    long klc = PR_KLC;
    ioctl(fd_, PIOCSET, &klc);
    walk(0);
    return true;
}

bool IrixTarget::attach(int pid, ttr::Str* err)
{
    pid_ = pid;
    prstatus_t st;
    if (!open_proc(err))
        return false;
    if (ioctl(fd_, PIOCSTOP, &st) != 0) {
        set_err(err, "PIOCSTOP");
        return false;
    }
    alive_ = true;
    trace(true);
    joint(true);
    /* If the debugger dies, it runs on. */
    long rlc = PR_RLC;
    ioctl(fd_, PIOCSET, &rlc);
    wait_all(&st);
    /* The executable, through the file it was mapped from. */
    exe_fd_ = ioctl(fd_, PIOCOPENM, 0);
    if (exe_fd_ >= 0) {
        char b[64];
        sprintf(b, "/dev/fd/%d", exe_fd_);
        path_ = b;
        arch_from(b);
    }
    return true;
}

bool IrixTarget::shared_objects(Addr dynamic, ttr::Vector<SharedObject>* out)
{
    return rld_objects(dynamic, out);
}

/* rld calls __dbx_link, an empty function there for debuggers, after it
 * changes its list (checked on 6.5.22: once per dlopen, with the new
 * object already on the list). rld is not on its own list, and sits at
 * its link address. */
bool IrixTarget::loader_hook(Addr, SharedObject* loader, Addr* addr, ttr::Str* symbol)
{
    loader->path = arch()->ptr_size == 8 ? "/lib64/rld" : "/lib32/rld";
    loader->bias = 0;
    *addr = 0;
    *symbol = "__dbx_link";
    return true;
}

bool IrixTarget::resume(bool step, int sig)
{
    if (!alive_)
        return false;
    prrun_t run;
    memset(&run, 0, sizeof(run));
    /* Forget the fault that stopped it (a breakpoint, a step), and the
     * signal unless it is to be delivered. */
    run.pr_flags = PRCFAULT;
    if (!sig)
        run.pr_flags |= PRCSIG;
    stepping_ = step;
    if (step) {
        /* The current thread alone; the others stay stopped. libpthread's
         * scheduling signals wait until it runs (hold_for_step). */
        hold_for_step();
        run.pr_flags |= PRSTEP;
        if (tctl(cur_, PIOCRUN, PTFD_EQL, &run) == 0)
            return true;
        unhold();
        return false;
    }
    return ioctl(fd_, PIOCRUN, &run) == 0;
}

void IrixTarget::gone()
{
    alive_ = false;
    if (fd_ >= 0)
        close(fd_);
    fd_ = -1;
}

bool IrixTarget::wait(Event* ev)
{
    memset(ev, 0, sizeof(*ev));
    prstatus_t st;
    int r;
    if (stepping_) {
        do
            r = tctl(cur_, PIOCWSTOP, PTFD_EQL, &st);
        while (r != 0 && errno == EINTR);
        unhold();
        if (r == 0) {
            stepping_ = false;
            if (!decode(st, ev))
                return ended(ev);
            return true;
        }
        /* The stepped thread ended: the others go on. */
        stepping_ = false;
        prrun_t run;
        memset(&run, 0, sizeof(run));
        run.pr_flags = PRCFAULT | PRCSIG;
        if (ioctl(fd_, PIOCRUN, &run) != 0)
            return ended(ev);
    }
    /* Not the process's PIOCWSTOP: with threads it can wait on after the
     * process has ended (checked on the Octane). Its status, then, with
     * naps that grow from 0.2 ms to 5 ms. */
    int naps;
    for (naps = 0;; naps++) {
        bool got = false;
        bool ok = poll(ev, &got);
        if (got)
            return ok;
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = naps < 50 ? 200000 : 5000000;
        nanosleep(&ts, 0);
    }
}

/* Our own child has ended (a zombie may still answer PIOCSTATUS). */
bool IrixTarget::reaped(Event* ev)
{
    int status = 0;
    if (!child_ || waitpid(pid_, &status, WNOHANG) != pid_)
        return false;
    gone();
    if (WIFSIGNALED(status)) {
        ev->kind = EV_KILLED;
        ev->sig = WTERMSIG(status);
    } else {
        ev->kind = EV_EXITED;
        ev->status = WEXITSTATUS(status);
    }
    return true;
}

/* PIOCSTATUS answers at once: stopped on something of interest or not. */
bool IrixTarget::poll(Event* ev, bool* got)
{
    memset(ev, 0, sizeof(*ev));
    *got = true;
    if (!alive_) {
        ev->kind = EV_ERROR;
        return false;
    }
    prstatus_t st;
    if (stepping_) {
        if (tctl(cur_, PIOCSTATUS, PTFD_EQL, &st) != 0)
            return wait(ev);        /* the thread ended */
        if (!(st.pr_flags & PR_ISTOP)) {
            *got = false;
            return true;
        }
        return wait(ev);
    }
    if (ioctl(fd_, PIOCSTATUS, &st) != 0)
        return ended(ev);
    if (!(st.pr_flags & PR_ISTOP) || !all_stopped()) {
        if (reaped(ev))
            return true;
        *got = false;
        return true;
    }
    if (!walk(&st) || !decode(st, ev))
        return ended(ev);
    return true;
}

/* PIOCSTOP waits until it has stopped; poll() then reports the stop
 * (PR_REQUESTED) as EV_PAUSED. */
bool IrixTarget::interrupt()
{
    prstatus_t st;
    return alive_ && ioctl(fd_, PIOCSTOP, &st) == 0;
}

/* It is gone. Our own child tells how it ended. */
bool IrixTarget::ended(Event* ev)
{
    int status = 0;
    gone();
    if (child_ && waitpid(pid_, &status, 0) == pid_) {
        if (WIFSIGNALED(status)) {
            ev->kind = EV_KILLED;
            ev->sig = WTERMSIG(status);
        } else {
            ev->kind = EV_EXITED;
            ev->status = WEXITSTATUS(status);
        }
        return true;
    }
    ev->kind = EV_EXITED;
    return true;
}

/* False if it ended (while stepping a watched write). */
bool IrixTarget::decode(const prstatus_t& st, Event* ev)
{
    cur_ = (int)st.pr_who;
    ev->thread = cur_;
    Addr pc = (Addr)st.pr_reg[CTX_EPC];
    if (arch()->ptr_size == 4)
        pc = (uint32_t)pc;
    ev->pc = pc;
    if (st.pr_why == PR_FAULTED && st.pr_what == FLTBPT) {
        ev->kind = EV_BREAK;        /* EPC is the break instruction */
    } else if (st.pr_why == PR_FAULTED && st.pr_what == FLTTRACE) {
        ev->kind = EV_STEP;
    } else if (((st.pr_why == PR_FAULTED && st.pr_what == FLTSCWATCH) ||
                (st.pr_why == PR_SIGNALLED && st.pr_what == SIGTRAP && !areas_.empty())) &&
               store_conditional(pc, ev)) {
        /* done: the store made, reported as a watched write (the Octane
         * reports it as a SIGTRAP at the sc, not as the fault) */
    } else if (st.pr_why == PR_FAULTED && (st.pr_what == FLTWATCH || st.pr_what == FLTSCWATCH)) {
        /* Stopped before the write, which faults again for as long as
         * the area is watched (checked on the Octane): out with the
         * areas, one instruction, in with them again, and the stop
         * reported after the write, as Linux does. Something else
         * stopping the step is reported instead. */
        int watch = watch_at((Addr)(unsigned long)st.pr_info.si_addr);
        int i;
        for (i = 0; i < areas_.size(); i++)
            set_watch(areas_[i].addr, 0, MA_WRITE);
        prrun_t run;
        memset(&run, 0, sizeof(run));
        run.pr_flags = PRCFAULT | PRSTEP;
        prstatus_t after;
        hold_for_step();
        int r = tctl(cur_, PIOCRUN, PTFD_EQL, &run);
        if (r == 0) {
            do
                r = tctl(cur_, PIOCWSTOP, PTFD_EQL, &after);
            while (r != 0 && errno == EINTR);
        }
        unhold();
        if (r != 0)
            return false;
        for (i = 0; i < areas_.size(); i++)
            set_watch(areas_[i].addr, areas_[i].size, MA_WRITE);
        if (after.pr_why != PR_FAULTED || after.pr_what != FLTTRACE)
            return decode(after, ev);
        ev->kind = EV_WATCH;
        ev->watch = watch;
        ev->pc = (Addr)after.pr_reg[CTX_EPC];
        if (arch()->ptr_size == 4)
            ev->pc = (uint32_t)ev->pc;
    } else if (st.pr_why == PR_FAULTED && st.pr_what == FLTKWATCH) {
        /* A system call wrote to it: reported after the call. */
        ev->kind = EV_WATCH;
        ev->watch = watch_at((Addr)(unsigned long)st.pr_info.si_addr);
    } else if (st.pr_why == PR_REQUESTED) {
        ev->kind = EV_PAUSED;       /* interrupt()'s PIOCSTOP */
    } else if (st.pr_why == PR_SIGNALLED) {
        ev->kind = EV_SIGNAL;
        ev->sig = st.pr_what;
    } else {
        ev->kind = EV_SIGNAL;
        ev->sig = st.pr_cursig;
    }
    return true;
}

void IrixTarget::kill()
{
    if (!alive_)
        return;
    ::kill(pid_, SIGKILL);
    if (child_) {
        int st;
        waitpid(pid_, &st, 0);
    }
    gone();
}

void IrixTarget::detach()
{
    if (!alive_)
        return;
    trace(false);
    joint(false);
    prrun_t run;
    memset(&run, 0, sizeof(run));
    run.pr_flags = PRCFAULT | PRCSIG;
    ioctl(fd_, PIOCRUN, &run);
    gone();
}

bool IrixTarget::get_regs(Regs* r)
{
    r->clear();
    gregset_t g;
    if (tctl(cur_, PIOCGREG, PTFD_EQL, &g) != 0)
        return false;
    int i;
    for (i = 0; i < 32; i++)
        r->set(i, (uint64_t)g[CTX_R0 + i]);
    r->pc = (Addr)g[CTX_EPC];
    if (arch()->ptr_size == 4) {
        /* n32: 64-bit registers holding 32-bit addresses. */
        r->pc = (uint32_t)r->pc;
        r->r[29] = (uint32_t)r->r[29];
        r->r[31] = (uint32_t)r->r[31];
    }
    return true;
}

bool IrixTarget::set_pc(Addr pc)
{
    gregset_t g;
    if (tctl(cur_, PIOCGREG, PTFD_EQL, &g) != 0)
        return false;
    g[CTX_EPC] = pc;
    return tctl(cur_, PIOCSREG, PTFD_EQL, &g) == 0;
}

bool IrixTarget::read(Addr a, void* buf, size_t n)
{
    if (fd_ < 0 || lseek(fd_, (off_t)a, SEEK_SET) == (off_t)-1)
        return false;
    return ::read(fd_, buf, n) == (ssize_t)n;
}

bool IrixTarget::write(Addr a, const void* buf, size_t n)
{
    if (fd_ < 0 || lseek(fd_, (off_t)a, SEEK_SET) == (off_t)-1)
        return false;
    return ::write(fd_, buf, n) == (ssize_t)n;
}

/* A store-conditional (SC, SCD) on a watched page, stopped before it
 * (FLTSCWATCH; the Octane reports a SIGTRAP at it). Stepping it cannot
 * work: the trap between its ll and it clears the link, so it fails, and
 * the lock around it retries forever (left untraced, the kernel sends
 * SIGTRAP instead). So the thread goes back to its ll and runs alone,
 * with the watched areas out, to a break after the sc: ll and sc
 * untrapped, atomic as the program meant. If the sc failed (another
 * thread's store came between), again from the ll. Reported as a watched
 * write: the Session goes on if the watched value is the same. Without
 * an ll in sight td makes the store itself (all threads stopped). */
bool IrixTarget::store_conditional(Addr pc, Event* ev)
{
    uint32_t insn;
    if (!read_insn(pc, &insn))
        return false;
    uint32_t op = insn >> 26;
    if (op != 0x38 && op != 0x3c)       /* SC, SCD */
        return false;
    int base = (int)((insn >> 21) & 31), rt = (int)((insn >> 16) & 31), i;
    int16_t off = (int16_t)(insn & 0xffff);
    gregset_t g;
    if (tctl(cur_, PIOCGREG, PTFD_EQL, &g) != 0)
        return false;
    Addr a = (Addr)g[CTX_R0 + base] + (Addr)(int64_t)off;
    if (arch()->ptr_size == 4)
        a = (uint32_t)a;
    /* Only on a page being watched: else it is the program's own trap. */
    Addr page = (Addr)getpagesize();
    for (i = 0; i < areas_.size(); i++) {
        Addr lo = areas_[i].addr & ~(page - 1);
        Addr hi = (areas_[i].addr + areas_[i].size + page - 1) & ~(page - 1);
        if (a >= lo && a < hi)
            break;
    }
    if (i == areas_.size())
        return false;
    /* Its ll: LL or LLD, a few instructions back, the same base and
     * offset. */
    Addr ll = 0;
    for (i = 1; i <= 16 && !ll; i++) {
        uint32_t x;
        if (!read_insn(pc - 4 * (Addr)i, &x))
            break;
        uint32_t xo = x >> 26;
        if ((xo == 0x30 || xo == 0x34) && (int)((x >> 21) & 31) == base &&
            (int16_t)(x & 0xffff) == off)
            ll = pc - 4 * (Addr)i;
    }
    if (ll && run_through_sc(ll, pc, rt, ev)) {
        ev->watch = watch_at(a);
        return true;
    }
    /* Make the store here. */
    if (tctl(cur_, PIOCGREG, PTFD_EQL, &g) != 0)
        return false;
    uint64_t v = (uint64_t)g[CTX_R0 + rt];
    int n = op == 0x38 ? 4 : 8;
    unsigned char w[8];
    for (i = 0; i < n; i++)
        w[i] = (unsigned char)(v >> (8 * (n - 1 - i)));    /* big-endian */
    if (!write(a, w, (size_t)n))
        return false;
    if (rt)
        g[CTX_R0 + rt] = 1;
    g[CTX_EPC] = g[CTX_EPC] + 4;
    if (tctl(cur_, PIOCSREG, PTFD_EQL, &g) != 0)
        return false;
    ev->kind = EV_WATCH;
    ev->watch = watch_at(a);
    ev->pc = pc + 4;
    return true;
}

bool IrixTarget::read_insn(Addr a, uint32_t* insn)
{
    unsigned char b[4];
    if (!read(a, b, 4))
        return false;
    *insn = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
    return true;
}

/* The current thread from its ll at ll to past its sc at sc, alone and
 * with the watched areas out (see store_conditional). False if it could
 * not start. */
bool IrixTarget::run_through_sc(Addr ll, Addr sc, int rt, Event* ev)
{
    const Arch* ar = arch();
    Addr brk = sc + 4;
    unsigned char saved[4];
    if (!read(brk, saved, 4))
        return false;
    int i;
    for (i = 0; i < areas_.size(); i++)
        set_watch(areas_[i].addr, 0, MA_WRITE);
    bool ok = write(brk, ar->bp, (size_t)ar->bp_len);
    int tries;
    bool done = false;
    prstatus_t st;
    for (tries = 0; ok && tries < 100 && !done; tries++) {
        if (!set_pc(ll)) {
            ok = false;
            break;
        }
        prrun_t run;
        memset(&run, 0, sizeof(run));
        run.pr_flags = PRCFAULT | PRCSIG;
        int r = tctl(cur_, PIOCRUN, PTFD_EQL, &run);
        /* Alone it may wait for a lock another (stopped) thread holds:
         * 50 ms at most, then stopped wherever it is. */
        int naps;
        for (naps = 0; r == 0; naps++) {
            r = tctl(cur_, PIOCSTATUS, PTFD_EQL, &st);
            if (r != 0 || (st.pr_flags & PR_ISTOP))
                break;
            if (naps == 250) {
                r = tctl(cur_, PIOCSTOP, PTFD_EQL, &st);
                break;
            }
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 200000;
            nanosleep(&ts, 0);
        }
        if (r != 0 || st.pr_why != PR_FAULTED || st.pr_what != FLTBPT) {
            ok = false;     /* something else stopped it, or the time did */
            break;
        }
        Addr at = (Addr)st.pr_reg[CTX_EPC];
        if (ar->ptr_size == 4)
            at = (uint32_t)at;
        if (at != brk) {
            ok = false;
            break;
        }
        done = rt == 0 || st.pr_reg[CTX_R0 + rt] != 0;     /* the sc succeeded */
    }
    write(brk, saved, 4);
    for (i = 0; i < areas_.size(); i++)
        set_watch(areas_[i].addr, areas_[i].size, MA_WRITE);
    if (!ok && tries == 0)
        return false;       /* nothing ran: the store is made here */
    ev->kind = EV_WATCH;
    ev->pc = brk;
    if (!ok || !done) {
        /* Stopped on the way, or the sc kept failing: it goes on from
         * where it is, its state the program's own (it was started at
         * the ll as a failed sc would). A stop with nothing changed, for
         * the Session to pass. */
        Regs r;
        if (get_regs(&r))
            ev->pc = r.pc;
    }
    return true;
}

/* prwatch_t holds a pointer: a 64-bit program's takes the 64-bit layout
 * (PIOC_IRIX5_64), whatever the debugger's own ABI. Size 0 removes an
 * area. */
bool IrixTarget::set_watch(Addr a, size_t n, int flags)
{
    int r;
    int tries = 0;
    do {
        if (arch()->ptr_size == 8) {
            struct {
                uint64_t vaddr, size, wflags, filler;
            } w;
            w.vaddr = a;
            w.size = n;
            w.wflags = (uint64_t)flags;
            w.filler = 0;
            r = ioctl(fd_, PIOCSWATCH | PIOC_IRIX5_64, &w);
        } else {
            prwatch_t w;
            memset(&w, 0, sizeof(w));
            w.pr_vaddr = (caddr_t)(unsigned long)a;
            w.pr_size = (ulong_t)n;
            w.pr_wflags = (ulong_t)flags;
            r = ioctl(fd_, PIOCSWATCH, &w);
        }
        /* EBUSY: the kernel is stepping over a watched page; again. */
    } while (r != 0 && errno == EBUSY && ++tries < 100);
    return r == 0;
}

/* Which area holds a (the fault's address, 32 bits wide for a 64-bit
 * program: compared on those). */
int IrixTarget::watch_at(Addr a) const
{
    int i;
    for (i = 0; i < areas_.size(); i++) {
        Addr lo = areas_[i].addr, hi = lo + areas_[i].size;
        if (arch()->ptr_size == 8 && a <= 0xffffffffu) {
            if ((uint32_t)a >= (uint32_t)lo && (uint32_t)a < (uint32_t)hi)
                return areas_[i].handle;
        } else if (a >= lo && a < hi) {
            return areas_[i].handle;
        }
    }
    return 0;
}

int IrixTarget::add_watch(Addr a, size_t n, ttr::Str* err)
{
    if (!set_watch(a, n, MA_WRITE)) {
        if (err)
            *err = errno == E2BIG    ? "the system watches no more areas"
                   : errno == EINVAL ? "it overlaps another watched area, or is not all memory"
                                     : strerror(errno);
        return -1;
    }
    Area w;
    w.addr = a;
    w.size = n;
    w.handle = next_watch_++;
    areas_.push_back(w);
    return w.handle;
}

void IrixTarget::remove_watch(int handle)
{
    int i;
    for (i = areas_.size() - 1; i >= 0; i--)
        if (areas_[i].handle == handle) {
            if (alive_)
                set_watch(areas_[i].addr, 0, MA_WRITE);
            areas_.remove_at(i);
        }
}

} /* namespace */

Target* Target::create()
{
    return new IrixTarget;
}

/* /proc/pinfo/<pid> (readable by everyone, what ps reads): the owner,
 * the name and the arguments from PIOCPSINFO. */
int Target::processes(ttr::Vector<ProcessInfo>* out)
{
    DIR* d = opendir("/proc/pinfo");
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
        char p[64];
        sprintf(p, "/proc/pinfo/%d", pids[i]);
        int fd = open(p, O_RDONLY);
        if (fd < 0)
            continue;
        prpsinfo_t ps;
        int r = ioctl(fd, PIOCPSINFO, &ps);
        close(fd);
        if (r != 0 || ps.pr_uid != getuid() || ps.pr_zomb)
            continue;
        ProcessInfo pi;
        pi.pid = pids[i];
        char n[sizeof(ps.pr_fname) + 1];
        memcpy(n, ps.pr_fname, sizeof(ps.pr_fname));
        n[sizeof(ps.pr_fname)] = 0;
        pi.name = n;
        char a[sizeof(ps.pr_psargs) + 1];
        memcpy(a, ps.pr_psargs, sizeof(ps.pr_psargs));
        a[sizeof(ps.pr_psargs)] = 0;
        pi.args = a;
        pi.start = (uint64_t)ps.pr_start.tv_sec * 1000000000u + (uint64_t)ps.pr_start.tv_nsec;
        out->push_back(pi);
    }
    sort_processes(out);
    return out->size();
}

} /* namespace td */
