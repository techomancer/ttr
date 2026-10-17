#include "shell/quicklaunch.h"
#include "core/icon.h"
#include "core/launch.h"
#include "core/painter.h"
#include "core/shortcuts.h"
#include "x11/iconload.h"
#include "x11/xpainter.h"

#include <Xm/Xm.h>
#include <Xm/DrawingA.h>

#include <string.h>

namespace ttr {

static const int ICON = 16;
static const int BUTTON = 22;       /* icon plus a 3 pixel ring */
static const int PAD = 4;           /* groove and gap at each end */
static const unsigned long k_tip_ms = 600;

QuickLaunch::QuickLaunch(Widget area, XPainter* painter, const ColorSet& cs,
                         IconLoader* icons)
    : area_(area), dpy_(XtDisplay(area)), painter_(painter), cs_(cs),
      hover_(-1), pressed_(-1), tip_(None), tip_timer_(0)
{
    ShortcutList list;
    Str path = ShortcutList::default_path();
    list.load(path.c_str());
    int i;
    for (i = 0; i < list.size(); i++) {
        const Shortcut* s = list.at(i);
        if (!s->quicklaunch)
            continue;
        Item* it = new Item;
        it->name = s->name.empty() ? s->command : s->name;
        it->command = s->command;
        it->icon = icons->load(s->icon.c_str(), ICON);
        items_.push_back(it);
    }

    XtAddCallback(area_, XmNexposeCallback, expose_cb, (XtPointer)this);
    XtAddEventHandler(area_, ButtonPressMask | ButtonReleaseMask |
                      PointerMotionMask | LeaveWindowMask, False, event_cb,
                      (XtPointer)this);

    XSetWindowAttributes sa;
    sa.override_redirect = True;
    sa.save_under = True;
    tip_ = XCreateWindow(dpy_, DefaultRootWindow(dpy_), 0, 0, 1, 1, 0,
                         CopyFromParent, InputOutput, CopyFromParent,
                         CWOverrideRedirect | CWSaveUnder, &sa);
}

QuickLaunch::~QuickLaunch()
{
    if (tip_timer_)
        XtRemoveTimeOut(tip_timer_);
    XDestroyWindow(dpy_, tip_);
    int i;
    for (i = 0; i < items_.size(); i++) {
        delete items_[i]->icon;
        delete items_[i];
    }
}

/* With no shortcuts the bar is just one groove. */
int QuickLaunch::preferred_width() const
{
    if (items_.empty())
        return 2;
    return 2 * PAD + items_.size() * BUTTON;
}

Rect QuickLaunch::slot(int i) const
{
    Dimension h = 0;
    Arg args[1];
    XtSetArg(args[0], XmNheight, &h);
    XtGetValues(area_, args, 1);
    return Rect(PAD + i * BUTTON, ((int)h - BUTTON) / 2, BUTTON, BUTTON);
}

int QuickLaunch::hit(int x, int y) const
{
    int i;
    for (i = 0; i < items_.size(); i++)
        if (slot(i).contains(x, y))
            return i;
    return -1;
}

static void groove(Painter& p, int x, int h, const ColorSet& cs)
{
    p.fill(Rect(x, 1, 1, h - 2), cs.bottom);
    p.fill(Rect(x + 1, 1, 1, h - 2), cs.top);
}

void QuickLaunch::paint()
{
    if (!XtIsRealized(area_))
        return;
    Dimension dw = 0, dh = 0;
    Arg args[2];
    XtSetArg(args[0], XmNwidth, &dw);
    XtSetArg(args[1], XmNheight, &dh);
    XtGetValues(area_, args, 2);
    int w = dw, h = dh;

    painter_->begin(XtWindow(area_), w, h);
    painter_->fill(Rect(0, 0, w, h), cs_.bg);
    groove(*painter_, 0, h, cs_);
    if (!items_.empty())
        groove(*painter_, w - 2, h, cs_);
    int i;
    for (i = 0; i < items_.size(); i++) {
        Rect r = slot(i);
        bool down = i == pressed_ && i == hover_;
        if (down)
            draw_bevel(*painter_, r, 1, cs_.bottom, cs_.top);
        else if (i == hover_)
            draw_bevel(*painter_, r, 1, cs_.top, cs_.bottom);
        int off = down ? 1 : 0;
        draw_app_icon(*painter_, items_[i]->icon, r.x + (BUTTON - ICON) / 2 + off,
                      r.y + (BUTTON - ICON) / 2 + off, ICON, cs_);
    }
    painter_->end();
}

void QuickLaunch::set_hover(int i)
{
    if (i == hover_)
        return;
    hover_ = i;
    hide_tip();
    if (hover_ >= 0)
        tip_timer_ = XtAppAddTimeOut(XtWidgetToApplicationContext(area_),
                                     k_tip_ms, tip_cb, (XtPointer)this);
    paint();
}

void QuickLaunch::expose_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ((QuickLaunch*)closure)->paint();
}

