/*
 * The boundary between the shell (taskbar, menus) and the window manager,
 * from docs/wm-design.md 3.5. WmLink carries shell -> WM requests,
 * ShellLink carries WM -> shell notifications. In X11 mode both sides are
 * in-process; in Wayland mode they become messages on a socket, so only
 * plain data crosses (toplevel ids are opaque to the shell).
 */
#ifndef TTR_CORE_WMLINK_H
#define TTR_CORE_WMLINK_H

#include "core/geom.h"

namespace ttr {

class Icon;

typedef unsigned long ToplevelId;

enum WmAction {
    ACT_RESTORE,
    ACT_MOVE,
    ACT_SIZE,
    ACT_MINIMIZE,
    ACT_MAXIMIZE,
    ACT_LOWER,
    ACT_CLOSE,
    ACT_ACTIVATE,   /* restore if minimized, raise and focus */
    ACT_COUNT
};

/* Bit (1 << WmAction) for each allowed window action. */
typedef unsigned int ActionMask;

inline ActionMask all_actions()
{
    return (1u << ACT_COUNT) - 1;
}

inline bool action_allowed(ActionMask m, WmAction a)
{
    return (m & (1u << a)) != 0;
}

/* A window's desktop: 0 .. count-1, or this for all of them. */
static const int ALL_DESKTOPS = -1;

/* Shell -> WM. */
class WmLink {
public:
    virtual ~WmLink() {}
    virtual void request(ToplevelId id, WmAction action) = 0;
    virtual void switch_desktop(int desktop) = 0;
    virtual void move_to_desktop(ToplevelId id, int desktop) = 0;
    /* The shell's panel window and where it sits; the WM keeps it on top
     * and excludes its area from maximized windows. */
    virtual void set_panel(unsigned long window, const Rect& r) = 0;
    virtual void quit() = 0;
    virtual void restart() = 0;
};

/* A window as the taskbar sees it. */
struct TaskInfo {
    ToplevelId id;
    const char* title;
    const Icon* icon;       /* may be null; valid until the next update */
    bool minimized;
    ActionMask allowed;
    int desktop;            /* or ALL_DESKTOPS */
    bool on_current;        /* visible on the current desktop */
};

/* WM -> shell. */
class ShellLink {
public:
    virtual ~ShellLink() {}
    virtual void task_added(const TaskInfo& t) = 0;
    virtual void task_changed(const TaskInfo& t) = 0;
    virtual void task_removed(ToplevelId id) = 0;
    /* The taskbar entry to show as active, or 0. */
    virtual void active_changed(ToplevelId id) = 0;
    virtual void show_window_menu(ToplevelId id, int x_root, int y_root,
                                  unsigned long time, ActionMask allowed,
                                  int desktop) = 0;
    virtual void desktop_changed(int current, int count) = 0;
    virtual void show_start_menu(unsigned long time) = 0;
    /* The WM is exiting; let go of anything borrowed (tray icons). */
    virtual void shutdown() = 0;
};

}

#endif
