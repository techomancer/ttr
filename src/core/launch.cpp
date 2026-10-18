#include "core/launch.h"

#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ttr {

void spawn(const char* command)
{
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        if (fork() == 0) {
            execl("/bin/sh", "sh", "-c", command, (char*)0);
            _exit(127);
        }
        _exit(0);
    }
    if (pid > 0)
        waitpid(pid, 0, 0);
}

Str shell_quote(const char* s)
{
    /* Each ' becomes '\'' (4 characters). */
    char* buf = new char[strlen(s) * 4 + 3];
    char* p = buf;
    *p++ = '\'';
    for (; *s; s++) {
        if (*s == '\'') {
            memcpy(p, "'\\''", 4);
            p += 4;
        } else {
            *p++ = *s;
        }
    }
    *p++ = '\'';
    *p = 0;
    Str r(buf);
    delete [] buf;
    return r;
}

}
