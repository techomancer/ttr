#include "x11/wm.h"
#include "x11/xpainter.h"
#include "core/icon.h"
#include "core/launch.h"
#include "core/shortcuts.h"
#include "x11/keys.h"
#include "core/switcher.h"

#include <Xm/Xm.h>
#include <Xm/DrawingA.h>
#include <Xm/GrabShell.h>
#include <Xm/MenuShell.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
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

/* Event types the WM looks at; everything else goes straight to Xt. */
static const int k_wm_events[] = {
    MapRequest, MapNotify, ConfigureRequest, UnmapNotify, DestroyNotify,
    PropertyNotify, ClientMessage, ButtonPress, ButtonRelease,
    MotionNotify, EnterNotify, Expose, KeyPress, KeyRelease, MappingNotify
};

static const unsigned int k_lock_masks[] = {
    0, LockMask, Mod2Mask, LockMask | Mod2Mask
};

/* Modifiers that distinguish bindings; Lock and NumLock are ignored. */
static const unsigned int k_binding_mods =
    ShiftMask | ControlMask | Mod1Mask | Mod4Mask;

/* Two clicks on a title bar within this many ms toggle maximize. */
static const Time k_double_click_ms = 400;

enum KeyCommand {
    KC_ACTION,      /* a WmAction on the active window */
    KC_SWITCH,      /* Alt+Tab: Windows-style or stack, per switchStyle */
    KC_SWITCH_BACK,
    KC_NEXT,        /* circulate the stack */
    KC_PREV,
    KC_MENU,        /* window menu */
    KC_START,       /* start menu */
    KC_DESK_PREV,   /* switch desktop */
    KC_DESK_NEXT,
    KC_TAKE_PREV,   /* move the active window along, and follow it */
    KC_TAKE_NEXT
};

struct KeyBinding {
    const char* keysym;
    unsigned int mods;
    KeyCommand cmd;
    WmAction action;    /* for KC_ACTION */
};

/* mwm's default bindings, plus Alt+Tab and the Windows key. */
static const KeyBinding k_keys[] = {
    { "F3", Mod1Mask, KC_ACTION, ACT_LOWER },
    { "F4", Mod1Mask, KC_ACTION, ACT_CLOSE },
    { "F5", Mod1Mask, KC_ACTION, ACT_RESTORE },
    { "F7", Mod1Mask, KC_ACTION, ACT_MOVE },
    { "F8", Mod1Mask, KC_ACTION, ACT_SIZE },
    { "F9", Mod1Mask, KC_ACTION, ACT_MINIMIZE },
    { "F10", Mod1Mask, KC_ACTION, ACT_MAXIMIZE },
    { "Tab", Mod1Mask, KC_SWITCH, ACT_COUNT },
    { "Tab", Mod1Mask | ShiftMask, KC_SWITCH_BACK, ACT_COUNT },
    { "Escape", Mod1Mask, KC_NEXT, ACT_COUNT },
    { "Escape", Mod1Mask | ShiftMask, KC_PREV, ACT_COUNT },
    { "Escape", ShiftMask, KC_MENU, ACT_COUNT },
    { "space", Mod1Mask, KC_MENU, ACT_COUNT },
    { "Escape", ControlMask, KC_START, ACT_COUNT },
    { "Super_L", 0, KC_START, ACT_COUNT },
    { "Super_R", 0, KC_START, ACT_COUNT },
    { "Left", ControlMask | Mod1Mask, KC_DESK_PREV, ACT_COUNT },
    { "Right", ControlMask | Mod1Mask, KC_DESK_NEXT, ACT_COUNT },
    { "Left", Mod1Mask | ShiftMask, KC_TAKE_PREV, ACT_COUNT },
    { "Right", Mod1Mask | ShiftMask, KC_TAKE_NEXT, ACT_COUNT },
    { "Left", ControlMask | Mod1Mask | ShiftMask, KC_TAKE_PREV, ACT_COUNT },
    { "Right", ControlMask | Mod1Mask | ShiftMask, KC_TAKE_NEXT, ACT_COUNT },
    /* Docking and snapping. CDE never used the Windows key, so these do
     * not collide with its Alt+function key window operations. */
    { "1", Mod4Mask, KC_ACTION, ACT_DOCK_LEFT },
    { "2", Mod4Mask, KC_ACTION, ACT_DOCK_CENTER },
    { "3", Mod4Mask, KC_ACTION, ACT_DOCK_RIGHT },
    { "Left", Mod4Mask, KC_ACTION, ACT_DOCK_LEFT },
    { "Right", Mod4Mask, KC_ACTION, ACT_DOCK_RIGHT },
    { "Up", Mod4Mask, KC_ACTION, ACT_SNAP_TOP },
    { "Down", Mod4Mask, KC_ACTION, ACT_SNAP_BOTTOM }
};

/* Snap zones: how close to a screen edge the pointer must be. */
static const int k_snap_edge = 2;
static const int k_preview_line = 4;

static const ActionMask k_snap_actions =
    (1u << ACT_DOCK_LEFT) | (1u << ACT_DOCK_CENTER) | (1u << ACT_DOCK_RIGHT) |
    (1u << ACT_SNAP_TOP) | (1u << ACT_SNAP_BOTTOM);

/* How long the desktop name stays up after a switch. */
static const unsigned long k_osd_ms = 900;

/* _NET_WM_DESKTOP for "all desktops". */
static const long k_net_all_desktops = 0xFFFFFFFFL;
static const int k_nkeys = (int)(sizeof(k_keys) / sizeof(k_keys[0]));
static KeySym g_key_syms[sizeof(k_keys) / sizeof(k_keys[0])];

/* Motif shells that grab the pointer while up: menus (popup, pulldown,
 * option) and the grab shell of combo box lists. Not every override shell
 * of ours: the taskbar is one and is always mapped. */
static bool is_motif_popup(Widget w)
{
    return XtIsSubclass(w, xmMenuShellWidgetClass) ||
           XtIsSubclass(w, xmGrabShellWidgetClass);
}

static bool is_super_key(KeySym ks)
{
    return ks == XK_Super_L || ks == XK_Super_R;
}

static bool is_alt_key(KeySym ks)
{
    return ks == XK_Alt_L || ks == XK_Alt_R || ks == XK_Meta_L ||
           ks == XK_Meta_R;
}

Client::Client()
    : win(None), frame(None), transient_for(None), x(0), y(0), w(1), h(1),
      old_bw(0), gravity(GRAV_NW), icon(0), type(WT_NORMAL),
      actions(all_actions()), accepts_input(true), take_focus(false),
      delete_window(false), maximized(false), fullscreen(false),
      minimized(false), listed(false), desktop(0), snap(SNAP_NONE),
      ignore_unmap(0),
      pressed(FP_NONE),
      cursor_part(FP_NONE)
{
}

Client::~Client()
{
    delete icon;
}

Wm::Wm()
    : app_(0), toplevel_(0), menu_parent_(0), dpy_(0), screen_(0),
      root_(None), check_win_(None), screen_w_(0), screen_h_(0),
      running_(true), restart_(false), painter_(0), shell_(0), panel_(None),
      cursor_normal_(None), cursor_move_(None), active_(0), place_x_(0),
      place_y_(0), last_click_time_(0), last_click_win_(None),
      super_pending_(false), shell_menu_up_(false), menu_click_time_(0),
      menu_click_win_(None),
      close_after_menu_(None), desktops_(1), current_(0), osd_win_(None),
      osd_timer_(0),
      switching_(false), switch_win_(None), switch_sel_(0)
{
    signal_pipe_[0] = signal_pipe_[1] = -1;
    memset(&drag_, 0, sizeof(drag_));
    drag_.mode = DRAG_NONE;
    g_wm = this;
}

