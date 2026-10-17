#include "x11/wm.h"
#include "x11/xpainter.h"
#include "shell/winmenu.h"

#include <Xm/Xm.h>
#include <Xm/DrawingA.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>

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
    MotionNotify, Expose, KeyPress, MappingNotify
};

static const unsigned int k_lock_masks[] = {
    0, LockMask, Mod2Mask, LockMask | Mod2Mask
};

/* Modifiers that distinguish bindings; Lock and NumLock are ignored. */
static const unsigned int k_binding_mods =
    ShiftMask | ControlMask | Mod1Mask | Mod4Mask;

/* Two clicks on a title bar within this many ms toggle maximize. */
static const Time k_double_click_ms = 400;

enum KeyCommand { KC_ACTION, KC_NEXT, KC_PREV, KC_MENU };

struct KeyBinding {
    const char* keysym;
    unsigned int mods;
    KeyCommand cmd;
    WmAction action;    /* for KC_ACTION */
};

/* mwm's default bindings, plus Alt+Tab. */
static const KeyBinding k_keys[] = {
    { "F3", Mod1Mask, KC_ACTION, ACT_LOWER },
    { "F4", Mod1Mask, KC_ACTION, ACT_CLOSE },
    { "F5", Mod1Mask, KC_ACTION, ACT_RESTORE },
    { "F7", Mod1Mask, KC_ACTION, ACT_MOVE },
    { "F8", Mod1Mask, KC_ACTION, ACT_SIZE },
    { "F9", Mod1Mask, KC_ACTION, ACT_MINIMIZE },
    { "F10", Mod1Mask, KC_ACTION, ACT_MAXIMIZE },
    { "Tab", Mod1Mask, KC_NEXT, ACT_COUNT },
    { "Tab", Mod1Mask | ShiftMask, KC_PREV, ACT_COUNT },
    { "Escape", Mod1Mask, KC_NEXT, ACT_COUNT },
    { "Escape", Mod1Mask | ShiftMask, KC_PREV, ACT_COUNT },
    { "Escape", ShiftMask, KC_MENU, ACT_COUNT },
    { "space", Mod1Mask, KC_MENU, ACT_COUNT }
};
static const int k_nkeys = (int)(sizeof(k_keys) / sizeof(k_keys[0]));
static KeySym g_key_syms[sizeof(k_keys) / sizeof(k_keys[0])];

Client::Client()
    : win(None), frame(None), transient_for(None), x(0), y(0), w(1), h(1),
      old_bw(0), gravity(GRAV_NW), type(WT_NORMAL),
      actions(all_actions()), accepts_input(true), take_focus(false),
      delete_window(false), maximized(false), fullscreen(false),
      ignore_unmap(0), pressed(FP_NONE), cursor_part(FP_NONE)
{
}

Wm::Wm()
    : app_(0), toplevel_(0), dpy_(0), screen_(0), root_(None),
      check_win_(None), screen_w_(0), screen_h_(0), running_(true),
      painter_(0), menu_(0), cursor_normal_(None), cursor_move_(None),
      active_(0), place_x_(0), place_y_(0), last_click_time_(0),
      last_click_win_(None)
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

    atoms_.init(dpy_);

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

    setup_ewmh();
    grab_keys();
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

