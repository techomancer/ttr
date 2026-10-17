/*
 * Reading ICCCM, EWMH and Motif window hints. Kept free of WM state so the
 * XWayland window manager in Wayland mode can reuse it.
 */
#ifndef TTR_X11_HINTS_H
#define TTR_X11_HINTS_H

#include "core/frame.h"
#include "core/str.h"
#include "core/wmlink.h"

#include <X11/Xlib.h>

namespace ttr {

struct Atoms {
    Atom wm_protocols, wm_delete_window, wm_take_focus;
    Atom wm_state, wm_change_state;
    Atom motif_wm_hints;
    Atom utf8_string;

    Atom net_supported, net_supporting_wm_check, net_wm_name;
    Atom net_client_list, net_client_list_stacking;
    Atom net_active_window, net_close_window, net_frame_extents;
    Atom net_wm_state, net_wm_state_fullscreen;
    Atom net_wm_state_maximized_vert, net_wm_state_maximized_horz;
    Atom net_wm_state_hidden, net_wm_icon, net_workarea;
    Atom net_number_of_desktops, net_current_desktop, net_wm_desktop;
    Atom net_desktop_names, net_desktop_geometry, net_desktop_viewport;
    Atom net_wm_window_type;
    Atom net_type_desktop, net_type_dock, net_type_toolbar, net_type_menu;
    Atom net_type_utility, net_type_splash, net_type_dialog;
    Atom net_type_notification, net_type_normal;

    void init(Display* dpy);
};

enum WindowType {
    WT_NORMAL,
    WT_DIALOG,      /* also utility and toolbar windows */
    WT_DESKTOP,
    WT_DOCK,
    WT_SPLASH,
    WT_NOTIFICATION /* also non-override-redirect menus */
};

/* _NET_WM_NAME (converted to Latin-1) if set, else WM_NAME. */
Str read_title(Display* dpy, const Atoms& a, Window w);

WindowType read_window_type(Display* dpy, const Atoms& a, Window w);

/* Restrict 'decor' and 'actions' by _MOTIF_WM_HINTS, if present. */
void apply_motif_hints(Display* dpy, const Atoms& a, Window w,
                       FrameDecor* decor, ActionMask* actions);

class Icon;

/* The window's icon: the _NET_WM_ICON image closest to 32x32, else the
 * WM_HINTS icon pixmap. Null if neither; the caller owns the result. */
Icon* read_icon(Display* dpy, const Atoms& a, Window w);

/* A CARDINAL property's first value, or 'def' if absent. */
long read_cardinal(Display* dpy, Window w, Atom prop, long def);

void read_net_wm_state(Display* dpy, const Atoms& a, Window w,
                       bool* fullscreen, bool* maximized);

}

#endif
