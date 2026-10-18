#include "core/startup.h"
#include "core/theme.h"
#include "shell/notifysrc.h"
#include "shell/shell.h"
#include "x11/wm.h"

#include <stdio.h>
#include <stdlib.h>
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
    ttr::set_program_path(argv[0]);

    /* Xt removes the options it understands from argv; keep the original
     * command line for "Restart ttr". */
    char** saved = new char*[argc + 1];
    int i;
    for (i = 0; i < argc; i++)
        saved[i] = argv[i];
    saved[argc] = 0;

    /* Set across "Restart ttr", so the startup programs run only once.
     * Cleared, so programs we start do not inherit it. */
    const char* again = getenv("TTR_RESTARTED");
    bool restarted = again && strcmp(again, "1") == 0;
    static char clear_env[] = "TTR_RESTARTED=";
    putenv(clear_env);

    int status;
    bool restart;
    {
        ttr::Wm wm;
        if (!wm.init(&argc, argv))
            return 1;
        ttr::Shell shell(wm.shell_parent(), &wm, wm.config());
        wm.set_shell(&shell);
        wm.start();
        if (!restarted)
            ttr::run_startup(wm.config().xdg_autostart,
                             wm.config().autostart_script,
                             wm.config().terminal.c_str());
        status = wm.run();
        restart = wm.restart_requested();
    }

    if (restart) {
        static char restart_env[] = "TTR_RESTARTED=1";
        putenv(restart_env);
        execvp(saved[0], saved);
        perror("ttr: restart");
        return 1;
    }
    return status;
}
