#include "shell/storagemenu.h"
#include "shell/runcmd.h"
#include "core/icon.h"
#include "core/launch.h"
#include "core/painter.h"
#include "shell/popup.h"
#include "x11/iconload.h"

#include <Xm/Xm.h>
#include <Xm/CascadeBG.h>
#include <Xm/LabelG.h>
#include <Xm/MessageB.h>
#include <Xm/PushBG.h>
#include <Xm/RowColumn.h>
#include <Xm/SeparatoG.h>

#include <stdio.h>
#include <string.h>

namespace ttr {

static const unsigned long k_poll_ms = 2000;
static const unsigned long k_op_ms = 60000;    /* mount can be slow */

static XmString xmstr(const char* s)
{
    return XmStringCreateLocalized((char*)s);
}

StorageMenu::StorageMenu(Widget parent, WmLink* wm, IconLoader* icons,
                         const ColorSet& cs, void (*changed)(void*),
                         void* closure)
    : parent_(parent), wm_(wm), cs_(cs), changed_(changed),
      closure_(closure), timer_(0), menu_(0), running_op_(OP_MOUNT)
{
    static const char* const names[] = {
        "drive-removable-media-usb", "drive-removable-media",
        "media-removable", 0
    };
    icon_ = 0;
    int i;
    for (i = 0; names[i] && !icon_; i++)
        icon_ = icons->load(names[i], 16);
    runner_ = new CommandRunner(XtWidgetToApplicationContext(parent));
    pending_.menu = this;
    pending_.op = OP_MOUNT;
    poll();
}

StorageMenu::~StorageMenu()
{
    if (timer_)
        XtRemoveTimeOut(timer_);
    delete runner_;
    delete icon_;
    storage_free(&volumes_);
    int i;
    for (i = 0; i < items_.size(); i++)
        delete items_[i];
}

/* ------------------------------------------------------------------ */
/* Watching for devices                                                */

bool StorageMenu::same_as(const Vector<StorageVolume*>& other) const
{
    if (other.size() != volumes_.size())
        return false;
    int i;
    for (i = 0; i < other.size(); i++)
        if (strcmp(other[i]->device.c_str(), volumes_[i]->device.c_str()) ||
            strcmp(other[i]->mount_point.c_str(),
                   volumes_[i]->mount_point.c_str()))
            return false;
    return true;
}

void StorageMenu::poll()
{
    Vector<StorageVolume*> now;
    storage_snapshot(&now);
    bool changed = !same_as(now);
    storage_free(&volumes_);
    int i;
    for (i = 0; i < now.size(); i++)
        volumes_.push_back(now[i]);
    now.clear();
    if (changed && changed_)
        changed_(closure_);
    timer_ = XtAppAddTimeOut(XtWidgetToApplicationContext(parent_), k_poll_ms,
                             poll_cb, (XtPointer)this);
}

void StorageMenu::poll_cb(XtPointer closure, XtIntervalId* id)
{
    (void)id;
    StorageMenu* self = (StorageMenu*)closure;
    self->timer_ = 0;
    self->poll();
}

StorageVolume* StorageMenu::find(const char* device)
{
    int i;
    for (i = 0; i < volumes_.size(); i++)
        if (strcmp(volumes_[i]->device.c_str(), device) == 0)
            return volumes_[i];
    return 0;
}

void StorageMenu::paint(Painter& p, const Rect& r)
{
    int x = r.x + (r.w - 16) / 2, y = r.y + (r.h - 16) / 2;
    if (icon_)
        p.icon(*icon_, x, y, 16, cs_.bg);
    else
        draw_storage_icon(p, x, y, 16, cs_);
}

void StorageMenu::button(XButtonEvent* ev)
{
    /* Either button, on release (see VolumeControl). */
    if (ev->type == ButtonRelease &&
        (ev->button == Button1 || ev->button == Button3))
        post_menu(ev);
}

/* ------------------------------------------------------------------ */
/* The menu                                                            */

void StorageMenu::add_item(Widget menu, const char* label, Op op,
                           const char* device)
{
    Item* it = new Item;
    it->menu = this;
    it->op = op;
    it->device = device;
    items_.push_back(it);
    XmString l = xmstr(label);
    Arg args[1];
    XtSetArg(args[0], XmNlabelString, l);
    Widget b = XmCreatePushButtonGadget(menu, (char*)"item", args, 1);
    XmStringFree(l);
    XtAddCallback(b, XmNactivateCallback, item_cb, (XtPointer)it);
    XtManageChild(b);
}

/* Rebuilt every time: devices come and go. One submenu per volume. */
void StorageMenu::post_menu(XButtonEvent* ev)
{
    if (menu_)
        XtDestroyWidget(XtParent(menu_));
    int i;
    for (i = 0; i < items_.size(); i++)
        delete items_[i];
    items_.clear();

    menu_ = XmCreatePopupMenu(parent_, (char*)"storageMenu", 0, 0);
    XtAddCallback(menu_, XmNmapCallback, menu_map_cb, (XtPointer)this);
    XtAddCallback(menu_, XmNunmapCallback, menu_unmap_cb, (XtPointer)this);

    Arg args[2];
    for (i = 0; i < volumes_.size(); i++) {
        const StorageVolume* v = volumes_[i];
        const char* dev = v->device.c_str();
        Widget sub = XmCreatePulldownMenu(menu_, (char*)"volume", 0, 0);
        if (v->method == MOUNT_NONE) {
            XmString l = xmstr("Cannot mount without root: install udisks2 "
                               "or pmount, or add a user entry to /etc/fstab");
            XtSetArg(args[0], XmNlabelString, l);
            XtManageChild(XmCreateLabelGadget(sub, (char*)"none", args, 1));
            XmStringFree(l);
        } else if (v->mount_point.empty()) {
            add_item(sub, "Mount and Open", OP_MOUNT_OPEN, dev);
            add_item(sub, "Mount", OP_MOUNT, dev);
        } else {
            add_item(sub, "Open", OP_OPEN, dev);
            add_item(sub, "Unmount", OP_UNMOUNT, dev);
        }
        if (v->method != MOUNT_NONE) {
            XtManageChild(XmCreateSeparatorGadget(sub, (char*)"sep", 0, 0));
            add_item(sub, "Eject (safe to remove)", OP_EJECT, dev);
        }

        Str title = storage_title(*v);
        char label[700];
        if (v->mount_point.empty())
            sprintf(label, "%.500s", title.c_str());
        else
            sprintf(label, "%.400s  on %.250s", title.c_str(),
                    v->mount_point.c_str());
        XmString l = xmstr(label);
        XtSetArg(args[0], XmNlabelString, l);
        XtSetArg(args[1], XmNsubMenuId, sub);
        XtManageChild(XmCreateCascadeButtonGadget(menu_, (char*)"volumeItem",
                                                  args, 2));
        XmStringFree(l);
    }

    post_popup_above(menu_, ev);
}

/* Run after the menu has popped down. */
void StorageMenu::item_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    Item* it = (Item*)closure;
    StorageMenu* self = it->menu;
    self->pending_.op = it->op;
    self->pending_.device = it->device;
    XtAppAddTimeOut(XtWidgetToApplicationContext(self->parent_), 0,
                    deferred_cb, (XtPointer)self);
}

