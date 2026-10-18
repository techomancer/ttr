/*
 * Run a shell command without blocking the event loop and hand its
 * standard output to a callback. One command at a time; a command that
 * runs past its timeout is killed. Used for sound server tools, which may
 * hang if the server does.
 */
#ifndef TTR_SHELL_RUNCMD_H
#define TTR_SHELL_RUNCMD_H

#include "core/str.h"

#include <X11/Intrinsic.h>
#include <sys/types.h>

namespace ttr {

class CommandRunner {
public:
    /* ok: exited with status 0. output: what it wrote to stdout. */
    typedef void (*Done)(void* closure, bool ok, const char* output);

    explicit CommandRunner(XtAppContext app);
    ~CommandRunner();

    bool busy() const { return pid_ > 0; }
    /* False if busy or the command could not be started. With
     * 'with_stderr' its error output is collected too. */
    bool run(const char* command, unsigned long timeout_ms, Done done,
             void* closure, bool with_stderr = false);

private:
    static void input_cb(XtPointer closure, int* fd, XtInputId* id);
    static void timeout_cb(XtPointer closure, XtIntervalId* id);
    void finish(bool timed_out);

    XtAppContext app_;
    pid_t pid_;
    int fd_;
    XtInputId input_;
    XtIntervalId timer_;
    char* out_;
    int out_len_;
    Done done_;
    void* closure_;
};

}

#endif
