/*
 * Programs started with ttr. When ttr starts (but not when it restarts
 * in place), it runs, in order:
 *
 *   0. XDG autostart entries (core/xdgautostart.h), if xdgAutostart is
 *      true (default false): the desktop's own helpers and services.
 *
 *   1. the enabled entries of the startup list, ~/.config/ttr/startup,
 *      edited on Settings > Startup or by hand:
 *
 *        [program]
 *        command: nm-applet
 *        enabled: true
 *
 *   2. the script ~/.config/ttr/autostart.sh, with sh, if it exists and
 *      autostartScript is true (the default).
 *
 * Each is started detached, like the start menu does, without waiting.
 */
#ifndef TTR_CORE_STARTUP_H
#define TTR_CORE_STARTUP_H

#include "core/str.h"
#include "core/vector.h"

namespace ttr {

struct StartupItem {
    Str command;
    bool enabled;

    StartupItem() : enabled(true) {}
};

class StartupList {
public:
    StartupList();
    ~StartupList();

    bool load(const char* path);
    bool save(const char* path) const;

    int size() const { return items_.size(); }
    StartupItem* at(int i) { return items_[i]; }
    const StartupItem* at(int i) const { return items_[i]; }

    /* Takes ownership. */
    void add(StartupItem* s);
    void remove(int i);
    void swap(int i, int j);
    void clear();

    /* $XDG_CONFIG_HOME/ttr/startup, or ~/.config/ttr/startup. */
    static Str default_path();

private:
    StartupList(const StartupList&);
    StartupList& operator=(const StartupList&);

    Vector<StartupItem*> items_;
};

/* $XDG_CONFIG_HOME/ttr/autostart.sh, or ~/.config/ttr/autostart.sh. */
Str autostart_script_path();

/* Write a commented example script if there is none. False if it cannot
 * be written. */
bool create_autostart_script(const char* path);

/* Start XDG autostart if 'xdg' (Terminal=true entries with 'terminal'),
 * then the list, then the script if 'script'. */
void run_startup(bool xdg, bool script, const char* terminal);

}

#endif
