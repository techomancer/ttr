#include "shell/notifier.h"
#include "core/icon.h"
#include "core/painter.h"
#include "core/scale.h"
#include "core/text.h"
#include "x11/config.h"
#include "x11/iconload.h"
#include "x11/xpainter.h"

#include <string.h>

namespace ttr {

/* Sizes at 100%; scaled() when used. */
static const int k_width = 320;
static const int k_margin = 8;          /* from the screen edge and each other */
static const int k_pad = 10;
static const int k_icon = 32;
static const int k_max_lines = 4;
static const int k_max_popups = 5;
static const int k_slide_step = 12;     /* pixels per frame */
static const unsigned long k_slide_ms = 15;

/* Pop-ups line up at the right edge of the area. */
static int popup_x(const Rect& area)
{
    return area.x + area.w - scaled(k_width) - scaled(k_margin);
}

static const char* const k_bold_fonts[] = {
    "-*-helvetica-bold-r-normal--12-*-*-*-*-*-iso8859-1", "variable", "fixed", 0
};
static const char* const k_text_fonts[] = {
    "-*-helvetica-medium-r-normal--12-*-*-*-*-*-iso8859-1", "variable",
    "fixed", 0
};

Notifier::Notifier(Widget widget, WmLink* wm, const Config& cfg,
                   IconLoader* icons, const Rect& area)
    : widget_(widget), dpy_(XtDisplay(widget)), wm_(wm), icons_(icons),
      area_(area), default_ms_(cfg.notification_ms), next_id_(1),
      slide_timer_(0), listener_(0)
{
    painter_ = new XPainter(dpy_, DefaultScreen(dpy_));
    const char* fonts[6];
    int k;
    fonts[0] = cfg.taskbar_xfont.c_str();
    for (k = 0; k < 4; k++)
        fonts[k + 1] = k_text_fonts[k];
    painter_->load_font(fonts);
    fonts[0] = cfg.menu_xfont.c_str();
    for (k = 0; k < 4; k++)
        fonts[k + 1] = k_bold_fonts[k];
    painter_->load_bold_font(fonts);
    cs_ = motif_colors(Rgb::hex(k_widget_bg));

    /* Our popups are not widgets; Xt hands their events to 'widget'. */
    XtAddEventHandler(widget_, ExposureMask | ButtonPressMask, False,
                      event_cb, (XtPointer)this);
}

Notifier::~Notifier()
{
    if (slide_timer_)
        XtRemoveTimeOut(slide_timer_);
    int i;
    for (i = 0; i < popups_.size(); i++) {
        Popup* p = popups_[i];
        if (p->timer)
            XtRemoveTimeOut(p->timer);
        wm_->set_overlay(p->win, false);
        XtUnregisterDrawable(dpy_, p->win);
        XDestroyWindow(dpy_, p->win);
        delete p->icon;
        delete p;
    }
    delete painter_;
}

Notifier::Popup* Notifier::find(unsigned long id)
{
    int i;
    for (i = 0; i < popups_.size(); i++)
        if (popups_[i]->id == id)
            return popups_[i];
    return 0;
}

Notifier::Popup* Notifier::find_window(Window w)
{
    int i;
    for (i = 0; i < popups_.size(); i++)
        if (popups_[i]->win == w)
            return popups_[i];
    return 0;
}

/* Text and size of a popup from its notification. */
void Notifier::layout(Popup* p)
{
    delete p->icon;
    const char* icon = p->n.icon.c_str();
    if (strncmp(icon, "file://", 7) == 0)
        icon += 7;
    p->icon = *icon ? icons_->load(icon, scaled(k_icon)) : 0;

    p->summary = utf8_to_latin1(p->n.summary.c_str(), p->n.summary.length());
    Str body = utf8_to_latin1(p->n.body.c_str(), p->n.body.length());
    p->lines.clear();
    int text_w = scaled(k_width) - 3 * scaled(k_pad) - scaled(k_icon);
    painter_->set_bold(false);
    wrap_text(*painter_, body.c_str(), text_w, k_max_lines, &p->lines);

    painter_->set_bold(true);
    int bh = painter_->text_ascent() + painter_->text_descent();
    painter_->set_bold(false);
    int th = painter_->text_ascent() + painter_->text_descent();
    int h = 2 * scaled(k_pad) + bh + scaled(4) + p->lines.size() * th;
    if (h < scaled(k_icon) + 2 * scaled(k_pad))
        h = scaled(k_icon) + 2 * scaled(k_pad);
    p->w = scaled(k_width);
    p->h = h;
    XResizeWindow(dpy_, p->win, p->w, p->h);
}

unsigned long Notifier::show(const Notification& n)
{
    Popup* p = n.id ? find(n.id) : 0;
    if (!p) {
        p = new Popup;
        p->owner = this;
        p->id = n.id ? n.id : next_id_++;
        p->icon = 0;
        p->timer = 0;
        p->h = 0;
        XSetWindowAttributes sa;
        sa.override_redirect = True;
        sa.save_under = True;
        sa.event_mask = ExposureMask | ButtonPressMask;
        p->win = XCreateWindow(dpy_, DefaultRootWindow(dpy_), 0, 0, 1, 1, 0,
                               CopyFromParent, InputOutput, CopyFromParent,
                               CWOverrideRedirect | CWSaveUnder | CWEventMask,
                               &sa);
        XtRegisterDrawable(dpy_, p->win, widget_);
        /* Start just below the stack, so it slides up into place. */
        p->y = area_.y + area_.h;
        popups_.insert_front(p);
        if (popups_.size() > k_max_popups)
            remove(popups_.back(), NOTIFY_EXPIRED);
        p->n = n;
        p->n.id = p->id;
        layout(p);
        XMoveWindow(dpy_, p->win, popup_x(area_), p->y);
        XMapRaised(dpy_, p->win);
        wm_->set_overlay(p->win, true);
    } else {
        p->n = n;
        p->n.id = p->id;
        layout(p);
    }
    arm_timer(p);
    restack();
    paint(p);
    return p->id;
}

void Notifier::arm_timer(Popup* p)
{
    if (p->timer) {
        XtRemoveTimeOut(p->timer);
        p->timer = 0;
    }
    int ms = p->n.timeout_ms < 0 ? default_ms_ : p->n.timeout_ms;
    if (p->n.urgency >= 2 || ms == 0)
        return;     /* until dismissed */
    p->timer = XtAppAddTimeOut(XtWidgetToApplicationContext(widget_), ms,
                               expire_cb, (XtPointer)p);
}

/* Newest at the bottom, the others above it; every one slides toward its
 * place. */
void Notifier::restack()
{
    int bottom = area_.y + area_.h - scaled(k_margin), i;
    for (i = 0; i < popups_.size(); i++) {
        Popup* p = popups_[i];
        p->target_y = bottom - p->h;
        bottom = p->target_y - scaled(k_margin);
    }
    if (!slide_timer_)
        slide_timer_ = XtAppAddTimeOut(XtWidgetToApplicationContext(widget_),
                                       k_slide_ms, slide_cb, (XtPointer)this);
}

void Notifier::slide_cb(XtPointer closure, XtIntervalId* id)
{
    (void)id;
    Notifier* self = (Notifier*)closure;
    self->slide_timer_ = 0;
    bool moving = false;
    int i;
    for (i = 0; i < self->popups_.size(); i++) {
        Popup* p = self->popups_[i];
        if (p->y == p->target_y)
            continue;
        int d = p->target_y - p->y;
        if (d > scaled(k_slide_step)) d = scaled(k_slide_step);
        if (d < -scaled(k_slide_step)) d = -scaled(k_slide_step);
        p->y += d;
        XMoveWindow(self->dpy_, p->win, popup_x(self->area_), p->y);
        moving = moving || p->y != p->target_y;
    }
    if (moving)
        self->slide_timer_ = XtAppAddTimeOut(
            XtWidgetToApplicationContext(self->widget_), k_slide_ms, slide_cb,
            closure);
}

void Notifier::paint(Popup* p)
{
    XPainter& g = *painter_;
    g.begin(p->win, p->w, p->h);
    g.fill(Rect(0, 0, p->w, p->h), cs_.bg);
    draw_bevel(g, Rect(0, 0, p->w, p->h), scaled_line(2), cs_.top,
               cs_.bottom);
    draw_app_icon(g, p->icon, scaled(k_pad), scaled(k_pad), scaled(k_icon),
                  cs_);

    int x = 2 * scaled(k_pad) + scaled(k_icon);
    g.set_bold(true);
    char buf[512];
    int len = fit_text(g, p->summary.c_str(), p->w - x - scaled(k_pad), buf,
                       sizeof(buf));
    g.text(x, scaled(k_pad) + g.text_ascent(), buf, len, cs_.fg);
    int top = scaled(k_pad) + g.text_ascent() + g.text_descent() + scaled(4);

    g.set_bold(false);
    int th = g.text_ascent() + g.text_descent();
    int i;
    for (i = 0; i < p->lines.size(); i++) {
        const Str& l = p->lines[i];
        g.text(x, top + i * th + g.text_ascent(), l.c_str(), l.length(),
               cs_.fg);
    }
    g.end();
}

void Notifier::remove(Popup* p, int reason)
{
    int i = popups_.find(p);
    if (i < 0)
        return;
    popups_.remove_at(i);
    if (p->timer)
        XtRemoveTimeOut(p->timer);
    wm_->set_overlay(p->win, false);
    XtUnregisterDrawable(dpy_, p->win);
    XDestroyWindow(dpy_, p->win);
    unsigned long id = p->id;
    delete p->icon;
    delete p;
    restack();
    if (listener_)
        listener_->closed(id, reason);
}

void Notifier::close(unsigned long id, int reason)
{
    Popup* p = find(id);
    if (p)
        remove(p, reason);
}

void Notifier::expire_cb(XtPointer closure, XtIntervalId* id)
{
    (void)id;
    Popup* p = (Popup*)closure;
    p->timer = 0;
    p->owner->remove(p, NOTIFY_EXPIRED);
}

void Notifier::event_cb(Widget w, XtPointer closure, XEvent* ev,
                        Boolean* cont)
{
    (void)w;
    (void)cont;
    Notifier* self = (Notifier*)closure;
    Popup* p = self->find_window(ev->xany.window);
    if (!p)
        return;
    if (ev->type == Expose && ev->xexpose.count == 0)
        self->paint(p);
    else if (ev->type == ButtonPress)
        self->remove(p, NOTIFY_DISMISSED);
}

}
