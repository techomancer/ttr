/*
 * XDG autostart (the freedesktop.org Desktop Application Autostart spec):
 * the .desktop files in $XDG_CONFIG_HOME/autostart (~/.config/autostart)
 * and in each of $XDG_CONFIG_DIRS (/etc/xdg) + /autostart. A file in an
 * earlier directory hides one of the same name in a later directory.
 *
 * An entry starts unless it is turned off (Hidden=true or
 * X-GNOME-Autostart-enabled=false), its OnlyShowIn / NotShowIn leaves us
 * out (we match $XDG_CURRENT_DESKTOP and "TTR"), its TryExec program is not
 * installed, its AutostartCondition does not hold, or systemd user units
 * start it (X-GNOME-HiddenUnderSystemd or X-KDE-HiddenUnderSystemd, on a
 * system running systemd). Of the conditions only
 * "if-exists FILE" and "unless-exists FILE" (relative to $XDG_CONFIG_HOME)
 * can be checked; the GNOME and GSettings ones are taken as false.
 *
 * Turning an entry off or on, as other desktops do, writes the user's copy
 * in ~/.config/autostart with or without Hidden=true.
 *
 * Linux desktops only; IRIX has none of these directories.
 */
#ifndef TTR_CORE_XDGAUTOSTART_H
#define TTR_CORE_XDGAUTOSTART_H

#include "core/str.h"
#include "core/vector.h"

namespace ttr {

struct XdgAutostartEntry {
    enum State {
        START,          /* will be started */
        OFF,            /* turned off */
        OTHER_DESKTOP,  /* OnlyShowIn / NotShowIn, or systemd starts it */
        MISSING,        /* TryExec not installed */
        CONDITION,      /* AutostartCondition does not hold */
        BROKEN          /* not an application, or no Exec */
    };

    Str id;             /* file name, e.g. "blueman.desktop" */
    Str path;           /* the file that counts */
    Str name;
    Str command;        /* Exec without field codes */
    Str detail;         /* why it does not start, for the settings page */
    bool terminal;
    State status;       /* not "Status": Xlib defines that */
};

/* Every entry, sorted by name. The caller owns them. */
void xdg_autostart_scan(Vector<XdgAutostartEntry*>* out);
void xdg_autostart_free(Vector<XdgAutostartEntry*>* list);

/* Write the user's copy of 'e' turned on or off. False if it cannot be
 * written. */
bool xdg_autostart_set_enabled(const XdgAutostartEntry& e, bool on);

/* $XDG_CONFIG_HOME/autostart (or ~/.config/autostart), for display. */
Str xdg_autostart_user_dir();

/* Start every entry whose status is START. 'terminal' runs Terminal=true
 * entries, e.g. "xterm -e". */
void run_xdg_autostart(const char* terminal);

}

#endif