Wm::~Wm()
{
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

static void close_on_exec(int fd)
{
    fcntl(fd, F_SETFD, FD_CLOEXEC);
}

bool Wm::init(int* argc, char** argv)
{
    /* XtOpenApplication removes the options it knows from argv; the
     * settings file goes under the original command line. */
    int argc0 = *argc;
    char** argv0 = new char*[argc0 + 1];
    int k;
    for (k = 0; k <= argc0; k++)
        argv0[k] = argv[k];

    toplevel_ = XtOpenApplication(&app_, "Ttr", config_options(),
                                  (Cardinal)config_option_count(), argc, argv,
                                  config_fallback_resources(),
                                  applicationShellWidgetClass, 0, 0);
    dpy_ = XtDisplay(toplevel_);
    screen_ = DefaultScreen(dpy_);
    root_ = RootWindow(dpy_, screen_);
    screen_w_ = DisplayWidth(dpy_, screen_);
    screen_h_ = DisplayHeight(dpy_, screen_);
    work_ = Rect(0, 0, screen_w_, screen_h_);
    close_on_exec(ConnectionNumber(dpy_));
    merge_settings_file(toplevel_, argc0, argv0);
    delete [] argv0;
    read_config(toplevel_, &config_);

    /* Programs we start must reach this server even if we were started
     * with -display. putenv keeps the string, so it must stay alive. */
    static char display_env[300];
    sprintf(display_env, "DISPLAY=%.280s", DisplayString(dpy_));
    putenv(display_env);

    /* The application shell only parents our Motif popups; it is never
     * shown. Override-redirect so it can never reach our own MapRequest. */
    Arg args[4];
    int n = 0;
    XtSetArg(args[n], XmNmappedWhenManaged, False); n++;
    XtSetArg(args[n], XmNoverrideRedirect, True); n++;
    XtSetArg(args[n], XmNwidth, 1); n++;
    XtSetArg(args[n], XmNheight, 1); n++;
    XtSetValues(toplevel_, args, n);
    menu_parent_ = XmCreateDrawingArea(toplevel_, (char*)"shell", 0, 0);
    XtManageChild(menu_parent_);
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
    const char* fonts[6];
    fonts[0] = config_.title_font.c_str();
    for (k = 0; k < 5; k++)
        fonts[k + 1] = k_title_fonts[k];
    if (!painter_->load_font(fonts)) {
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

    XSetWindowAttributes sa;
    sa.override_redirect = True;
    sa.save_under = True;
    sa.event_mask = ExposureMask;
    switch_win_ = XCreateWindow(dpy_, root_, 0, 0, 1, 1, 0, CopyFromParent,
                                InputOutput, CopyFromParent,
                                CWOverrideRedirect | CWSaveUnder |
                                CWEventMask, &sa);
    osd_win_ = XCreateWindow(dpy_, root_, 0, 0, 1, 1, 0, CopyFromParent,
                             InputOutput, CopyFromParent,
                             CWOverrideRedirect | CWSaveUnder | CWEventMask,
                             &sa);
    XSetWindowAttributes pa;
    pa.override_redirect = True;
    pa.background_pixel = painter_->pixel(theme_.active.bg);
    for (i = 0; i < 4; i++)
        snap_preview_[i] = XCreateWindow(dpy_, root_, 0, 0, 1, 1, 0,
                                         CopyFromParent, InputOutput,
                                         CopyFromParent,
                                         CWOverrideRedirect | CWBackPixel,
                                         &pa);

    /* Keep the desktop we were on across a restart. */
    desktops_ = config_.desktops;
    current_ = (int)read_cardinal(dpy_, root_, atoms_.net_current_desktop, 0);
    if (current_ < 0 || current_ >= desktops_)
        current_ = 0;

    if (pipe(signal_pipe_) == 0) {
        close_on_exec(signal_pipe_[0]);
        close_on_exec(signal_pipe_[1]);
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
    load_user_keys();
    grab_keys();
    return true;
}

void Wm::start()
{
    if (shell_)
        shell_->desktop_changed(current_, desktops_);
    adopt_existing();
}

int Wm::run()
{
    while (running_)
        XtAppProcessEvent(app_, XtIMAll);
    if (shell_)
        shell_->shutdown();
    restore_all();
    XSync(dpy_, False);
    return 0;
}

void Wm::quit()
{
    running_ = false;
}

void Wm::restart()
{
    restart_ = true;
    running_ = false;
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
        atoms_.net_workarea, atoms_.net_wm_icon,
        atoms_.net_wm_state, atoms_.net_wm_state_fullscreen,
        atoms_.net_wm_state_maximized_vert,
        atoms_.net_wm_state_maximized_horz, atoms_.net_wm_state_hidden,
        atoms_.net_wm_window_type, atoms_.net_number_of_desktops,
        atoms_.net_current_desktop, atoms_.net_wm_desktop,
        atoms_.net_desktop_names, atoms_.net_desktop_geometry,
        atoms_.net_desktop_viewport,
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
    publish_desktops();
}

void Wm::load_user_keys()
{
    ShortcutList list;
    Str path = ShortcutList::default_path();
    list.load(path.c_str());
    user_keys_.clear();
    int i;
    for (i = 0; i < list.size(); i++) {
        const Shortcut* s = list.at(i);
        UserKey k;
        if (s->key.empty() || !parse_key_spec(s->key.c_str(), &k.sym, &k.mods))
            continue;
        k.command = s->command;
        user_keys_.push_back(k);
    }
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
    for (i = 0; i < user_keys_.size(); i++) {
        KeyCode kc = XKeysymToKeycode(dpy_, user_keys_[i].sym);
        if (!kc)
            continue;
        for (j = 0; j < 4; j++)
            XGrabKey(dpy_, kc, user_keys_[i].mods | k_lock_masks[j], root_,
                     False, GrabModeAsync, GrabModeAsync);
    }
}

long Wm::get_wm_state(Window w)
{
    Atom type;
    int format;
    unsigned long n = 0, after;
    unsigned char* data = 0;
    long state = -1;
    if (XGetWindowProperty(dpy_, w, atoms_.wm_state, 0, 2, False,
                           atoms_.wm_state, &type, &format, &n, &after,
                           &data) == Success && data) {
        if (format == 32 && n >= 1)
            state = ((long*)data)[0];
        XFree(data);
    }
    return state;
}

void Wm::adopt_existing()
{
    Window root_ret, parent_ret;
    Window* children = 0;
    unsigned int count = 0, i;

    XGrabServer(dpy_);
    if (XQueryTree(dpy_, root_, &root_ret, &parent_ret, &children, &count)) {
        for (i = 0; i < count; i++) {
            Window w = children[i];
            XWindowAttributes attr;
            if (!XGetWindowAttributes(dpy_, w, &attr))
                continue;
            if (attr.override_redirect || w == XtWindow(toplevel_) ||
                w == check_win_ || w == switch_win_ || w == osd_win_ ||
                w == snap_preview_[0] || w == snap_preview_[1] ||
                w == snap_preview_[2] || w == snap_preview_[3])
                continue;
            if (attr.map_state == IsViewable)
                manage(w, MANAGE_MAPPED);
            else if (get_wm_state(w) == IconicState)
                manage(w, MANAGE_ICONIC);   /* minimized before a restart */
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
        if (c->minimized)
            set_wm_state(c->win, IconicState);  /* adopted again on restart */
        else
            XMapWindow(dpy_, c->win);
        XDestroyWindow(dpy_, c->frame);
        delete c;
    }
    clients_.clear();
    mapped_.clear();
    mru_.clear();
    active_ = 0;
    XSetInputFocus(dpy_, PointerRoot, RevertToPointerRoot, CurrentTime);

    XDeleteProperty(dpy_, root_, atoms_.net_supporting_wm_check);
    XDeleteProperty(dpy_, root_, atoms_.net_supported);
    XDeleteProperty(dpy_, root_, atoms_.net_active_window);
    XDeleteProperty(dpy_, root_, atoms_.net_client_list);
    XDeleteProperty(dpy_, root_, atoms_.net_client_list_stacking);
    XDeleteProperty(dpy_, root_, atoms_.net_workarea);
    if (!restart_) {
        /* A restart keeps the current desktop; windows keep their
         * _NET_WM_DESKTOP either way, for whoever manages them next. */
        XDeleteProperty(dpy_, root_, atoms_.net_current_desktop);
        XDeleteProperty(dpy_, root_, atoms_.net_number_of_desktops);
        XDeleteProperty(dpy_, root_, atoms_.net_desktop_names);
    }
    XDestroyWindow(dpy_, check_win_);
    XDestroyWindow(dpy_, switch_win_);
    XDestroyWindow(dpy_, osd_win_);
    for (i = 0; i < 4; i++)
        XDestroyWindow(dpy_, snap_preview_[i]);
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
    /* While one of the shell's Motif menus is up, pointer and key events
     * belong to it. Xt passes clicks on our frames (not widgets) to the
     * menu's grab, which pops it down; taking them ourselves would leave
     * Motif's grab in a broken state. */
    if (menu_up()) {
        switch (ev->type) {
        case ButtonPress:
            menu_double_click(&ev->xbutton);
            return false;
        case ButtonRelease:
        case MotionNotify:
        case EnterNotify:
        case KeyPress:
        case KeyRelease:
            return false;
        }
    }

    switch (ev->type) {
    case MapRequest:
        on_map_request(&ev->xmaprequest);
        return true;
    case MapNotify:
        return on_map_notify(&ev->xmap);
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
    case EnterNotify:
        return on_enter(&ev->xcrossing);
    case Expose:
        return on_expose(&ev->xexpose);
    case KeyPress:
        return on_key_press(&ev->xkey);
    case KeyRelease:
        return on_key_release(&ev->xkey);
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
        set_minimized(c, false);
        activate(c, CurrentTime);
        return;
    }
    manage(ev->window, MANAGE_NEW);
}

/* Our own top-level windows (the shell's Motif dialogs) are not
 * redirected to us, because the X server only redirects other clients'
 * requests. Pick them up when they appear. */
bool Wm::on_map_notify(XMapEvent* ev)
{
    if (ev->event != root_)
        return false;
    if (ev->override_redirect) {
        /* One of our own Motif popups (menu, option menu, combo box list):
         * it grabs the pointer, so keep out of its way while it is up. */
        Widget w = XtWindowToWidget(dpy_, ev->window);
        if (w && is_motif_popup(w) && own_popups_.find(ev->window) < 0) {
            own_popups_.push_back(ev->window);
            popups_changed();
        }
        return false;
    }
    Window w = ev->window;
    if (find_client(w) || find_frame(w) || w == check_win_ ||
        w == switch_win_ || w == osd_win_ || w == panel_ ||
        w == XtWindow(toplevel_))
        return false;
    manage(w, MANAGE_OWN);
    return false;   /* Xt tracks mapping of its own shells */
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
    if ((ev->value_mask & CWStackMode) && ev->detail == Above &&
        !c->minimized)
        raise(c);
    send_configure(c);
}

bool Wm::on_unmap(XUnmapEvent* ev)
{
    int p = own_popups_.find(ev->window);
    if (p >= 0 && ev->event == root_) {
        own_popups_.remove_at(p);
        popups_changed();
        return false;
    }
    /* Our own shell windows also report their unmapping to themselves;
     * that copy belongs to Xt. We act on the one reported to the frame
     * (or root), and on the synthetic ICCCM withdrawal sent to root. */
    if (ev->event == ev->window && !ev->send_event)
        return false;
    Client* c = find_client(ev->window);
    if (!c)
        return false;
    if (c->ignore_unmap > 0 && !ev->send_event) {
        c->ignore_unmap--;
        return true;
    }
    unmanage(c, false);
    return true;
}

bool Wm::on_destroy(XDestroyWindowEvent* ev)
{
    if (ev->event == ev->window)
        return false;   /* see on_unmap */
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
        return false;   /* the root's go on to Xt: the shell watches some */
    if (ev->atom == XA_WM_NAME || ev->atom == atoms_.net_wm_name) {
        c->title = read_title(dpy_, atoms_, c->win);
        paint(c);
        notify(c);
        if (switching_)
            paint_switch();
    } else if (ev->atom == XA_WM_NORMAL_HINTS) {
        read_size_hints(c);
    } else if (ev->atom == XA_WM_HINTS) {
        read_wm_hints(c);
        if (!c->icon) {     /* an icon pixmap may have been added */
            c->icon = read_icon(dpy_, atoms_, c->win);
            if (c->icon)
                notify(c);
        }
    } else if (ev->atom == atoms_.net_wm_icon) {
        delete c->icon;
        c->icon = read_icon(dpy_, atoms_, c->win);
        notify(c);
        if (switching_)
            paint_switch();
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
    if (ev->window == root_) {
        if (ev->message_type == atoms_.net_current_desktop) {
            switch_desktop((int)ev->data.l[0]);
            return true;
        }
        return false;
    }

    Client* c = find_client(ev->window);
    if (!c)
        return false;

    if (ev->message_type == atoms_.net_active_window) {
        activate(c, ev->data.l[1] ? (Time)ev->data.l[1] : CurrentTime);
    } else if (ev->message_type == atoms_.net_wm_desktop) {
        long d = ev->data.l[0];
        set_desktop(c, (d & 0xFFFFFFFFL) == k_net_all_desktops ? ALL_DESKTOPS
                                                               : (int)d);
    } else if (ev->message_type == atoms_.net_close_window) {
        if (action_allowed(c->actions, ACT_CLOSE))
            close(c);
    } else if (ev->message_type == atoms_.wm_change_state) {
        if (ev->data.l[0] == IconicState &&
            action_allowed(allowed(c), ACT_MINIMIZE))
            set_minimized(c, true);
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
    return true;
}

bool Wm::on_button_press(XButtonEvent* ev)
{
    if (drag_.mode != DRAG_NONE) {
        if (drag_.explicit_grab)
            end_drag(ev->time, false);
        return true;
    }
    if (switching_)
        return true;

    Client* c = find_frame(ev->window);
    if (c) {
        FrameDecor d = decor_of(c);
        FramePart part = theme_.hit(d, frame_w(c), frame_h(c), ev->x, ev->y);
        ActionMask ok = allowed(c);
        raise(c);
        focus(c, ev->time);

        /* Menus are posted on release: a Motif menu posted while the
         * button is still down closes again when it is released. */
        if (ev->button == Button3 && part == FP_TITLE) {
            c->pressed = FP_TITLE;
            return true;
        }
        if (ev->button != Button1)
            return true;

        if (part == FP_MENU) {
            c->pressed = FP_MENU;
            menu_click_win_ = c->win;
            menu_click_time_ = ev->time;
            paint(c);
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
        /* Passive grab: click to focus (and raise), Alt+drag to move. */
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
        if (part == pressed && pressed == FP_MENU) {
            post_menu_at_button(c, ev->time);
        } else if (part == pressed && pressed == FP_TITLE &&
                   ev->button == Button3) {
            post_menu(c, ev->x_root, ev->y_root, ev->time);
        } else if (part == pressed && pressed == FP_MAXIMIZE) {
            if (c->maximized)
                set_maximized(c, false);
            else if (action_allowed(allowed(c), ACT_MAXIMIZE))
                set_maximized(c, true);
        } else if (part == pressed && pressed == FP_MINIMIZE &&
                   action_allowed(allowed(c), ACT_MINIMIZE)) {
            set_minimized(c, true);
        }
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

/* focusPolicy: pointer. Focus follows the mouse into a window and stays
 * there when it moves onto the root window. */
bool Wm::on_enter(XCrossingEvent* ev)
{
    Client* c = find_frame(ev->window);
    if (!c)
        return false;
    if (config_.focus == Config::FOCUS_POINTER && ev->mode == NotifyNormal &&
        ev->detail != NotifyInferior && c != active_ && !switching_ &&
        drag_.mode == DRAG_NONE && cycles(c))
        focus(c, ev->time);
    return true;
}

bool Wm::on_expose(XExposeEvent* ev)
{
    if (ev->window == switch_win_) {
        if (ev->count == 0)
            paint_switch();
        return true;
    }
    if (ev->window == osd_win_) {
        if (ev->count == 0)
            paint_osd();
        return true;
    }
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

    if (switching_) {
        if (ks == XK_Tab)
            step_switch((ev->state & ShiftMask) ? -1 : 1);
        else if (ks == XK_Escape)
            end_switch(false, ev->time);
        else if (ks == XK_Return || ks == XK_KP_Enter)
            end_switch(true, ev->time);
        return true;
    }

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

    /* Any key pressed with the Windows key held makes it a modifier. */
    super_pending_ = false;

    unsigned int mods = ev->state & k_binding_mods;
    bool windows = config_.switch_style == Config::SWITCH_WINDOWS;
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
        case KC_SWITCH:
            if (windows)
                begin_switch(true, ev->time);
            else
                cycle(true, ev->time);
            break;
        case KC_SWITCH_BACK:
            if (windows)
                begin_switch(false, ev->time);
            else
                cycle(false, ev->time);
            break;
        case KC_NEXT:
            cycle(true, ev->time);
            break;
        case KC_PREV:
            cycle(false, ev->time);
            break;
        case KC_MENU:
            if (active_)
                post_menu_at_button(active_, ev->time);
            break;
        case KC_START:
            if (is_super_key(ks))
                super_pending_ = true;      /* opens on release */
            else if (shell_)
                shell_->show_start_menu(ev->time);
            break;
        case KC_DESK_PREV:
            switch_desktop(current_ - 1);
            break;
        case KC_DESK_NEXT:
            switch_desktop(current_ + 1);
            break;
        case KC_TAKE_PREV:
        case KC_TAKE_NEXT: {
            int d = current_ + (k_keys[i].cmd == KC_TAKE_NEXT ? 1 : -1);
            if (active_ && cycles(active_) && d >= 0 && d < desktops_) {
                Client* c = active_;
                set_desktop(c, d);
                switch_desktop(d);
                activate(c, ev->time);
            }
            break;
        }
        }
        return true;
    }

    /* Shortcut keys; built-in bindings above take precedence. Keysyms
     * are compared by their unshifted form, so Shift+letter matches. */
    for (i = 0; i < user_keys_.size(); i++) {
        if (user_keys_[i].sym == ks && user_keys_[i].mods == mods) {
            spawn(user_keys_[i].command.c_str());
            return true;
        }
    }
    return true;
}

bool Wm::on_key_release(XKeyEvent* ev)
{
    KeySym ks = XLookupKeysym(ev, 0);
    if (super_pending_ && is_super_key(ks)) {
        super_pending_ = false;
        if (shell_)
            shell_->show_start_menu(ev->time);
        return true;
    }
    if (!switching_)
        return false;
    if (is_alt_key(ks))
        end_switch(true, ev->time);
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

void Wm::manage(Window w, ManageMode mode)
{
    XWindowAttributes attr;
    if (!XGetWindowAttributes(dpy_, w, &attr) || attr.override_redirect)
        return;

    /* Event masks are per client; for our own (shell) windows this one is
     * Xt's, so add to it rather than replace it. */
    XSelectInput(dpy_, w, attr.your_event_mask | PropertyChangeMask);

    Client* c = new Client;
    c->win = w;
    c->w = attr.width;
    c->h = attr.height;
    c->old_bw = attr.border_width;
    if (!XGetTransientForHint(dpy_, w, &c->transient_for))
        c->transient_for = None;
    c->type = read_window_type(dpy_, atoms_, w);
    c->title = read_title(dpy_, atoms_, w);
    c->icon = read_icon(dpy_, atoms_, w);
    read_decor(c);
    read_size_hints(c);
    read_wm_hints(c);
    read_protocols(c);

    FrameExtents e = extents_of(c);

    /* Fit in the work area, frame included. */
    int max_w = work_.w - e.width();
    int max_h = work_.h - e.height();
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

    /* Our own dialogs position themselves relative to the hidden 1x1
     * shell; place them like new windows instead. */
    if (mode == MANAGE_OWN)
        positioned = false;
    if (mode == MANAGE_MAPPED || mode == MANAGE_ICONIC || positioned) {
        int dx, dy;
        gravity_offset(c->gravity, e, c->old_bw, &dx, &dy);
        c->x = attr.x + dx;
        c->y = attr.y + dy;
    } else if (parent || c->transient_for != None) {
        /* Center over the parent (or the screen, for dialogs of windows
         * we do not manage), keeping the title bar on screen. */
        if (parent) {
            c->x = parent->x + (frame_w(parent) - fw) / 2;
            c->y = parent->y + (frame_h(parent) - fh) / 2;
        } else {
            c->x = work_.x + (work_.w - fw) / 2;
            c->y = work_.y + (work_.h - fh) / 2;
        }
        if (c->x + fw > work_.x + work_.w) c->x = work_.x + work_.w - fw;
        if (c->y + fh > work_.y + work_.h) c->y = work_.y + work_.h - fh;
        if (c->x < work_.x) c->x = work_.x;
        if (c->y < work_.y) c->y = work_.y;
    } else {
        /* Cascade. */
        place_x_ += 24;
        place_y_ += 24;
        if (place_x_ + fw > work_.x + work_.w ||
            place_y_ + fh > work_.y + work_.h)
            place_x_ = place_y_ = 24;
        c->x = work_.x + place_x_;
        c->y = work_.y + place_y_;
    }
    c->saved = Rect(c->x, c->y, c->w, c->h);

    XSetWindowAttributes sa;
    sa.background_pixel = painter_->pixel(theme_.inactive.bg);
    sa.event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
                    ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                    EnterWindowMask | ExposureMask;
    sa.cursor = cursor_normal_;
    c->frame = XCreateWindow(dpy_, root_, c->x, c->y, fw, fh, 0,
                             CopyFromParent, InputOutput, CopyFromParent,
                             CWBackPixel | CWEventMask | CWCursor, &sa);

    XAddToSaveSet(dpy_, w);
    XSetWindowBorderWidth(dpy_, w, 0);
    if (mode == MANAGE_MAPPED || mode == MANAGE_OWN)
        c->ignore_unmap++;  /* reparenting a mapped window unmaps it */
    XReparentWindow(dpy_, w, c->frame, e.left, e.top);
    XResizeWindow(dpy_, w, c->w, c->h);

    /* Desktop: kept from before a restart, else the parent's (for
     * transients) or the current one. Panels and the like are on all. */
    long d = read_cardinal(dpy_, w, atoms_.net_wm_desktop, -2);
    if (!cycles(c))
        c->desktop = ALL_DESKTOPS;
    else if ((d & 0xFFFFFFFFL) == k_net_all_desktops)
        c->desktop = ALL_DESKTOPS;
    else if (d >= 0)
        c->desktop = d < desktops_ ? (int)d : desktops_ - 1;
    else if (parent)
        c->desktop = parent->desktop;
    else
        c->desktop = current_;
    long nd = c->desktop == ALL_DESKTOPS ? k_net_all_desktops : c->desktop;
    XChangeProperty(dpy_, w, atoms_.net_wm_desktop, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char*)&nd, 1);

    clients_.push_back(c);
    mapped_.push_back(c);
    mru_.push_back(c);
    update_grabs(c, false);

    /* Start minimized if asked (WM_HINTS) or if it was minimized when the
     * previous window manager exited. */
    bool iconic = mode == MANAGE_ICONIC;
    if (mode == MANAGE_NEW) {
        XWMHints* wh = XGetWMHints(dpy_, w);
        if (wh) {
            iconic = (wh->flags & StateHint) &&
                     wh->initial_state == IconicState;
            XFree(wh);
        }
    }
    if (iconic && !on_taskbar(c))
        iconic = false;     /* nowhere to restore it from */

    if (iconic) {
        c->minimized = true;
        set_wm_state(w, IconicState);
    } else {
        XMapWindow(dpy_, w);
        if (on_current(c))
            XMapWindow(dpy_, c->frame);
        set_wm_state(w, NormalState);
    }

    bool fs = false, max = false;
    read_net_wm_state(dpy_, atoms_, w, &fs, &max);
    c->fullscreen = fs;
    c->maximized = max && action_allowed(c->actions, ACT_MAXIMIZE);
    apply_state(c, false);

    if (c->type == WT_DESKTOP)
        lower(c);
    else if (!iconic)
        raise(c);
    update_client_lists();

    notify(c);
    if (!iconic && on_current(c) && (cycles(c) || c->type == WT_SPLASH))
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
    i = mru_.find(c);
    if (i >= 0)
        mru_.remove_at(i);
    update_client_lists();
    if (c->listed && shell_)
        shell_->task_removed(c->win);

    if (switching_) {
        i = switch_list_.find(c);
        if (i >= 0) {
            switch_list_.remove_at(i);
            if (switch_list_.empty())
                end_switch(false, CurrentTime);
            else {
                if (switch_sel_ >= switch_list_.size())
                    switch_sel_ = 0;
                paint_switch();
            }
        }
    }

    if (active_ == c) {
        active_ = 0;
        focus(next_focus(c), CurrentTime);
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
    if (!shell_ || !on_taskbar(c))
        m &= ~(1u << ACT_MINIMIZE);     /* nowhere to restore it from */
    if (c->maximized || c->fullscreen) {
        m &= ~((1u << ACT_MOVE) | (1u << ACT_SIZE) | (1u << ACT_MAXIMIZE));
        m |= 1u << ACT_RESTORE;
    } else if (c->snap != SNAP_NONE) {
        m |= 1u << ACT_RESTORE;     /* moving or sizing it undocks it */
    } else {
        m &= ~(1u << ACT_RESTORE);
    }
    /* Docking places and sizes the window. */
    if (!action_allowed(c->actions, ACT_MOVE) ||
        !action_allowed(c->actions, ACT_SIZE) || c->fullscreen)
        m &= ~k_snap_actions;
    return m;
}

/* Whether Alt+Tab visits this window and it takes focus when mapped. */
bool Wm::cycles(const Client* c) const
{
    return c->type == WT_NORMAL || c->type == WT_DIALOG;
}

/* Top-level application windows get taskbar buttons; their transients
 * share the owner's. */
bool Wm::on_taskbar(const Client* c) const
{
    return cycles(c) && c->transient_for == None;
}

/* The window whose taskbar button stands for c. */
Client* Wm::owner_of(Client* c)
{
    int depth;
    for (depth = 0; c && !c->listed && c->transient_for != None && depth < 8;
         depth++)
        c = find_client(c->transient_for);
    return c && c->listed ? c : 0;
}

/* Most recently focused window that can take focus now. */
Client* Wm::next_focus(Client* except)
{
    int i;
    for (i = 0; i < mru_.size(); i++) {
        Client* c = mru_[i];
        if (c != except && !c->minimized && cycles(c) && on_current(c))
            return c;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Focus, stacking, actions                                            */

void Wm::focus(Client* c, Time t)
{
    if (c && c->minimized)
        return;
    /* Toolkits (Motif among them) ignore WM_TAKE_FOCUS without a real
     * timestamp, so use the last one the server sent us. */
    if (t == CurrentTime)
        t = XtLastTimestampProcessed(dpy_);
    Client* old = active_;
    active_ = c;
    if (old && old != c) {
        update_grabs(old, false);
        paint(old);
    }
    Window w = c ? c->win : None;
    XChangeProperty(dpy_, root_, atoms_.net_active_window, XA_WINDOW, 32,
                    PropModeReplace, (unsigned char*)&w, 1);
    if (shell_) {
        Client* owner = c ? owner_of(c) : 0;
        shell_->active_changed(owner ? owner->win : 0);
    }
    if (!c) {
        XSetInputFocus(dpy_, PointerRoot, RevertToPointerRoot, t);
        return;
    }

    int i = mru_.find(c);
    if (i > 0) {
        mru_.remove_at(i);
        mru_.push_back(c);
        int j;
        for (j = mru_.size() - 1; j > 0; j--)
            mru_[j] = mru_[j - 1];
        mru_[0] = c;
    }

    update_grabs(c, true);
    if (c->accepts_input)
        XSetInputFocus(dpy_, c->win, RevertToPointerRoot, t);
    if (c->take_focus)
        send_protocol(c, atoms_.wm_take_focus, t);
    paint(c);
}

void Wm::activate(Client* c, Time t)
{
    if (!on_current(c))
        switch_desktop(c->desktop);
    set_minimized(c, false);
    raise(c);
    focus(c, t);
}

/* Raise a window, then its transients above it. Desktop windows stay at
 * the bottom; the panel stays on top unless a fullscreen window covers
 * it. */
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

    if (panel_ != None && !c->fullscreen)
        XRaiseWindow(dpy_, panel_);
    /* Transient popups of our own stay above everything while shown. */
    if (osd_timer_)
        XRaiseWindow(dpy_, osd_win_);
    if (switching_)
        XRaiseWindow(dpy_, switch_win_);
    for (i = 0; i < overlays_.size(); i++)
        XRaiseWindow(dpy_, overlays_[i]);
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
        if (!cycles(clients_[i]) || clients_[i]->minimized ||
            !on_current(clients_[i]))
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
        if (cycles(clients_[i]) && !clients_[i]->minimized &&
            on_current(clients_[i])) {
            focus(clients_[i], t);
            break;
        }
    }
}

/* With click-to-focus, unfocused windows get a synchronous grab on every
 * button so the first click focuses them (and is then replayed). With
 * pointer focus every window keeps it, so a click still raises. All
 * windows get Alt+Button1 for moving. Lock and NumLock variants are
 * grabbed too. */
void Wm::update_grabs(Client* c, bool focused)
{
    XUngrabButton(dpy_, AnyButton, AnyModifier, c->win);
    if (!focused || config_.focus == Config::FOCUS_POINTER)
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

void Wm::post_menu(Client* c, int x, int y, Time t)
{
    if (shell_)
        shell_->show_window_menu(c->win, x, y, t, allowed(c), c->desktop);
}

/* Post the window menu below the menu button, or at the frame's top left
 * corner if the window has none. */
void Wm::post_menu_at_button(Client* c, Time t)
{
    FrameDecor d = decor_of(c);
    Rect r = theme_.menu_rect(d, frame_w(c));
    if (r.w > 0) {
        post_menu(c, c->x + r.x, c->y + r.y + r.h, t);
    } else {
        FrameExtents e = extents_of(c);
        post_menu(c, c->x + e.left, c->y + e.top, t);
    }
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
        else if (c->maximized)
            set_maximized(c, false);
        else
            set_snap(c, SNAP_NONE);
        break;
    case ACT_DOCK_LEFT:
    case ACT_DOCK_CENTER:
    case ACT_DOCK_RIGHT:
    case ACT_SNAP_TOP:
    case ACT_SNAP_BOTTOM: {
        int snap = action == ACT_DOCK_LEFT ? SNAP_LEFT
                 : action == ACT_DOCK_CENTER ? SNAP_CENTER
                 : action == ACT_DOCK_RIGHT ? SNAP_RIGHT
                 : action == ACT_SNAP_TOP ? SNAP_TOP : SNAP_BOTTOM;
        /* The same dock again undocks, like a toggle. */
        set_snap(c, c->snap == snap && !c->maximized ? (int)SNAP_NONE : snap);
        break;
    }
    case ACT_MAXIMIZE:
        set_maximized(c, true);
        break;
    case ACT_MINIMIZE:
        set_minimized(c, true);
        break;
    case ACT_ACTIVATE:
        activate(c, CurrentTime);
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
    case ACT_COUNT:
        break;
    }
}

void Wm::set_panel(unsigned long window, const Rect& r)
{
    panel_ = (Window)window;
    /* Only a panel along the top or bottom edge reserves space. */
    if (r.y + r.h >= screen_h_)
        work_ = Rect(0, 0, screen_w_, r.y);
    else if (r.y <= 0)
        work_ = Rect(0, r.h, screen_w_, screen_h_ - r.h);
    update_workarea();
    XRaiseWindow(dpy_, panel_);

    int i;
    for (i = 0; i < clients_.size(); i++)
        if (clients_[i]->maximized)
            apply_state(clients_[i], false);
}

/* ------------------------------------------------------------------ */
/* Window states                                                       */

void Wm::set_maximized(Client* c, bool on)
{
    if (c->maximized == on)
        return;
    if (!c->maximized && !c->fullscreen && c->snap == SNAP_NONE)
        c->saved = Rect(c->x, c->y, c->w, c->h);
    c->maximized = on;
    c->snap = SNAP_NONE;    /* maximizing ends docking */
    apply_state(c, true);
}

void Wm::set_fullscreen(Client* c, bool on)
{
    if (c->fullscreen == on)
        return;
    if (!c->maximized && !c->fullscreen && c->snap == SNAP_NONE)
        c->saved = Rect(c->x, c->y, c->w, c->h);
    c->fullscreen = on;
    if (on)
        raise(c);
    else if (panel_ != None)
        XRaiseWindow(dpy_, panel_);
    apply_state(c, true);
}

/* Frame rectangle for a dock or snap position: columns are full height
 * and dockWidth (a third or half) wide; top and bottom are full width and
 * the same fraction of the height. */
Rect Wm::snap_rect(int snap) const
{
    int div = config_.dock_div;
    int w = work_.w / div, h = work_.h / div;
    switch (snap) {
    case SNAP_LEFT:
        return Rect(work_.x, work_.y, w, work_.h);
    case SNAP_CENTER:
        return Rect(work_.x + (work_.w - w) / 2, work_.y, w, work_.h);
    case SNAP_RIGHT:
        return Rect(work_.x + work_.w - w, work_.y, w, work_.h);
    case SNAP_TOP:
        return Rect(work_.x, work_.y, work_.w, h);
    case SNAP_BOTTOM:
        return Rect(work_.x, work_.y + work_.h - h, work_.w, h);
    }
    return Rect();
}

/* Dock or snap a window (SNAP_NONE restores it). Like maximize, the
 * normal geometry is kept to come back to. */
void Wm::set_snap(Client* c, int snap)
{
    if (c->fullscreen)
        return;
    if (snap != SNAP_NONE &&
        !action_allowed(allowed(c), (WmAction)(ACT_DOCK_LEFT + snap -
                                                SNAP_LEFT)))
        return;
    if (!c->maximized && c->snap == SNAP_NONE)
        c->saved = Rect(c->x, c->y, c->w, c->h);
    bool restore = snap == SNAP_NONE && (c->snap != SNAP_NONE || c->maximized);
    c->maximized = false;
    c->snap = snap;
    if (snap != SNAP_NONE || restore)
        apply_state(c, true);
}

/* Which snap zone (screen edge) the pointer is in while moving. */
int Wm::snap_zone(int x_root, int y_root) const
{
    if (x_root <= k_snap_edge)
        return SNAP_LEFT;
    if (x_root >= screen_w_ - 1 - k_snap_edge)
        return SNAP_RIGHT;
    if (y_root <= k_snap_edge)
        return SNAP_TOP;
    if (y_root >= screen_h_ - 1 - k_snap_edge)
        return SNAP_BOTTOM;
    return SNAP_NONE;
}

/* Four thin windows outline the target, so nothing is drawn on the
 * windows underneath. */
void Wm::show_snap_preview(int snap)
{
    int i;
    if (snap == SNAP_NONE) {
        for (i = 0; i < 4; i++)
            XUnmapWindow(dpy_, snap_preview_[i]);
        return;
    }
    Rect r = snap_rect(snap);
    int t = k_preview_line;
    XMoveResizeWindow(dpy_, snap_preview_[0], r.x, r.y, r.w, t);
    XMoveResizeWindow(dpy_, snap_preview_[1], r.x, r.y + r.h - t, r.w, t);
    XMoveResizeWindow(dpy_, snap_preview_[2], r.x, r.y, t, r.h);
    XMoveResizeWindow(dpy_, snap_preview_[3], r.x + r.w - t, r.y, t, r.h);
    for (i = 0; i < 4; i++)
        XMapRaised(dpy_, snap_preview_[i]);
}

/* Minimized windows are unmapped and live on as taskbar buttons. Their
 * transients go and come back with them. */
void Wm::set_minimized(Client* c, bool on)
{
    if (c->minimized == on)
        return;
    c->minimized = on;
    if (on) {
        c->ignore_unmap++;
        XUnmapWindow(dpy_, c->win);
        XUnmapWindow(dpy_, c->frame);
        set_wm_state(c->win, IconicState);
        /* Last in the focus history, as on Windows. */
        int i = mru_.find(c);
        if (i >= 0) {
            mru_.remove_at(i);
            mru_.push_back(c);
        }
    } else {
        XMapWindow(dpy_, c->win);
        if (on_current(c))
            XMapWindow(dpy_, c->frame);
        set_wm_state(c->win, NormalState);
    }

    int i;
    Vector<Client*> transients;
    for (i = 0; i < clients_.size(); i++)
        if (clients_[i]->transient_for == c->win && clients_[i] != c)
            transients.push_back(clients_[i]);
    for (i = 0; i < transients.size(); i++)
        set_minimized(transients[i], on);

    if (on && active_ && (active_ == c || active_->minimized)) {
        active_ = 0;
        focus(next_focus(c), CurrentTime);
    }
    set_net_state(c);
    notify(c);
}

/* Lay the window out for its current state and decoration. 'restore'
 * returns a window in the normal state to its saved geometry. */
void Wm::apply_state(Client* c, bool restore)
{
    if (c->fullscreen) {
        set_geometry(c, 0, 0, screen_w_, screen_h_);
    } else if (c->maximized || c->snap != SNAP_NONE) {
        /* Fill the slot exactly: resize increments (terminals ask for
         * whole character cells) would leave a gap, so only the minimum
         * and maximum sizes apply here. */
        Rect r = c->maximized ? work_ : snap_rect(c->snap);
        FrameExtents e = extents_of(c);
        int w = r.w - e.width(), h = r.h - e.height();
        SizeConstraints fit = c->size;
        fit.inc_w = fit.inc_h = 0;
        fit.apply(&w, &h);
        /* A window that still cannot fill it (maximum size) sits against
         * the edge it was snapped to. */
        int fw = w + e.width(), fh = h + e.height();
        int x = r.x, y = r.y;
        if (c->snap == SNAP_RIGHT)
            x = r.x + r.w - fw;
        else if (c->snap == SNAP_CENTER)
            x = r.x + (r.w - fw) / 2;
        if (c->snap == SNAP_BOTTOM)
            y = r.y + r.h - fh;
        set_geometry(c, x, y, w, h);
    } else if (restore) {
        set_geometry(c, c->saved.x, c->saved.y, c->saved.w, c->saved.h);
    } else {
        set_geometry(c, c->x, c->y, c->w, c->h);
    }
    paint(c);
    set_net_state(c);
    set_frame_extents(c);
    send_configure(c);
    notify(c);
}

/* ------------------------------------------------------------------ */
/* Shell menus                                                         */

void Wm::menu_shown(bool shown)
{
    shell_menu_up_ = shown;
    popups_changed();
}

void Wm::set_overlay(unsigned long window, bool on)
{
    int i = overlays_.find((Window)window);
    if (on && i < 0)
        overlays_.push_back((Window)window);
    else if (!on && i >= 0)
        overlays_.remove_at(i);
}

bool Wm::menu_up() const
{
    return shell_menu_up_ || !own_popups_.empty();
}

void Wm::popups_changed()
{
    if (menu_up() || close_after_menu_ == None)
        return;
    /* The menu has released its grab; now act on the double click. */
    Client* c = find_client(close_after_menu_);
    close_after_menu_ = None;
    if (c && action_allowed(allowed(c), ACT_CLOSE))
        close(c);
}

/* mwm: double clicking the window menu button closes the window. The
 * first click posted the menu, so the second arrives while it is up; the
 * menu still gets the click (and pops down), and we close afterwards. */
void Wm::menu_double_click(XButtonEvent* ev)
{
    Client* c = find_frame(ev->window);
    if (!c || ev->button != Button1 || c->win != menu_click_win_ ||
        ev->time - menu_click_time_ >= k_double_click_ms)
        return;
    if (theme_.hit(decor_of(c), frame_w(c), frame_h(c), ev->x, ev->y) !=
        FP_MENU)
        return;
    menu_click_win_ = None;
    close_after_menu_ = c->win;
}

/* ------------------------------------------------------------------ */
/* Virtual desktops                                                    */

bool Wm::on_current(const Client* c) const
{
    return c->desktop == ALL_DESKTOPS || c->desktop == current_;
}

void Wm::publish_desktops()
{
    long v[2];
    v[0] = desktops_;
    XChangeProperty(dpy_, root_, atoms_.net_number_of_desktops, XA_CARDINAL,
                    32, PropModeReplace, (unsigned char*)v, 1);
    v[0] = current_;
    XChangeProperty(dpy_, root_, atoms_.net_current_desktop, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char*)v, 1);
    v[0] = screen_w_;
    v[1] = screen_h_;
    XChangeProperty(dpy_, root_, atoms_.net_desktop_geometry, XA_CARDINAL,
                    32, PropModeReplace, (unsigned char*)v, 2);
    long vp[2 * k_max_desktops];
    memset(vp, 0, sizeof(vp));
    XChangeProperty(dpy_, root_, atoms_.net_desktop_viewport, XA_CARDINAL,
                    32, PropModeReplace, (unsigned char*)vp, 2 * desktops_);

    /* "Desktop 1\0Desktop 2\0..." */
    char names[16 * k_max_desktops];
    int len = 0, i;
    for (i = 0; i < desktops_; i++) {
        sprintf(names + len, "Desktop %d", i + 1);
        len += (int)strlen(names + len) + 1;
    }
    XChangeProperty(dpy_, root_, atoms_.net_desktop_names,
                    atoms_.utf8_string, 8, PropModeReplace,
                    (unsigned char*)names, len);
    update_workarea();
}

/* Frames of other desktops are unmapped; their clients stay mapped inside,
 * so applications do not see an iconify. */
void Wm::switch_desktop(int desktop)
{
    if (desktop < 0 || desktop >= desktops_ || desktop == current_)
        return;
    if (switching_)
        end_switch(false, CurrentTime);
    current_ = desktop;

    /* Map the new desktop first, then unmap the old, to avoid a flash of
     * empty root window. */
    int i;
    for (i = clients_.size() - 1; i >= 0; i--)
        if (!clients_[i]->minimized && on_current(clients_[i]))
            XMapWindow(dpy_, clients_[i]->frame);
    for (i = 0; i < clients_.size(); i++)
        if (!on_current(clients_[i]))
            XUnmapWindow(dpy_, clients_[i]->frame);

    publish_desktops();
    if (active_ && !on_current(active_))
        active_ = 0;
    focus(active_ ? active_ : next_focus(0), CurrentTime);
    for (i = 0; i < clients_.size(); i++)
        notify(clients_[i]);
    if (shell_)
        shell_->desktop_changed(current_, desktops_);
    show_osd();
}

void Wm::move_to_desktop(ToplevelId id, int desktop)
{
    Client* c = find_id(id);
    if (c)
        set_desktop(c, desktop);
}

/* Move a window (and its transients) to another desktop, or all. */
void Wm::set_desktop(Client* c, int desktop)
{
    if (desktop != ALL_DESKTOPS && (desktop < 0 || desktop >= desktops_))
        return;
    if (!cycles(c) || c->desktop == desktop)
        return;
    c->desktop = desktop;
    long nd = desktop == ALL_DESKTOPS ? k_net_all_desktops : desktop;
    XChangeProperty(dpy_, c->win, atoms_.net_wm_desktop, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char*)&nd, 1);
    if (!c->minimized) {
        if (on_current(c))
            XMapWindow(dpy_, c->frame);
        else
            XUnmapWindow(dpy_, c->frame);
    }

    int i;
    Vector<Client*> transients;
    for (i = 0; i < clients_.size(); i++)
        if (clients_[i]->transient_for == c->win && clients_[i] != c)
            transients.push_back(clients_[i]);
    for (i = 0; i < transients.size(); i++)
        set_desktop(transients[i], desktop);

    if (active_ == c && !on_current(c)) {
        active_ = 0;
        focus(next_focus(c), CurrentTime);
    }
    notify(c);
}

void Wm::show_osd()
{
    int w, h;
    desktop_osd_size(*painter_, desktops_, &w, &h);
    XMoveResizeWindow(dpy_, osd_win_, work_.x + (work_.w - w) / 2,
                      work_.y + (work_.h - h) / 2, w, h);
    XMapRaised(dpy_, osd_win_);
    paint_osd();
    if (osd_timer_)
        XtRemoveTimeOut(osd_timer_);
    osd_timer_ = XtAppAddTimeOut(app_, k_osd_ms, osd_timeout, (XtPointer)this);
}

void Wm::paint_osd()
{
    int w, h;
    desktop_osd_size(*painter_, desktops_, &w, &h);
    char name[32];
    sprintf(name, "Desktop %d", current_ + 1);
    painter_->begin(osd_win_, w, h);
    paint_desktop_osd(*painter_, theme_.inactive, name, current_, desktops_,
                      w, h);
    painter_->end();
}

void Wm::osd_timeout(XtPointer closure, XtIntervalId* id)
{
    (void)id;
    Wm* self = (Wm*)closure;
    self->osd_timer_ = 0;
    XUnmapWindow(self->dpy_, self->osd_win_);
}

/* ------------------------------------------------------------------ */
/* Windows-style Alt+Tab                                               */

void Wm::begin_switch(bool forward, Time t)
{
    switch_list_.clear();
    int i;
    for (i = 0; i < mru_.size(); i++)
        if (cycles(mru_[i]) && on_current(mru_[i]))
            switch_list_.push_back(mru_[i]);
    if (switch_list_.empty())
        return;
    if (XGrabKeyboard(dpy_, root_, False, GrabModeAsync, GrabModeAsync, t) !=
        GrabSuccess)
        return;

    int n = switch_list_.size();
    switching_ = true;
    switch_sel_ = n > 1 ? (forward ? 1 : n - 1) : 0;

    int w, h;
    switcher_size(*painter_, n, &w, &h);
    XMoveResizeWindow(dpy_, switch_win_, work_.x + (work_.w - w) / 2,
                      work_.y + (work_.h - h) / 2, w, h);
    XMapRaised(dpy_, switch_win_);
    paint_switch();

    /* Alt may already be up if the keys were tapped quickly. */
    Window r, child;
    int rx, ry, wx, wy;
    unsigned int mask = 0;
    XQueryPointer(dpy_, root_, &r, &child, &rx, &ry, &wx, &wy, &mask);
    if (!(mask & Mod1Mask))
        end_switch(true, t);
}

void Wm::step_switch(int dir)
{
    int n = switch_list_.size();
    if (n == 0)
        return;
    switch_sel_ = (switch_sel_ + dir + n) % n;
    paint_switch();
}

void Wm::end_switch(bool commit, Time t)
{
    switching_ = false;
    XUngrabKeyboard(dpy_, t);
    XUnmapWindow(dpy_, switch_win_);
    if (commit && switch_sel_ >= 0 && switch_sel_ < switch_list_.size())
        activate(switch_list_[switch_sel_], t);
    switch_list_.clear();
}

void Wm::paint_switch()
{
    if (!switching_)
        return;
    int n = switch_list_.size();
    SwitchItem* items = new SwitchItem[n > 0 ? n : 1];
    int i;
    for (i = 0; i < n; i++) {
        items[i].icon = switch_list_[i]->icon;
        items[i].title = switch_list_[i]->title.c_str();
    }
    int w, h;
    switcher_size(*painter_, n, &w, &h);
    painter_->begin(switch_win_, w, h);
    paint_switcher(*painter_, theme_.inactive, items, n, switch_sel_, w, h);
    painter_->end();
    delete [] items;
}

/* ------------------------------------------------------------------ */
/* Geometry, properties, painting                                      */

/* Tell the shell about a window on the taskbar: the first call adds its
 * button, later ones update it. */
void Wm::notify(Client* c)
{
    if (!shell_ || !on_taskbar(c))
        return;
    TaskInfo t;
    t.id = c->win;
    t.title = c->title.c_str();
    t.icon = c->icon;
    t.minimized = c->minimized;
    t.allowed = allowed(c);
    t.desktop = c->desktop;
    t.on_current = on_current(c);
    if (c->listed) {
        shell_->task_changed(t);
    } else {
        c->listed = true;
        shell_->task_added(t);
    }
}

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
    Atom state[4];
    int n = 0;
    if (c->fullscreen)
        state[n++] = atoms_.net_wm_state_fullscreen;
    if (c->maximized) {
        state[n++] = atoms_.net_wm_state_maximized_vert;
        state[n++] = atoms_.net_wm_state_maximized_horz;
    } else if (c->snap == SNAP_LEFT || c->snap == SNAP_CENTER ||
               c->snap == SNAP_RIGHT) {
        state[n++] = atoms_.net_wm_state_maximized_vert;
    } else if (c->snap == SNAP_TOP || c->snap == SNAP_BOTTOM) {
        state[n++] = atoms_.net_wm_state_maximized_horz;
    }
    if (c->minimized)
        state[n++] = atoms_.net_wm_state_hidden;
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

/* One work area per desktop, as EWMH asks; they are all the same. */
void Wm::update_workarea()
{
    long v[4 * k_max_desktops];
    int i;
    for (i = 0; i < desktops_; i++) {
        v[i * 4 + 0] = work_.x;
        v[i * 4 + 1] = work_.y;
        v[i * 4 + 2] = work_.w;
        v[i * 4 + 3] = work_.h;
    }
    XChangeProperty(dpy_, root_, atoms_.net_workarea, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char*)v, 4 * desktops_);
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

    drag_.was_snap = c->snap;
    if (c->snap != SNAP_NONE) {
        if (mode == DRAG_MOVE) {
            /* Pull it out at its old size, keeping the same spot of the
             * title bar under the pointer. */
            int fw = frame_w(c);
            int nfw = c->saved.w + extents_of(c).width();
            int nx = x_root - (x_root - c->x) * nfw / (fw > 0 ? fw : 1);
            c->snap = SNAP_NONE;
            set_geometry(c, nx, c->y, c->saved.w, c->saved.h);
        } else {
            c->snap = SNAP_NONE;    /* resized from where it is */
        }
        paint(c);
        set_net_state(c);
        notify(c);
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
    drag_.zone = SNAP_NONE;
    /* begin_drag() sets was_snap before undocking; keep it. */
}

void Wm::update_drag(int x_root, int y_root)
{
    Client* c = drag_.client;
    int dx = x_root - drag_.start_x;
    int dy = y_root - drag_.start_y;

    if (drag_.mode == DRAG_MOVE) {
        set_geometry(c, drag_.orig_x + dx, drag_.orig_y + dy, c->w, c->h);
        int zone = (allowed(c) & k_snap_actions) ? snap_zone(x_root, y_root)
                                                 : (int)SNAP_NONE;
        if (zone != drag_.zone) {
            drag_.zone = zone;
            show_snap_preview(zone);
        }
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
    int zone = drag_.zone;
    if (zone != SNAP_NONE)
        show_snap_preview(SNAP_NONE);
    drag_.mode = DRAG_NONE;
    drag_.client = 0;
    drag_.zone = SNAP_NONE;
    int was_snap = drag_.was_snap;
    drag_.was_snap = SNAP_NONE;
    if (c && !cancel && zone != SNAP_NONE)
        set_snap(c, zone);
    else if (c && cancel && was_snap != SNAP_NONE)
        set_snap(c, was_snap);      /* back where it was docked */
    else if (c)
        send_configure(c);
}

}
