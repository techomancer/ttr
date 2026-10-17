#include "shell/taskbar.h"
#include "shell/startmenu.h"
#include "shell/tray.h"
#include "shell/winmenu.h"
#include "core/painter.h"
#include "x11/config.h"
#include "x11/xpainter.h"

#include <Xm/Xm.h>
#include <Xm/DrawingA.h>
#include <Xm/Form.h>
#include <Xm/ToggleB.h>
#include <X11/Shell.h>

#include <string.h>

namespace ttr {

static const char* const k_task_fonts[] = {
    "-*-helvetica-medium-r-normal--12-*-*-*-*-*-iso8859-1",
    "-*-helvetica-medium-r-normal--*-120-*-*-*-*-*-*",
    "variable",
    "fixed",
    0
};

static const int k_max_button = 160;    /* Windows 2000 metrics */
static const int k_button_gap = 3;
static const int k_margin = 2;

Taskbar::Taskbar(Widget parent, WmLink* wm, const Config& cfg,
                 StartMenu* start, WindowMenu* winmenu)
    : wm_(wm), start_menu_(start), winmenu_(winmenu), tray_(0), active_(0),
      pressed_(-1)
{
    Display* dpy = XtDisplay(parent);
    int screen = DefaultScreen(dpy);
    painter_ = new XPainter(dpy, screen);
    painter_->load_font(k_task_fonts);
    cs_ = motif_colors(Rgb::hex(k_widget_bg));

    int h = painter_->text_ascent() + painter_->text_descent() + 14;
    if (h < 28)
        h = 28;
    geom_ = Rect(0, DisplayHeight(dpy, screen) - h, DisplayWidth(dpy, screen),
                 h);

    Arg args[12];
    int n = 0;
    XtSetArg(args[n], XmNx, geom_.x); n++;
    XtSetArg(args[n], XmNy, geom_.y); n++;
    XtSetArg(args[n], XmNwidth, geom_.w); n++;
    XtSetArg(args[n], XmNheight, geom_.h); n++;
    shell_ = XtCreatePopupShell("taskbar", overrideShellWidgetClass, parent,
                                args, n);

    n = 0;
    XtSetArg(args[n], XmNshadowThickness, 1); n++;
    XtSetArg(args[n], XmNshadowType, XmSHADOW_OUT); n++;
    form_ = XmCreateForm(shell_, (char*)"bar", args, n);

    XmString l = XmStringCreateLocalized((char*)"Start");
    n = 0;
    XtSetArg(args[n], XmNlabelString, l); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNleftOffset, k_margin); n++;
    XtSetArg(args[n], XmNtopOffset, k_margin); n++;
    XtSetArg(args[n], XmNbottomOffset, k_margin); n++;
    XtSetArg(args[n], XmNmarginWidth, 8); n++;
    XtSetArg(args[n], XmNtraversalOn, False); n++;
    XtSetArg(args[n], XmNindicatorOn, False); n++;
    XtSetArg(args[n], XmNshadowThickness, 2); n++;
    XtSetArg(args[n], XmNfillOnSelect, False); n++;
    start_ = XmCreateToggleButton(form_, (char*)"start", args, n);
    XmStringFree(l);
    /* Ahead of Motif's own translations: a button in a shell without the
     * keyboard focus does not arm on its first click. */
    XtInsertEventHandler(start_, ButtonPressMask | ButtonReleaseMask, False,
                         start_press, (XtPointer)this, XtListHead);
    start_menu_->on_unpost(start_unposted, (void*)this);

