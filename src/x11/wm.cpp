#include "x11/wm.h"
#include "x11/xpainter.h"
#include "shell/winmenu.h"

#include <Xm/Xm.h>
#include <Xm/DrawingA.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace ttr {

static Wm* g_wm = 0;
static bool g_another_wm = false;
static XtEventDispatchProc g_old_dispatch[LASTEvent];
static int g_signal_fd = -1;

static const char* const k_title_fonts[] = {
    "-*-helvetica-bold-r-normal--12-*-*-*-*-*-iso8859-1",
    "-*-helvetica-bold-r-normal--*-120-*-*-*-*-*-*",
    "variable",
    "fixed",
    0
};

static const char* k_fallback_resources[] = {
    "*background: #b8b8b8",
    "*fontList: -*-helvetica-bold-r-normal--12-*-*-*-*-*-iso8859-1",
    0
};

/* Event types the WM looks at; everything else goes straight to Xt. */
static const int k_wm_events[] = {
    MapRequest, ConfigureRequest, UnmapNotify, DestroyNotify,
    PropertyNotify, ClientMessage, ButtonPress, ButtonRelease,
    MotionNotify, Expose
};

static const unsigned int k_lock_masks[] = {
    0, LockMask, Mod2Mask, LockMask | Mod2Mask
};

Client::Client()
    : win(None), frame(None), x(0), y(0), w(1), h(1), old_bw(0),
      accepts_input(true), take_focus(false), delete_window(false),
      maximized(false), ignore_unmap(0), pressed(FP_NONE),
      cursor_part(FP_NONE)
{
}

Wm::Wm()
    : app_(0), toplevel_(0), dpy_(0), screen_(0), root_(None),
      screen_w_(0), screen_h_(0), running_(true), painter_(0), menu_(0),
      cursor_normal_(None), cursor_move_(None), active_(0),
      place_x_(0), place_y_(0)
{
    signal_pipe_[0] = signal_pipe_[1] = -1;
    memset(&drag_, 0, sizeof(drag_));
    drag_.mode = DRAG_NONE;
    g_wm = this;
}

Wm::~Wm()
{
    delete menu_;
    delete painter_;
    g_wm = 0;
}

/* ------------------------------------------------------------------ */
/* Startup and shutdown                                                */

static void on_signal(int sig)
{
    (void)sig;
    char c = 0;
    if (write(g_signal_fd, &c, 1) < 0) {
        /* nothing useful to do in a signal handler */
    }
}

