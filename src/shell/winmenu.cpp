#include "shell/winmenu.h"

#include <Xm/Xm.h>
#include <Xm/CascadeBG.h>
#include <Xm/PushBG.h>
#include <Xm/RowColumn.h>
#include <Xm/SeparatoG.h>
#include <Xm/ToggleBG.h>

#include <stdio.h>
#include <string.h>

namespace ttr {

WindowMenu::WindowMenu(Widget parent, WmLink* link, int desktops)
    : app_(XtWidgetToApplicationContext(parent)), link_(link),
      desktops_(desktops), desk_items_(0), target_(0), pending_move_(false),
      pending_(ACT_RESTORE), pending_desktop_(0)
{
    memset(buttons_, 0, sizeof(buttons_));
    menu_ = XmCreatePopupMenu(parent, (char*)"windowMenu", 0, 0);

    add_item(ACT_RESTORE, "Restore", 'R', "Alt+F5");
    add_item(ACT_MOVE, "Move", 'M', "Alt+F7");
    add_item(ACT_SIZE, "Size", 'S', "Alt+F8");
    add_item(ACT_MINIMIZE, "Minimize", 'n', "Alt+F9");
    add_item(ACT_MAXIMIZE, "Maximize", 'x', "Alt+F10");

    Widget snap = XmCreatePulldownMenu(menu_, (char*)"snap", 0, 0);
    add_item(ACT_DOCK_LEFT, "Dock Left", 'L', "Super+1", snap);
    add_item(ACT_DOCK_CENTER, "Dock Center", 'C', "Super+2", snap);
    add_item(ACT_DOCK_RIGHT, "Dock Right", 'R', "Super+3", snap);
    XtManageChild(XmCreateSeparatorGadget(snap, (char*)"sep", 0, 0));
    add_item(ACT_SNAP_TOP, "Top", 'T', "Super+Up", snap);
    add_item(ACT_SNAP_BOTTOM, "Bottom", 'B', "Super+Down", snap);
    {
        XmString l = XmStringCreateLocalized((char*)"Snap");
        Arg sargs[3];
        int n = 0;
        XtSetArg(sargs[n], XmNlabelString, l); n++;
        XtSetArg(sargs[n], XmNmnemonic, (KeySym)'p'); n++;
        XtSetArg(sargs[n], XmNsubMenuId, snap); n++;
        XtManageChild(XmCreateCascadeButtonGadget(menu_, (char*)"snapItem",
                                                  sargs, n));
        XmStringFree(l);
    }

    add_item(ACT_LOWER, "Lower", 'L', "Alt+F3");

    if (desktops_ > 1) {
        Arg args[3];
        XtSetArg(args[0], XmNradioBehavior, True);
        Widget sub = XmCreatePulldownMenu(menu_, (char*)"desktops", args, 1);
        desk_items_ = new DeskItem[desktops_ + 1];
        int i;
        for (i = 0; i < desktops_; i++) {
            char label[32];
            sprintf(label, "Desktop %d", i + 1);
            add_desktop_item(sub, i, i, label);
        }
        XtManageChild(XmCreateSeparatorGadget(sub, (char*)"sep", 0, 0));
        add_desktop_item(sub, desktops_, ALL_DESKTOPS, "All Desktops");

        XmString l = XmStringCreateLocalized((char*)"Move to Desktop");
        int n = 0;
        XtSetArg(args[n], XmNlabelString, l); n++;
        XtSetArg(args[n], XmNmnemonic, (KeySym)'D'); n++;
        XtSetArg(args[n], XmNsubMenuId, sub); n++;
        XtManageChild(XmCreateCascadeButtonGadget(menu_, (char*)"desktop",
                                                  args, n));
        XmStringFree(l);
    }

    XtManageChild(XmCreateSeparatorGadget(menu_, (char*)"sep", 0, 0));
    add_item(ACT_CLOSE, "Close", 'C', "Alt+F4");

    XtAddCallback(menu_, XmNmapCallback, map_cb, (XtPointer)this);
    XtAddCallback(menu_, XmNunmapCallback, unmap_cb, (XtPointer)this);
}

void WindowMenu::map_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ((WindowMenu*)closure)->link_->menu_shown(true);
}

