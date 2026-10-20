#include "tp.h"
#include "sys.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

namespace tp {

Pty::Pty() : fd_(-1)
{
    slave_[0] = 0;
    error_[0] = 0;
    size_.cols = 80;
    size_.rows = 24;
    size_.width_px = size_.height_px = 0;
}

Pty::~Pty()
{
    close();
}

void Pty::set_error(const char* what, int err)
{
    sprintf(error_, "%.80s: %.70s", what, strerror(err));
}

bool Pty::open(const WinSize& size)
{
    close();
    errno = 0;
    int fd = sys::open_master(slave_, sizeof(slave_));
    if (fd < 0) {
        set_error("cannot open a pseudo-terminal", errno ? errno : ENOENT);
        return false;
    }
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0 ||
        fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
        set_error("fcntl on the pty master", errno);
        ::close(fd);
        return false;
    }
    fd_ = fd;
    size_ = size;
    /* Some systems only take the size once the slave is open; the child
     * sets it again on the slave. */
    resize(size);
    return true;
}

void Pty::close()
{
    if (fd_ >= 0)
        ::close(fd_);
    fd_ = -1;
    slave_[0] = 0;
}

bool Pty::resize(const WinSize& size)
{
    size_ = size;
    if (fd_ < 0)
        return false;
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_col = (unsigned short)size.cols;
    ws.ws_row = (unsigned short)size.rows;
    ws.ws_xpixel = (unsigned short)size.width_px;
    ws.ws_ypixel = (unsigned short)size.height_px;
    if (ioctl(fd_, TIOCSWINSZ, &ws) < 0) {
        set_error("TIOCSWINSZ", errno);
        return false;
    }
    return true;
}

long Pty::read(void* buf, size_t n)
{
    if (fd_ < 0)
        return -1;
    for (;;) {
        ssize_t r = ::read(fd_, buf, n);
        if (r > 0)
            return (long)r;
        if (r == 0)
            return -1;                  /* IRIX: end of file at hangup */
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        if (errno != EIO)               /* Linux: EIO at hangup */
            set_error("reading the pty", errno);
        return -1;
    }
}

long Pty::write(const void* buf, size_t n)
{
    if (fd_ < 0)
        return -1;
    for (;;) {
        ssize_t r = ::write(fd_, buf, n);
        if (r >= 0)
            return (long)r;
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        set_error("writing the pty", errno);
        return -1;
    }
}

int Pty::wait_readable(int timeout_ms)
{
    if (fd_ < 0)
        return -1;
    return sys::wait_readable(fd_, timeout_ms);
}

} /* namespace tp */
