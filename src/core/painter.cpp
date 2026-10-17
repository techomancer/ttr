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
