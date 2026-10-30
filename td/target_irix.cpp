/*
 * td/target_irix.cpp - Target on IRIX through the /proc ioctls
 * (td/DESIGN.md section 3.2): PIOCSTOP, PIOCRUN, PIOCWSTOP, fault and
 * system call tracing, registers and memory through /proc/<pid>.
 */
#include "target.h"

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
#include <unistd.h>

namespace td {

namespace {

class IrixTarget : public Target {
public:
    IrixTarget()
        : pid_(-1), fd_(-1), exe_fd_(-1), alive_(false), child_(false), arch_(0),
          next_watch_(1)
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
    void gone();
    void arch_from(const char* path);
    bool ended(Event* ev);
    bool decode(const prstatus_t& st, Event* ev);
    bool set_watch(Addr a, size_t n, int flags);
    int watch_at(Addr a) const;

    pid_t pid_;
    int fd_;            /* /proc/<pid> */
    int exe_fd_;        /* attach: the executable, from PIOCOPENM */
    bool alive_;
    bool child_;        /* started by us: waitpid gives its status */
    const Arch* arch_;
    ttr::Str path_;
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
    }
    return ioctl(fd_, PIOCSFAULT, &f) == 0 && ioctl(fd_, PIOCSTRACE, &s) == 0;
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
    if (step)
        run.pr_flags |= PRSTEP;
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
    do
        r = ioctl(fd_, PIOCWSTOP, &st);
    while (r != 0 && errno == EINTR);
    if (r != 0 || !decode(st, ev))
        return ended(ev);
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
    if (ioctl(fd_, PIOCSTATUS, &st) != 0)
        return ended(ev);
    if (!(st.pr_flags & PR_ISTOP)) {
        *got = false;
        return true;
    }
    if (!decode(st, ev))
        return ended(ev);
    return true;
}

/* PIOCSTOP waits until it has stopped; poll() then reports the stop
 * (PR_REQUESTED) as a signal 0. */
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
    Addr pc = (Addr)st.pr_reg[CTX_EPC];
    if (arch()->ptr_size == 4)
        pc = (uint32_t)pc;
    ev->pc = pc;
    if (st.pr_why == PR_FAULTED && st.pr_what == FLTBPT) {
        ev->kind = EV_BREAK;        /* EPC is the break instruction */
    } else if (st.pr_why == PR_FAULTED && st.pr_what == FLTTRACE) {
        ev->kind = EV_STEP;
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
        int r = ioctl(fd_, PIOCRUN, &run);
        if (r == 0) {
            do
                r = ioctl(fd_, PIOCWSTOP, &after);
            while (r != 0 && errno == EINTR);
        }
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
    if (ioctl(fd_, PIOCGREG, &g) != 0)
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
    if (ioctl(fd_, PIOCGREG, &g) != 0)
        return false;
    g[CTX_EPC] = pc;
    return ioctl(fd_, PIOCSREG, &g) == 0;
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

} /* namespace td */
