#include "x11/hints.h"
#include "core/icon.h"
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
        "_NET_WM_STATE_HIDDEN", "_NET_WM_ICON", "_NET_WORKAREA",
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
        &net_wm_state_hidden, &net_wm_icon, &net_workarea,
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
        ActionMask m = (1u << ACT_LOWER) | (1u << ACT_ACTIVATE);
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

/* ------------------------------------------------------------------ */
/* Icons                                                               */

static const int k_max_icon = 256;

static Icon* read_net_wm_icon(Display* dpy, const Atoms& a, Window w)
{
    Atom type;
    int format;
    unsigned long n = 0, after = 0;
    unsigned char* data = 0;
    /* Up to 4M longs; big icon sets are common. */
    if (XGetWindowProperty(dpy, w, a.net_wm_icon, 0, 1L << 22, False,
                           XA_CARDINAL, &type, &format, &n, &after,
                           &data) != Success || !data)
        return 0;
    if (type != XA_CARDINAL || format != 32 || n < 3) {
        XFree(data);
        return 0;
    }

    /* Format-32 data is an array of long; each image is w, h, w*h pixels.
     * Pick the smallest image of at least 32, else the largest. */
    long* v = (long*)data;
    unsigned long i = 0, best = 0;
    long best_w = 0, best_h = 0;
    while (i + 2 <= n) {
        long iw = v[i], ih = v[i + 1];
        if (iw <= 0 || ih <= 0 || iw > 1024 || ih > 1024 ||
            i + 2 + (unsigned long)(iw * ih) > n)
            break;
        bool better;
        if (best_w == 0)
            better = true;
        else if (best_w >= 32)
            better = iw >= 32 && iw < best_w;
        else
            better = iw > best_w;
        if (better) {
            best = i;
            best_w = iw;
            best_h = ih;
        }
        i += 2 + iw * ih;
    }

    Icon* ic = 0;
    if (best_w > 0) {
        ic = new Icon((int)best_w, (int)best_h);
        uint32_t* px = ic->pixels();
        long k;
        for (k = 0; k < best_w * best_h; k++)
            px[k] = (uint32_t)(v[best + 2 + k] & 0xffffffffUL);
    }
    XFree(data);
    return ic;
}

/* Bit position and width of a TrueColor channel mask. */
static void mask_shift(unsigned long mask, int* shift, int* bits)
{
    *shift = 0;
    *bits = 0;
    if (!mask)
        return;
    while (!(mask & 1)) {
        mask >>= 1;
        (*shift)++;
    }
    while (mask & 1) {
        mask >>= 1;
        (*bits)++;
    }
}

static unsigned long channel8(unsigned long pixel, int shift, int bits)
{
    if (bits == 0)
        return 0;
    unsigned long v = (pixel >> shift) & ((1UL << bits) - 1);
    return bits >= 8 ? v >> (bits - 8) : v * 255 / ((1UL << bits) - 1);
}

/* ICCCM icon pixmaps: depth 1 (drawn black on white) or the screen depth. */
static Icon* read_icon_pixmap(Display* dpy, Pixmap pm, Pixmap mask)
{
    Window root;
    int x, y;
    unsigned int w, h, bw, depth;
    if (!XGetGeometry(dpy, pm, &root, &x, &y, &w, &h, &bw, &depth) ||
        w == 0 || h == 0 || w > (unsigned)k_max_icon ||
        h > (unsigned)k_max_icon)
        return 0;
    XImage* img = XGetImage(dpy, pm, 0, 0, w, h, AllPlanes, ZPixmap);
    if (!img)
        return 0;
    XImage* mimg = mask != None ?
        XGetImage(dpy, mask, 0, 0, w, h, 1, ZPixmap) : 0;

    int screen = DefaultScreen(dpy);
    Visual* vis = DefaultVisual(dpy, screen);
    bool truecolor = vis->c_class == TrueColor || vis->c_class == DirectColor;
    int rs, rb, gs, gb, bs, bb;
    mask_shift(vis->red_mask, &rs, &rb);
    mask_shift(vis->green_mask, &gs, &gb);
    mask_shift(vis->blue_mask, &bs, &bb);

    /* Colormapped visuals: look all pixels up in one request. */
    XColor* colors = 0;
    if (depth != 1 && !truecolor) {
        colors = new XColor[w * h];
        unsigned int i, j;
        for (j = 0; j < h; j++)
            for (i = 0; i < w; i++)
                colors[j * w + i].pixel = XGetPixel(img, i, j);
        XQueryColors(dpy, DefaultColormap(dpy, screen), colors, w * h);
    }

    Icon* ic = new Icon(w, h);
    uint32_t* px = ic->pixels();
    unsigned int i, j;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            unsigned long p = XGetPixel(img, i, j);
            unsigned long r, g, b;
            if (depth == 1) {
                r = g = b = p ? 0 : 255;
            } else if (truecolor) {
                r = channel8(p, rs, rb);
                g = channel8(p, gs, gb);
                b = channel8(p, bs, bb);
            } else {
                XColor& c = colors[j * w + i];
                r = c.red >> 8;
                g = c.green >> 8;
                b = c.blue >> 8;
            }
            unsigned long alpha = (!mimg || XGetPixel(mimg, i, j)) ? 255 : 0;
            px[j * w + i] = (uint32_t)((alpha << 24) | (r << 16) | (g << 8) |
                                       b);
        }
    }

    delete [] colors;
    if (mimg)
        XDestroyImage(mimg);
    XDestroyImage(img);
    return ic;
}

Icon* read_icon(Display* dpy, const Atoms& a, Window w)
{
    Icon* ic = read_net_wm_icon(dpy, a, w);
    if (ic)
        return ic;
    XWMHints* h = XGetWMHints(dpy, w);
    if (h) {
        if (h->flags & IconPixmapHint)
            ic = read_icon_pixmap(dpy, h->icon_pixmap,
                                  (h->flags & IconMaskHint) ? h->icon_mask
                                                            : None);
        XFree(h);
    }
    return ic;
}

}
