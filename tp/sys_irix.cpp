/*
 * tp system layer for IRIX 6.5: _getpty, SVR4 controlling terminal
 * (the first tty a session leader opens), VMIN/VTIME sharing the VEOF and
 * VEOL slots, no IUTF8, getdtablehi. Facts measured on 6.5.22; see
 * ttrt/XWSH-NOTES.md section 3.
 */
#include "sys.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

namespace tp {
namespace sys {

int open_master(char* slave, size_t len)
{
    int fd = -1;
    /* Mode 0620 like grantpt(): the owner reads and writes, group tty may
     * write (mesg, write). For non-root callers _getpty asks the setuid
     * /usr/sbin/mkpts helper to set it up. */
    char* name = _getpty(&fd, O_RDWR | O_NOCTTY, 0620, 0);
    if (!name || fd < 0)
        return -1;
    if (strlen(name) >= len) {
        close(fd);
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(slave, name);
    return fd;
}

int child_acquire_tty(const char* slave)
{
    if (setsid() < 0)
        return -1;
    /* No O_NOCTTY: opening it makes it our controlling terminal. IRIX has
     * no TIOCSCTTY. */
    return open(slave, O_RDWR);
}

void tune_termios(struct termios* t, bool utf8)
{
    (void)utf8;                         /* no IUTF8 on IRIX */
    t->c_iflag |= IMAXBEL;
    t->c_lflag |= ECHOCTL | ECHOKE;
    t->c_cc[VWERASE] = 0x17;
    t->c_cc[VREPRINT] = 0x12;
    t->c_cc[VLNEXT] = 0x16;
    t->c_cc[VDISCARD] = 0x0f;
    t->c_cc[VDSUSP] = 0x19;
    /* VMIN and VTIME are VEOF and VEOL here: leave them alone. */
}

int fd_limit()
{
    int n = getdtablehi();
    return n > 0 ? n : 256;
}

int child_set_console(int fd)
{
    return ioctl(fd, TIOCCONS, 0);
}

int wait_readable(int fd, int timeout_ms)
{
    struct pollfd p;
    p.fd = fd;
    p.events = POLLIN;
    p.revents = 0;
    int r;
    do {
        r = poll(&p, 1, timeout_ms);
    } while (r < 0 && errno == EINTR);
    if (r < 0)
        return -1;
    return r > 0 ? 1 : 0;
}

void sleep_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, 0);
}

const char* default_shell()
{
    const char* s = getenv("SHELL");
    if (s && *s)
        return s;
    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_shell && *pw->pw_shell)
        return pw->pw_shell;
    return "/bin/sh";
}

} /* namespace sys */
} /* namespace tp */

/* ------------------------------------------------------------------ */
/* Other processes, through IRIX's /proc ioctls (ttrc/DESIGN.md 4.5). */

#include <dirent.h>
#include <stdio.h>
#include <sys/procfs.h>

/* The checkpoint/restart ioctl that opens a process's working directory;
 * not in the 6.5 headers, verified on 6.5.22. */
#ifndef PIOCCKPTOPENCWD
#define PIOCCKPTOPENCWD (PIOC | 216)
#endif

namespace tp {
namespace sys {

bool process_cwd(int pid, char* buf, size_t n)
{
    char path[64];
    sprintf(path, "/proc/%d", pid);
    int pr = open(path, O_RDONLY);
    if (pr < 0)
        return false;
    int dir = ioctl(pr, PIOCCKPTOPENCWD, 0);
    close(pr);
    if (dir < 0)
        return false;
    /* getcwd only works on our own directory: go there and back. */
    char here[1024];
    bool ok = false;
    if (getcwd(here, sizeof(here)) && fchdir(dir) == 0) {
        ok = getcwd(buf, n) != 0;
        if (chdir(here) != 0)
            ok = false;
    }
    close(dir);
    return ok;
}

int process_has_children(int pid)
{
    /* /proc/pinfo answers PIOCPSINFO for everyone's processes. */
    const char* base = "/proc/pinfo";
    DIR* d = opendir(base);
    if (!d) {
        base = "/proc";
        d = opendir(base);
    }
    if (!d)
        return -1;
    int found = 0;
    bool any = false;
    struct dirent* e;
    while ((e = readdir(d)) != 0) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9' || strlen(e->d_name) > 20)
            continue;
        char path[64];
        sprintf(path, "%s/%s", base, e->d_name);
        int fd = open(path, O_RDONLY);
        if (fd < 0)
            continue;
        prpsinfo_t ps;
        if (ioctl(fd, PIOCPSINFO, &ps) == 0) {
            any = true;
            if ((int)ps.pr_ppid == pid)
                found = 1;
        }
        close(fd);
        if (found)
            break;
    }
    closedir(d);
    return any ? found : -1;
}

} /* namespace sys */
} /* namespace tp */
