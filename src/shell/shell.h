/*
 * The Motif shell: taskbar, start menu, window menu, tray. Talks to the
 * window manager only through WmLink and receives ShellLink calls, so the
 * same code can later run as a separate XWayland client.
 */
#ifndef TTR_SHELL_SHELL_H
#define TTR_SHELL_SHELL_H

#include "core/wmlink.h"

#include <X11/Intrinsic.h>

namespace ttr {

class StartMenu;
class Taskbar;
class WindowMenu;
struct Config;

class Shell : public ShellLink {
public:
    Shell(Widget parent, WmLink* wm, const Config& cfg);
    ~Shell();

    void task_added(const TaskInfo& t);
    void task_changed(const TaskInfo& t);
    void task_removed(ToplevelId id);
    void active_changed(ToplevelId id);
    void show_window_menu(ToplevelId id, int x_root, int y_root,
                          unsigned long time, ActionMask allowed,
                          int desktop);
    void desktop_changed(int current, int count);
    void show_start_menu(unsigned long time);
    void shutdown();

private:
    WindowMenu* winmenu_;
    StartMenu* start_;
    Taskbar* taskbar_;
};

}

#endif
