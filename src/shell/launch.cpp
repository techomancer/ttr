#include "shell/launch.h"

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

}