void QuickLaunch::event_cb(Widget w, XtPointer closure, XEvent* ev,
                           Boolean* cont)
{
    (void)w;
    (void)cont;
    QuickLaunch* self = (QuickLaunch*)closure;
    switch (ev->type) {
    case MotionNotify:
        self->set_hover(self->hit(ev->xmotion.x, ev->xmotion.y));
        break;
    case LeaveNotify:
        self->set_hover(-1);
        break;
    case ButtonPress:
        if (ev->xbutton.button == Button1) {
            self->hide_tip();
            self->pressed_ = self->hit(ev->xbutton.x, ev->xbutton.y);
            self->paint();
        }
        break;
    case ButtonRelease:
        if (ev->xbutton.button == Button1) {
            int i = self->hit(ev->xbutton.x, ev->xbutton.y);
            if (i >= 0 && i == self->pressed_)
                spawn(self->items_[i]->command.c_str());
            self->pressed_ = -1;
            self->paint();
        }
        break;
    }
}

void QuickLaunch::tip_cb(XtPointer closure, XtIntervalId* id)
{
    (void)id;
    QuickLaunch* self = (QuickLaunch*)closure;
    self->tip_timer_ = 0;
    self->show_tip();
}

/* A Windows-style tooltip above the button: pale yellow, black frame. */
void QuickLaunch::show_tip()
{
    if (hover_ < 0 || hover_ >= items_.size())
        return;
    const char* text = items_[hover_]->name.c_str();
    int len = (int)strlen(text);
    int asc = painter_->text_ascent(), desc = painter_->text_descent();
    int w = painter_->text_width(text, len) + 8;
    int h = asc + desc + 4;

    Rect r = slot(hover_);
    int x = 0, y = 0;
    Window child;
    XTranslateCoordinates(dpy_, XtWindow(area_), DefaultRootWindow(dpy_), r.x,
                          r.y, &x, &y, &child);
    y -= h + 4;
    if (y < 0)
        y = r.y + r.h + 4;
    int sw = DisplayWidth(dpy_, DefaultScreen(dpy_));
    if (x + w > sw)
        x = sw - w;

    XMoveResizeWindow(dpy_, tip_, x, y, w, h);
    XMapRaised(dpy_, tip_);
    painter_->begin(tip_, w, h);
    painter_->fill(Rect(0, 0, w, h), Rgb(0, 0, 0));
    painter_->fill(Rect(1, 1, w - 2, h - 2), Rgb::hex(0xffffe1));
    painter_->text(4, 2 + asc, text, len, Rgb(0, 0, 0));
    painter_->end();
}

void QuickLaunch::hide_tip()
{
    if (tip_timer_) {
        XtRemoveTimeOut(tip_timer_);
        tip_timer_ = 0;
    }
    XUnmapWindow(dpy_, tip_);
}

}
