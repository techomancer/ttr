/*
 * Where notifications come from (see core/notify.h):
 *
 *   DBusNotifySource  the freedesktop.org notification service,
 *                     org.freedesktop.Notifications on the session bus
 *                     (Linux; TTR_HAVE_DBUS, libdbus-1)
 *   XNotifySource     "ttr -notify SUMMARY [BODY]", from any X client:
 *                     appends to the _TTR_NOTIFY property on the root
 *   LogNotifySource   new lines in a file (notificationLog), e.g. IRIX's
 *                     /var/adm/SYSLOG for console and kernel messages
 */
#ifndef TTR_SHELL_NOTIFYSRC_H
#define TTR_SHELL_NOTIFYSRC_H

#include "core/notify.h"
#include "core/str.h"

#include <X11/Intrinsic.h>

namespace ttr {

/* For "ttr -notify": send one notification to the running ttr. */
bool send_x_notification(Display* dpy, const char* app, const char* summary,
                         const char* body);

class XNotifySource {
public:
    /* One at a time: it sits in front of Xt's PropertyNotify dispatch. */
    XNotifySource(Widget widget, NotifySink* sink);
    ~XNotifySource();

private:
    static Boolean dispatch(XEvent* ev);
    void read();

    XtEventDispatchProc previous_;
    Display* dpy_;
    Window root_;
    Atom atom_, utf8_;
    NotifySink* sink_;
};

class LogNotifySource {
public:
    LogNotifySource(XtAppContext app, const char* path, NotifySink* sink);
    ~LogNotifySource();

private:
    static void poll_cb(XtPointer closure, XtIntervalId* id);
    void poll();

    XtAppContext app_;
    Str path_;
    Str title_;
    long offset_;       /* bytes already seen */
    NotifySink* sink_;
    XtIntervalId timer_;
};

class DBusNotifySource : public NotifyListener {
public:
    DBusNotifySource(XtAppContext app, NotifySink* sink);
    ~DBusNotifySource();

    /* Take the bus name. False (with a message) if D-Bus is not built
     * in, there is no session bus, or another daemon owns the name. */
    bool start();
    void closed(unsigned long id, int reason);

private:
    static void input_cb(XtPointer closure, int* fd, XtInputId* id);

    XtAppContext app_;
    NotifySink* sink_;
    void* conn_;            /* DBusConnection, kept out of this header */
    XtInputId input_;
};

}

#endif
