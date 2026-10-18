#include "shell/notifysrc.h"

#include <X11/Xatom.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#if defined(TTR_HAVE_DBUS)
#include <dbus/dbus.h>
#endif

namespace ttr {

/* ------------------------------------------------------------------ */
/* X property: "app\0summary\0body\0", appended by senders             */

static const char* const k_x_atom = "_TTR_NOTIFY";

bool send_x_notification(Display* dpy, const char* app, const char* summary,
                         const char* body)
{
    int la = (int)strlen(app), ls = (int)strlen(summary),
        lb = (int)strlen(body);
    char* data = new char[la + ls + lb + 3];
    memcpy(data, app, la + 1);
    memcpy(data + la + 1, summary, ls + 1);
    memcpy(data + la + ls + 2, body, lb + 1);
    Atom atom = XInternAtom(dpy, k_x_atom, False);
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    XChangeProperty(dpy, DefaultRootWindow(dpy), atom, utf8, 8,
                    PropModeAppend, (unsigned char*)data, la + ls + lb + 3);
    XSync(dpy, False);
    delete [] data;
    return true;
}

static XNotifySource* g_x_source = 0;

/* The root window's PropertyNotify events do not reach widget handlers:
 * Motif registers the root to its own screen object. So watch them in
 * front of the dispatcher already installed (the WM's, in X11 mode). */
XNotifySource::XNotifySource(Widget widget, NotifySink* sink)
    : dpy_(XtDisplay(widget)), root_(DefaultRootWindow(dpy_)), sink_(sink)
{
    atom_ = XInternAtom(dpy_, k_x_atom, False);
    utf8_ = XInternAtom(dpy_, "UTF8_STRING", False);
    XWindowAttributes attr;
    XGetWindowAttributes(dpy_, root_, &attr);
    XSelectInput(dpy_, root_, attr.your_event_mask | PropertyChangeMask);
    g_x_source = this;
    previous_ = XtSetEventDispatcher(dpy_, PropertyNotify, dispatch);
    read();     /* anything sent before we ran */
}

XNotifySource::~XNotifySource()
{
    XtSetEventDispatcher(dpy_, PropertyNotify, previous_);
    g_x_source = 0;
}

Boolean XNotifySource::dispatch(XEvent* ev)
{
    XNotifySource* self = g_x_source;
    if (self && ev->xproperty.window == self->root_ &&
        ev->xproperty.atom == self->atom_ &&
        ev->xproperty.state == PropertyNewValue) {
        self->read();
        return True;
    }
    return self && self->previous_ ? self->previous_(ev) : False;
}

/* Take the whole property (deleting it) and show each record. */
void XNotifySource::read()
{
    Atom type;
    int format;
    unsigned long n = 0, after = 0;
    unsigned char* data = 0;
    if (XGetWindowProperty(dpy_, root_, atom_, 0, 65536, True, utf8_, &type,
                           &format, &n, &after, &data) != Success || !data)
        return;
    if (format == 8) {
        const char* p = (const char*)data;
        const char* end = p + n;
        while (p < end) {
            const char* f[3];
            int k;
            for (k = 0; k < 3 && p < end; k++) {
                f[k] = p;
                p += strlen(p) + 1;
            }
            if (k < 3)
                break;
            Notification note;
            note.app = f[0];
            note.summary = f[1];
            note.body = f[2];
            sink_->show(note);
        }
    }
    XFree(data);
}

/* ------------------------------------------------------------------ */
/* Log file: new lines since we started                                */

static const unsigned long k_log_poll_ms = 2000;
static const int k_log_max_lines = 3;      /* per poll, against floods */

LogNotifySource::LogNotifySource(XtAppContext app, const char* path,
                                 NotifySink* sink)
    : app_(app), path_(path), offset_(0), sink_(sink), timer_(0)
{
    const char* slash = strrchr(path, '/');
    title_ = slash ? slash + 1 : path;
    struct stat st;
    if (stat(path, &st) == 0)
        offset_ = (long)st.st_size;     /* only what comes from now on */
    timer_ = XtAppAddTimeOut(app_, k_log_poll_ms, poll_cb, (XtPointer)this);
}

LogNotifySource::~LogNotifySource()
{
    if (timer_)
        XtRemoveTimeOut(timer_);
}

void LogNotifySource::poll_cb(XtPointer closure, XtIntervalId* id)
{
    (void)id;
    LogNotifySource* self = (LogNotifySource*)closure;
    self->timer_ = 0;
    self->poll();
    self->timer_ = XtAppAddTimeOut(self->app_, k_log_poll_ms, poll_cb,
                                   closure);
}

void LogNotifySource::poll()
{
    struct stat st;
    if (stat(path_.c_str(), &st) != 0)
        return;
    if ((long)st.st_size < offset_)
        offset_ = 0;        /* rotated or truncated */
    if ((long)st.st_size == offset_)
        return;
    FILE* f = fopen(path_.c_str(), "r");
    if (!f)
        return;
    fseek(f, offset_, SEEK_SET);
    /* Collect the new complete lines; show the last few. */
    Str lines[k_log_max_lines];
    int count = 0;
    char buf[1024];
    long pos = offset_;
    while (fgets(buf, sizeof(buf), f)) {
        int n = (int)strlen(buf);
        if (n == 0 || buf[n - 1] != '\n')
            break;      /* partial line; read it next time */
        pos += n;
        buf[--n] = 0;
        if (n == 0)
            continue;
        lines[count % k_log_max_lines] = buf;
        count++;
    }
    fclose(f);
    offset_ = pos;
    int first = count > k_log_max_lines ? count - k_log_max_lines : 0, i;
    for (i = first; i < count; i++) {
        Notification note;
        note.app = "log";
        note.summary = title_;
        note.body = lines[i % k_log_max_lines];
        note.icon = "dialog-information";
        sink_->show(note);
    }
}

/* ------------------------------------------------------------------ */
/* D-Bus: org.freedesktop.Notifications                                */

#if defined(TTR_HAVE_DBUS)

static const char* const k_name = "org.freedesktop.Notifications";
static const char* const k_path = "/org/freedesktop/Notifications";
static const char* const k_iface = "org.freedesktop.Notifications";

static const char* const k_introspect =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection "
    "1.0//EN\" \"http://www.freedesktop.org/standards/dbus/1.0/"
    "introspect.dtd\">\n"
    "<node><interface name=\"org.freedesktop.Notifications\">"
    "<method name=\"GetCapabilities\"><arg type=\"as\" direction=\"out\"/>"
    "</method>"
    "<method name=\"Notify\"><arg type=\"s\" direction=\"in\"/>"
    "<arg type=\"u\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/>"
    "<arg type=\"s\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/>"
    "<arg type=\"as\" direction=\"in\"/>"
    "<arg type=\"a{sv}\" direction=\"in\"/><arg type=\"i\" direction=\"in\"/>"
    "<arg type=\"u\" direction=\"out\"/></method>"
    "<method name=\"CloseNotification\"><arg type=\"u\" direction=\"in\"/>"
    "</method>"
    "<method name=\"GetServerInformation\"><arg type=\"s\" direction=\"out\"/>"
    "<arg type=\"s\" direction=\"out\"/><arg type=\"s\" direction=\"out\"/>"
    "<arg type=\"s\" direction=\"out\"/></method>"
    "<signal name=\"NotificationClosed\"><arg type=\"u\"/><arg type=\"u\"/>"
    "</signal></interface>"
    "<interface name=\"org.freedesktop.DBus.Introspectable\">"
    "<method name=\"Introspect\"><arg type=\"s\" direction=\"out\"/>"
    "</method></interface></node>\n";

/* Strings and urgency from the a{sv} hints. */
static void read_hints(DBusMessageIter* dict, Notification* n)
{
    while (dbus_message_iter_get_arg_type(dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry, var;
        dbus_message_iter_recurse(dict, &entry);
        const char* key = 0;
        if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING) {
            dbus_message_iter_get_basic(&entry, &key);
            dbus_message_iter_next(&entry);
            if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT) {
                dbus_message_iter_recurse(&entry, &var);
                int t = dbus_message_iter_get_arg_type(&var);
                if (strcmp(key, "urgency") == 0 && t == DBUS_TYPE_BYTE) {
                    unsigned char u;
                    dbus_message_iter_get_basic(&var, &u);
                    n->urgency = u;
                } else if ((strcmp(key, "image-path") == 0 ||
                            strcmp(key, "image_path") == 0) &&
                           t == DBUS_TYPE_STRING && n->icon.empty()) {
                    const char* s;
                    dbus_message_iter_get_basic(&var, &s);
                    n->icon = s;
                }
            }
        }
        dbus_message_iter_next(dict);
    }
}

