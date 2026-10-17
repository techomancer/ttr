#include "core/frame.h"
#include "core/painter.h"

#include <string.h>

namespace ttr {

void draw_bevel(Painter& p, const Rect& r, int thickness,
                const Rgb& top, const Rgb& bottom)
{
    int i;
    for (i = 0; i < thickness; i++) {
        int w = r.w - 2 * i;
        int h = r.h - 2 * i;
        if (w <= 0 || h <= 0)
            break;
        p.fill(Rect(r.x + i, r.y + i, w - 1, 1), top);
        p.fill(Rect(r.x + i, r.y + i, 1, h - 1), top);
        p.fill(Rect(r.x + i, r.y + r.h - 1 - i, w, 1), bottom);
        p.fill(Rect(r.x + r.w - 1 - i, r.y + i, 1, h), bottom);
    }
}

FrameTheme::FrameTheme()
    : border(6), shadow(2), title_h(20)
{
    active = motif_colors(Rgb::hex(0x6080a8));
    inactive = motif_colors(Rgb::hex(0xb8b8b8));
}

void FrameTheme::fit_font(int font_height)
{
    title_h = font_height + 2 * shadow + 6;
}

Rect FrameTheme::menu_rect(int fw) const
{
    (void)fw;
    return Rect(border, border, title_h, title_h);
}

Rect FrameTheme::title_rect(int fw) const
{
    return Rect(border + title_h, border, fw - 2 * border - 3 * title_h,
                title_h);
}

Rect FrameTheme::minimize_rect(int fw) const
{
    return Rect(fw - border - 2 * title_h, border, title_h, title_h);
}

Rect FrameTheme::maximize_rect(int fw) const
{
    return Rect(fw - border - title_h, border, title_h, title_h);
}

FramePart FrameTheme::hit(int fw, int fh, int x, int y) const
{
    if (x < 0 || y < 0 || x >= fw || y >= fh)
        return FP_NONE;

    bool l = x < border, r = x >= fw - border;
    bool t = y < border, b = y >= fh - border;
    if (l || r || t || b) {
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

    if (y < border + title_h) {
        if (menu_rect(fw).contains(x, y)) return FP_MENU;
        if (minimize_rect(fw).contains(x, y)) return FP_MINIMIZE;
        if (maximize_rect(fw).contains(x, y)) return FP_MAXIMIZE;
        return FP_TITLE;
    }
    return FP_CLIENT;
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
    draw_bevel(p, b, 1, cs.top, cs.bottom);
}

void paint_frame(Painter& p, const FrameTheme& t, int fw, int fh,
                 const char* title, bool active, FramePart pressed)
{
    const ColorSet& cs = active ? t.active : t.inactive;
    int B = t.border, T = t.title_h, S = t.shadow;

    p.fill(Rect(0, 0, fw, fh), cs.bg);
    draw_bevel(p, Rect(0, 0, fw, fh), S, cs.top, cs.bottom);
    draw_bevel(p, Rect(B - 1, B - 1, fw - 2 * B + 2, fh - 2 * B + 2), 1,
               cs.bottom, cs.top);

    /* Corner handle grooves across the resize border. */
    int c = B + T;
    int depth = B - 1 - S;
    groove_v(p, c, S, depth, cs);
    groove_v(p, fw - c - 1, S, depth, cs);
    groove_v(p, c, fh - B + 1, depth, cs);
    groove_v(p, fw - c - 1, fh - B + 1, depth, cs);
    groove_h(p, S, c, depth, cs);
    groove_h(p, S, fh - c - 1, depth, cs);
    groove_h(p, fw - B + 1, c, depth, cs);
    groove_h(p, fw - B + 1, fh - c - 1, depth, cs);

    Rect menu = t.menu_rect(fw);
    button(p, menu, S, pressed == FP_MENU, cs);
    icon_box(p, menu, T * 3 / 5, T / 5 > 4 ? T / 5 : 4, cs);

    Rect mini = t.minimize_rect(fw);
    button(p, mini, S, pressed == FP_MINIMIZE, cs);
    icon_box(p, mini, T / 5 > 4 ? T / 5 : 4, T / 5 > 4 ? T / 5 : 4, cs);

    Rect maxi = t.maximize_rect(fw);
    button(p, maxi, S, pressed == FP_MAXIMIZE, cs);
    icon_box(p, maxi, T / 2, T / 2, cs);

    Rect tr = t.title_rect(fw);
    if (tr.w <= 2 * S)
        return;
    button(p, tr, S, false, cs);

    int len = title ? (int)strlen(title) : 0;
    int avail = tr.w - 2 * S - 8;
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
