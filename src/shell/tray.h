/*
 * The notification area: XEmbed system tray icons (freedesktop.org System
 * Tray protocol) and a clock, in a sunken box at the right of the taskbar.
 */
#ifndef TTR_SHELL_TRAY_H
#define TTR_SHELL_TRAY_H

#include "core/color.h"
#include "core/geom.h"
#include "core/str.h"
#include "core/vector.h"

#include <X11/Intrinsic.h>

namespace ttr {

class TrayItem;
class XPainter;

class Tray {
public:
    /* 'area' is a realized XmDrawingArea. 'resized' is called when the
     * preferred width changes. */
    Tray(Widget area, XPainter* painter, const ColorSet& cs,
         const char* clock_format, void (*resized)(void*), void* closure);
    ~Tray();

    int preferred_width();
    void paint();
    /* Expose on the tray area: repaint and make docked icons redraw. */
    void exposed(XEvent* ev);
    /* Built-in icons, left to right, before the docked ones. */
    void add_item(TrayItem* item);
    /* Something about the built-in icons changed. */
    void relayout() { layout(); }
    /* Hand docked icons back to the root window. */
    void release();

private:
    struct Item {
        Window win;
        bool mapped;
        Rect rect;      /* last placement in the tray */
    };

    static void event_cb(Widget w, XtPointer closure, XEvent* ev,
                         Boolean* cont);
    static void clock_cb(XtPointer closure, XtIntervalId* id);
    static void input_cb(Widget w, XtPointer closure, XtPointer call);
    Rect item_rect(int i);

    void acquire();
    void dock(Window icon);
    void remove(Window icon);
    int find(Window icon);
    void layout();
    void refresh_icons();
    void clock_text(char* buf, int size);
    void schedule_clock();

    Widget area_;
    Display* dpy_;
    Window win_;
    XPainter* painter_;
    ColorSet cs_;
    Str clock_format_;
    void (*resized_)(void*);
    void* closure_;
    bool owner_;
    Atom sel_, manager_, opcode_, xembed_, xembed_info_;
    Atom orientation_, visual_;
    Vector<Item> items_;
    int last_width_;
    Vector<TrayItem*> builtins_;
};

}

#endif
