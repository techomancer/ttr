/*
 * The mwm window menu, as a Motif popup menu, plus "Move to Desktop".
 */
#ifndef TTR_SHELL_WINMENU_H
#define TTR_SHELL_WINMENU_H

#include "core/wmlink.h"

#include <X11/Intrinsic.h>

namespace ttr {

class WindowMenu {
public:
    WindowMenu(Widget parent, WmLink* link, int desktops);
    ~WindowMenu();

    /* Post the menu at (x_root, y_root) for toplevel 'id', with only the
     * 'allowed' actions sensitive and 'desktop' (the window's) marked.
     * 'ev' is the button press that caused it; for keyboard posting pass
     * 0 and the key event time. */
    void post(ToplevelId id, int x_root, int y_root, const XButtonEvent* ev,
              Time time, ActionMask allowed, int desktop);

private:
    struct Item {
        WindowMenu* menu;
        WmAction action;
    };
    struct DeskItem {
        WindowMenu* menu;
        int desktop;    /* or ALL_DESKTOPS */
        Widget button;
    };

    static void activate_cb(Widget w, XtPointer closure, XtPointer call);
    static void map_cb(Widget w, XtPointer closure, XtPointer call);
    static void unmap_cb(Widget w, XtPointer closure, XtPointer call);
    static void desktop_cb(Widget w, XtPointer closure, XtPointer call);
    static void deliver_cb(XtPointer closure, XtIntervalId* id);

    void add_item(WmAction action, const char* label, char mnemonic,
                  const char* accel);
    void add_desktop_item(Widget menu, int index, int desktop,
                          const char* label);

    XtAppContext app_;
    WmLink* link_;
    Widget menu_;
    Widget buttons_[ACT_COUNT];
    Item items_[ACT_COUNT];
    int desktops_;
    DeskItem* desk_items_;      /* desktops_ + 1, the last is "all" */
    ToplevelId target_;
    bool pending_move_;         /* deliver a desktop move, not an action */
    WmAction pending_;
    int pending_desktop_;
};

}

#endif