static DBusMessage* handle_notify(DBusMessage* msg, NotifySink* sink)
{
    DBusMessageIter it;
    Notification n;
    if (!dbus_message_iter_init(msg, &it))
        return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, "no args");
    const char* s;
    dbus_uint32_t replaces = 0;
    dbus_int32_t timeout = -1;
    int field = 0;
    /* Notify(s app, u replaces, s icon, s summary, s body, as actions,
     *        a{sv} hints, i timeout); actions are not offered. */
    do {
        int t = dbus_message_iter_get_arg_type(&it);
        if (t == DBUS_TYPE_STRING &&
            (field == 0 || field == 2 || field == 3 || field == 4)) {
            dbus_message_iter_get_basic(&it, &s);
            if (field == 0)
                n.app = s;
            else if (field == 2)
                n.icon = s;
            else if (field == 3)
                n.summary = s;
            else
                n.body = s;
        } else if (field == 1 && t == DBUS_TYPE_UINT32) {
            dbus_message_iter_get_basic(&it, &replaces);
        } else if (field == 6 && t == DBUS_TYPE_ARRAY) {
            DBusMessageIter dict;
            dbus_message_iter_recurse(&it, &dict);
            read_hints(&dict, &n);
        } else if (field == 7 && t == DBUS_TYPE_INT32) {
            dbus_message_iter_get_basic(&it, &timeout);
        }
        field++;
    } while (dbus_message_iter_next(&it));

    n.id = replaces;
    n.timeout_ms = timeout;
    dbus_uint32_t id = (dbus_uint32_t)sink->show(n);
    DBusMessage* reply = dbus_message_new_method_return(msg);
    dbus_message_append_args(reply, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID);
    return reply;
}

