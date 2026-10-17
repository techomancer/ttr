#include "x11/hints.h"
#include "core/text.h"

#include <X11/Xatom.h>
#include <X11/Xutil.h>

namespace ttr {

void Atoms::init(Display* dpy)
{
    static const char* names[] = {
        "WM_PROTOCOLS", "WM_DELETE_WINDOW", "WM_TAKE_FOCUS",
        "WM_STATE", "WM_CHANGE_STATE",
        "_MOTIF_WM_HINTS",
        "UTF8_STRING",
        "_NET_SUPPORTED", "_NET_SUPPORTING_WM_CHECK", "_NET_WM_NAME",
        "_NET_CLIENT_LIST", "_NET_CLIENT_LIST_STACKING",
        "_NET_ACTIVE_WINDOW", "_NET_CLOSE_WINDOW", "_NET_FRAME_EXTENTS",
        "_NET_WM_STATE", "_NET_WM_STATE_FULLSCREEN",
        "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MAXIMIZED_HORZ",
        "_NET_WM_WINDOW_TYPE",
        "_NET_WM_WINDOW_TYPE_DESKTOP", "_NET_WM_WINDOW_TYPE_DOCK",
        "_NET_WM_WINDOW_TYPE_TOOLBAR", "_NET_WM_WINDOW_TYPE_MENU",
        "_NET_WM_WINDOW_TYPE_UTILITY", "_NET_WM_WINDOW_TYPE_SPLASH",
        "_NET_WM_WINDOW_TYPE_DIALOG", "_NET_WM_WINDOW_TYPE_NOTIFICATION",
        "_NET_WM_WINDOW_TYPE_NORMAL"
    };
    Atom* slots[] = {
        &wm_protocols, &wm_delete_window, &wm_take_focus,
        &wm_state, &wm_change_state,
        &motif_wm_hints,
        &utf8_string,
        &net_supported, &net_supporting_wm_check, &net_wm_name,
        &net_client_list, &net_client_list_stacking,
        &net_active_window, &net_close_window, &net_frame_extents,
        &net_wm_state, &net_wm_state_fullscreen,
        &net_wm_state_maximized_vert, &net_wm_state_maximized_horz,
        &net_wm_window_type,
        &net_type_desktop, &net_type_dock,
        &net_type_toolbar, &net_type_menu,
        &net_type_utility, &net_type_splash,
        &net_type_dialog, &net_type_notification,
        &net_type_normal
    };
    const int n = (int)(sizeof(names) / sizeof(names[0]));
    Atom atoms[sizeof(names) / sizeof(names[0])];
    XInternAtoms(dpy, (char**)names, n, False, atoms);
    int i;
    for (i = 0; i < n; i++)
        *slots[i] = atoms[i];
}

/* Fetch a whole property. Returns the item count, 0 if absent or of the
 * wrong type/format. Caller XFree()s *data when the result is > 0. */
static unsigned long get_property(Display* dpy, Window w, Atom prop,
                                  Atom type, int format,
                                  unsigned char** data)
{
    Atom actual_type;
    int actual_format;
    unsigned long n = 0, after = 0;
    *data = 0;
    if (XGetWindowProperty(dpy, w, prop, 0, 1024, False, type, &actual_type,
                           &actual_format, &n, &after, data) != Success)
        return 0;
    if (actual_type != type || actual_format != format || n == 0) {
        if (*data)
            XFree(*data);
        *data = 0;
        return 0;
    }
    return n;
}

Str read_title(Display* dpy, const Atoms& a, Window w)
{
    unsigned char* data;
    unsigned long n = get_property(dpy, w, a.net_wm_name, a.utf8_string, 8,
                                   &data);
    if (n > 0) {
        Str s = utf8_to_latin1((const char*)data, (int)n);
        XFree(data);
        return s;
    }

    char* name = 0;
    if (XFetchName(dpy, w, &name) && name) {
        Str s(name);
        XFree(name);
        return s;
    }
    return Str("");
}

WindowType read_window_type(Display* dpy, const Atoms& a, Window w)
{
    unsigned char* data;
    unsigned long n = get_property(dpy, w, a.net_wm_window_type, XA_ATOM, 32,
                                   &data);
    WindowType t = WT_NORMAL;
    unsigned long i;
    /* The first type we understand wins. */
    for (i = 0; i < n; i++) {
        Atom at = ((Atom*)data)[i];
        if (at == a.net_type_normal) { t = WT_NORMAL; break; }
        if (at == a.net_type_dialog || at == a.net_type_utility ||
            at == a.net_type_toolbar) { t = WT_DIALOG; break; }
        if (at == a.net_type_desktop) { t = WT_DESKTOP; break; }
        if (at == a.net_type_dock) { t = WT_DOCK; break; }
        if (at == a.net_type_splash) { t = WT_SPLASH; break; }
        if (at == a.net_type_notification || at == a.net_type_menu) {
            t = WT_NOTIFICATION;
            break;
        }
    }
    if (n > 0)
        XFree(data);
    return t;
}

/* _MOTIF_WM_HINTS layout, from Xm/MwmUtil.h. */
enum {
    MWM_HINTS_FUNCTIONS = 1 << 0,
    MWM_HINTS_DECORATIONS = 1 << 1,

