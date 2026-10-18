#include "shell/notifysrc.h"
#include "shell/shell.h"
#include "x11/wm.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* ttr -notify SUMMARY [BODY]: show a notification in the running ttr. */
static int send_notification(int argc, char** argv)
{
    Display* dpy = XOpenDisplay(0);
    if (!dpy) {
        fprintf(stderr, "ttr: cannot open display\n");
        return 1;
    }
    ttr::send_x_notification(dpy, "ttr-notify", argv[2],
                             argc > 3 ? argv[3] : "");
    XCloseDisplay(dpy);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc >= 3 && strcmp(argv[1], "-notify") == 0)
        return send_notification(argc, argv);

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
