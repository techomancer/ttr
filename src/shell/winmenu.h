/*
 * The mwm window menu, as a Motif popup menu.
 */
#ifndef TTR_SHELL_WINMENU_H
#define TTR_SHELL_WINMENU_H

#include "core/wmlink.h"

#include <X11/Intrinsic.h>

namespace ttr {

class WindowMenu {
public:
    WindowMenu(Widget parent, WmLink* link);

    /* Post the menu at (x_root, y_root) for toplevel 'id', with only the
     * 'allowed' actions sensitive. 'ev' is the button press that caused
     * it; for keyboard posting pass 0 and the key event time. */
    void post(ToplevelId id, int x_root, int y_root, const XButtonEvent* ev,
              Time time, ActionMask allowed);

private:
    struct Item {
        WindowMenu* menu;
        WmAction action;
    };

    static void activate_cb(Widget w, XtPointer closure, XtPointer call);
    static void deliver_cb(XtPointer closure, XtIntervalId* id);

    void add_item(WmAction action, const char* label, char mnemonic,
                  const char* accel);

    XtAppContext app_;
    WmLink* link_;
    Widget menu_;
    Widget buttons_[ACT_COUNT];
    Item items_[ACT_COUNT];
    ToplevelId target_;
    WmAction pending_;
};

}

#endif
