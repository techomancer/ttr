/*
 * tp system layer for Linux and the BSDs: posix_openpt, TIOCSCTTY,
 * IUTF8, separate VMIN/VTIME slots.
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
    int fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (fd < 0)
        return -1;
    const char* name;
    if (grantpt(fd) < 0 || unlockpt(fd) < 0 || !(name = ptsname(fd)) || strlen(name) >= len) {
        int e = errno;
        close(fd);
        errno = e;
        return -1;
    }
    strcpy(slave, name);
    return fd;
}

int child_acquire_tty(const char* slave)
{
    if (setsid() < 0)
        return -1;
    int fd = open(slave, O_RDWR | O_NOCTTY);
    if (fd < 0)
        return -1;
    if (ioctl(fd, TIOCSCTTY, 0) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

void tune_termios(struct termios* t, bool utf8)
{
    t->c_iflag |= IMAXBEL;
    if (utf8)
        t->c_iflag |= IUTF8;
    t->c_lflag |= ECHOCTL | ECHOKE;
    t->c_cc[VWERASE] = 0x17;
    t->c_cc[VREPRINT] = 0x12;
    t->c_cc[VLNEXT] = 0x16;
    t->c_cc[VDISCARD] = 0x0f;
    t->c_cc[VMIN] = 1;
    t->c_cc[VTIME] = 0;
}

int fd_limit()
{
    long n = sysconf(_SC_OPEN_MAX);
    if (n <= 0 || n > 65536)
        n = 1024;
    return (int)n;
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
/* Other processes, through Linux's /proc. Elsewhere (the BSDs, without
 * /proc mounted) the answers are "cannot tell". */

#include <dirent.h>
#include <stdio.h>

namespace tp {
namespace sys {

bool process_cwd(int pid, char* buf, size_t n)
{
    char link[64];
    sprintf(link, "/proc/%d/cwd", pid);
    if (n == 0)
        return false;
    ssize_t r = readlink(link, buf, n - 1);
    if (r <= 0)
        return false;
    buf[r] = 0;
    return buf[0] == '/';
}

/* The parent pid from /proc/N/stat: "pid (comm) state ppid ...", where
 * comm may itself contain spaces and parentheses. */
static int parent_of(const char* pid)
{
    char path[64], buf[512];
    if (strlen(pid) > 20)
        return -1;
    sprintf(path, "/proc/%s/stat", pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    ssize_t r = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (r <= 0)
        return -1;
    buf[r] = 0;
    char* p = strrchr(buf, ')');
    if (!p || p[1] != ' ')
        return -1;
    char state;
    int ppid;
    if (sscanf(p + 2, "%c %d", &state, &ppid) != 2)
        return -1;
    return ppid;
}

int process_has_children(int pid)
{
    DIR* d = opendir("/proc");
    if (!d)
        return -1;
    int found = 0;
    bool any = false;
    struct dirent* e;
    while ((e = readdir(d)) != 0) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9')
            continue;
        int pp = parent_of(e->d_name);
        if (pp < 0)
            continue;
        any = true;
        if (pp == pid) {
            found = 1;
            break;
        }
    }
    closedir(d);
    return any ? found : -1;
}

} /* namespace sys */
} /* namespace tp */