    n = 0;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightOffset, k_margin); n++;
    XtSetArg(args[n], XmNtopOffset, k_margin); n++;
    XtSetArg(args[n], XmNbottomOffset, k_margin); n++;
    XtSetArg(args[n], XmNwidth, 60); n++;
    XtSetArg(args[n], XmNresizePolicy, XmRESIZE_NONE); n++;
    XtSetArg(args[n], XmNtraversalOn, False); n++;
    tray_area_ = XmCreateDrawingArea(form_, (char*)"tray", args, n);
    XtAddCallback(tray_area_, XmNexposeCallback, tray_expose_cb,
                  (XtPointer)this);

    n = 0;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNleftWidget, start_); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNrightWidget, tray_area_); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNleftOffset, 6); n++;
    XtSetArg(args[n], XmNrightOffset, 6); n++;
    XtSetArg(args[n], XmNtopOffset, k_margin + 1); n++;
    XtSetArg(args[n], XmNbottomOffset, k_margin); n++;
    XtSetArg(args[n], XmNresizePolicy, XmRESIZE_NONE); n++;
    XtSetArg(args[n], XmNtraversalOn, False); n++;
    tasks_ = XmCreateDrawingArea(form_, (char*)"tasks", args, n);
    XtAddCallback(tasks_, XmNexposeCallback, tasks_expose_cb,
                  (XtPointer)this);
    XtAddCallback(tasks_, XmNresizeCallback, tasks_expose_cb,
                  (XtPointer)this);
    XtAddCallback(tasks_, XmNinputCallback, tasks_input_cb, (XtPointer)this);

    XtManageChild(start_);
    XtManageChild(tray_area_);
    XtManageChild(tasks_);
    XtManageChild(form_);
    XtPopup(shell_, XtGrabNone);

    tray_ = new Tray(tray_area_, painter_, cs_, cfg.clock_format.c_str(),
                     tray_resized, (void*)this);
    tray_resized((void*)this);

    wm_->set_panel(XtWindow(shell_), geom_);
}

Taskbar::~Taskbar()
{
    delete tray_;
    int i;
    for (i = 0; i < list_.size(); i++)
        delete list_[i];
    delete painter_;
}

void Taskbar::release()
{
    if (tray_)
        tray_->release();
}

int Taskbar::find(ToplevelId id)
{
    int i;
    for (i = 0; i < list_.size(); i++)
        if (list_[i]->id == id)
            return i;
    return -1;
}

void Taskbar::add(const TaskInfo& t)
{
    if (find(t.id) >= 0) {
        change(t);
        return;
    }
    Task* task = new Task;
    task->id = t.id;
    list_.push_back(task);
    change(t);
}

void Taskbar::change(const TaskInfo& t)
{
    int i = find(t.id);
    if (i < 0)
        return;
    Task* task = list_[i];
    task->title = t.title;
    task->icon = t.icon;
    task->minimized = t.minimized;
    task->allowed = t.allowed;
    paint_tasks();
}

void Taskbar::remove(ToplevelId id)
{
    int i = find(id);
    if (i < 0)
        return;
    delete list_[i];
    list_.remove_at(i);
    pressed_ = -1;
    paint_tasks();
}

void Taskbar::set_active(ToplevelId id)
{
    active_ = id;
    paint_tasks();
}

/* The Start button stays pushed in while the menu is up. */
void Taskbar::post_start_menu(unsigned long time)
{
    XmToggleButtonSetState(start_, True, False);
    start_menu_->post(geom_.x + k_margin, geom_.y, time);
}

/* Pushed in on press, menu on release: a Motif menu posted while the
 * button is still down would close again when it is released. */
void Taskbar::start_press(Widget w, XtPointer closure, XEvent* ev,
                          Boolean* cont)
{
    Taskbar* self = (Taskbar*)closure;
    if ((ev->type != ButtonPress && ev->type != ButtonRelease) ||
        ev->xbutton.button != Button1)
        return;
    *cont = False;
    if (ev->type == ButtonPress) {
        XmToggleButtonSetState(self->start_, True, False);
        return;
    }
    Dimension bw = 0, bh = 0;
    Arg args[2];
    XtSetArg(args[0], XmNwidth, &bw);
    XtSetArg(args[1], XmNheight, &bh);
    XtGetValues(w, args, 2);
    if (ev->xbutton.x >= 0 && ev->xbutton.y >= 0 && ev->xbutton.x < bw &&
        ev->xbutton.y < bh)
        self->post_start_menu(ev->xbutton.time);
    else
        XmToggleButtonSetState(self->start_, False, False);
}

void Taskbar::start_unposted(void* closure)
{
    XmToggleButtonSetState(((Taskbar*)closure)->start_, False, False);
}

Rect Taskbar::button_rect(int i, int w, int h)
{
    int n = list_.size();
    int bw = k_max_button;
    if (n > 0 && (w + k_button_gap) / n - k_button_gap < bw)
        bw = (w + k_button_gap) / n - k_button_gap;
    if (bw < 24)
        bw = 24;
    return Rect(i * (bw + k_button_gap), 0, bw, h);
}