void WindowMenu::unmap_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ((WindowMenu*)closure)->link_->menu_shown(false);
}

WindowMenu::~WindowMenu()
{
    delete [] desk_items_;
}

void WindowMenu::add_item(WmAction action, const char* label, char mnemonic,
                          const char* accel, Widget menu)
{
    if (!menu)
        menu = menu_;
    XmString l = XmStringCreateLocalized((char*)label);
    XmString a = XmStringCreateLocalized((char*)accel);
    Arg args[3];
    int n = 0;
    XtSetArg(args[n], XmNlabelString, l); n++;
    XtSetArg(args[n], XmNmnemonic, (KeySym)mnemonic); n++;
    XtSetArg(args[n], XmNacceleratorText, a); n++;
    Widget b = XmCreatePushButtonGadget(menu, (char*)label, args, n);
    XmStringFree(l);
    XmStringFree(a);

    items_[action].menu = this;
    items_[action].action = action;
    XtAddCallback(b, XmNactivateCallback, activate_cb,
                  (XtPointer)&items_[action]);
    XtManageChild(b);
    buttons_[action] = b;
}

void WindowMenu::add_desktop_item(Widget menu, int index, int desktop,
                                  const char* label)
{
    XmString l = XmStringCreateLocalized((char*)label);
    Arg args[3];
    int n = 0;
    XtSetArg(args[n], XmNlabelString, l); n++;
    XtSetArg(args[n], XmNindicatorType, XmONE_OF_MANY); n++;
    XtSetArg(args[n], XmNvisibleWhenOff, True); n++;
    Widget b = XmCreateToggleButtonGadget(menu, (char*)"desktopItem", args, n);
    XmStringFree(l);
    DeskItem& it = desk_items_[index];
    it.menu = this;
    it.desktop = desktop;
    it.button = b;
    XtAddCallback(b, XmNvalueChangedCallback, desktop_cb, (XtPointer)&it);
    XtManageChild(b);
}

void WindowMenu::post(ToplevelId id, int x_root, int y_root,
                      const XButtonEvent* ev, Time time, ActionMask allowed,
                      int desktop)
{
    target_ = id;
    int i;
    for (i = 0; i < ACT_COUNT; i++)
        if (buttons_[i])
            XtSetSensitive(buttons_[i], action_allowed(allowed, (WmAction)i));
    if (desk_items_)
        for (i = 0; i <= desktops_; i++)
            XmToggleButtonGadgetSetState(desk_items_[i].button,
                                         desk_items_[i].desktop == desktop,
                                         False);

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

/* Actions are delivered after the menu has popped down and released its
 * grab, so ones like Move can take their own pointer grab. */
void WindowMenu::activate_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    Item* item = (Item*)closure;
    WindowMenu* self = item->menu;
    self->pending_move_ = false;
    self->pending_ = item->action;
    XtAppAddTimeOut(self->app_, 0, deliver_cb, (XtPointer)self);
}

void WindowMenu::desktop_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    XmToggleButtonCallbackStruct* cbs = (XmToggleButtonCallbackStruct*)call;
    if (!cbs->set)
        return;     /* the radio partner being switched off */
    DeskItem* item = (DeskItem*)closure;
    WindowMenu* self = item->menu;
    self->pending_move_ = true;
    self->pending_desktop_ = item->desktop;
    XtAppAddTimeOut(self->app_, 0, deliver_cb, (XtPointer)self);
}

void WindowMenu::deliver_cb(XtPointer closure, XtIntervalId* id)
{
    (void)id;
    WindowMenu* self = (WindowMenu*)closure;
    if (self->pending_move_)
        self->link_->move_to_desktop(self->target_, self->pending_desktop_);
    else
        self->link_->request(self->target_, self->pending_);
}

}
