#include "core/switcher.h"
#include "core/icon.h"
#include "core/painter.h"
#include "core/scale.h"

namespace ttr {

/* Sizes at 100% (Windows 2000 metrics); scaled() when used. */
static const int k_cell = 43;
static const int k_icon = 32;
static const int MAX_COLS = 7;
static const int k_margin = 10;
static const int k_desk_w = 28, k_desk_h = 20, k_desk_gap = 6;

static int columns(int count)
{
    return count < MAX_COLS ? (count > 0 ? count : 1) : MAX_COLS;
}

static int text_box_h(Painter& p)
{
    return p.text_ascent() + p.text_descent() + scaled(8);
}

void switcher_size(Painter& p, int count, int* w, int* h)
{
    int cols = columns(count);
    int rows = (count + cols - 1) / cols;
    if (rows < 1)
        rows = 1;
    int CELL = scaled(k_cell), MARGIN = scaled(k_margin);
    *w = 2 * MARGIN + cols * CELL;
    if (*w < scaled(280))
        *w = scaled(280);
    *h = 2 * MARGIN + rows * CELL + scaled(6) + text_box_h(p);
}

void paint_switcher(Painter& p, const ColorSet& cs, const SwitchItem* items,
                    int count, int selected, int w, int h)
{
    int CELL = scaled(k_cell), ICON = scaled(k_icon),
        MARGIN = scaled(k_margin), S = scaled_line(2);
    p.fill(Rect(0, 0, w, h), cs.bg);
    draw_bevel(p, Rect(0, 0, w, h), S, cs.top, cs.bottom);

    int cols = columns(count);
    int grid_w = cols * CELL;
    int x0 = (w - grid_w) / 2;
    int i;
    for (i = 0; i < count; i++) {
        int cx = x0 + (i % cols) * CELL;
        int cy = MARGIN + (i / cols) * CELL;
        if (i == selected)
            draw_bevel(p, Rect(cx + 1, cy + 1, CELL - 2, CELL - 2), S,
                       cs.bottom, cs.top);
        int off = (CELL - ICON) / 2;
        draw_app_icon(p, items[i].icon, cx + off, cy + off, ICON, cs);
    }

    int rows = (count + cols - 1) / cols;
    Rect box(MARGIN, MARGIN + rows * CELL + scaled(6), w - 2 * MARGIN,
             text_box_h(p));
    draw_bevel(p, box, scaled_line(1), cs.bottom, cs.top);
    if (selected >= 0 && selected < count && items[selected].title) {
        char buf[256];
        int len = fit_text(p, items[selected].title, box.w - scaled(8), buf,
                           sizeof(buf));
        int tw = p.text_width(buf, len);
        int y = box.y + (box.h - p.text_ascent() - p.text_descent()) / 2 +
                p.text_ascent();
        p.text(box.x + (box.w - tw) / 2, y, buf, len, cs.fg);
    }
}

void desktop_osd_size(Painter& p, int count, int* w, int* h)
{
    int MARGIN = scaled(k_margin);
    *w = 2 * MARGIN + count * scaled(k_desk_w) +
         (count - 1) * scaled(k_desk_gap);
    if (*w < scaled(160))
        *w = scaled(160);
    *h = 2 * MARGIN + scaled(k_desk_h) + scaled(6) + text_box_h(p);
}

void paint_desktop_osd(Painter& p, const ColorSet& cs, const char* name,
                       int current, int count, int w, int h)
{
    int MARGIN = scaled(k_margin), DESK_W = scaled(k_desk_w),
        DESK_H = scaled(k_desk_h), DESK_GAP = scaled(k_desk_gap),
        S = scaled_line(2);
    p.fill(Rect(0, 0, w, h), cs.bg);
    draw_bevel(p, Rect(0, 0, w, h), S, cs.top, cs.bottom);

    int row_w = count * DESK_W + (count - 1) * DESK_GAP;
    int x = (w - row_w) / 2;
    int i;
    for (i = 0; i < count; i++) {
        Rect r(x + i * (DESK_W + DESK_GAP), MARGIN, DESK_W, DESK_H);
        if (i == current) {
            p.fill(r, cs.select);
            draw_bevel(p, r, S, cs.bottom, cs.top);
        } else {
            draw_bevel(p, r, S, cs.top, cs.bottom);
        }
    }

    Rect box(MARGIN, MARGIN + DESK_H + scaled(6), w - 2 * MARGIN,
             text_box_h(p));
    draw_bevel(p, box, scaled_line(1), cs.bottom, cs.top);
    int len = 0;
    while (name[len])
        len++;
    int tw = p.text_width(name, len);
    int y = box.y + (box.h - p.text_ascent() - p.text_descent()) / 2 +
            p.text_ascent();
    p.text(box.x + (box.w - tw) / 2, y, name, len, cs.fg);
}

}
