/*
 * tp/sys.h - what differs between systems, implemented once per system:
 * sys_posix.cpp (Linux, the BSDs) and sys_irix.cpp. Not part of the
 * public API.
 *
 * Functions marked "child" run between fork() and exec() and may only
 * use async-signal-safe calls.
 */
#ifndef TP_SYS_H
#define TP_SYS_H

#include <stddef.h>

struct termios;

namespace tp {
namespace sys {

/* Open a pty master; store the slave's path. Returns the fd, or -1 with
 * errno set. The caller makes it non-blocking and close-on-exec. */
int open_master(char* slave, size_t len);

/* child: become a session leader with the slave as controlling
 * terminal. Returns the slave fd, or -1. */
int child_acquire_tty(const char* slave);

/* Add what is not portable to termios settings that already hold the
 * POSIX control characters and flags: UTF-8 input mode, the extra
 * control characters, VMIN and VTIME (which on SVR4 systems such as
 * IRIX share their slots with VEOF and VEOL and must not be set). */
void tune_termios(struct termios* t, bool utf8);

/* One more than the highest descriptor that may be open (called in the
 * parent; the child closes everything from 3 up to it). */
int fd_limit();

/* child: make this terminal receive console output. 0 or -1. */
int child_set_console(int fd);

/* Wait for fd to become readable: 1 readable, 0 timeout, -1 error. */
int wait_readable(int fd, int timeout_ms);

/* Sleep briefly. */
void sleep_ms(int ms);

/* The shell to run when none is given: $SHELL, the passwd entry, then
 * this system's default. */
const char* default_shell();

/* See tp::process_cwd and tp::process_has_children. */
bool process_cwd(int pid, char* buf, size_t n);
int process_has_children(int pid);

} /* namespace sys */
} /* namespace tp */

#endif
