#include "core/frame.h"
#include "core/painter.h"
#include "core/scale.h"

#include <string.h>

namespace ttr {

FrameDecor::FrameDecor()
    : border(true), resizeh(true), title(true), menu(true), minimize(true),
      maximize(true)
{
}

FrameDecor FrameDecor::none()
{
    FrameDecor d;
    d.border = d.resizeh = d.title = d.menu = d.minimize = d.maximize = false;
    return d;
}

FrameTheme::FrameTheme()
    : border(6), shadow(2), title_h(20)
{
    active = motif_colors(Rgb::hex(0x6080a8));
    inactive = motif_colors(Rgb::hex(0xb8b8b8));
}

void FrameTheme::fit_font(int font_height)
{
    border = scaled(6);
    shadow = scaled_line(2);
    title_h = font_height + 2 * shadow + scaled(6);
}

int FrameTheme::border_w(const FrameDecor& d) const
{
    if (!d.border)
        return 0;
    return d.resizeh ? border : shadow + 1;
}

int FrameTheme::title_height(const FrameDecor& d) const
{
    return d.title ? title_h : 0;
}

FrameExtents FrameTheme::extents(const FrameDecor& d) const
{
    FrameExtents e;
    int b = border_w(d);
    e.left = e.right = e.bottom = b;
    e.top = b + title_height(d);
    return e;
}

int FrameTheme::min_client_w(const FrameDecor& d) const
{
    if (!d.title)
        return 1;
    int buttons = (d.menu ? 1 : 0) + (d.minimize ? 1 : 0) +
                  (d.maximize ? 1 : 0);
    return (buttons + 1) * title_h;
}

Rect FrameTheme::menu_rect(const FrameDecor& d, int fw) const
{
    (void)fw;
    if (!d.title || !d.menu)
        return Rect();
    int b = border_w(d);
    return Rect(b, b, title_h, title_h);
}

Rect FrameTheme::maximize_rect(const FrameDecor& d, int fw) const
{
    if (!d.title || !d.maximize)
        return Rect();
    int b = border_w(d);
    return Rect(fw - b - title_h, b, title_h, title_h);
}

Rect FrameTheme::minimize_rect(const FrameDecor& d, int fw) const
{
    if (!d.title || !d.minimize)
        return Rect();
    int b = border_w(d);
    int right = fw - b - (d.maximize ? title_h : 0);
    return Rect(right - title_h, b, title_h, title_h);
}

Rect FrameTheme::title_rect(const FrameDecor& d, int fw) const
{
    if (!d.title)
        return Rect();
    int b = border_w(d);
    int left = b + (d.menu ? title_h : 0);
    int right = fw - b - (d.minimize ? title_h : 0) -
                (d.maximize ? title_h : 0);
    return Rect(left, b, right > left ? right - left : 0, title_h);
}

FramePart FrameTheme::hit(const FrameDecor& d, int fw, int fh,
                          int x, int y) const
{
    if (x < 0 || y < 0 || x >= fw || y >= fh)
        return FP_NONE;

    int bw = border_w(d);
    bool l = x < bw, r = x >= fw - bw;
    bool t = y < bw, b = y >= fh - bw;
    if (l || r || t || b) {
        if (!d.resizeh)
            return FP_NONE;
        int c = border + title_h;  /* length of a corner handle */
        bool near_l = x < c, near_r = x >= fw - c;
        bool near_t = y < c, near_b = y >= fh - c;
        if ((t && near_l) || (l && near_t)) return FP_NW;
        if ((t && near_r) || (r && near_t)) return FP_NE;
        if ((b && near_l) || (l && near_b)) return FP_SW;
        if ((b && near_r) || (r && near_b)) return FP_SE;
        if (t) return FP_N;
        if (b) return FP_S;
        if (l) return FP_W;
        return FP_E;
    }

    if (y < bw + title_height(d)) {
        if (menu_rect(d, fw).contains(x, y)) return FP_MENU;
        if (minimize_rect(d, fw).contains(x, y)) return FP_MINIMIZE;
        if (maximize_rect(d, fw).contains(x, y)) return FP_MAXIMIZE;
        return FP_TITLE;
    }
    return FP_CLIENT;
}

void gravity_offset(int gravity, const FrameExtents& e, int bw,
                    int* dx, int* dy)
{
    switch (gravity) {
    case GRAV_N: case GRAV_CENTER: case GRAV_S:
        *dx = bw - e.width() / 2;
        break;
    case GRAV_NE: case GRAV_E: case GRAV_SE:
        *dx = 2 * bw - e.width();
        break;
    case GRAV_STATIC:
        *dx = bw - e.left;
        break;
    default:
        *dx = 0;
        break;
    }
    switch (gravity) {
    case GRAV_W: case GRAV_CENTER: case GRAV_E:
        *dy = bw - e.height() / 2;
        break;
    case GRAV_SW: case GRAV_S: case GRAV_SE:
        *dy = 2 * bw - e.height();
        break;
    case GRAV_STATIC:
        *dy = bw - e.top;
        break;
    default:
        *dy = 0;
        break;
    }
}

bool frame_part_is_border(FramePart p)
{
    return p >= FP_N && p <= FP_NW;
}

/* A groove is a bottom-shadow line next to a top-shadow line. */
static void groove_v(Painter& p, int x, int y, int h, const ColorSet& cs)
{
    p.fill(Rect(x, y, 1, h), cs.bottom);
    p.fill(Rect(x + 1, y, 1, h), cs.top);
}

static void groove_h(Painter& p, int x, int y, int w, const ColorSet& cs)
{
    p.fill(Rect(x, y, w, 1), cs.bottom);
    p.fill(Rect(x, y + 1, w, 1), cs.top);
}

static void button(Painter& p, const Rect& r, int shadow, bool pressed,
                   const ColorSet& cs)
{
    if (pressed)
        draw_bevel(p, r, shadow, cs.bottom, cs.top);
    else
        draw_bevel(p, r, shadow, cs.top, cs.bottom);
}

/* Small raised box centered in r. */
static void icon_box(Painter& p, const Rect& r, int w, int h,
                     const ColorSet& cs)
{
    Rect b(r.x + (r.w - w) / 2, r.y + (r.h - h) / 2, w, h);
    draw_bevel(p, b, scaled_line(1), cs.top, cs.bottom);
}

void paint_frame(Painter& p, const FrameTheme& t, const FrameDecor& d,
                 int fw, int fh, const char* title, bool active,
                 FramePart pressed)
{
    const ColorSet& cs = active ? t.active : t.inactive;
    int B = t.border_w(d), T = t.title_h, S = t.shadow;

    if (B == 0 && !d.title)
        return;

    p.fill(Rect(0, 0, fw, fh), cs.bg);
    if (B > 0) {
        int s = B > S ? S : B;
        draw_bevel(p, Rect(0, 0, fw, fh), s, cs.top, cs.bottom);
    }
    if (d.resizeh) {
        draw_bevel(p, Rect(B - 1, B - 1, fw - 2 * B + 2, fh - 2 * B + 2), 1,
                   cs.bottom, cs.top);

        /* Corner handle grooves across the resize border. */
        int c = t.border + T;
        int depth = B - 1 - S;
        groove_v(p, c, S, depth, cs);
        groove_v(p, fw - c - 1, S, depth, cs);
        groove_v(p, c, fh - B + 1, depth, cs);
        groove_v(p, fw - c - 1, fh - B + 1, depth, cs);
        groove_h(p, S, c, depth, cs);
        groove_h(p, S, fh - c - 1, depth, cs);
        groove_h(p, fw - B + 1, c, depth, cs);
        groove_h(p, fw - B + 1, fh - c - 1, depth, cs);
    }

    if (!d.title)
        return;

    int small = T / 5 > scaled(4) ? T / 5 : scaled(4);
    if (d.menu) {
        Rect r = t.menu_rect(d, fw);
        button(p, r, S, pressed == FP_MENU, cs);
        icon_box(p, r, T * 3 / 5, small, cs);
    }
    if (d.minimize) {
        Rect r = t.minimize_rect(d, fw);
        button(p, r, S, pressed == FP_MINIMIZE, cs);
        icon_box(p, r, small, small, cs);
    }
    if (d.maximize) {
        Rect r = t.maximize_rect(d, fw);
        button(p, r, S, pressed == FP_MAXIMIZE, cs);
        icon_box(p, r, T / 2, T / 2, cs);
    }

    Rect tr = t.title_rect(d, fw);
    if (tr.w <= 2 * S)
        return;
    button(p, tr, S, false, cs);

    int len = title ? (int)strlen(title) : 0;
    int avail = tr.w - 2 * S - scaled(8);
    while (len > 0 && p.text_width(title, len) > avail)
        len--;
    if (len > 0) {
        int tw = p.text_width(title, len);
        int asc = p.text_ascent(), desc = p.text_descent();
        int x = tr.x + (tr.w - tw) / 2;
        int y = tr.y + (tr.h - (asc + desc)) / 2 + asc;
        p.text(x, y, title, len, cs.fg);
    }
}

}