void StorageMenu::deferred_cb(XtPointer closure, XtIntervalId* id)
{
    (void)id;
    StorageMenu* self = (StorageMenu*)closure;
    Str device = self->pending_.device;
    self->run(self->pending_.op, device.c_str());
}

/* ------------------------------------------------------------------ */
/* Doing it                                                            */

void StorageMenu::run(Op op, const char* device)
{
    StorageVolume* v = find(device);
    if (!v) {
        message("The device is gone.", true);
        return;
    }
    if (op == OP_OPEN) {
        open(v->mount_point.c_str());
        return;
    }
    if (runner_->busy()) {
        message("Still busy with the previous request; try again in a "
                "moment.", true);
        return;
    }
    Str cmd;
    if (op == OP_MOUNT || op == OP_MOUNT_OPEN) {
        cmd = storage_mount_command(*v);
    } else if (op == OP_UNMOUNT) {
        cmd = storage_unmount_command(*v);
    } else {
        Vector<StorageVolume*> same;
        int i;
        for (i = 0; i < volumes_.size(); i++)
            if (strcmp(volumes_[i]->disk.c_str(), v->disk.c_str()) == 0)
                same.push_back(volumes_[i]);
        cmd = storage_eject_command(*v, same);
    }
    if (cmd.empty())
        return;
    running_op_ = op;
    running_device_ = device;
    if (!runner_->run(cmd.c_str(), k_op_ms, done_cb, (void*)this, true))
        message("Could not run the mount tool.", true);
}

void StorageMenu::done_cb(void* closure, bool ok, const char* output)
{
    StorageMenu* self = (StorageMenu*)closure;
    if (!ok) {
        char msg[2200];
        sprintf(msg, "%s failed:\n\n%.2000s",
                self->running_op_ == OP_UNMOUNT ? "Unmounting"
                : self->running_op_ == OP_EJECT ? "Ejecting" : "Mounting",
                output && *output ? output : "(no message)");
        self->message(msg, true);
        return;
    }
    /* See the new state now rather than on the next poll. */
    if (self->timer_) {
        XtRemoveTimeOut(self->timer_);
        self->timer_ = 0;
    }
    self->poll();
    if (self->running_op_ == OP_MOUNT_OPEN) {
        StorageVolume* v = self->find(self->running_device_.c_str());
        if (v && !v->mount_point.empty())
            self->open(v->mount_point.c_str());
    } else if (self->running_op_ == OP_EJECT) {
        self->message("It is now safe to remove the device.", false);
    }
}

void StorageMenu::open(const char* path)
{
    Str cmd = open_folder_command(path);
    if (cmd.empty()) {
        char msg[1200];
        sprintf(msg, "No file manager found to open %.1000s", path);
        message(msg, true);
        return;
    }
    spawn(cmd.c_str());
}

/* Message boxes go away for good when dismissed. */
static void destroy_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)closure;
    (void)call;
    XtDestroyWidget(XtParent(w));
}

void StorageMenu::message(const char* text, bool error)
{
    XmString m = xmstr(text);
    XmString t = xmstr("Removable Storage");
    Arg args[2];
    XtSetArg(args[0], XmNmessageString, m);
    XtSetArg(args[1], XmNdialogTitle, t);
    Widget d = error ? XmCreateErrorDialog(parent_, (char*)"storageError",
                                           args, 2)
                     : XmCreateInformationDialog(parent_, (char*)"storageInfo",
                                                 args, 2);
    XmStringFree(m);
    XmStringFree(t);
    XtUnmanageChild(XtNameToWidget(d, "Help"));
    XtUnmanageChild(XtNameToWidget(d, "Cancel"));
    XtAddCallback(d, XmNokCallback, destroy_cb, 0);
    XtManageChild(d);
}

void StorageMenu::menu_map_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ((StorageMenu*)closure)->wm_->menu_shown(true);
}

void StorageMenu::menu_unmap_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ((StorageMenu*)closure)->wm_->menu_shown(false);
}

}