void Wm::setup_ewmh()
{
    check_win_ = XCreateSimpleWindow(dpy_, root_, -100, -100, 1, 1, 0, 0, 0);
    XChangeProperty(dpy_, root_, atoms_.net_supporting_wm_check, XA_WINDOW,
                    32, PropModeReplace, (unsigned char*)&check_win_, 1);
    XChangeProperty(dpy_, check_win_, atoms_.net_supporting_wm_check,
                    XA_WINDOW, 32, PropModeReplace,
                    (unsigned char*)&check_win_, 1);
    XChangeProperty(dpy_, check_win_, atoms_.net_wm_name, atoms_.utf8_string,
                    8, PropModeReplace, (unsigned char*)"ttr", 3);

    Atom supported[] = {
        atoms_.net_supported, atoms_.net_supporting_wm_check,
        atoms_.net_wm_name, atoms_.net_client_list,
        atoms_.net_client_list_stacking, atoms_.net_active_window,
        atoms_.net_close_window, atoms_.net_frame_extents,
        atoms_.net_wm_state, atoms_.net_wm_state_fullscreen,
        atoms_.net_wm_state_maximized_vert,
        atoms_.net_wm_state_maximized_horz, atoms_.net_wm_window_type,
        atoms_.net_type_desktop, atoms_.net_type_dock,
        atoms_.net_type_toolbar, atoms_.net_type_menu,
        atoms_.net_type_utility, atoms_.net_type_splash,
        atoms_.net_type_dialog, atoms_.net_type_notification,
        atoms_.net_type_normal
    };
    XChangeProperty(dpy_, root_, atoms_.net_supported, XA_ATOM, 32,
                    PropModeReplace, (unsigned char*)supported,
                    (int)(sizeof(supported) / sizeof(supported[0])));
    update_client_lists();
}

void Wm::grab_keys()
{
    XUngrabKey(dpy_, AnyKey, AnyModifier, root_);
    int i, j;
    for (i = 0; i < k_nkeys; i++) {
        g_key_syms[i] = XStringToKeysym(k_keys[i].keysym);
        KeyCode kc = XKeysymToKeycode(dpy_, g_key_syms[i]);
        if (!kc)
            continue;
        for (j = 0; j < 4; j++)
            XGrabKey(dpy_, kc, k_keys[i].mods | k_lock_masks[j], root_,
                     False, GrabModeAsync, GrabModeAsync);
    }
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
        int dx, dy;
        gravity_offset(c->gravity, extents_of(c), c->old_bw, &dx, &dy);
        XReparentWindow(dpy_, c->win, root_, c->x - dx, c->y - dy);
        XSetWindowBorderWidth(dpy_, c->win, c->old_bw);
        XRemoveFromSaveSet(dpy_, c->win);
        XMapWindow(dpy_, c->win);
        XDestroyWindow(dpy_, c->frame);
        delete c;
    }
    clients_.clear();
    mapped_.clear();
    active_ = 0;
    XSetInputFocus(dpy_, PointerRoot, RevertToPointerRoot, CurrentTime);

    XDeleteProperty(dpy_, root_, atoms_.net_supporting_wm_check);
    XDeleteProperty(dpy_, root_, atoms_.net_supported);
    XDeleteProperty(dpy_, root_, atoms_.net_active_window);
    XDeleteProperty(dpy_, root_, atoms_.net_client_list);
    XDeleteProperty(dpy_, root_, atoms_.net_client_list_stacking);
    XDestroyWindow(dpy_, check_win_);
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
    case KeyPress:
        return on_key_press(&ev->xkey);
    case MappingNotify:
        on_mapping(&ev->xmapping);
        return false;   /* Xt needs to see it too */
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

    if (ev->value_mask & CWBorderWidth)
        c->old_bw = ev->border_width;

    if (!c->maximized && !c->fullscreen) {
        int x = c->x, y = c->y, w = c->w, h = c->h;
        int dx, dy;
        gravity_offset(c->gravity, extents_of(c), c->old_bw, &dx, &dy);
        if (ev->value_mask & CWX) x = ev->x + dx;
        if (ev->value_mask & CWY) y = ev->y + dy;
        if (ev->value_mask & CWWidth) w = ev->width;
        if (ev->value_mask & CWHeight) h = ev->height;
        c->size.apply(&w, &h);
        set_geometry(c, x, y, w, h);
    }
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
    if (ev->atom == XA_WM_NAME || ev->atom == atoms_.net_wm_name) {
        c->title = read_title(dpy_, atoms_, c->win);
        paint(c);
    } else if (ev->atom == XA_WM_NORMAL_HINTS) {
        read_size_hints(c);
    } else if (ev->atom == XA_WM_HINTS) {
        read_wm_hints(c);
    } else if (ev->atom == atoms_.wm_protocols) {
        read_protocols(c);
    } else if (ev->atom == atoms_.motif_wm_hints) {
        read_decor(c);
        read_size_hints(c);
        apply_state(c, false);
    }
    return true;
}

