#include "core/switcher.h"
#include "core/icon.h"
#include "core/painter.h"

namespace ttr {

static const int CELL = 43;     /* Windows 2000 metrics */
static const int ICON = 32;
static const int MAX_COLS = 7;
static const int MARGIN = 10;

static int columns(int count)
{
    return count < MAX_COLS ? (count > 0 ? count : 1) : MAX_COLS;
}

static int text_box_h(Painter& p)
{
    return p.text_ascent() + p.text_descent() + 8;
}

void switcher_size(Painter& p, int count, int* w, int* h)
{
    int cols = columns(count);
    int rows = (count + cols - 1) / cols;
    if (rows < 1)
        rows = 1;
    *w = 2 * MARGIN + cols * CELL;
    if (*w < 280)
        *w = 280;
    *h = 2 * MARGIN + rows * CELL + 6 + text_box_h(p);
}

void paint_switcher(Painter& p, const ColorSet& cs, const SwitchItem* items,
                    int count, int selected, int w, int h)
{
    p.fill(Rect(0, 0, w, h), cs.bg);
    draw_bevel(p, Rect(0, 0, w, h), 2, cs.top, cs.bottom);

    int cols = columns(count);
    int grid_w = cols * CELL;
    int x0 = (w - grid_w) / 2;
    int i;
    for (i = 0; i < count; i++) {
        int cx = x0 + (i % cols) * CELL;
        int cy = MARGIN + (i / cols) * CELL;
        if (i == selected)
            draw_bevel(p, Rect(cx + 1, cy + 1, CELL - 2, CELL - 2), 2,
                       cs.bottom, cs.top);
        int off = (CELL - ICON) / 2;
        draw_app_icon(p, items[i].icon, cx + off, cy + off, ICON, cs);
    }

    int rows = (count + cols - 1) / cols;
    Rect box(MARGIN, MARGIN + rows * CELL + 6, w - 2 * MARGIN, text_box_h(p));
    draw_bevel(p, box, 1, cs.bottom, cs.top);
    if (selected >= 0 && selected < count && items[selected].title) {
        char buf[256];
        int len = fit_text(p, items[selected].title, box.w - 8, buf,
                           sizeof(buf));
        int tw = p.text_width(buf, len);
        int y = box.y + (box.h - p.text_ascent() - p.text_descent()) / 2 +
                p.text_ascent();
        p.text(box.x + (box.w - tw) / 2, y, buf, len, cs.fg);
    }
}

static const int DESK_W = 28, DESK_H = 20, DESK_GAP = 6;

void desktop_osd_size(Painter& p, int count, int* w, int* h)
{
    *w = 2 * MARGIN + count * DESK_W + (count - 1) * DESK_GAP;
    if (*w < 160)
        *w = 160;
    *h = 2 * MARGIN + DESK_H + 6 + text_box_h(p);
}

void paint_desktop_osd(Painter& p, const ColorSet& cs, const char* name,
                       int current, int count, int w, int h)
{
    p.fill(Rect(0, 0, w, h), cs.bg);
    draw_bevel(p, Rect(0, 0, w, h), 2, cs.top, cs.bottom);

    int row_w = count * DESK_W + (count - 1) * DESK_GAP;
    int x = (w - row_w) / 2;
    int i;
    for (i = 0; i < count; i++) {
        Rect r(x + i * (DESK_W + DESK_GAP), MARGIN, DESK_W, DESK_H);
        if (i == current) {
            p.fill(r, cs.select);
            draw_bevel(p, r, 2, cs.bottom, cs.top);
        } else {
            draw_bevel(p, r, 2, cs.top, cs.bottom);
        }
    }

    Rect box(MARGIN, MARGIN + DESK_H + 6, w - 2 * MARGIN, text_box_h(p));
    draw_bevel(p, box, 1, cs.bottom, cs.top);
    int len = 0;
    while (name[len])
        len++;
    int tw = p.text_width(name, len);
    int y = box.y + (box.h - p.text_ascent() - p.text_descent()) / 2 +
            p.text_ascent();
    p.text(box.x + (box.w - tw) / 2, y, name, len, cs.fg);
}

}
