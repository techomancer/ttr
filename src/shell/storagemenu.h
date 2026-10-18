/*
 * Removable storage in the notification area, like Windows' "Safely
 * Remove Hardware": an icon while a USB stick or card is present, and a
 * menu per volume to mount, open, unmount and eject it (core/storage.h
 * decides how, without root). Errors show the tool's own message.
 */
#ifndef TTR_SHELL_STORAGEMENU_H
#define TTR_SHELL_STORAGEMENU_H

#include "core/color.h"
#include "core/storage.h"
#include "core/str.h"
#include "core/vector.h"
#include "core/wmlink.h"
#include "shell/trayitem.h"

#include <X11/Intrinsic.h>

namespace ttr {

class CommandRunner;
class Icon;
class IconLoader;

class StorageMenu : public TrayItem {
public:
    StorageMenu(Widget parent, WmLink* wm, IconLoader* icons,
                const ColorSet& cs, void (*changed)(void*), void* closure);
    ~StorageMenu();

    bool visible() const { return !volumes_.empty(); }
    void paint(Painter& p, const Rect& r);
    void button(XButtonEvent* ev);

private:
    enum Op { OP_MOUNT, OP_MOUNT_OPEN, OP_OPEN, OP_UNMOUNT, OP_EJECT };

    struct Item {
        StorageMenu* menu;
        Op op;
        Str device;     /* looked up again when the command runs */
    };

    static void poll_cb(XtPointer closure, XtIntervalId* id);
    static void item_cb(Widget w, XtPointer closure, XtPointer call);
    static void deferred_cb(XtPointer closure, XtIntervalId* id);
    static void done_cb(void* closure, bool ok, const char* output);
    static void menu_map_cb(Widget w, XtPointer closure, XtPointer call);
    static void menu_unmap_cb(Widget w, XtPointer closure, XtPointer call);

    void poll();
    bool same_as(const Vector<StorageVolume*>& other) const;
    void post_menu(XButtonEvent* ev);
    void add_item(Widget menu, const char* label, Op op, const char* device);
    StorageVolume* find(const char* device);
    void run(Op op, const char* device);
    void open(const char* path);
    void message(const char* text, bool error);

    Widget parent_;
    WmLink* wm_;
    ColorSet cs_;
    void (*changed_)(void*);
    void* closure_;
    Icon* icon_;
    CommandRunner* runner_;
    Vector<StorageVolume*> volumes_;
    XtIntervalId timer_;
    Widget menu_;
    Vector<Item*> items_;       /* of the menu being shown */
    Item pending_;
    Op running_op_;
    Str running_device_;
};

}

#endif
