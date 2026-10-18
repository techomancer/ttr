#include "core/painter.h"
#include "core/icon.h"

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

void draw_app_icon(Painter& p, const Icon* ic, int x, int y, int size,
                   const ColorSet& cs)
{
    if (ic) {
        p.icon(*ic, x, y, size, cs.bg);
        return;
    }
    /* A little window: frame, title bar, client area. */
    int m = size / 8;
    Rect r(x + m, y + m, size - 2 * m, size - 2 * m);
    draw_bevel(p, r, 1, cs.top, cs.bottom);
    int t = size / 5 > 3 ? size / 5 : 3;
    p.fill(Rect(r.x + 1, r.y + 1, r.w - 2, t), Rgb::hex(0x6080a8));
    p.fill(Rect(r.x + 1, r.y + 1 + t, r.w - 2, r.h - 2 - t),
           Rgb(0xffff, 0xffff, 0xffff));
}

void draw_speaker_icon(Painter& p, int x, int y, int size, int level,
                       bool muted, const ColorSet& cs)
{
    int u = size / 8 > 1 ? size / 8 : 1;    /* one unit */
    int cy = y + size / 2;
    /* Box and cone, filled column by column. */
    p.fill(Rect(x + u, cy - u, 2 * u, 2 * u + 1), cs.fg);
    int i;
    for (i = 0; i < 3 * u; i++) {
        int half = u + (i * 2 * u) / (3 * u);
        p.fill(Rect(x + 3 * u + i, cy - half, 1, 2 * half + 1), cs.fg);
    }
    /* Waves: short vertical arcs right of the cone. */
    int k;
    for (k = 0; k < level && k < 3; k++) {
        int wx = x + 7 * u + k * 2 * u;
        int h = 2 * u + k * 2 * u;
        p.fill(Rect(wx, cy - h + u, u, 2 * h - 2 * u + 1), cs.fg);
        p.fill(Rect(wx - u, cy - h, u, u), cs.fg);
        p.fill(Rect(wx - u, cy + h, u, u), cs.fg);
    }
    if (muted) {
        Rgb red = Rgb::hex(0xc00000);
        for (i = 0; i < 4 * u; i++) {
            p.fill(Rect(x + size - 5 * u + i, cy - 2 * u + i, u, u), red);
            p.fill(Rect(x + size - 5 * u + i, cy + 2 * u - i, u, u), red);
        }
    }
}

void draw_network_icon(Painter& p, int x, int y, int size, int kind,
                       int level, const ColorSet& cs)
{
    int u = size / 8 > 1 ? size / 8 : 1;
    if (kind == 2) {
        /* Four bars of rising height, the first 'level' filled. */
        int i;
        for (i = 0; i < 4; i++) {
            int h = (i + 1) * 3 * u / 2 + u;
            Rect r(x + u + i * 2 * u, y + size - u - h, u + u / 2, h);
            if (i < level)
                p.fill(r, cs.fg);
            else
                draw_bevel(p, r, 1, cs.bottom, cs.top);
        }
        return;
    }
    /* A small monitor on a stand. */
    Rect screen(x + u, y + u, size - 2 * u, size - 5 * u);
    p.fill(screen, cs.fg);
    p.fill(Rect(screen.x + u, screen.y + u, screen.w - 2 * u,
                screen.h - 2 * u), kind == 1 ? Rgb::hex(0x3060c0)
                                             : Rgb::hex(0x808080));
    p.fill(Rect(x + size / 2 - u, y + size - 4 * u, 2 * u, 2 * u), cs.fg);
    p.fill(Rect(x + 2 * u, y + size - 2 * u, size - 4 * u, u), cs.fg);
    if (kind == 0) {
        Rgb red = Rgb::hex(0xc00000);
        int i;
        for (i = 0; i < 4 * u; i++) {
            p.fill(Rect(x + size - 5 * u + i, y + size - 5 * u + i, u, u), red);
            p.fill(Rect(x + size - 5 * u + i, y + size - u - i, u, u), red);
        }
    }
}

void draw_storage_icon(Painter& p, int x, int y, int size, const ColorSet& cs)
{
    int u = size / 8 > 1 ? size / 8 : 1;
    /* Plug on top, body below. */
    p.fill(Rect(x + 3 * u, y, 2 * u, 3 * u), Rgb::hex(0xa0a0a0));
    p.fill(Rect(x + 3 * u + u / 2, y + u, u, u), cs.fg);
    Rect body(x + 2 * u, y + 3 * u, 4 * u, size - 3 * u);
    p.fill(body, Rgb::hex(0x3060c0));
    draw_bevel(p, body, 1, Rgb::hex(0x80a0e0), Rgb::hex(0x203060));
}

void draw_folder_icon(Painter& p, int x, int y, int size)
{
    Rgb edge = Rgb::hex(0x806010), face = Rgb::hex(0xf0d060),
        light = Rgb::hex(0xfff0a0);
    int tab_w = size * 2 / 5, tab_h = size / 6 > 1 ? size / 6 : 1;
    int top = y + size / 6 + tab_h;
    int h = size - (top - y) - size / 8;
    p.fill(Rect(x + 1, top - tab_h, tab_w, tab_h + 1), edge);
    p.fill(Rect(x + 2, top - tab_h + 1, tab_w - 2, tab_h), face);
    p.fill(Rect(x, top, size, h), edge);
    p.fill(Rect(x + 1, top + 1, size - 2, h - 2), face);
    p.fill(Rect(x + 1, top + 1, size - 2, 1), light);
}

int fit_text(Painter& p, const char* s, int width, char* buf, int size)
{
    int len = (int)strlen(s);
    if (len > size - 4)
        len = size - 4;
    memcpy(buf, s, len);
    buf[len] = 0;
    if (p.text_width(buf, len) <= width)
        return len;
    int dots = p.text_width("...", 3);
    while (len > 0 && p.text_width(buf, len) + dots > width)
        len--;
    strcpy(buf + len, "...");
    return len + 3;
}

}