bool Wm::on_client_message(XClientMessageEvent* ev)
{
    Client* c = find_client(ev->window);
    if (!c)
        return false;

    if (ev->message_type == atoms_.net_active_window) {
        raise(c);
        focus(c, ev->data.l[1] ? (Time)ev->data.l[1] : CurrentTime);
    } else if (ev->message_type == atoms_.net_close_window) {
        if (action_allowed(c->actions, ACT_CLOSE))
            close(c);
    } else if (ev->message_type == atoms_.net_wm_state) {
        /* data.l[0]: 0 remove, 1 add, 2 toggle; l[1], l[2]: properties.
         * Maximized vert and horz are only supported together. */
        long op = ev->data.l[0];
        bool fs = false, max = false;
        int i;
        for (i = 1; i <= 2; i++) {
            Atom p = (Atom)ev->data.l[i];
            if (p == atoms_.net_wm_state_fullscreen)
                fs = true;
            else if (p == atoms_.net_wm_state_maximized_vert ||
                     p == atoms_.net_wm_state_maximized_horz)
                max = true;
        }
        if (fs)
            set_fullscreen(c, op == 1 ? true : op == 0 ? false
                                                       : !c->fullscreen);
        if (max)
            set_maximized(c, op == 1 ? true : op == 0 ? false
                                                      : !c->maximized);
    }
    /* WM_CHANGE_STATE to IconicState needs the taskbar; ignored for now. */
    return true;
}

