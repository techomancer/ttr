#include "editsource.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ttrcode {

namespace {

tw::CellStyle style(uint32_t fg, uint32_t bg)
{
    tw::CellStyle s;
    s.fg = fg;
    s.bg = bg;
    s.deco = fg;
    s.flags = 0;
    return s;
}

bool same(const tw::CellData& a, const tw::CellData& b)
{
    return a.cp == b.cp && a.width == b.width && a.style == b.style;
}

} /* namespace */

EditSource::EditSource()
    : v_(0), numbers_(true), cols_(0), rows_(0), gutter_(0), cur_(0), prev_(0), line_(0), lo_(0),
      hi_(0), full_(true), scroll_(0), prev_valid_(false), prev_top_(0), prev_left_(0),
      prev_gutter_(0), prev_cx_(-1), prev_cy_(-1)
{
    colors_.text = 0x000000;
    colors_.background = 0xffffff;
    colors_.selected_text = 0xffffff;
    colors_.selected_background = 0x000080;
    colors_.gutter_text = 0x808080;
    colors_.gutter_background = 0xf0f0f0;
    colors_.control = 0x800000;
    colors_.found_background = 0xffff80;
    colors_.cursor = 0x000000;
}

EditSource::~EditSource()
{
    free(cur_);
    free(prev_);
    free(line_);
    free(lo_);
    free(hi_);
}

void EditSource::set_view(te::View* v)
{
    v_ = v;
    full_ = true;
    prev_valid_ = false;
    set_size(cols_, rows_);
}

void EditSource::set_colors(const EditColors& c)
{
    colors_ = c;
    full_ = true;
}

void EditSource::set_line_numbers(bool on)
{
    numbers_ = on;
    set_size(cols_, rows_);
}

int EditSource::gutter_for(long lines) const
{
    if (!numbers_)
        return 0;
    int d = 1;
    for (long n = lines; n >= 10; n /= 10)
        d++;
    if (d < 3)
        d = 3;
    return d + 2;           /* a space before and after */
}

void EditSource::set_size(int cols, int rows)
{
    if (cols != cols_ || rows != rows_ || !cur_) {
        size_t n = (size_t)(cols > 0 ? cols : 1) * (size_t)(rows > 0 ? rows : 1);
        free(cur_);
        free(prev_);
        free(line_);
        free(lo_);
        free(hi_);
        cur_ = (tw::CellData*)calloc(n, sizeof(tw::CellData));
        prev_ = (tw::CellData*)calloc(n, sizeof(tw::CellData));
        line_ = (te::Cell*)calloc((size_t)(cols > 0 ? cols : 1), sizeof(te::Cell));
        lo_ = (int*)calloc((size_t)(rows > 0 ? rows : 1), sizeof(int));
        hi_ = (int*)calloc((size_t)(rows > 0 ? rows : 1), sizeof(int));
        cols_ = cols;
        rows_ = rows;
        full_ = true;
        prev_valid_ = false;
    }
    gutter_ = v_ ? gutter_for(v_->document().lines()) : 0;
    if (gutter_ > cols_ / 2)
        gutter_ = 0;
    if (v_)
        v_->set_size(cols_ - gutter_, rows_);
}

void EditSource::layout()
{
    tw::CellStyle text = style(colors_.text, colors_.background);
    tw::CellStyle sel = style(colors_.selected_text, colors_.selected_background);
    tw::CellStyle ctrl = style(colors_.control, colors_.background);
    tw::CellStyle found = style(colors_.text, colors_.found_background);
    tw::CellStyle gut = style(colors_.gutter_text, colors_.gutter_background);
    int tcols = cols_ - gutter_;
    for (int y = 0; y < rows_; y++) {
        tw::CellData* row = cur_ + (size_t)y * (size_t)cols_;
        bool have = v_ && v_->row(y, line_);
        /* The gutter: the line number, right-aligned. */
        char num[24];
        num[0] = 0;
        if (have && gutter_ > 0)
            sprintf(num, "%ld", v_->top() + y + 1);
        int nl = (int)strlen(num);
        for (int x = 0; x < gutter_; x++) {
            int k = x - (gutter_ - 1 - nl);
            row[x].cp = k >= 0 && k < nl ? (uint32_t)num[k] : ' ';
            row[x].width = 1;
            row[x].cluster = 0;
            row[x].style = gut;
        }
        for (int x = 0; x < tcols; x++) {
            tw::CellData& c = row[gutter_ + x];
            c.cluster = 0;
            if (!have) {
                c.cp = ' ';
                c.width = 1;
                c.style = text;
                continue;
            }
            const te::Cell& e = line_[x];
            c.cp = e.cp;
            c.width = e.width;
            switch (e.kind) {
            case te::CELL_SELECTED:
            case te::CELL_BLOCK:
                c.style = sel;
                break;
            case te::CELL_CONTROL:
                c.style = ctrl;
                break;
            case te::CELL_FOUND:
                c.style = found;
                break;
            default:
                c.style = text;
                break;
            }
        }
    }
}