    MWM_FUNC_ALL = 1 << 0,
    MWM_FUNC_RESIZE = 1 << 1,
    MWM_FUNC_MOVE = 1 << 2,
    MWM_FUNC_MINIMIZE = 1 << 3,
    MWM_FUNC_MAXIMIZE = 1 << 4,
    MWM_FUNC_CLOSE = 1 << 5,

    MWM_DECOR_ALL = 1 << 0,
    MWM_DECOR_BORDER = 1 << 1,
    MWM_DECOR_RESIZEH = 1 << 2,
    MWM_DECOR_TITLE = 1 << 3,
    MWM_DECOR_MENU = 1 << 4,
    MWM_DECOR_MINIMIZE = 1 << 5,
    MWM_DECOR_MAXIMIZE = 1 << 6
};

void apply_motif_hints(Display* dpy, const Atoms& a, Window w,
                       FrameDecor* decor, ActionMask* actions)
{
    unsigned char* data;
    unsigned long n = get_property(dpy, w, a.motif_wm_hints,
                                   a.motif_wm_hints, 32, &data);
    if (n < 3) {
        if (n > 0)
            XFree(data);
        return;
    }
    /* Format-32 properties come back as an array of long. */
    long* v = (long*)data;
    long flags = v[0], funcs = v[1], decs = v[2];
    XFree(data);

    if (flags & MWM_HINTS_FUNCTIONS) {
        /* With FUNC_ALL set, the other bits name functions to remove. */
        if (funcs & MWM_FUNC_ALL)
            funcs = ~funcs;
        ActionMask m = (1u << ACT_LOWER);
        if (funcs & MWM_FUNC_RESIZE) m |= 1u << ACT_SIZE;
        if (funcs & MWM_FUNC_MOVE) m |= 1u << ACT_MOVE;
        if (funcs & MWM_FUNC_MINIMIZE) m |= 1u << ACT_MINIMIZE;
        if (funcs & MWM_FUNC_MAXIMIZE)
            m |= (1u << ACT_MAXIMIZE) | (1u << ACT_RESTORE);
        if (funcs & MWM_FUNC_CLOSE) m |= 1u << ACT_CLOSE;
        *actions &= m;
    }

    if (flags & MWM_HINTS_DECORATIONS) {
        if (decs & MWM_DECOR_ALL)
            decs = ~decs;
        if (!(decs & MWM_DECOR_BORDER)) decor->border = false;
        if (!(decs & MWM_DECOR_RESIZEH)) decor->resizeh = false;
        if (!(decs & MWM_DECOR_TITLE)) decor->title = false;
        if (!(decs & MWM_DECOR_MENU)) decor->menu = false;
        if (!(decs & MWM_DECOR_MINIMIZE)) decor->minimize = false;
        if (!(decs & MWM_DECOR_MAXIMIZE)) decor->maximize = false;
        /* Resize handles are part of the border. */
        if (!decor->border)
            decor->resizeh = false;
    }
}

void read_net_wm_state(Display* dpy, const Atoms& a, Window w,
                       bool* fullscreen, bool* maximized)
{
    unsigned char* data;
    unsigned long n = get_property(dpy, w, a.net_wm_state, XA_ATOM, 32,
                                   &data);
    bool vert = false, horz = false;
    unsigned long i;
    *fullscreen = false;
    for (i = 0; i < n; i++) {
        Atom at = ((Atom*)data)[i];
        if (at == a.net_wm_state_fullscreen)
            *fullscreen = true;
        else if (at == a.net_wm_state_maximized_vert)
            vert = true;
        else if (at == a.net_wm_state_maximized_horz)
            horz = true;
    }
    if (n > 0)
        XFree(data);
    *maximized = vert && horz;
}

}
