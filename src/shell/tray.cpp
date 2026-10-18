#include "shell/tray.h"
#include "core/painter.h"
#include "shell/trayitem.h"
#include "x11/xpainter.h"

#include <Xm/Xm.h>
#include <Xm/DrawingA.h>
#include <X11/Xatom.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

namespace ttr {

static const int ICON = 16;     /* Windows 2000 tray icons */
static const int GAP = 2;
static const int PAD = 4;

/* System Tray protocol opcodes and XEmbed messages. */
enum {
    SYSTEM_TRAY_REQUEST_DOCK = 0,
    XEMBED_EMBEDDED_NOTIFY = 0,
    XEMBED_MAPPED = 1 << 0
};

Tray::Tray(Widget area, XPainter* painter, const ColorSet& cs,
           const char* clock_format, void (*resized)(void*), void* closure)
    : area_(area), dpy_(XtDisplay(area)), win_(XtWindow(area)),
      painter_(painter), cs_(cs), clock_format_(clock_format),
      resized_(resized), closure_(closure), owner_(false), last_width_(0)
{
    char name[64];
    sprintf(name, "_NET_SYSTEM_TRAY_S%d", DefaultScreen(dpy_));
    sel_ = XInternAtom(dpy_, name, False);
    manager_ = XInternAtom(dpy_, "MANAGER", False);
    opcode_ = XInternAtom(dpy_, "_NET_SYSTEM_TRAY_OPCODE", False);
    xembed_ = XInternAtom(dpy_, "_XEMBED", False);
    xembed_info_ = XInternAtom(dpy_, "_XEMBED_INFO", False);
    orientation_ = XInternAtom(dpy_, "_NET_SYSTEM_TRAY_ORIENTATION", False);
    visual_ = XInternAtom(dpy_, "_NET_SYSTEM_TRAY_VISUAL", False);

    /* Substructure events tell us about docked icons; the tray protocol
     * messages and SelectionClear are non-maskable. */
    XtAddEventHandler(area_, SubstructureNotifyMask, True, event_cb,
                      (XtPointer)this);
    XtAddCallback(area_, XmNinputCallback, input_cb, (XtPointer)this);
    acquire();
    schedule_clock();
}

Tray::~Tray()
{
    release();
}

void Tray::acquire()
{
    if (XGetSelectionOwner(dpy_, sel_) != None) {
        fprintf(stderr, "ttr: another system tray is running\n");
        return;
    }
    Time t = XtLastTimestampProcessed(dpy_);
    XSetSelectionOwner(dpy_, sel_, win_, t);
    if (XGetSelectionOwner(dpy_, sel_) != win_)
        return;
    owner_ = true;

    long orient = 0;    /* horizontal */
    XChangeProperty(dpy_, win_, orientation_, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char*)&orient, 1);
    long vis = (long)XVisualIDFromVisual(DefaultVisual(dpy_,
                                                       DefaultScreen(dpy_)));
    XChangeProperty(dpy_, win_, visual_, XA_VISUALID, 32, PropModeReplace,
                    (unsigned char*)&vis, 1);

    /* Tell running tray applications that we are here. */
    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = DefaultRootWindow(dpy_);
    ev.xclient.message_type = manager_;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = (long)t;
    ev.xclient.data.l[1] = (long)sel_;
    ev.xclient.data.l[2] = (long)win_;
    XSendEvent(dpy_, DefaultRootWindow(dpy_), False, StructureNotifyMask,
               &ev);
}

void Tray::release()
{
    int i;
    for (i = 0; i < items_.size(); i++) {
        XUnmapWindow(dpy_, items_[i].win);
        XReparentWindow(dpy_, items_[i].win, DefaultRootWindow(dpy_), 0, 0);
        XRemoveFromSaveSet(dpy_, items_[i].win);
    }
    items_.clear();
    if (owner_) {
        XSetSelectionOwner(dpy_, sel_, None, CurrentTime);
        owner_ = false;
    }
}

int Tray::find(Window icon)
{
    int i;
    for (i = 0; i < items_.size(); i++)
        if (items_[i].win == icon)
            return i;
    return -1;
}

void Tray::dock(Window icon)
{
    if (find(icon) >= 0)
        return;

    /* _XEMBED_INFO: version, flags. Without it the icon wants mapping. */
    bool map = true;
    Atom type;
    int format;
    unsigned long n = 0, after;
    unsigned char* data = 0;
    if (XGetWindowProperty(dpy_, icon, xembed_info_, 0, 2, False,
                           xembed_info_, &type, &format, &n, &after,
                           &data) == Success && data) {
        if (format == 32 && n >= 2)
            map = (((long*)data)[1] & XEMBED_MAPPED) != 0;
        XFree(data);
    }

    XAddToSaveSet(dpy_, icon);
    XReparentWindow(dpy_, icon, win_, 0, 0);
    XResizeWindow(dpy_, icon, ICON, ICON);

    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = icon;
    ev.xclient.message_type = xembed_;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = (long)XtLastTimestampProcessed(dpy_);
    ev.xclient.data.l[1] = XEMBED_EMBEDDED_NOTIFY;
    ev.xclient.data.l[3] = (long)win_;
    ev.xclient.data.l[4] = 0;   /* protocol version */
    XSendEvent(dpy_, icon, False, NoEventMask, &ev);

    Item it;
    it.win = icon;
    it.mapped = map;
    items_.push_back(it);
    if (map)
        XMapRaised(dpy_, icon);
    layout();
}

void Tray::remove(Window icon)
{
    int i = find(icon);
    if (i < 0)
        return;
    items_.remove_at(i);
    layout();
}

