/*
 * Desktop notifications: what sources deliver and the popups show.
 *
 * Sources (shell/notifysrc.h): the freedesktop.org notification service on
 * D-Bus (Linux), "ttr -notify" through an X property (any X client, IRIX
 * included), and new lines in a log file such as IRIX's /var/adm/SYSLOG.
 */
#ifndef TTR_CORE_NOTIFY_H
#define TTR_CORE_NOTIFY_H

#include "core/str.h"

namespace ttr {

struct Notification {
    unsigned long id;   /* 0: a new one; else the one to replace */
    Str app;
    Str summary;
    Str body;           /* plain text, UTF-8 */
    Str icon;           /* theme name, path or file:// URI; may be empty */
    int timeout_ms;     /* -1: the user's setting; 0: until dismissed */
    int urgency;        /* 0 low, 1 normal, 2 critical (stays up) */

    Notification() : id(0), timeout_ms(-1), urgency(1) {}
};

/* Why a notification went away (the freedesktop.org codes). */
enum NotifyCloseReason {
    NOTIFY_EXPIRED = 1,
    NOTIFY_DISMISSED = 2,
    NOTIFY_CLOSED_BY_CALL = 3
};

/* Told when a notification goes away, e.g. to send NotificationClosed. */
class NotifyListener {
public:
    virtual ~NotifyListener() {}
    virtual void closed(unsigned long id, int reason) = 0;
};

/* Shows notifications. */
class NotifySink {
public:
    virtual ~NotifySink() {}
    /* Returns the notification's id (n.id if it replaced one). */
    virtual unsigned long show(const Notification& n) = 0;
    virtual void close(unsigned long id, int reason) = 0;
};

}

#endif
