/*
 * Windows 2000-style taskbar along the bottom of the screen: Start button,
 * one button per window, notification area. The bar is a Motif override
 * shell; the task buttons are drawn by us in an XmDrawingArea because
 * Motif 2.1 buttons cannot show an icon and a label together.
 *
 * The buttons can be put in any order: dragged, or moved with the window
 * menu and keys (ACT_TASK_*). The order is kept in the _TTR_TASK_ORDER
 * property on the root window, so it survives Restart ttr.
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

class IconLoader;
class QuickLaunch;
class StartMenu;
class Tray;
class NetStatus;
class StorageMenu;
class VolumeControl;
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
    /* Move a button: ACT_TASK_FIRST, ACT_TASK_LEFT or ACT_TASK_RIGHT,
     * among the buttons of the current desktop. */
    void move(ToplevelId id, WmAction where);
    void post_start_menu(unsigned long time);
    /* Root coordinates of the bar. */
    const Rect& geometry() const { return geom_; }
    void release();

private:
    struct Task {
        ToplevelId id;
        Str title;
        const Icon* icon;
        bool minimized;
        ActionMask allowed;
        int desktop;
        bool on_current;
    };

    static void start_press(Widget w, XtPointer closure, XEvent* ev,
                            Boolean* cont);
    static void start_unposted(void* closure);
    static void tasks_expose_cb(Widget w, XtPointer closure, XtPointer call);
    static void tasks_event(Widget w, XtPointer closure, XEvent* ev,
                            Boolean* cont);
    void on_motion(Widget w, XEvent* ev);
    static void tray_expose_cb(Widget w, XtPointer closure, XtPointer call);
    static void tray_resized(void* closure);

    int find(ToplevelId id);
    int visible_count();
    Rect button_rect(int slot, int count, int w, int h);
    int hit(int x, int y);
    void paint_tasks();
    void activate(Task* t);
    int visible_slot(int i);        /* of list_[i], or -1 */
    int visible_index(int slot);    /* into list_, or -1 */
    void move_to(int from, int to);
    void drag_to(int x);
    void save_order();
    int saved_position(ToplevelId id);

    WmLink* wm_;
    StartMenu* start_menu_;
    WindowMenu* winmenu_;
    XPainter* painter_;
    ColorSet cs_;
    Widget shell_, form_, start_, quick_area_, tasks_, tray_area_;
    Tray* tray_;
    IconLoader* icons_;
    QuickLaunch* quick_;
    StorageMenu* storage_;
    NetStatus* net_;
    VolumeControl* volume_;
    static void volume_changed(void* closure);
    Rect geom_;             /* root coordinates of the bar */
    Vector<Task*> list_;
    ToplevelId active_;
    int pressed_;
    int press_x_;
    bool dragging_;         /* the pressed button follows the pointer */
    Cursor drag_cursor_;
    Atom order_atom_;
    Vector<ToplevelId> saved_order_;    /* from before a restart */
    bool closing_;          /* after release(): the WM is freeing windows */
};

}

#endif