void Tray::event_cb(Widget w, XtPointer closure, XEvent* ev, Boolean* cont)
{
    (void)w;
    (void)cont;
    Tray* self = (Tray*)closure;
    int i;
    switch (ev->type) {
    case ClientMessage:
        if (ev->xclient.message_type == self->opcode_ &&
            ev->xclient.data.l[1] == SYSTEM_TRAY_REQUEST_DOCK)
            self->dock((Window)ev->xclient.data.l[2]);
        break;
    case DestroyNotify:
        self->remove(ev->xdestroywindow.window);
        break;
    case ReparentNotify:
        if (ev->xreparent.parent != self->win_)
            self->remove(ev->xreparent.window);
        break;
    case MapNotify:
        i = self->find(ev->xmap.window);
        if (i >= 0 && !self->items_[i].mapped) {
            self->items_[i].mapped = true;
            self->layout();
        }
        break;
    case UnmapNotify:
        i = self->find(ev->xunmap.window);
        if (i >= 0 && self->items_[i].mapped) {
            self->items_[i].mapped = false;
            self->layout();
        }
        break;
    case SelectionClear:
        if (ev->xselectionclear.selection == self->sel_)
            self->owner_ = false;
        break;
    }
}

void Tray::clock_text(char* buf, int size)
{
    time_t now = time(0);
    struct tm* tm = localtime(&now);
    if (!tm || strftime(buf, size, clock_format_.c_str(), tm) == 0)
        buf[0] = 0;
}

void Tray::add_item(TrayItem* item)
{
    builtins_.push_back(item);
    layout();
}

/* Built-in icons come first, then the docked ones. Empty if hidden. */
Rect Tray::item_rect(int i)
{
    Dimension h = 0;
    Arg args[1];
    XtSetArg(args[0], XmNheight, &h);
    XtGetValues(area_, args, 1);
    int x = PAD, k;
    for (k = 0; k < builtins_.size(); k++) {
        if (!builtins_[k]->visible())
            continue;
        if (k == i)
            return Rect(x, ((int)h - ICON) / 2, ICON, ICON);
        x += ICON + GAP;
    }
    return Rect();
}

void Tray::input_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    Tray* self = (Tray*)closure;
    XEvent* ev = ((XmDrawingAreaCallbackStruct*)call)->event;
    if (!ev || (ev->type != ButtonPress && ev->type != ButtonRelease))
        return;
    int i;
    for (i = 0; i < self->builtins_.size(); i++) {
        if (self->item_rect(i).contains(ev->xbutton.x, ev->xbutton.y)) {
            self->builtins_[i]->button(&ev->xbutton);
            break;
        }
    }
}

int Tray::preferred_width()
{
    char buf[64];
    clock_text(buf, sizeof(buf));
    int n = 0, i;
    for (i = 0; i < items_.size(); i++)
        if (items_[i].mapped)
            n++;
    for (i = 0; i < builtins_.size(); i++)
        if (builtins_[i]->visible())
            n++;
    int w = PAD + painter_->text_width(buf, (int)strlen(buf)) + 2 * PAD;
    if (n > 0)
        w += n * (ICON + GAP) + PAD;
    return w;
}

void Tray::layout()
{
    Dimension h = 0;
    Arg args[1];
    XtSetArg(args[0], XmNheight, &h);
    XtGetValues(area_, args, 1);
    int x = PAD, i;
    for (i = 0; i < builtins_.size(); i++)
        if (builtins_[i]->visible())
            x += ICON + GAP;
    for (i = 0; i < items_.size(); i++) {
        if (!items_[i].mapped)
            continue;
        XMoveResizeWindow(dpy_, items_[i].win, x, ((int)h - ICON) / 2,
                          ICON, ICON);
        x += ICON + GAP;
    }
    int w = preferred_width();
    if (w != last_width_) {
        last_width_ = w;
        if (resized_)
            resized_(closure_);
    }
    paint();
}

void Tray::paint()
{
    if (!XtIsRealized(area_))
        return;
    Dimension w = 0, h = 0;
    Arg args[2];
    XtSetArg(args[0], XmNwidth, &w);
    XtSetArg(args[1], XmNheight, &h);
    XtGetValues(area_, args, 2);

    painter_->begin(win_, w, h);
    painter_->fill(Rect(0, 0, w, h), cs_.bg);
    draw_bevel(*painter_, Rect(0, 0, w, h), 1, cs_.bottom, cs_.top);
    char buf[64];
    clock_text(buf, sizeof(buf));
    int len = (int)strlen(buf);
    int tw = painter_->text_width(buf, len);
    int asc = painter_->text_ascent(), desc = painter_->text_descent();
    painter_->text(w - PAD - tw - PAD, (h - asc - desc) / 2 + asc, buf, len,
                   cs_.fg);
    int i;
    for (i = 0; i < builtins_.size(); i++) {
        Rect r = item_rect(i);
        if (r.w > 0)
            builtins_[i]->paint(*painter_, r);
    }
    painter_->end();
}

void Tray::schedule_clock()
{
    time_t now = time(0);
    unsigned long ms = (unsigned long)(60 - now % 60) * 1000 + 50;
    XtAppAddTimeOut(XtWidgetToApplicationContext(area_), ms, clock_cb,
                    (XtPointer)this);
}

void Tray::clock_cb(XtPointer closure, XtIntervalId* id)
{
    (void)id;
    Tray* self = (Tray*)closure;
    self->layout();     /* the text width may change */
    self->schedule_clock();
}

}
