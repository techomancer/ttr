#include "tp.h"
#include "sys.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

extern char** environ;

namespace tp {

namespace {

/* What the child reports through the status pipe when something fails
 * between fork and exec. */
enum Stage {
    STAGE_TTY = 1,
    STAGE_MODES,
    STAGE_CWD,
    STAGE_EXEC
};

struct Report {
    int stage;
    int err;
};

/* A null-terminated array of malloc'ed strings. */
struct StrList {
    char** v;
    int n, cap;
    StrList() : v(0), n(0), cap(0) {}
    ~StrList()
    {
        for (int i = 0; i < n; i++)
            free(v[i]);
        free(v);
    }
    bool add(const char* s, size_t len)
    {
        if (n + 2 > cap) {
            int ncap = cap ? cap * 2 : 32;
            char** p = (char**)realloc(v, (size_t)ncap * sizeof(char*));
            if (!p)
                return false;
            v = p;
            cap = ncap;
        }
        char* c = (char*)malloc(len + 1);
        if (!c)
            return false;
        memcpy(c, s, len);
        c[len] = 0;
        v[n++] = c;
        v[n] = 0;
        return true;
    }
    bool add(const char* s) { return add(s, strlen(s)); }
};

size_t name_len(const char* entry)
{
    const char* eq = strchr(entry, '=');
    return eq ? (size_t)(eq - entry) : strlen(entry);
}

bool same_name(const char* a, const char* b)
{
    size_t la = name_len(a), lb = name_len(b);
    return la == lb && strncmp(a, b, la) == 0;
}

/* The environment for the child: ours, minus the names opts.env
 * mentions, plus its "NAME=value" entries. */
bool build_env(const char* const* changes, StrList& out)
{
    for (char** e = environ; e && *e; e++) {
        bool drop = false;
        for (const char* const* c = changes; c && *c; c++)
            if (same_name(*e, *c)) {
                drop = true;
                break;
            }
        if (!drop && !out.add(*e))
            return false;
    }
    for (const char* const* c = changes; c && *c; c++)
        if (strchr(*c, '=') && !out.add(*c))
            return false;
    return true;
}

/* PATH lookup in the parent, so the child only calls execve. */
bool find_program(const char* prog, char* path, size_t len)
{
    if (strchr(prog, '/')) {
        if (strlen(prog) >= len)
            return false;
        strcpy(path, prog);
        return true;
    }
    const char* p = getenv("PATH");
    if (!p || !*p)
        p = "/usr/bin:/bin";
    while (*p) {
        const char* colon = strchr(p, ':');
        size_t dl = colon ? (size_t)(colon - p) : strlen(p);
        if (dl + 1 + strlen(prog) + 1 <= len) {
            if (dl == 0) {
                strcpy(path, prog);
            } else {
                memcpy(path, p, dl);
                path[dl] = '/';
                strcpy(path + dl + 1, prog);
            }
            struct stat st;
            if (stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, X_OK) == 0)
                return true;
        }
        if (!colon)
            break;
        p = colon + 1;
    }
    return false;
}

const char* base_name(const char* path)
{
    const char* s = strrchr(path, '/');
    return s ? s + 1 : path;
}

/* Sane, complete tty modes: nothing is left to whatever the pty driver
 * starts with (IRIX starts with SysV control characters). */
void make_termios(struct termios* t, const SpawnOptions& o)
{
    memset(t, 0, sizeof(*t));
    t->c_iflag = BRKINT | ICRNL | IXON;
    t->c_oflag = OPOST | ONLCR;
    t->c_cflag = CS8 | CREAD | HUPCL;
    t->c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN;
    t->c_cc[VINTR] = 0x03;
    t->c_cc[VQUIT] = 0x1c;
    t->c_cc[VERASE] = (cc_t)(o.erase ? o.erase : 0x7f);
    t->c_cc[VKILL] = 0x15;
    t->c_cc[VEOF] = 0x04;
    t->c_cc[VSTART] = 0x11;
    t->c_cc[VSTOP] = 0x13;
    t->c_cc[VSUSP] = 0x1a;
    cfsetispeed(t, B38400);
    cfsetospeed(t, B38400);
    sys::tune_termios(t, o.utf8);
}

void report(int fd, int stage)
{
    Report r;
    r.stage = stage;
    r.err = errno;
    ssize_t w = write(fd, &r, sizeof(r));
    (void)w;
    _exit(127);
}

} /* namespace */

Process::Process() : pid_(-1), done_(false), exited_(false), code_(0)
{
    error_[0] = 0;
}

Process::~Process()
{
}

