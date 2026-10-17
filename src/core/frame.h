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

struct FrameTheme {
    int border;     /* resize border width, including bevels */
    int shadow;     /* bevel thickness */
    int title_h;    /* title bar height; buttons are square */
    ColorSet active;
    ColorSet inactive;

    FrameTheme();

    void fit_font(int font_height);

    int client_x() const { return border; }
    int client_y() const { return border + title_h; }
    int extra_w() const { return 2 * border; }
    int extra_h() const { return 2 * border + title_h; }
    int min_client_w() const { return 3 * title_h; }

    Rect menu_rect(int fw) const;
    Rect title_rect(int fw) const;
    Rect minimize_rect(int fw) const;
    Rect maximize_rect(int fw) const;

    FramePart hit(int fw, int fh, int x, int y) const;
};

/* True for the eight resize handles. */
bool frame_part_is_border(FramePart p);

void paint_frame(Painter& p, const FrameTheme& t, int fw, int fh,
                 const char* title, bool active, FramePart pressed);

}

#endif
