/*
 * tp: pseudo-terminals and the processes that run on them.
 *
 * Portable C++98 over POSIX. Everything that differs between systems
 * lives in one implementation file per system (sys_posix.cpp,
 * sys_irix.cpp) behind tp/sys.h; the Makefile links the right one. See
 * ttrt/DESIGN.md section 4.
 */
#ifndef TP_TP_H
#define TP_TP_H

#include <stddef.h>

namespace tp {

struct WinSize {
    int cols, rows;
    int width_px, height_px;     /* 0 if unknown */
};

/* The master side of a pseudo-terminal. */
class Pty {
public:
    Pty();
    ~Pty();

    /* Open a master, non-blocking and close-on-exec. */
    bool open(const WinSize& size);
    void close();
    bool is_open() const { return fd_ >= 0; }
    int fd() const { return fd_; }
    const char* slave_name() const { return slave_; }
    const WinSize& size() const { return size_; }

    /* Tell the child its new size; it gets SIGWINCH. */
    bool resize(const WinSize& size);

    /* > 0: bytes read; 0: nothing available now; -1: the other side is
     * gone (every process holding the slave has exited) or an error. */
    long read(void* buf, size_t n);
    /* >= 0: bytes written (0: would block); -1: error. */
    long write(const void* buf, size_t n);
    /* Wait up to timeout_ms for data: 1 readable, 0 timeout, -1 error. */
    int wait_readable(int timeout_ms);
    /* The process group in the foreground of the terminal (the job
     * running in it), or -1 where the system cannot tell from the master
     * side. */
    int foreground_group() const;

    const char* error() const { return error_; }

private:
    void set_error(const char* what, int err);

    int fd_;
    char slave_[64];
    WinSize size_;
    char error_[160];

    Pty(const Pty&);
    Pty& operator=(const Pty&);
};

struct SpawnOptions {
    /* Program and arguments, null-terminated; null: the user's shell. A
     * program without a slash is looked up in PATH. */
    const char* const* argv;
    /* Start the shell as a login shell (argv[0] with a leading '-'). */
    bool login;
    /* Environment changes, null-terminated: "NAME=value" sets, "NAME"
     * removes. Everything else is inherited. */
    const char* const* env;
    const char* cwd;             /* null: inherit */
    bool console;                /* redirect console output here (TIOCCONS) */
    bool utf8;                   /* UTF-8 tty mode where the system has one */
    int erase;                   /* erase character, 0x7f (default) or 0x08 */

    SpawnOptions() : argv(0), login(false), env(0), cwd(0), console(false),
                     utf8(true), erase(0x7f) {}
};

/* A child process running on a pty, as the leader of its own session. */
class Process {
public:
    Process();
    ~Process();

    /* Fork and exec. Returns false if anything up to and including exec
     * fails; error() says what. */
    bool spawn(Pty& pty, const SpawnOptions& opts);

    int pid() const { return pid_; }
    /* Reap without blocking. True while the child is running. */
    bool poll();
    /* Reap, waiting up to timeout_ms (-1: forever). True if it ended. */
    bool wait(int timeout_ms);
    bool running() const { return pid_ > 0 && !done_; }
    bool exited() const { return done_ && exited_; }
    int exit_code() const { return code_; }
    int term_signal() const { return done_ && !exited_ ? code_ : 0; }

    /* Send a signal to the child's process group (the job running in the
     * terminal). */
    bool signal_group(int sig);

    const char* error() const { return error_; }

private:
    void record(int status);

    int pid_;
    bool done_;
    bool exited_;
    int code_;
    char error_[200];

    Process(const Process&);
    Process& operator=(const Process&);
};

/* Facts about other processes, read from the kernel without their help
 * (ttrc/DESIGN.md sections 4.4 and 4.5). Per system: /proc on Linux,
 * the /proc ioctls on IRIX. */

/* The working directory of process pid. False where it cannot be read. */
bool process_cwd(int pid, char* buf, size_t n);
/* 1 if pid has child processes, 0 if not, -1 if the system cannot tell. */
int process_has_children(int pid);

} /* namespace tp */

#endif
