/*
 * The quick launch bar next to Start: flat 16 pixel icons for the
 * shortcuts marked "quicklaunch", raised under the pointer, with a name
 * tooltip (Motif 2.1 has none of its own). Windows 2000 style.
 */
#ifndef TTR_SHELL_QUICKLAUNCH_H
#define TTR_SHELL_QUICKLAUNCH_H

#include "core/color.h"
#include "core/geom.h"
#include "core/str.h"
#include "core/vector.h"

#include <X11/Intrinsic.h>

namespace ttr {

class Icon;
class IconLoader;
class XPainter;

class QuickLaunch {
public:
    /* 'area' is an XmDrawingArea in the taskbar; 'painter' draws the
     * tooltip text in the taskbar font. Loads the shortcuts file. */
    QuickLaunch(Widget area, XPainter* painter, const ColorSet& cs,
                IconLoader* icons);
    ~QuickLaunch();

    int preferred_width() const;

private:
    struct Item {
        Str name;
        Str command;
        Icon* icon;
    };

    static void expose_cb(Widget w, XtPointer closure, XtPointer call);
    static void event_cb(Widget w, XtPointer closure, XEvent* ev,
                         Boolean* cont);
    static void tip_cb(XtPointer closure, XtIntervalId* id);

    Rect slot(int i) const;
    int hit(int x, int y) const;
    void paint();
    void set_hover(int i);
    void show_tip();
    void hide_tip();

    Widget area_;
    Display* dpy_;
    XPainter* painter_;
    ColorSet cs_;
    Vector<Item*> items_;
    int hover_;
    int pressed_;
    Window tip_;
    XtIntervalId tip_timer_;
};

}

#endif
