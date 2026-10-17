#include "shell/winmenu.h"

#include <Xm/Xm.h>
#include <Xm/PushBG.h>
#include <Xm/RowColumn.h>
#include <Xm/SeparatoG.h>

#include <string.h>

namespace ttr {

WindowMenu::WindowMenu(Widget parent, WmLink* link)
    : app_(XtWidgetToApplicationContext(parent)), link_(link),
      target_(0), pending_(ACT_RESTORE)
{
    memset(buttons_, 0, sizeof(buttons_));
    menu_ = XmCreatePopupMenu(parent, (char*)"windowMenu", 0, 0);

    add_item(ACT_RESTORE, "Restore", 'R', "Alt+F5");
    add_item(ACT_MOVE, "Move", 'M', "Alt+F7");
    add_item(ACT_SIZE, "Size", 'S', "Alt+F8");
    add_item(ACT_MINIMIZE, "Minimize", 'n', "Alt+F9");
    add_item(ACT_MAXIMIZE, "Maximize", 'x', "Alt+F10");
    add_item(ACT_LOWER, "Lower", 'L', "Alt+F3");
    XtManageChild(XmCreateSeparatorGadget(menu_, (char*)"sep", 0, 0));
    add_item(ACT_CLOSE, "Close", 'C', "Alt+F4");
}

void WindowMenu::add_item(WmAction action, const char* label, char mnemonic,
                          const char* accel)
{
    XmString l = XmStringCreateLocalized((char*)label);
    XmString a = XmStringCreateLocalized((char*)accel);
    Arg args[3];
    int n = 0;
    XtSetArg(args[n], XmNlabelString, l); n++;
    XtSetArg(args[n], XmNmnemonic, (KeySym)mnemonic); n++;
    XtSetArg(args[n], XmNacceleratorText, a); n++;
    Widget b = XmCreatePushButtonGadget(menu_, (char*)label, args, n);
    XmStringFree(l);
    XmStringFree(a);

    items_[action].menu = this;
    items_[action].action = action;
    XtAddCallback(b, XmNactivateCallback, activate_cb,
                  (XtPointer)&items_[action]);
    XtManageChild(b);
    buttons_[action] = b;
}

void WindowMenu::post(ToplevelId id, int x_root, int y_root,
                      const XButtonEvent* ev, Time time, ActionMask allowed)
{
    target_ = id;
    int i;
    for (i = 0; i < ACT_COUNT; i++)
        if (buttons_[i])
            XtSetSensitive(buttons_[i], action_allowed(allowed, (WmAction)i));

    XButtonEvent bev;
    if (ev) {
        bev = *ev;
    } else {
        memset(&bev, 0, sizeof(bev));
        bev.type = ButtonPress;
        bev.time = time;
    }
    bev.x_root = x_root;
    bev.y_root = y_root;
    XmMenuPosition(menu_, &bev);
    XtManageChild(menu_);
}

void WindowMenu::activate_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    Item* item = (Item*)closure;
    WindowMenu* self = item->menu;
    /* Deliver after the menu has popped down and released its grab, so
     * actions like Move can take their own pointer grab. */
    self->pending_ = item->action;
    XtAppAddTimeOut(self->app_, 0, deliver_cb, (XtPointer)self);
}

void WindowMenu::deliver_cb(XtPointer closure, XtIntervalId* id)
{
    (void)id;
    WindowMenu* self = (WindowMenu*)closure;
    self->link_->request(self->target_, self->pending_);
}

}