bool Wm::init(int* argc, char** argv)
{
    toplevel_ = XtOpenApplication(&app_, "Ttr", 0, 0, argc, argv,
                                  (String*)k_fallback_resources,
                                  applicationShellWidgetClass, 0, 0);
    dpy_ = XtDisplay(toplevel_);
    screen_ = DefaultScreen(dpy_);
    root_ = RootWindow(dpy_, screen_);
    screen_w_ = DisplayWidth(dpy_, screen_);
    screen_h_ = DisplayHeight(dpy_, screen_);

    /* The application shell only parents our Motif popups; it is never
     * shown. Override-redirect so it can never reach our own MapRequest. */
    Arg args[4];
    int n = 0;
    XtSetArg(args[n], XmNmappedWhenManaged, False); n++;
    XtSetArg(args[n], XmNoverrideRedirect, True); n++;
    XtSetArg(args[n], XmNwidth, 1); n++;
    XtSetArg(args[n], XmNheight, 1); n++;
    XtSetValues(toplevel_, args, n);
    Widget area = XmCreateDrawingArea(toplevel_, (char*)"menuParent", 0, 0);
    XtManageChild(area);
    XtRealizeWidget(toplevel_);

    /* Only one client can select SubstructureRedirect on the root. */
    XSetErrorHandler(on_startup_error);
    XSelectInput(dpy_, root_,
                 SubstructureRedirectMask | SubstructureNotifyMask |
                 PropertyChangeMask);
    XSync(dpy_, False);
    if (g_another_wm) {
        fprintf(stderr, "ttr: another window manager is already running\n");
        return false;
    }
    XSetErrorHandler(on_x_error);

    wm_protocols_ = XInternAtom(dpy_, "WM_PROTOCOLS", False);
    wm_delete_window_ = XInternAtom(dpy_, "WM_DELETE_WINDOW", False);
    wm_take_focus_ = XInternAtom(dpy_, "WM_TAKE_FOCUS", False);
    wm_state_ = XInternAtom(dpy_, "WM_STATE", False);
    wm_change_state_ = XInternAtom(dpy_, "WM_CHANGE_STATE", False);

    painter_ = new XPainter(dpy_, screen_);
    if (!painter_->load_font(k_title_fonts)) {
        fprintf(stderr, "ttr: no usable font\n");
        return false;
    }
    theme_.fit_font(painter_->text_ascent() + painter_->text_descent());

    cursor_normal_ = XCreateFontCursor(dpy_, XC_left_ptr);
    cursor_move_ = XCreateFontCursor(dpy_, XC_fleur);
    static const unsigned int shapes[8] = {
        XC_top_side, XC_top_right_corner, XC_right_side,
        XC_bottom_right_corner, XC_bottom_side, XC_bottom_left_corner,
        XC_left_side, XC_top_left_corner
    };
    int i;
    for (i = 0; i < 8; i++)
        cursor_border_[i] = XCreateFontCursor(dpy_, shapes[i]);
    XDefineCursor(dpy_, root_, cursor_normal_);

    menu_ = new WindowMenu(area, this);

    if (pipe(signal_pipe_) == 0) {
        g_signal_fd = signal_pipe_[1];
        XtAppAddInput(app_, signal_pipe_[0], (XtPointer)XtInputReadMask,
                      on_signal_input, (XtPointer)this);
        signal(SIGINT, on_signal);
        signal(SIGTERM, on_signal);
        signal(SIGHUP, on_signal);
    }

    for (i = 0; i < (int)(sizeof(k_wm_events) / sizeof(k_wm_events[0])); i++)
        g_old_dispatch[k_wm_events[i]] =
            XtSetEventDispatcher(dpy_, k_wm_events[i], dispatch);

    adopt_existing();
    return true;
}

int Wm::run()
{
    while (running_)
        XtAppProcessEvent(app_, XtIMAll);
    restore_all();
    XSync(dpy_, False);
    return 0;
}

void Wm::on_signal_input(XtPointer closure, int* fd, XtInputId* id)
{
    (void)id;
    char buf[16];
    if (read(*fd, buf, sizeof(buf)) < 0) {
        /* ignore */
    }
    ((Wm*)closure)->running_ = false;
}

void Wm::adopt_existing()
{
    Window root_ret, parent_ret;
    Window* children = 0;
    unsigned int count = 0, i;

    XGrabServer(dpy_);
    if (XQueryTree(dpy_, root_, &root_ret, &parent_ret, &children, &count)) {
        for (i = 0; i < count; i++) {
            XWindowAttributes attr;
            if (!XGetWindowAttributes(dpy_, children[i], &attr))
                continue;
            if (attr.override_redirect || attr.map_state != IsViewable)
                continue;
            if (children[i] == XtWindow(toplevel_))
                continue;
            manage(children[i], true);
        }
        if (children)
            XFree(children);
    }
    XUngrabServer(dpy_);
}

/* Put every client back on the root window so it survives without us. */
void Wm::restore_all()
{
    int i;
    for (i = 0; i < clients_.size(); i++) {
        Client* c = clients_[i];
        XReparentWindow(dpy_, c->win, root_, c->x, c->y);
        XSetWindowBorderWidth(dpy_, c->win, c->old_bw);
        XRemoveFromSaveSet(dpy_, c->win);
        XMapWindow(dpy_, c->win);
        XDestroyWindow(dpy_, c->frame);
        delete c;
    }
    clients_.clear();
    active_ = 0;
    XSetInputFocus(dpy_, PointerRoot, RevertToPointerRoot, CurrentTime);
}