static DBusHandlerResult on_message(DBusConnection* conn, DBusMessage* msg,
                                    void* data)
{
    NotifySink* sink = (NotifySink*)data;
    DBusMessage* reply = 0;

    if (dbus_message_is_method_call(msg, k_iface, "Notify")) {
        reply = handle_notify(msg, sink);
    } else if (dbus_message_is_method_call(msg, k_iface, "GetCapabilities")) {
        reply = dbus_message_new_method_return(msg);
        DBusMessageIter it, arr;
        dbus_message_iter_init_append(reply, &it);
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &arr);
        const char* cap = "body";
        dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &cap);
        dbus_message_iter_close_container(&it, &arr);
    } else if (dbus_message_is_method_call(msg, k_iface,
                                           "GetServerInformation")) {
        reply = dbus_message_new_method_return(msg);
        const char* name = "ttr";
        const char* vendor = "To The Roots";
        const char* version = "0.1";
        const char* spec = "1.2";
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &name,
                                 DBUS_TYPE_STRING, &vendor, DBUS_TYPE_STRING,
                                 &version, DBUS_TYPE_STRING, &spec,
                                 DBUS_TYPE_INVALID);
    } else if (dbus_message_is_method_call(msg, k_iface,
                                           "CloseNotification")) {
        dbus_uint32_t id = 0;
        dbus_message_get_args(msg, 0, DBUS_TYPE_UINT32, &id,
                              DBUS_TYPE_INVALID);
        sink->close(id, NOTIFY_CLOSED_BY_CALL);
        reply = dbus_message_new_method_return(msg);
    } else if (dbus_message_is_method_call(msg,
                                           "org.freedesktop.DBus.Introspectable",
                                           "Introspect")) {
        reply = dbus_message_new_method_return(msg);
        const char* xml = k_introspect;
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml,
                                 DBUS_TYPE_INVALID);
    } else {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    if (reply) {
        dbus_connection_send(conn, reply, 0);
        dbus_message_unref(reply);
        dbus_connection_flush(conn);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
}

DBusNotifySource::DBusNotifySource(XtAppContext app, NotifySink* sink)
    : app_(app), sink_(sink), conn_(0), input_(0)
{
}

DBusNotifySource::~DBusNotifySource()
{
    if (input_)
        XtRemoveInput(input_);
    if (conn_) {
        DBusConnection* c = (DBusConnection*)conn_;
        dbus_connection_close(c);
        dbus_connection_unref(c);
    }
}

bool DBusNotifySource::start()
{
    DBusError err;
    dbus_error_init(&err);
    /* A private connection: its fd is ours to watch from Xt. */
    DBusConnection* c = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
    if (!c) {
        fprintf(stderr, "ttr: no D-Bus session bus (%s); app notifications "
                "are off\n", err.message ? err.message : "?");
        dbus_error_free(&err);
        return false;
    }
    dbus_connection_set_exit_on_disconnect(c, FALSE);
    int r = dbus_bus_request_name(c, k_name, DBUS_NAME_FLAG_DO_NOT_QUEUE,
                                  &err);
    if (r != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        fprintf(stderr, "ttr: another notification daemon owns %s; "
                "leaving it to that one\n", k_name);
        dbus_error_free(&err);
        dbus_connection_close(c);
        dbus_connection_unref(c);
        return false;
    }
    DBusObjectPathVTable vt;
    memset(&vt, 0, sizeof(vt));
    vt.message_function = on_message;
    dbus_connection_register_object_path(c, k_path, &vt, (void*)sink_);

    int fd = -1;
    if (!dbus_connection_get_unix_fd(c, &fd)) {
        dbus_connection_close(c);
        dbus_connection_unref(c);
        return false;
    }
    conn_ = c;
    input_ = XtAppAddInput(app_, fd, (XtPointer)XtInputReadMask, input_cb,
                           (XtPointer)this);
    while (dbus_connection_dispatch(c) == DBUS_DISPATCH_DATA_REMAINS) {
    }
    return true;
}

void DBusNotifySource::input_cb(XtPointer closure, int* fd, XtInputId* id)
{
    (void)fd;
    (void)id;
    DBusNotifySource* self = (DBusNotifySource*)closure;
    DBusConnection* c = (DBusConnection*)self->conn_;
    if (!c)
        return;
    if (!dbus_connection_read_write(c, 0) ||
        !dbus_connection_get_is_connected(c)) {
        XtRemoveInput(self->input_);
        self->input_ = 0;
        fprintf(stderr, "ttr: lost the D-Bus session bus\n");
        return;
    }
    while (dbus_connection_dispatch(c) == DBUS_DISPATCH_DATA_REMAINS) {
    }
}

void DBusNotifySource::closed(unsigned long id, int reason)
{
    DBusConnection* c = (DBusConnection*)conn_;
    if (!c)
        return;
    DBusMessage* sig = dbus_message_new_signal(k_path, k_iface,
                                               "NotificationClosed");
    dbus_uint32_t i = (dbus_uint32_t)id, r = (dbus_uint32_t)reason;
    dbus_message_append_args(sig, DBUS_TYPE_UINT32, &i, DBUS_TYPE_UINT32, &r,
                             DBUS_TYPE_INVALID);
    dbus_connection_send(c, sig, 0);
    dbus_message_unref(sig);
    dbus_connection_flush(c);
}

#else

DBusNotifySource::DBusNotifySource(XtAppContext app, NotifySink* sink)
    : app_(app), sink_(sink), conn_(0), input_(0)
{
}

DBusNotifySource::~DBusNotifySource() {}

bool DBusNotifySource::start()
{
    return false;   /* built without D-Bus */
}

void DBusNotifySource::closed(unsigned long id, int reason)
{
    (void)id;
    (void)reason;
}

void DBusNotifySource::input_cb(XtPointer closure, int* fd, XtInputId* id)
{
    (void)closure;
    (void)fd;
    (void)id;
}

#endif

}