int Taskbar::hit(int x, int y)
{
    Dimension w = 0, h = 0;
    Arg args[2];
    XtSetArg(args[0], XmNwidth, &w);
    XtSetArg(args[1], XmNheight, &h);
    XtGetValues(tasks_, args, 2);
    int i;
    for (i = 0; i < list_.size(); i++)
        if (button_rect(i, w, h).contains(x, y))
            return i;
    return -1;
}

void Taskbar::paint_tasks()
{
    if (!XtIsRealized(tasks_))
        return;
    Dimension dw = 0, dh = 0;
    Arg args[2];
    XtSetArg(args[0], XmNwidth, &dw);
    XtSetArg(args[1], XmNheight, &dh);
    XtGetValues(tasks_, args, 2);
    int w = dw, h = dh;

    /* The active window's button is pushed in on a lighter face. */
    ColorSet down = cs_;
    down.bg = Rgb((unsigned short)(((long)cs_.bg.r + cs_.top.r) / 2),
                  (unsigned short)(((long)cs_.bg.g + cs_.top.g) / 2),
                  (unsigned short)(((long)cs_.bg.b + cs_.top.b) / 2));

    painter_->begin(XtWindow(tasks_), w, h);
    painter_->fill(Rect(0, 0, w, h), cs_.bg);
    int i;
    for (i = 0; i < list_.size(); i++) {
        Task* t = list_[i];
        Rect r = button_rect(i, w, h);
        bool is_down = (t->id == active_ && !t->minimized) || i == pressed_;
        const ColorSet& c = is_down ? down : cs_;
        painter_->fill(r, c.bg);
        if (is_down)
            draw_bevel(*painter_, r, 2, cs_.bottom, cs_.top);
        else
            draw_bevel(*painter_, r, 2, cs_.top, cs_.bottom);

        int off = is_down ? 1 : 0;
        draw_app_icon(*painter_, t->icon, r.x + 5 + off,
                      r.y + (r.h - 16) / 2 + off, 16, c);

        char buf[256];
        int len = fit_text(*painter_, t->title.c_str(), r.w - 30, buf,
                           sizeof(buf));
        int asc = painter_->text_ascent(), desc = painter_->text_descent();
        painter_->text(r.x + 25 + off, r.y + (r.h - asc - desc) / 2 + asc + off,
                       buf, len, cs_.fg);
    }
    painter_->end();
}

void Taskbar::activate(Task* t)
{
    if (t->id == active_ && !t->minimized &&
        action_allowed(t->allowed, ACT_MINIMIZE))
        wm_->request(t->id, ACT_MINIMIZE);
    else
        wm_->request(t->id, ACT_ACTIVATE);
}

void Taskbar::tasks_expose_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ((Taskbar*)closure)->paint_tasks();
}

void Taskbar::tasks_input_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    Taskbar* self = (Taskbar*)closure;
    XEvent* ev = ((XmDrawingAreaCallbackStruct*)call)->event;
    if (!ev)
        return;

    if (ev->type == ButtonPress) {
        int i = self->hit(ev->xbutton.x, ev->xbutton.y);
        if (i < 0)
            return;
        if (ev->xbutton.button == Button1) {
            self->pressed_ = i;
            self->paint_tasks();
        }
    } else if (ev->type == ButtonRelease && ev->xbutton.button == Button3) {
        int i = self->hit(ev->xbutton.x, ev->xbutton.y);
        if (i >= 0) {
            Task* t = self->list_[i];
            self->winmenu_->post(t->id, ev->xbutton.x_root, ev->xbutton.y_root,
                                 0, ev->xbutton.time, t->allowed);
        }
    } else if (ev->type == ButtonRelease && ev->xbutton.button == Button1) {
        int i = self->hit(ev->xbutton.x, ev->xbutton.y);
        int pressed = self->pressed_;
        self->pressed_ = -1;
        if (i >= 0 && i == pressed)
            self->activate(self->list_[i]);
        self->paint_tasks();
    }
}

void Taskbar::tray_expose_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    Taskbar* self = (Taskbar*)closure;
    if (self->tray_)
        self->tray_->paint();
}

void Taskbar::tray_resized(void* closure)
{
    Taskbar* self = (Taskbar*)closure;
    if (!self->tray_)
        return;
    Arg args[1];
    XtSetArg(args[0], XmNwidth, self->tray_->preferred_width());
    XtSetValues(self->tray_area_, args, 1);
}

}