int Wm::on_startup_error(Display* dpy, XErrorEvent* ev)
{
    (void)dpy;
    if (ev->error_code == BadAccess)
        g_another_wm = true;
    return 0;
}

int Wm::on_x_error(Display* dpy, XErrorEvent* ev)
{
    /* Clients vanish at any time; requests on dead windows are expected. */
    if (ev->error_code == BadWindow || ev->error_code == BadDrawable ||
        ev->error_code == BadMatch)
        return 0;
    char msg[128];
    XGetErrorText(dpy, ev->error_code, msg, sizeof(msg));
    fprintf(stderr, "ttr: X error: %s (request %d.%d, resource 0x%lx)\n",
            msg, ev->request_code, ev->minor_code, ev->resourceid);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Event dispatch                                                      */

Boolean Wm::dispatch(XEvent* ev)
{
    if (g_wm && g_wm->handle(ev))
        return True;
    return g_old_dispatch[ev->type](ev);
}

bool Wm::handle(XEvent* ev)
{
    switch (ev->type) {
    case MapRequest:
        on_map_request(&ev->xmaprequest);
        return true;
    case ConfigureRequest:
        on_configure_request(&ev->xconfigurerequest);
        return true;
    case UnmapNotify:
        return on_unmap(&ev->xunmap);
    case DestroyNotify:
        return on_destroy(&ev->xdestroywindow);
    case PropertyNotify:
        return on_property(&ev->xproperty);
    case ClientMessage:
        return on_client_message(&ev->xclient);
    case ButtonPress:
        return on_button_press(&ev->xbutton);
    case ButtonRelease:
        return on_button_release(&ev->xbutton);
    case MotionNotify:
        return on_motion(&ev->xmotion);
    case Expose:
        return on_expose(&ev->xexpose);
    }
    return false;
}

void Wm::on_map_request(XMapRequestEvent* ev)
{
    Client* c = find_client(ev->window);
    if (c) {
        XMapWindow(dpy_, c->win);
        XMapWindow(dpy_, c->frame);
        set_wm_state(c->win, NormalState);
        raise(c);
        focus(c, CurrentTime);
        return;
    }
    manage(ev->window, false);
}

void Wm::on_configure_request(XConfigureRequestEvent* ev)
{
    Client* c = find_client(ev->window);
    if (!c) {
        XWindowChanges wc;
        wc.x = ev->x;
        wc.y = ev->y;
        wc.width = ev->width;
        wc.height = ev->height;
        wc.border_width = ev->border_width;
        wc.sibling = ev->above;
        wc.stack_mode = ev->detail;
        XConfigureWindow(dpy_, ev->window, (unsigned int)ev->value_mask, &wc);
        return;
    }

    int x = c->x, y = c->y, w = c->w, h = c->h;
    if (!c->maximized) {
        if (ev->value_mask & CWX) x = ev->x;
        if (ev->value_mask & CWY) y = ev->y;
        if (ev->value_mask & CWWidth) w = ev->width;
        if (ev->value_mask & CWHeight) h = ev->height;
        c->size.apply(&w, &h);
    }
    set_geometry(c, x, y, w, h);
    if ((ev->value_mask & CWStackMode) && ev->detail == Above)
        raise(c);
    send_configure(c);
}

bool Wm::on_unmap(XUnmapEvent* ev)
{
    Client* c = find_client(ev->window);
    if (!c)
        return false;
    if (c->ignore_unmap > 0) {
        c->ignore_unmap--;
        return true;
    }
    unmanage(c, false);
    return true;
}

bool Wm::on_destroy(XDestroyWindowEvent* ev)
{
    Client* c = find_client(ev->window);
    if (!c)
        return false;
    unmanage(c, true);
    return true;
}

bool Wm::on_property(XPropertyEvent* ev)
{
    Client* c = find_client(ev->window);
    if (!c)
        return ev->window == root_;
    if (ev->atom == XA_WM_NAME) {
        read_title(c);
        paint(c);
    } else if (ev->atom == XA_WM_NORMAL_HINTS) {
        read_size_hints(c);
    } else if (ev->atom == XA_WM_HINTS) {
        read_wm_hints(c);
    } else if (ev->atom == wm_protocols_) {
        read_protocols(c);
    }
    return true;
}

bool Wm::on_client_message(XClientMessageEvent* ev)
{
    Client* c = find_client(ev->window);
    if (!c)
        return false;
    /* WM_CHANGE_STATE to IconicState needs the taskbar; ignored for now. */
    return true;
}

bool Wm::on_button_press(XButtonEvent* ev)
{
    if (drag_.mode != DRAG_NONE) {
        if (drag_.explicit_grab)
            end_drag(ev->time);
        return true;
    }

    Client* c = find_frame(ev->window);
    if (c) {
        int fw = c->w + theme_.extra_w(), fh = c->h + theme_.extra_h();
        FramePart part = theme_.hit(fw, fh, ev->x, ev->y);
        raise(c);
        focus(c, ev->time);

        if (ev->button == Button3 && part == FP_TITLE) {
            menu_->post(c->win, ev->x_root, ev->y_root, ev, c->maximized);
            return true;
        }
        if (ev->button != Button1)
            return true;

        if (part == FP_MENU) {
            Rect r = theme_.menu_rect(fw);
            menu_->post(c->win, c->x + r.x, c->y + r.y + r.h, ev,
                        c->maximized);
        } else if (part == FP_TITLE) {
            begin_drag(c, DRAG_MOVE, part, ev->x_root, ev->y_root, false,
                       ev->time);
        } else if (part == FP_MINIMIZE || part == FP_MAXIMIZE) {
            c->pressed = part;
            paint(c);
        } else if (frame_part_is_border(part)) {
            begin_drag(c, DRAG_RESIZE, part, ev->x_root, ev->y_root, false,
                       ev->time);
        }
        return true;
    }

    c = find_client(ev->window);
    if (c) {
        /* Passive grab: click-to-focus on unfocused windows, Alt+drag. */
        raise(c);
        focus(c, ev->time);
        if (ev->button == Button1 && (ev->state & Mod1Mask))
            begin_drag(c, DRAG_MOVE, FP_TITLE, ev->x_root, ev->y_root, false,
                       ev->time);
        else
            XAllowEvents(dpy_, ReplayPointer, ev->time);
        return true;
    }
    return false;
}

bool Wm::on_button_release(XButtonEvent* ev)
{
    if (drag_.mode != DRAG_NONE) {
        if (!drag_.explicit_grab)
            end_drag(ev->time);
        return true;
    }

    Client* c = find_frame(ev->window);
    if (!c)
        return false;
    if (c->pressed != FP_NONE) {
        int fw = c->w + theme_.extra_w(), fh = c->h + theme_.extra_h();
        FramePart part = theme_.hit(fw, fh, ev->x, ev->y);
        FramePart pressed = c->pressed;
        c->pressed = FP_NONE;
        paint(c);
        if (part == pressed && pressed == FP_MAXIMIZE)
            toggle_maximize(c);
        /* FP_MINIMIZE: needs the taskbar. */
    }
    return true;
}

bool Wm::on_motion(XMotionEvent* ev)
{
    if (drag_.mode != DRAG_NONE) {
        XEvent next;
        int x = ev->x_root, y = ev->y_root;
        while (XCheckTypedEvent(dpy_, MotionNotify, &next)) {
            x = next.xmotion.x_root;
            y = next.xmotion.y_root;
        }
        update_drag(x, y);
        return true;
    }

    Client* c = find_frame(ev->window);
    if (!c)
        return false;
    int fw = c->w + theme_.extra_w(), fh = c->h + theme_.extra_h();
    FramePart part = theme_.hit(fw, fh, ev->x, ev->y);
    if (!frame_part_is_border(part))
        part = FP_NONE;
    if (part != c->cursor_part) {
        c->cursor_part = part;
        XDefineCursor(dpy_, c->frame, cursor_for(part));
    }
    return true;
}

bool Wm::on_expose(XExposeEvent* ev)
{
    Client* c = find_frame(ev->window);
    if (!c)
        return false;
    if (ev->count == 0)
        paint(c);
    return true;
}

/* ------------------------------------------------------------------ */
/* Managing clients                                                    */

void Wm::manage(Window w, bool already_mapped)
{
    XWindowAttributes attr;
    if (!XGetWindowAttributes(dpy_, w, &attr) || attr.override_redirect)
        return;

    Client* c = new Client;
    c->win = w;
    c->w = attr.width;
    c->h = attr.height;
    c->old_bw = attr.border_width;
    read_title(c);
    read_size_hints(c);
    read_wm_hints(c);
    read_protocols(c);

    /* Fit on screen, frame included. */
    int max_w = screen_w_ - theme_.extra_w();
    int max_h = screen_h_ - theme_.extra_h();
    if (c->w > max_w) c->w = max_w;
    if (c->h > max_h) c->h = max_h;
    if (c->w < theme_.min_client_w()) c->w = theme_.min_client_w();
    c->size.apply(&c->w, &c->h);

    XSizeHints hints;
    long supplied = 0;
    bool positioned = false;
    if (XGetWMNormalHints(dpy_, w, &hints, &supplied))
        positioned = (hints.flags & (USPosition | PPosition)) != 0;
    if (already_mapped || positioned) {
        c->x = attr.x;
        c->y = attr.y;
    } else {
        /* Cascade. */
        place_x_ += 24;
        place_y_ += 24;
        if (place_x_ + c->w + theme_.extra_w() > screen_w_ ||
            place_y_ + c->h + theme_.extra_h() > screen_h_)
            place_x_ = place_y_ = 24;
        c->x = place_x_;
        c->y = place_y_;
    }

    int fw = c->w + theme_.extra_w(), fh = c->h + theme_.extra_h();
    XSetWindowAttributes sa;
    sa.background_pixel = painter_->pixel(theme_.inactive.bg);
    sa.event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
                    ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                    ExposureMask;
    sa.cursor = cursor_normal_;
    c->frame = XCreateWindow(dpy_, root_, c->x, c->y, fw, fh, 0,
                             CopyFromParent, InputOutput, CopyFromParent,
                             CWBackPixel | CWEventMask | CWCursor, &sa);

    XSelectInput(dpy_, w, PropertyChangeMask);
    XAddToSaveSet(dpy_, w);
    XSetWindowBorderWidth(dpy_, w, 0);
    if (already_mapped)
        c->ignore_unmap++;  /* reparenting a mapped window unmaps it */
    XReparentWindow(dpy_, w, c->frame, theme_.client_x(), theme_.client_y());
    XResizeWindow(dpy_, w, c->w, c->h);

    clients_.push_back(c);
    update_grabs(c, false);

    XMapWindow(dpy_, w);
    XMapWindow(dpy_, c->frame);
    set_wm_state(w, NormalState);
    send_configure(c);
    focus(c, CurrentTime);
}

void Wm::unmanage(Client* c, bool destroyed)
{
    if (drag_.client == c)
        drag_.mode = DRAG_NONE;

    XGrabServer(dpy_);
    if (!destroyed) {
        XUngrabButton(dpy_, AnyButton, AnyModifier, c->win);
        XReparentWindow(dpy_, c->win, root_, c->x, c->y);
        XSetWindowBorderWidth(dpy_, c->win, c->old_bw);
        XRemoveFromSaveSet(dpy_, c->win);
        set_wm_state(c->win, WithdrawnState);
    }
    XDestroyWindow(dpy_, c->frame);
    XUngrabServer(dpy_);

    int i = clients_.find(c);
    if (i >= 0)
        clients_.remove_at(i);
    if (active_ == c) {
        active_ = 0;
        focus(clients_.empty() ? 0 : clients_.back(), CurrentTime);
    }
    delete c;
}

void Wm::read_title(Client* c)
{
    char* name = 0;
    if (XFetchName(dpy_, c->win, &name) && name) {
        c->title = name;
        XFree(name);
    } else {
        c->title = "";
    }
}

void Wm::read_size_hints(Client* c)
{
    XSizeHints h;
    long supplied = 0;
    c->size = SizeConstraints();
    if (!XGetWMNormalHints(dpy_, c->win, &h, &supplied))
        return;
    if (h.flags & PMinSize) {
        c->size.min_w = h.min_width;
        c->size.min_h = h.min_height;
    }
    if (h.flags & PMaxSize) {
        c->size.max_w = h.max_width;
        c->size.max_h = h.max_height;
    }
    if (h.flags & PResizeInc) {
        c->size.inc_w = h.width_inc;
        c->size.inc_h = h.height_inc;
    }
    if (h.flags & PBaseSize) {
        c->size.base_w = h.base_width;
        c->size.base_h = h.base_height;
    }
    if (c->size.min_w < theme_.min_client_w())
        c->size.min_w = theme_.min_client_w();
}

void Wm::read_wm_hints(Client* c)
{
    XWMHints* h = XGetWMHints(dpy_, c->win);
    c->accepts_input = true;
    if (h) {
        if (h->flags & InputHint)
            c->accepts_input = h->input != False;
        XFree(h);
    }
}

void Wm::read_protocols(Client* c)
{
    Atom* protos = 0;
    int n = 0, i;
    c->take_focus = false;
    c->delete_window = false;
    if (XGetWMProtocols(dpy_, c->win, &protos, &n)) {
        for (i = 0; i < n; i++) {
            if (protos[i] == wm_take_focus_)
                c->take_focus = true;
            else if (protos[i] == wm_delete_window_)
                c->delete_window = true;
        }
        XFree(protos);
    }
}

Client* Wm::find_client(Window w)
{
    int i;
    for (i = 0; i < clients_.size(); i++)
        if (clients_[i]->win == w)
            return clients_[i];
    return 0;
}

Client* Wm::find_frame(Window w)
{
    int i;
    for (i = 0; i < clients_.size(); i++)
        if (clients_[i]->frame == w)
            return clients_[i];
    return 0;
}

Client* Wm::find_id(ToplevelId id)
{
    return find_client((Window)id);
}

/* ------------------------------------------------------------------ */
/* Focus, stacking, actions                                            */

void Wm::focus(Client* c, Time t)
{
    Client* old = active_;
    active_ = c;
    if (old && old != c) {
        update_grabs(old, false);
        paint(old);
    }
    if (!c) {
        XSetInputFocus(dpy_, PointerRoot, RevertToPointerRoot, t);
        return;
    }
    update_grabs(c, true);
    if (c->accepts_input)
        XSetInputFocus(dpy_, c->win, RevertToPointerRoot, t);
    if (c->take_focus)
        send_protocol(c, wm_take_focus_, t);
    paint(c);
}

void Wm::raise(Client* c)
{
    XRaiseWindow(dpy_, c->frame);
    int i = clients_.find(c);
    if (i >= 0) {
        clients_.remove_at(i);
        clients_.push_back(c);
    }
}

void Wm::lower(Client* c)
{
    XLowerWindow(dpy_, c->frame);
    int i = clients_.find(c);
    if (i > 0) {
        clients_.remove_at(i);
        /* Move to the bottom by shifting everyone up one slot. */
        clients_.push_back(c);
        int j;
        for (j = clients_.size() - 1; j > 0; j--)
            clients_[j] = clients_[j - 1];
        clients_[0] = c;
    }
}

/* Unfocused windows get a synchronous grab on every button so the first
 * click focuses them (and is then replayed). All windows get Alt+Button1
 * for moving. Lock and NumLock variants are grabbed too. */
void Wm::update_grabs(Client* c, bool focused)
{
    XUngrabButton(dpy_, AnyButton, AnyModifier, c->win);
    if (!focused)
        XGrabButton(dpy_, AnyButton, AnyModifier, c->win, False,
                    ButtonPressMask, GrabModeSync, GrabModeAsync, None, None);
    int i;
    for (i = 0; i < 4; i++)
        XGrabButton(dpy_, Button1, Mod1Mask | k_lock_masks[i], c->win, False,
                    ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                    GrabModeAsync, GrabModeAsync, None, cursor_move_);
}

void Wm::send_protocol(Client* c, Atom proto, Time t)
{
    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = c->win;
    ev.xclient.message_type = wm_protocols_;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = (long)proto;
    ev.xclient.data.l[1] = (long)t;
    XSendEvent(dpy_, c->win, False, NoEventMask, &ev);
}

void Wm::close(Client* c)
{
    if (c->delete_window)
        send_protocol(c, wm_delete_window_, CurrentTime);
    else
        XKillClient(dpy_, c->win);
}

void Wm::toggle_maximize(Client* c)
{
    if (c->maximized) {
        c->maximized = false;
        set_geometry(c, c->saved.x, c->saved.y, c->saved.w, c->saved.h);
    } else {
        c->saved = Rect(c->x, c->y, c->w, c->h);
        c->maximized = true;
        int w = screen_w_ - theme_.extra_w();
        int h = screen_h_ - theme_.extra_h();
        c->size.apply(&w, &h);
        set_geometry(c, 0, 0, w, h);
    }
    send_configure(c);
}

void Wm::request(ToplevelId id, WmAction action)
{
    Client* c = find_id(id);
    if (!c)
        return;
    int fw = c->w + theme_.extra_w(), fh = c->h + theme_.extra_h();

    switch (action) {
    case ACT_RESTORE:
        if (c->maximized)
            toggle_maximize(c);
        break;
    case ACT_MAXIMIZE:
        if (!c->maximized)
            toggle_maximize(c);
        break;
    case ACT_MOVE: {
        Rect t = theme_.title_rect(fw);
        int x = c->x + t.x + t.w / 2, y = c->y + t.y + t.h / 2;
        XWarpPointer(dpy_, None, root_, 0, 0, 0, 0, x, y);
        begin_drag(c, DRAG_MOVE, FP_TITLE, x, y, true, CurrentTime);
        break;
    }
    case ACT_SIZE: {
        int x = c->x + fw - 1, y = c->y + fh - 1;
        XWarpPointer(dpy_, None, root_, 0, 0, 0, 0, x, y);
        begin_drag(c, DRAG_RESIZE, FP_SE, x, y, true, CurrentTime);
        break;
    }
    case ACT_LOWER:
        lower(c);
        break;
    case ACT_CLOSE:
        close(c);
        break;
    case ACT_MINIMIZE:
    case ACT_COUNT:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Geometry and painting                                               */

void Wm::set_geometry(Client* c, int x, int y, int w, int h)
{
    bool resized = w != c->w || h != c->h;
    c->x = x;
    c->y = y;
    c->w = w;
    c->h = h;
    XMoveResizeWindow(dpy_, c->frame, x, y, w + theme_.extra_w(),
                      h + theme_.extra_h());
    if (resized) {
        XResizeWindow(dpy_, c->win, w, h);
        paint(c);
    }
}

/* ICCCM 4.1.5: tell the client where it is in root coordinates. */
void Wm::send_configure(Client* c)
{
    XConfigureEvent ce;
    memset(&ce, 0, sizeof(ce));
    ce.type = ConfigureNotify;
    ce.event = c->win;
    ce.window = c->win;
    ce.x = c->x + theme_.client_x();
    ce.y = c->y + theme_.client_y();
    ce.width = c->w;
    ce.height = c->h;
    ce.border_width = 0;
    ce.above = None;
    ce.override_redirect = False;
    XSendEvent(dpy_, c->win, False, StructureNotifyMask, (XEvent*)&ce);
}

void Wm::set_wm_state(Window w, long state)
{
    long data[2];
    data[0] = state;
    data[1] = None;
    XChangeProperty(dpy_, w, wm_state_, wm_state_, 32, PropModeReplace,
                    (unsigned char*)data, 2);
}

void Wm::paint(Client* c)
{
    int fw = c->w + theme_.extra_w(), fh = c->h + theme_.extra_h();
    painter_->begin(c->frame, fw, fh);
    paint_frame(*painter_, theme_, fw, fh, c->title.c_str(), c == active_,
                c->pressed);
    painter_->end();
}

Cursor Wm::cursor_for(FramePart part)
{
    if (frame_part_is_border(part))
        return cursor_border_[part - FP_N];
    return cursor_normal_;
}

/* ------------------------------------------------------------------ */
/* Interactive move and resize                                         */

void Wm::begin_drag(Client* c, DragMode mode, FramePart part,
                    int x_root, int y_root, bool explicit_grab, Time t)
{
    Cursor cur = mode == DRAG_MOVE ? cursor_move_ : cursor_for(part);
    unsigned int mask = ButtonPressMask | ButtonReleaseMask |
                        PointerMotionMask;
    if (explicit_grab) {
        if (XGrabPointer(dpy_, root_, False, mask, GrabModeAsync,
                         GrabModeAsync, None, cur, t) != GrabSuccess)
            return;
    } else {
        XChangeActivePointerGrab(dpy_, mask, cur, t);
    }

    drag_.mode = mode;
    drag_.client = c;
    drag_.part = part;
    drag_.explicit_grab = explicit_grab;
    drag_.start_x = x_root;
    drag_.start_y = y_root;
    drag_.orig_x = c->x;
    drag_.orig_y = c->y;
    drag_.orig_w = c->w;
    drag_.orig_h = c->h;
}

void Wm::update_drag(int x_root, int y_root)
{
    Client* c = drag_.client;
    int dx = x_root - drag_.start_x;
    int dy = y_root - drag_.start_y;

    if (drag_.mode == DRAG_MOVE) {
        if (c->maximized)
            return;
        set_geometry(c, drag_.orig_x + dx, drag_.orig_y + dy, c->w, c->h);
        return;
    }

    FramePart p = drag_.part;
    bool west = p == FP_W || p == FP_NW || p == FP_SW;
    bool east = p == FP_E || p == FP_NE || p == FP_SE;
    bool north = p == FP_N || p == FP_NW || p == FP_NE;
    bool south = p == FP_S || p == FP_SW || p == FP_SE;

    int w = drag_.orig_w + (east ? dx : 0) - (west ? dx : 0);
    int h = drag_.orig_h + (south ? dy : 0) - (north ? dy : 0);
    c->size.apply(&w, &h);
    int x = west ? drag_.orig_x + drag_.orig_w - w : drag_.orig_x;
    int y = north ? drag_.orig_y + drag_.orig_h - h : drag_.orig_y;
    c->maximized = false;
    set_geometry(c, x, y, w, h);
}

void Wm::end_drag(Time t)
{
    if (drag_.explicit_grab)
        XUngrabPointer(dpy_, t);
    Client* c = drag_.client;
    drag_.mode = DRAG_NONE;
    drag_.client = 0;
    if (c)
        send_configure(c);
}

}
