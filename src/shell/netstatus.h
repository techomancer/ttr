/*
 * The network icon in the notification area, its status dialog and menu.
 * Polls core/net.h every two seconds.
 *
 *   left click   Network dialog: every interface's state, addresses,
 *                traffic and rates, and the access point on Wi-Fi; a
 *                network settings tool to open
 *   right click  menu: Network Status..., every installed settings tool
 */
#ifndef TTR_SHELL_NETSTATUS_H
#define TTR_SHELL_NETSTATUS_H

#include "core/color.h"
#include "core/net.h"
#include "core/str.h"
#include "core/vector.h"
#include "core/wmlink.h"
#include "shell/trayitem.h"

#include <X11/Intrinsic.h>

namespace ttr {

class Icon;
class IconLoader;
struct Config;

class NetStatus : public TrayItem {
public:
    NetStatus(Widget parent, WmLink* wm, const Config& cfg,
              IconLoader* icons, const ColorSet& cs, void (*changed)(void*),
              void* closure);
    ~NetStatus();

    bool visible() const { return supported_; }
    void paint(Painter& p, const Rect& r);
    void button(XButtonEvent* ev);

private:
    enum { ICON_OFFLINE, ICON_WIRED, ICON_WIFI_0, ICON_WIFI_1, ICON_WIFI_2,
           ICON_WIFI_3, ICON_WIFI_4, ICON_COUNT };

    static void poll_cb(XtPointer closure, XtIntervalId* id);
    static void open_cb(Widget w, XtPointer closure, XtPointer call);
    static void choice_cb(Widget w, XtPointer closure, XtPointer call);
    static void close_cb(Widget w, XtPointer closure, XtPointer call);
    static void item_cb(Widget w, XtPointer closure, XtPointer call);
    static void deferred_cb(XtPointer closure, XtIntervalId* id);
    static void menu_map_cb(Widget w, XtPointer closure, XtPointer call);
    static void menu_unmap_cb(Widget w, XtPointer closure, XtPointer call);

    void poll();
    int icon_state() const;
    void show_dialog();
    void build_dialog();
    void update_report();
    void post_menu(XButtonEvent* ev);
    void open_tool(int i);

    Widget parent_;
    WmLink* wm_;
    ColorSet cs_;
    void (*changed_)(void*);
    void* closure_;
    bool supported_;
    Icon* icons_[ICON_COUNT];
    int state_;                 /* ICON_* shown now */

    Vector<NetIface*> now_;     /* latest snapshot */
    Vector<NetIface*> prev_;    /* the one before, for rates */
    double now_time_, prev_time_;
    XtIntervalId timer_;

    Vector<NetTool*> tools_;
    Widget dialog_;
    Widget report_;
    int dialog_tool_;
    Widget menu_;

    struct ToolItem {
        NetStatus* status;
        int index;              /* into tools_, or -1: Network Status... */
    };
    Vector<ToolItem*> items_;
    int pending_;
};

}

#endif
