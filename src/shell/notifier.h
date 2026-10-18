/*
 * Notification popups: small bevelled panels stacked at the bottom right,
 * just above the taskbar, sliding up into place. Each shows an icon, the
 * summary in bold and the body wrapped to a few lines; a click dismisses
 * it, and it goes by itself after notificationTime seconds (or the app's
 * own timeout). Critical ones stay until clicked.
 */
#ifndef TTR_SHELL_NOTIFIER_H
#define TTR_SHELL_NOTIFIER_H

#include "core/color.h"
#include "core/geom.h"
#include "core/notify.h"
#include "core/str.h"
#include "core/vector.h"
#include "core/wmlink.h"

#include <X11/Intrinsic.h>

namespace ttr {

class Icon;
class IconLoader;
class XPainter;
struct Config;

class Notifier : public NotifySink {
public:
    /* 'area' is where popups may go (above the taskbar). 'widget' routes
     * the popups' events through Xt. */
    Notifier(Widget widget, WmLink* wm, const Config& cfg, IconLoader* icons,
             const Rect& area);
    ~Notifier();

    void set_listener(NotifyListener* l) { listener_ = l; }

    unsigned long show(const Notification& n);
    void close(unsigned long id, int reason);

private:
    struct Popup {
        Notifier* owner;
        unsigned long id;
        Notification n;
        Window win;
        Icon* icon;
        Vector<Str> lines;
        Str summary;            /* Latin-1, for the core font */
        int w, h;
        int y, target_y;        /* top edge, sliding toward target_y */
        XtIntervalId timer;
    };

    static void event_cb(Widget w, XtPointer closure, XEvent* ev,
                         Boolean* cont);
    static void expire_cb(XtPointer closure, XtIntervalId* id);
    static void slide_cb(XtPointer closure, XtIntervalId* id);

    Popup* find(unsigned long id);
    Popup* find_window(Window w);
    void layout(Popup* p);
    void restack();
    void paint(Popup* p);
    void arm_timer(Popup* p);
    void remove(Popup* p, int reason);

    Widget widget_;
    Display* dpy_;
    WmLink* wm_;
    IconLoader* icons_;
    XPainter* painter_;     /* body font, with the summary font as bold */
    ColorSet cs_;
    Rect area_;
    int default_ms_;
    unsigned long next_id_;
    Vector<Popup*> popups_;     /* bottom first */
    XtIntervalId slide_timer_;
    NotifyListener* listener_;
};

}

#endif