bool Process::spawn(Pty& pty, const SpawnOptions& o)
{
    if (!pty.is_open()) {
        strcpy(error_, "the pty is not open");
        return false;
    }

    /* Everything the child needs is prepared here, before fork. */
    const char* shell_argv[2];
    const char* const* argv = o.argv;
    if (!argv || !argv[0]) {
        shell_argv[0] = sys::default_shell();
        shell_argv[1] = 0;
        argv = shell_argv;
    }
    char path[1024];
    if (!find_program(argv[0], path, sizeof(path))) {
        sprintf(error_, "%.100s: command not found", argv[0]);
        return false;
    }
    StrList args;
    for (int i = 0; argv[i]; i++) {
        if (i == 0 && o.login) {
            char a0[256];
            a0[0] = '-';
            strncpy(a0 + 1, base_name(argv[0]), sizeof(a0) - 2);
            a0[sizeof(a0) - 1] = 0;
            if (!args.add(a0))
                return false;
        } else if (!args.add(argv[i])) {
            return false;
        }
    }
    StrList env;
    if (!build_env(o.env, env)) {
        strcpy(error_, "out of memory");
        return false;
    }
    char* empty_env[1] = { 0 };
    char** envp = env.v ? env.v : empty_env;

    struct termios modes;
    make_termios(&modes, o);
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_col = (unsigned short)pty.size().cols;
    ws.ws_row = (unsigned short)pty.size().rows;
    ws.ws_xpixel = (unsigned short)pty.size().width_px;
    ws.ws_ypixel = (unsigned short)pty.size().height_px;
    int fd_limit = sys::fd_limit();
    char slave[64];
    strncpy(slave, pty.slave_name(), sizeof(slave) - 1);
    slave[sizeof(slave) - 1] = 0;

    int status_pipe[2];
    if (pipe(status_pipe) < 0) {
        sprintf(error_, "pipe: %.100s", strerror(errno));
        return false;
    }
    fcntl(status_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(status_pipe[1], F_SETFD, FD_CLOEXEC);

    pid_t pid = fork();
    if (pid < 0) {
        sprintf(error_, "fork: %.100s", strerror(errno));
        close(status_pipe[0]);
        close(status_pipe[1]);
        return false;
    }
    if (pid == 0) {
        /* Child: async-signal-safe calls only from here on. */
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, 0);
        struct sigaction dfl;
        memset(&dfl, 0, sizeof(dfl));
        dfl.sa_handler = SIG_DFL;
        for (int s = 1; s < 65; s++)
            sigaction(s, &dfl, 0);

        int wfd = status_pipe[1];
        close(status_pipe[0]);
        close(pty.fd());

        int tty = sys::child_acquire_tty(slave);
        if (tty < 0)
            report(wfd, STAGE_TTY);
        if (tcsetattr(tty, TCSANOW, &modes) < 0)
            report(wfd, STAGE_MODES);
        ioctl(tty, TIOCSWINSZ, &ws);
        if (o.console)
            sys::child_set_console(tty);      /* needs privilege; not fatal */
        if (dup2(tty, 0) < 0 || dup2(tty, 1) < 0 || dup2(tty, 2) < 0)
            report(wfd, STAGE_TTY);
        for (int fd = 3; fd < fd_limit; fd++)
            if (fd != wfd)
                close(fd);
        if (o.cwd && chdir(o.cwd) < 0)
            report(wfd, STAGE_CWD);
        execve(path, args.v, envp);
        report(wfd, STAGE_EXEC);
    }

    /* Parent: an empty status pipe means exec succeeded. */
    close(status_pipe[1]);
    Report r;
    ssize_t got;
    do {
        got = read(status_pipe[0], &r, sizeof(r));
    } while (got < 0 && errno == EINTR);
    close(status_pipe[0]);
    pid_ = (int)pid;
    done_ = false;
    exited_ = false;
    code_ = 0;
    if (got == (ssize_t)sizeof(r)) {
        static const char* const what[] = {
            "", "cannot make the pty the controlling terminal",
            "cannot set the terminal modes", "cannot change directory", "cannot run"
        };
        sprintf(error_, "%.60s%s%.60s: %.60s", what[r.stage],
                r.stage == STAGE_EXEC ? " " : "", r.stage == STAGE_EXEC ? path : "",
                strerror(r.err));
        wait(-1);
        return false;
    }
    return true;
}

void Process::record(int status)
{
    done_ = true;
    if (WIFEXITED(status)) {
        exited_ = true;
        code_ = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exited_ = false;
        code_ = WTERMSIG(status);
    }
}

bool Process::poll()
{
    if (pid_ <= 0 || done_)
        return false;
    int status = 0;
    pid_t r;
    do {
        r = waitpid((pid_t)pid_, &status, WNOHANG);
    } while (r < 0 && errno == EINTR);
    if (r == (pid_t)pid_) {
        if (WIFEXITED(status) || WIFSIGNALED(status))
            record(status);
        return !done_;
    }
    if (r < 0) {
        /* ECHILD: someone else reaped it (SIGCHLD ignored, for one). */
        done_ = true;
        exited_ = false;
        code_ = 0;
        return false;
    }
    return true;
}

bool Process::wait(int timeout_ms)
{
    int waited = 0;
    while (poll()) {
        if (timeout_ms >= 0 && waited >= timeout_ms)
            return false;
        sys::sleep_ms(10);
        waited += 10;
    }
    return done_;
}

bool Process::signal_group(int sig)
{
    if (pid_ <= 0 || done_)
        return false;
    if (kill(-(pid_t)pid_, sig) == 0)
        return true;
    return kill((pid_t)pid_, sig) == 0;
}

} /* namespace tp */
