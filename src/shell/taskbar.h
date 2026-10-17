/*
 * Windows 2000-style taskbar along the bottom of the screen: Start button,
 * one button per window, notification area. The bar is a Motif override
 * shell; the task buttons are drawn by us in an XmDrawingArea because
 * Motif 2.1 buttons cannot show an icon and a label together.
 */
#ifndef TTR_SHELL_TASKBAR_H
#define TTR_SHELL_TASKBAR_H

#include "core/color.h"
#include "core/geom.h"
#include "core/str.h"
#include "core/vector.h"
#include "core/wmlink.h"

#include <X11/Intrinsic.h>

namespace ttr {

class StartMenu;
class Tray;
class WindowMenu;
class XPainter;
struct Config;

class Taskbar {
public:
    Taskbar(Widget parent, WmLink* wm, const Config& cfg, StartMenu* start,
            WindowMenu* winmenu);
    ~Taskbar();

    void add(const TaskInfo& t);
    void change(const TaskInfo& t);
    void remove(ToplevelId id);
    void set_active(ToplevelId id);
    void post_start_menu(unsigned long time);
    void release();

private:
    struct Task {
        ToplevelId id;
        Str title;
        const Icon* icon;
        bool minimized;
        ActionMask allowed;
    };

    static void start_press(Widget w, XtPointer closure, XEvent* ev,
                            Boolean* cont);
    static void start_unposted(void* closure);
    static void tasks_expose_cb(Widget w, XtPointer closure, XtPointer call);
    static void tasks_input_cb(Widget w, XtPointer closure, XtPointer call);
    static void tray_expose_cb(Widget w, XtPointer closure, XtPointer call);
    static void tray_resized(void* closure);

    int find(ToplevelId id);
    Rect button_rect(int i, int w, int h);
    int hit(int x, int y);
    void paint_tasks();
    void activate(Task* t);

    WmLink* wm_;
    StartMenu* start_menu_;
    WindowMenu* winmenu_;
    XPainter* painter_;
    ColorSet cs_;
    Widget shell_, form_, start_, tasks_, tray_area_;
    Tray* tray_;
    Rect geom_;             /* root coordinates of the bar */
    Vector<Task*> list_;
    ToplevelId active_;
    int pressed_;
};

}

#endif
