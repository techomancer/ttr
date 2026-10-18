#include "shell/runcmd.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ttr {

static const int k_max_output = 65536;

CommandRunner::CommandRunner(XtAppContext app)
    : app_(app), pid_(-1), fd_(-1), input_(0), timer_(0), out_(0),
      out_len_(0), done_(0), closure_(0)
{
    out_ = new char[k_max_output + 1];
}

CommandRunner::~CommandRunner()
{
    if (pid_ > 0) {
        done_ = 0;
        kill(pid_, SIGKILL);
        finish(true);
    }
    delete [] out_;
}

bool CommandRunner::run(const char* command, unsigned long timeout_ms,
                        Done done, void* closure, bool with_stderr)
{
    if (busy())
        return false;
    int fds[2];
    if (pipe(fds) != 0)
        return false;
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return false;
    }
    if (pid == 0) {
        dup2(fds[1], 1);
        if (with_stderr)
            dup2(fds[1], 2);
        int null = open("/dev/null", O_RDWR);
        if (null >= 0) {
            dup2(null, 0);
            if (!with_stderr)
                dup2(null, 2);
        }
        close(fds[0]);
        close(fds[1]);
        execl("/bin/sh", "sh", "-c", command, (char*)0);
        _exit(127);
    }

    close(fds[1]);
    fd_ = fds[0];
    fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL) | O_NONBLOCK);
    fcntl(fd_, F_SETFD, FD_CLOEXEC);
    pid_ = pid;
    out_len_ = 0;
    done_ = done;
    closure_ = closure;
    input_ = XtAppAddInput(app_, fd_, (XtPointer)XtInputReadMask, input_cb,
                           (XtPointer)this);
    timer_ = XtAppAddTimeOut(app_, timeout_ms, timeout_cb, (XtPointer)this);
    return true;
}

void CommandRunner::input_cb(XtPointer closure, int* fd, XtInputId* id)
{
    (void)id;
    CommandRunner* self = (CommandRunner*)closure;
    char buf[4096];
    for (;;) {
        ssize_t n = read(*fd, buf, sizeof(buf));
        if (n > 0) {
            int room = k_max_output - self->out_len_;
            if (n > room)
                n = room;
            memcpy(self->out_ + self->out_len_, buf, n);
            self->out_len_ += (int)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            return;     /* more later */
        break;          /* end of output, or an error */
    }
    self->finish(false);
}

void CommandRunner::timeout_cb(XtPointer closure, XtIntervalId* id)
{
    (void)id;
    CommandRunner* self = (CommandRunner*)closure;
    self->timer_ = 0;
    kill(self->pid_, SIGKILL);
    self->finish(true);
}

void CommandRunner::finish(bool timed_out)
{
    if (input_) {
        XtRemoveInput(input_);
        input_ = 0;
    }
    if (timer_) {
        XtRemoveTimeOut(timer_);
        timer_ = 0;
    }
    close(fd_);
    fd_ = -1;
    /* The output is closed, so the command is all but done. Never block
     * on it for long: whatever still runs after ~100 ms is killed. */
    int status = 0;
    bool exited = false;
    int tries;
    for (tries = 0; tries < 20 && !exited; tries++) {
        if (waitpid(pid_, &status, WNOHANG) == pid_)
            exited = true;
        else
            usleep(5000);
    }
    if (!exited) {
        kill(pid_, SIGKILL);
        waitpid(pid_, &status, 0);
        timed_out = true;
    }
    pid_ = -1;
    out_[out_len_] = 0;

    bool ok = !timed_out && exited && WIFEXITED(status) &&
              WEXITSTATUS(status) == 0;
    Done done = done_;
    done_ = 0;
    if (done)
        done(closure_, ok, out_);
}

}