bool Wm::on_button_press(XButtonEvent* ev)
{
    if (drag_.mode != DRAG_NONE) {
        if (drag_.explicit_grab)
            end_drag(ev->time, false);
        return true;
    }

    Client* c = find_frame(ev->window);
    if (c) {
        FrameDecor d = decor_of(c);
        FramePart part = theme_.hit(d, frame_w(c), frame_h(c), ev->x, ev->y);
        ActionMask ok = allowed(c);
        raise(c);
        focus(c, ev->time);

        if (ev->button == Button3 && part == FP_TITLE) {
            menu_->post(c->win, ev->x_root, ev->y_root, ev, ev->time, ok);
            return true;
        }
        if (ev->button != Button1)
            return true;

        if (part == FP_MENU) {
            post_menu(c, ev, ev->time);
        } else if (part == FP_TITLE) {
            bool dbl = last_click_win_ == c->win &&
                       ev->time - last_click_time_ < k_double_click_ms;
            last_click_win_ = c->win;
            last_click_time_ = ev->time;
            if (dbl) {
                last_click_win_ = None;
                if (c->maximized)
                    set_maximized(c, false);
                else if (action_allowed(ok, ACT_MAXIMIZE))
                    set_maximized(c, true);
            } else if (action_allowed(ok, ACT_MOVE)) {
                begin_drag(c, DRAG_MOVE, part, ev->x_root, ev->y_root, false,
                           ev->time);
            }
        } else if (part == FP_MINIMIZE || part == FP_MAXIMIZE) {
            c->pressed = part;
            paint(c);
        } else if (frame_part_is_border(part) &&
                   action_allowed(ok, ACT_SIZE)) {
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
        if (ev->button == Button1 && (ev->state & Mod1Mask)) {
            if (action_allowed(allowed(c), ACT_MOVE))
                begin_drag(c, DRAG_MOVE, FP_TITLE, ev->x_root, ev->y_root,
                           false, ev->time);
        } else {
            XAllowEvents(dpy_, ReplayPointer, ev->time);
        }
        return true;
    }
    return false;
}

bool Wm::on_button_release(XButtonEvent* ev)
{
    if (drag_.mode != DRAG_NONE) {
        if (!drag_.explicit_grab)
            end_drag(ev->time, false);
        return true;
    }

    Client* c = find_frame(ev->window);
    if (!c)
        return false;
    if (c->pressed != FP_NONE) {
        FramePart part = theme_.hit(decor_of(c), frame_w(c), frame_h(c),
                                    ev->x, ev->y);
        FramePart pressed = c->pressed;
        c->pressed = FP_NONE;
        paint(c);
        if (part == pressed && pressed == FP_MAXIMIZE) {
            if (c->maximized)
                set_maximized(c, false);
            else if (action_allowed(allowed(c), ACT_MAXIMIZE))
                set_maximized(c, true);
        }
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
    FramePart part = theme_.hit(decor_of(c), frame_w(c), frame_h(c),
                                ev->x, ev->y);
    if (!frame_part_is_border(part) || !action_allowed(allowed(c), ACT_SIZE))
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

bool Wm::on_key_press(XKeyEvent* ev)
{
    KeySym ks = XLookupKeysym(ev, 0);

    if (drag_.mode != DRAG_NONE) {
        int step = (ev->state & ControlMask) ? 1 : 10;
        int dx = 0, dy = 0;
        switch (ks) {
        case XK_Escape:
            end_drag(ev->time, true);
            return true;
        case XK_Return:
        case XK_KP_Enter:
            end_drag(ev->time, false);
            return true;
        case XK_Left: dx = -step; break;
        case XK_Right: dx = step; break;
        case XK_Up: dy = -step; break;
        case XK_Down: dy = step; break;
        default:
            return true;
        }
        /* Moving the pointer drives the drag through MotionNotify. */
        XWarpPointer(dpy_, None, None, 0, 0, 0, 0, dx, dy);
        return true;
    }

    if (ev->window != root_)
        return false;

    unsigned int mods = ev->state & k_binding_mods;
    int i;
    for (i = 0; i < k_nkeys; i++) {
        if (g_key_syms[i] != ks || k_keys[i].mods != mods)
            continue;
        switch (k_keys[i].cmd) {
        case KC_ACTION:
            if (active_ && action_allowed(allowed(active_),
                                          k_keys[i].action))
                request(active_->win, k_keys[i].action);
            break;
        case KC_NEXT:
            cycle(true, ev->time);
            break;
        case KC_PREV:
            cycle(false, ev->time);
            break;
        case KC_MENU:
            if (active_)
                post_menu(active_, 0, ev->time);
            break;
        }
        return true;
    }
    return true;
}

void Wm::on_mapping(XMappingEvent* ev)
{
    if (ev->request != MappingKeyboard && ev->request != MappingModifier)
        return;
    XRefreshKeyboardMapping(ev);
    grab_keys();
}

/* ------------------------------------------------------------------ */
/* Managing clients                                                    */

void Wm::manage(Window w, bool already_mapped)
{
    XWindowAttributes attr;
    if (!XGetWindowAttributes(dpy_, w, &attr) || attr.override_redirect)
        return;

    XSelectInput(dpy_, w, PropertyChangeMask);

    Client* c = new Client;
    c->win = w;
    c->w = attr.width;
    c->h = attr.height;
    c->old_bw = attr.border_width;
    if (!XGetTransientForHint(dpy_, w, &c->transient_for))
        c->transient_for = None;
    c->type = read_window_type(dpy_, atoms_, w);
    c->title = read_title(dpy_, atoms_, w);
    read_decor(c);
    read_size_hints(c);
    read_wm_hints(c);
    read_protocols(c);

    FrameExtents e = extents_of(c);

    /* Fit on screen, frame included. */
    int max_w = screen_w_ - e.width();
    int max_h = screen_h_ - e.height();
    if (c->w > max_w) c->w = max_w;
    if (c->h > max_h) c->h = max_h;
    c->size.apply(&c->w, &c->h);

    XSizeHints hints;
    long supplied = 0;
    bool positioned = false;
    if (XGetWMNormalHints(dpy_, w, &hints, &supplied))
        positioned = (hints.flags & (USPosition | PPosition)) != 0;
    Client* parent = c->transient_for != None ? find_client(c->transient_for)
                                              : 0;
    int fw = c->w + e.width(), fh = c->h + e.height();

    if (already_mapped || positioned) {
        int dx, dy;
        gravity_offset(c->gravity, e, c->old_bw, &dx, &dy);
        c->x = attr.x + dx;
        c->y = attr.y + dy;
    } else if (parent) {
        /* Center over the parent, but keep the title bar on screen. */
        c->x = parent->x + (frame_w(parent) - fw) / 2;
        c->y = parent->y + (frame_h(parent) - fh) / 2;
        if (c->x + fw > screen_w_) c->x = screen_w_ - fw;
        if (c->y + fh > screen_h_) c->y = screen_h_ - fh;
        if (c->x < 0) c->x = 0;
        if (c->y < 0) c->y = 0;
    } else {
        /* Cascade. */
        place_x_ += 24;
        place_y_ += 24;
        if (place_x_ + fw > screen_w_ || place_y_ + fh > screen_h_)
            place_x_ = place_y_ = 24;
        c->x = place_x_;
        c->y = place_y_;
    }
    c->saved = Rect(c->x, c->y, c->w, c->h);

    XSetWindowAttributes sa;
    sa.background_pixel = painter_->pixel(theme_.inactive.bg);
    sa.event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
                    ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                    ExposureMask;
    sa.cursor = cursor_normal_;
    c->frame = XCreateWindow(dpy_, root_, c->x, c->y, fw, fh, 0,
                             CopyFromParent, InputOutput, CopyFromParent,
                             CWBackPixel | CWEventMask | CWCursor, &sa);

    XAddToSaveSet(dpy_, w);
    XSetWindowBorderWidth(dpy_, w, 0);
    if (already_mapped)
        c->ignore_unmap++;  /* reparenting a mapped window unmaps it */
    XReparentWindow(dpy_, w, c->frame, e.left, e.top);
    XResizeWindow(dpy_, w, c->w, c->h);

    clients_.push_back(c);
    mapped_.push_back(c);
    update_grabs(c, false);

    XMapWindow(dpy_, w);
    XMapWindow(dpy_, c->frame);
    set_wm_state(w, NormalState);

    bool fs = false, max = false;
    read_net_wm_state(dpy_, atoms_, w, &fs, &max);
    c->fullscreen = fs;
    c->maximized = max && action_allowed(c->actions, ACT_MAXIMIZE);
    apply_state(c, false);

    if (c->type == WT_DESKTOP)
        lower(c);
    else
        raise(c);
    update_client_lists();

    if (cycles(c) || c->type == WT_SPLASH)
        focus(c, CurrentTime);
}

void Wm::unmanage(Client* c, bool destroyed)
{
    if (drag_.client == c)
        drag_.mode = DRAG_NONE;

    XGrabServer(dpy_);
    if (!destroyed) {
        int dx, dy;
        gravity_offset(c->gravity, extents_of(c), c->old_bw, &dx, &dy);
        XUngrabButton(dpy_, AnyButton, AnyModifier, c->win);
        XReparentWindow(dpy_, c->win, root_, c->x - dx, c->y - dy);
        XSetWindowBorderWidth(dpy_, c->win, c->old_bw);
        XRemoveFromSaveSet(dpy_, c->win);
        XDeleteProperty(dpy_, c->win, atoms_.net_wm_state);
        XDeleteProperty(dpy_, c->win, atoms_.net_frame_extents);
        set_wm_state(c->win, WithdrawnState);
    }
    XDestroyWindow(dpy_, c->frame);
    XUngrabServer(dpy_);

    int i = clients_.find(c);
    if (i >= 0)
        clients_.remove_at(i);
    i = mapped_.find(c);
    if (i >= 0)
        mapped_.remove_at(i);
    update_client_lists();

    if (active_ == c) {
        active_ = 0;
        Client* next = 0;
        for (i = clients_.size() - 1; i >= 0 && !next; i--)
            if (cycles(clients_[i]))
                next = clients_[i];
        focus(next, CurrentTime);
    }
    delete c;
}

/* Decorations and allowed actions from the window type, then restricted
 * by _MOTIF_WM_HINTS. Transients get mwm's transient defaults. */
void Wm::read_decor(Client* c)
{
    FrameDecor d;
    ActionMask a = all_actions();
    const ActionMask no_resize_state = (1u << ACT_MINIMIZE) |
                                       (1u << ACT_MAXIMIZE) |
                                       (1u << ACT_RESTORE);
    switch (c->type) {
    case WT_DESKTOP:
    case WT_DOCK:
    case WT_SPLASH:
    case WT_NOTIFICATION:
        d = FrameDecor::none();
        a = 1u << ACT_CLOSE;
        break;
    case WT_DIALOG:
        d.minimize = d.maximize = false;
        a &= ~no_resize_state;
        break;
    case WT_NORMAL:
        if (c->transient_for != None) {
            d.minimize = d.maximize = false;
            a &= ~no_resize_state;
        }
        break;
    }
    apply_motif_hints(dpy_, atoms_, c->win, &d, &a);
    c->decor = d;
    c->actions = a;
}

void Wm::read_size_hints(Client* c)
{
    XSizeHints h;
    long supplied = 0;
    c->size = SizeConstraints();
    c->gravity = GRAV_NW;
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
    if (h.flags & PWinGravity)
        c->gravity = h.win_gravity;
    int min_w = theme_.min_client_w(c->decor);
    if (c->size.min_w < min_w)
        c->size.min_w = min_w;
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
            if (protos[i] == atoms_.wm_take_focus)
                c->take_focus = true;
            else if (protos[i] == atoms_.wm_delete_window)
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

FrameDecor Wm::decor_of(const Client* c) const
{
    return c->fullscreen ? FrameDecor::none() : c->decor;
}

FrameExtents Wm::extents_of(const Client* c) const
{
    return theme_.extents(decor_of(c));
}

int Wm::frame_w(const Client* c) const
{
    return c->w + extents_of(c).width();
}

int Wm::frame_h(const Client* c) const
{
    return c->h + extents_of(c).height();
}

/* What the window menu, buttons and keys may do right now. */
ActionMask Wm::allowed(const Client* c) const
{
    ActionMask m = c->actions;
    m &= ~(1u << ACT_MINIMIZE);     /* needs the taskbar */
    if (c->maximized || c->fullscreen) {
        m &= ~((1u << ACT_MOVE) | (1u << ACT_SIZE) | (1u << ACT_MAXIMIZE));
        m |= 1u << ACT_RESTORE;
    } else {
        m &= ~(1u << ACT_RESTORE);
    }
    return m;
}

/* Whether Alt+Tab visits this window and it takes focus when mapped. */
bool Wm::cycles(const Client* c) const
{
    return c->type == WT_NORMAL || c->type == WT_DIALOG;
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
    Window w = c ? c->win : None;
    XChangeProperty(dpy_, root_, atoms_.net_active_window, XA_WINDOW, 32,
                    PropModeReplace, (unsigned char*)&w, 1);
    if (!c) {
        XSetInputFocus(dpy_, PointerRoot, RevertToPointerRoot, t);
        return;
    }
    update_grabs(c, true);
    if (c->accepts_input)
        XSetInputFocus(dpy_, c->win, RevertToPointerRoot, t);
    if (c->take_focus)
        send_protocol(c, atoms_.wm_take_focus, t);
    paint(c);
}

/* Raise a window, then its transients above it. Desktop windows stay at
 * the bottom. */
void Wm::raise(Client* c)
{
    if (c->type == WT_DESKTOP)
        return;
    XRaiseWindow(dpy_, c->frame);
    int i = clients_.find(c);
    if (i >= 0) {
        clients_.remove_at(i);
        clients_.push_back(c);
    }

    Vector<Client*> transients;
    for (i = 0; i < clients_.size(); i++)
        if (clients_[i]->transient_for == c->win && clients_[i] != c)
            transients.push_back(clients_[i]);
    for (i = 0; i < transients.size(); i++)
        raise(transients[i]);
    update_client_lists();
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
    /* Desktop windows go back underneath. */
    if (c->type != WT_DESKTOP) {
        Vector<Client*> desktops;
        for (i = 0; i < clients_.size(); i++)
            if (clients_[i]->type == WT_DESKTOP)
                desktops.push_back(clients_[i]);
        for (i = 0; i < desktops.size(); i++)
            lower(desktops[i]);
    }
    update_client_lists();
}

/* mwm-style circulation: forward raises the bottom-most window, backward
 * lowers the top-most. The new top window gets the focus. */
void Wm::cycle(bool forward, Time t)
{
    Client* bottom = 0;
    Client* top = 0;
    int i, n = 0;
    for (i = 0; i < clients_.size(); i++) {
        if (!cycles(clients_[i]))
            continue;
        if (!bottom)
            bottom = clients_[i];
        top = clients_[i];
        n++;
    }
    if (n == 0)
        return;

    if (forward) {
        raise(bottom);
        focus(bottom, t);
        return;
    }
    if (n > 1)
        lower(top);
    for (i = clients_.size() - 1; i >= 0; i--) {
        if (cycles(clients_[i])) {
            focus(clients_[i], t);
            break;
        }
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
    ev.xclient.message_type = atoms_.wm_protocols;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = (long)proto;
    ev.xclient.data.l[1] = (long)t;
    XSendEvent(dpy_, c->win, False, NoEventMask, &ev);
}

void Wm::close(Client* c)
{
    if (c->delete_window)
        send_protocol(c, atoms_.wm_delete_window, CurrentTime);
    else
        XKillClient(dpy_, c->win);
}

/* Post the window menu below the menu button, or at the frame's top left
 * corner if the window has none. */
void Wm::post_menu(Client* c, const XButtonEvent* ev, Time t)
{
    FrameDecor d = decor_of(c);
    Rect r = theme_.menu_rect(d, frame_w(c));
    int x, y;
    if (r.w > 0) {
        x = c->x + r.x;
        y = c->y + r.y + r.h;
    } else {
        FrameExtents e = extents_of(c);
        x = c->x + e.left;
        y = c->y + e.top;
    }
    menu_->post(c->win, x, y, ev, t, allowed(c));
}

void Wm::request(ToplevelId id, WmAction action)
{
    Client* c = find_id(id);
    if (!c)
        return;
    if (!action_allowed(allowed(c), action))
        return;
    int fw = frame_w(c), fh = frame_h(c);

    switch (action) {
    case ACT_RESTORE:
        if (c->fullscreen)
            set_fullscreen(c, false);
        else
            set_maximized(c, false);
        break;
    case ACT_MAXIMIZE:
        set_maximized(c, true);
        break;
    case ACT_MOVE: {
        Rect t = theme_.title_rect(decor_of(c), fw);
        int x = c->x + (t.w > 0 ? t.x + t.w / 2 : fw / 2);
        int y = c->y + (t.w > 0 ? t.y + t.h / 2 : fh / 2);
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
/* Window states                                                       */

void Wm::set_maximized(Client* c, bool on)
{
    if (c->maximized == on)
        return;
    if (!c->maximized && !c->fullscreen)
        c->saved = Rect(c->x, c->y, c->w, c->h);
    c->maximized = on;
    apply_state(c, true);
}

void Wm::set_fullscreen(Client* c, bool on)
{
    if (c->fullscreen == on)
        return;
    if (!c->maximized && !c->fullscreen)
        c->saved = Rect(c->x, c->y, c->w, c->h);
    c->fullscreen = on;
    if (on)
        raise(c);
    apply_state(c, true);
}

/* Lay the window out for its current state and decoration. 'restore'
 * returns a window in the normal state to its saved geometry. */
void Wm::apply_state(Client* c, bool restore)
{
    if (c->fullscreen) {
        set_geometry(c, 0, 0, screen_w_, screen_h_);
    } else if (c->maximized) {
        FrameExtents e = extents_of(c);
        int w = screen_w_ - e.width(), h = screen_h_ - e.height();
        c->size.apply(&w, &h);
        set_geometry(c, 0, 0, w, h);
    } else if (restore) {
        set_geometry(c, c->saved.x, c->saved.y, c->saved.w, c->saved.h);
    } else {
        set_geometry(c, c->x, c->y, c->w, c->h);
    }
    paint(c);
    set_net_state(c);
    set_frame_extents(c);
    send_configure(c);
}

/* ------------------------------------------------------------------ */
/* Geometry, properties, painting                                      */

void Wm::set_geometry(Client* c, int x, int y, int w, int h)
{
    bool resized = w != c->w || h != c->h;
    FrameExtents e = extents_of(c);
    c->x = x;
    c->y = y;
    c->w = w;
    c->h = h;
    XMoveResizeWindow(dpy_, c->frame, x, y, w + e.width(), h + e.height());
    XMoveResizeWindow(dpy_, c->win, e.left, e.top, w, h);
    if (resized)
        paint(c);
}

/* ICCCM 4.1.5: tell the client where it is in root coordinates. */
void Wm::send_configure(Client* c)
{
    FrameExtents e = extents_of(c);
    XConfigureEvent ce;
    memset(&ce, 0, sizeof(ce));
    ce.type = ConfigureNotify;
    ce.event = c->win;
    ce.window = c->win;
    ce.x = c->x + e.left;
    ce.y = c->y + e.top;
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
    XChangeProperty(dpy_, w, atoms_.wm_state, atoms_.wm_state, 32,
                    PropModeReplace, (unsigned char*)data, 2);
}

void Wm::set_net_state(Client* c)
{
    Atom state[3];
    int n = 0;
    if (c->fullscreen)
        state[n++] = atoms_.net_wm_state_fullscreen;
    if (c->maximized) {
        state[n++] = atoms_.net_wm_state_maximized_vert;
        state[n++] = atoms_.net_wm_state_maximized_horz;
    }
    XChangeProperty(dpy_, c->win, atoms_.net_wm_state, XA_ATOM, 32,
                    PropModeReplace, (unsigned char*)state, n);
}

void Wm::set_frame_extents(Client* c)
{
    FrameExtents e = extents_of(c);
    long v[4];
    v[0] = e.left;
    v[1] = e.right;
    v[2] = e.top;
    v[3] = e.bottom;
    XChangeProperty(dpy_, c->win, atoms_.net_frame_extents, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char*)v, 4);
}

void Wm::update_client_lists()
{
    int n = clients_.size(), i;
    Window* list = new Window[n > 0 ? n : 1];
    for (i = 0; i < mapped_.size(); i++)
        list[i] = mapped_[i]->win;
    XChangeProperty(dpy_, root_, atoms_.net_client_list, XA_WINDOW, 32,
                    PropModeReplace, (unsigned char*)list, mapped_.size());
    for (i = 0; i < n; i++)
        list[i] = clients_[i]->win;
    XChangeProperty(dpy_, root_, atoms_.net_client_list_stacking, XA_WINDOW,
                    32, PropModeReplace, (unsigned char*)list, n);
    delete [] list;
}

void Wm::paint(Client* c)
{
    FrameDecor d = decor_of(c);
    if (!d.border && !d.title)
        return;
    int fw = frame_w(c), fh = frame_h(c);
    painter_->begin(c->frame, fw, fh);
    paint_frame(*painter_, theme_, d, fw, fh, c->title.c_str(), c == active_,
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
    /* Keyboard for Escape (cancel), Return (done) and arrow keys. */
    XGrabKeyboard(dpy_, root_, False, GrabModeAsync, GrabModeAsync, t);

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
    set_geometry(c, x, y, w, h);
}

void Wm::end_drag(Time t, bool cancel)
{
    Client* c = drag_.client;
    if (cancel && c)
        set_geometry(c, drag_.orig_x, drag_.orig_y, drag_.orig_w,
                     drag_.orig_h);
    XUngrabKeyboard(dpy_, t);
    if (drag_.explicit_grab)
        XUngrabPointer(dpy_, t);
    drag_.mode = DRAG_NONE;
    drag_.client = 0;
    if (c)
        send_configure(c);
}

}
