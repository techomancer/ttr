/*
 * mwm-style frame: resize border with corner handles, title bar with a
 * window menu button on the left and minimize/maximize buttons on the right.
 *
 *  +--------------------------------------------+
 *  | +-----+------------------------+----+----+ |
 *  | | [-] |         title          | .  | [] | |
 *  | +-----+------------------------+----+----+ |
 *  | |                                        | |
 *  | |                client                  | |
 *  | |                                        | |
 *  | +----------------------------------------+ |
 *  +--------------------------------------------+
 *
 * Any part can be switched off per window (FrameDecor), following the
 * decoration bits of _MOTIF_WM_HINTS.
 */
#ifndef TTR_CORE_FRAME_H
#define TTR_CORE_FRAME_H

#include "core/color.h"
#include "core/geom.h"

namespace ttr {

class Painter;

enum FramePart {
    FP_NONE,
    FP_CLIENT,
    FP_TITLE,
    FP_MENU,
    FP_MINIMIZE,
    FP_MAXIMIZE,
    FP_N, FP_NE, FP_E, FP_SE, FP_S, FP_SW, FP_W, FP_NW
};

/* Which parts of the frame a window gets. */
struct FrameDecor {
    bool border;    /* a border at all */
    bool resizeh;   /* full-width border with resize handles */
    bool title;
    bool menu;
    bool minimize;
    bool maximize;

    FrameDecor();
    static FrameDecor none();
};

/* Space the frame adds around the client on each side. */
struct FrameExtents {
    int left, right, top, bottom;

    FrameExtents() : left(0), right(0), top(0), bottom(0) {}
    int width() const { return left + right; }
    int height() const { return top + bottom; }
};

struct FrameTheme {
    int border;     /* resize border width, including bevels */
    int shadow;     /* bevel thickness */
    int title_h;    /* title bar height; buttons are square */
    ColorSet active;
    ColorSet inactive;

    FrameTheme();

    /* Sizes for the title font and the UI scale (core/scale.h). */
    void fit_font(int font_height);

    /* Width of the border for this decoration (0, thin, or full). */
    int border_w(const FrameDecor& d) const;
    int title_height(const FrameDecor& d) const;
    FrameExtents extents(const FrameDecor& d) const;
    int min_client_w(const FrameDecor& d) const;

    /* Button and title rectangles; empty (w == 0) if not present. */
    Rect menu_rect(const FrameDecor& d, int fw) const;
    Rect title_rect(const FrameDecor& d, int fw) const;
    Rect minimize_rect(const FrameDecor& d, int fw) const;
    Rect maximize_rect(const FrameDecor& d, int fw) const;

    FramePart hit(const FrameDecor& d, int fw, int fh, int x, int y) const;
};

/* Window gravity, with the same values as X11's (ICCCM 4.1.2.3). */
enum Gravity {
    GRAV_FORGET = 0,
    GRAV_NW = 1, GRAV_N = 2, GRAV_NE = 3,
    GRAV_W = 4, GRAV_CENTER = 5, GRAV_E = 6,
    GRAV_SW = 7, GRAV_S = 8, GRAV_SE = 9,
    GRAV_STATIC = 10
};

/* Offset from a client's own position (outer corner, including its border
 * width bw) to the frame position that keeps its gravity reference point
 * in place: frame = client + (dx, dy). */
void gravity_offset(int gravity, const FrameExtents& e, int bw,
                    int* dx, int* dy);

/* True for the eight resize handles. */
bool frame_part_is_border(FramePart p);

void paint_frame(Painter& p, const FrameTheme& t, const FrameDecor& d,
                 int fw, int fh, const char* title, bool active,
                 FramePart pressed);

}

#endif
