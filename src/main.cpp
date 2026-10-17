#include "shell/shell.h"
#include "x11/wm.h"

#include <stdio.h>
#include <unistd.h>

int main(int argc, char** argv)
{
    /* Xt removes the options it understands from argv; keep the original
     * command line for "Restart ttr". */
    char** saved = new char*[argc + 1];
    int i;
    for (i = 0; i < argc; i++)
        saved[i] = argv[i];
    saved[argc] = 0;

    int status;
    bool restart;
    {
        ttr::Wm wm;
        if (!wm.init(&argc, argv))
            return 1;
        ttr::Shell shell(wm.shell_parent(), &wm, wm.config());
        wm.set_shell(&shell);
        wm.start();
        status = wm.run();
        restart = wm.restart_requested();
    }

    if (restart) {
        execvp(saved[0], saved);
        perror("ttr: restart");
        return 1;
    }
    return status;
}