void EditSource::frame_begin()
{
    if (v_) {
        /* The gutter grows with the line count. */
        int g = gutter_for(v_->document().lines());
        if (g > cols_ / 2)
            g = 0;
        if (g != gutter_)
            set_size(cols_, rows_);
    }
    layout();
    scroll_ = 0;
    if (!prev_valid_ || !v_ || v_->left() != prev_left_ || gutter_ != prev_gutter_)
        full_ = true;
    if (full_)
        return;
    long d = v_->top() - prev_top_;
    if (d != 0 && d > -rows_ && d < rows_) {
        scroll_ = (int)d;
    } else if (d != 0) {
        full_ = true;
        return;
    }
    /* Compare with the last frame, moved as tw will move the window. */
    for (int y = 0; y < rows_; y++) {
        int py = y + scroll_;
        tw::CellData* c = cur_ + (size_t)y * (size_t)cols_;
        if (py < 0 || py >= rows_) {
            lo_[y] = 0;
            hi_[y] = cols_ - 1;
            continue;
        }
        tw::CellData* p = prev_ + (size_t)py * (size_t)cols_;
        int lo = -1, hi = -1;
        for (int x = 0; x < cols_; x++)
            if (!same(c[x], p[x])) {
                if (lo < 0)
                    lo = x;
                hi = x;
            }
        /* The cell the caret left. */
        if (py == prev_cy_ && prev_cx_ >= 0 && prev_cx_ < cols_) {
            if (lo < 0 || prev_cx_ < lo)
                lo = prev_cx_;
            if (prev_cx_ > hi)
                hi = prev_cx_;
        }
        lo_[y] = lo;
        hi_[y] = hi;
    }
}

void EditSource::scroll(int, int* top, int* bottom, int* count) const
{
    *top = 0;
    *bottom = rows_ - 1;
    *count = scroll_;
}

bool EditSource::row_dirty(int y, int* lo, int* hi) const
{
    if (y < 0 || y >= rows_ || lo_[y] < 0)
        return false;
    *lo = lo_[y];
    *hi = hi_[y];
    return true;
}

void EditSource::cells(int y, int lo, int hi, tw::CellData* out) const
{
    if (y < 0 || y >= rows_)
        return;
    memcpy(out, cur_ + (size_t)y * (size_t)cols_ + lo, sizeof(tw::CellData) * (size_t)(hi - lo + 1));
}

tw::Cursor EditSource::cursor() const
{
    tw::Cursor c;
    c.x = c.y = 0;
    c.visible = false;
    c.blink = true;
    c.wide = false;
    c.shape = tw::CURSOR_BAR;
    int x, y;
    if (v_ && v_->screen_cursor(&x, &y) && x + gutter_ < cols_) {
        c.x = x + gutter_;
        c.y = y;
        c.visible = true;
    }
    return c;
}

void EditSource::frame_done()
{
    tw::CellData* t = prev_;
    prev_ = cur_;
    cur_ = t;
    prev_valid_ = v_ != 0;
    full_ = false;
    scroll_ = 0;
    if (v_) {
        prev_top_ = v_->top();
        prev_left_ = v_->left();
    }
    prev_gutter_ = gutter_;
    tw::Cursor c = cursor();
    prev_cx_ = c.visible ? c.x : -1;
    prev_cy_ = c.visible ? c.y : -1;
}

} /* namespace ttrcode */
