#include "surface.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

namespace tw {

/* Synchronized output never holds a frame longer than this. */
static const int k_hold_timeout_ms = 150;
/* Frames are at least this far apart while output keeps coming. */
static const int k_min_frame_ms = 16;

Surface::Surface()
    : dpy_(0), win_(0), sched_(0), r_(0), src_(0), margin_x_(2), margin_y_(2),
      frame_delay_(8), blink_ms_(500), scheduled_(false), full_pending_(true), focused_(false),
      blink_on_(true), blink_armed_(false), hold_since_(-1), last_frame_(0), row_buf_(0),
      row_cap_(0), stat_cells_(0)
{
    memset(&g_, 0, sizeof(g_));
    fallback_dirs_[0] = 0;
}

void Surface::set_fallback_dirs(const char* dirs)
{
    strncpy(fallback_dirs_, dirs ? dirs : "", sizeof(fallback_dirs_) - 1);
    fallback_dirs_[sizeof(fallback_dirs_) - 1] = 0;
}

Surface::~Surface()
{
    detach();
    fonts_.unload();
    free(row_buf_);
}

bool Surface::set_fonts(Display* dpy, const char* regular, const char* bold)
{
    if (!fonts_.load(dpy, regular, bold))
        return false;
    /* The bundled fonts: the first directory that has a suitable size. */
    const char* p = fallback_dirs_;
    while (*p) {
        const char* colon = strchr(p, ':');
        size_t len = colon ? (size_t)(colon - p) : strlen(p);
        char dir[700];
        if (len > 0 && len < sizeof(dir)) {
            memcpy(dir, p, len);
            dir[len] = 0;
            if (fonts_.add_fallback_dir(dir))
                break;
        }
        if (!colon)
            break;
        p = colon + 1;
    }
    g_.cell_w = fonts_.cell_width();
    g_.cell_h = fonts_.cell_height();
    g_.ascent = fonts_.ascent();
    full_pending_ = true;
    if (r_) {
        int c, r;
        resize(g_.width, g_.height, &c, &r);
    }
    return true;
}

bool Surface::attach(Display* dpy, Window win, Visual* visual, Colormap cmap, int depth,
                     Scheduler* sched, const char* kind)
{
    detach();
    dpy_ = dpy;
    win_ = win;
    sched_ = sched;
    const char* order[3] = { kind ? kind : "core", "soft", "core" };
    for (int i = 0; i < 3 && !r_; i++) {
        if (i > 0 && strcmp(order[i], order[0]) == 0)
            continue;
        Renderer* r = strcmp(order[i], "gl") == 0 ? create_gl_renderer()
                    : strcmp(order[i], "soft") == 0 ? create_soft_renderer()
                                                     : create_core_renderer();
        if (r && r->init(dpy, win, visual, cmap, depth, &fonts_)) {
            r_ = r;
        } else {
            delete r;
        }
    }
    if (!r_)
        return false;
    r_->set_geometry(g_);
    full_pending_ = true;
    return true;
}

const char* Surface::renderer_name() const
{
    return r_ ? r_->name() : "";
}

bool Surface::set_background(const char* name)
{
    if (!r_ || !r_->set_background(name))
        return false;
    full_pending_ = true;
    damage();
    return true;
}

void Surface::detach()
{
    delete r_;
    r_ = 0;
    win_ = 0;
}

void Surface::set_source(Source* src)
{
    src_ = src;
    lookup_.src = src;
    full_pending_ = true;
    damage();
}

void Surface::resize(int width, int height, int* cols, int* rows)
{
    g_.width = width;
    g_.height = height;
    g_.margin_x = margin_x_;
    g_.margin_y = margin_y_;
    int c = g_.cell_w ? (width - 2 * margin_x_) / g_.cell_w : 1;
    int r = g_.cell_h ? (height - 2 * margin_y_) / g_.cell_h : 1;
    g_.cols = c < 1 ? 1 : c;
    g_.rows = r < 1 ? 1 : r;
    if (r_)
        r_->set_geometry(g_);
    full_pending_ = true;
    *cols = g_.cols;
    *rows = g_.rows;
    damage();
}

void Surface::cell_at(int px, int py, int* col, int* row) const
{
    int c = g_.cell_w ? (px - g_.margin_x) / g_.cell_w : 0;
    int r = g_.cell_h ? (py - g_.margin_y) / g_.cell_h : 0;
    if (px < g_.margin_x) c = 0;
    if (py < g_.margin_y) r = 0;
    if (c >= g_.cols) c = g_.cols - 1;
    if (r >= g_.rows) r = g_.rows - 1;
    *col = c;
    *row = r;
}

long Surface::now_ms() const
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (long)tv.tv_sec * 1000L + (long)(tv.tv_usec / 1000);
}

void Surface::damage()
{
    if (!sched_ || scheduled_)
        return;
    long since = now_ms() - last_frame_;
    int delay = frame_delay_;
    if (since < k_min_frame_ms && k_min_frame_ms - since > delay)
        delay = (int)(k_min_frame_ms - since);
    scheduled_ = true;
    sched_->schedule_frame(delay);
}

void Surface::expose()
{
    /* Any expose repaints everything: an exposed area drawn from the
     * current model would be scrolled again by pending scroll records. */
    full_pending_ = true;
    if (sched_ && !scheduled_) {
        scheduled_ = true;
        sched_->schedule_frame(0);
    }
}

void Surface::set_focus(bool in)
{
    if (in == focused_)
        return;
    focused_ = in;
    blink_on_ = true;
    if (r_ && r_->full_frames()) {
        full_pending_ = true;
        damage();
        return;
    }
    if (src_ && r_) {
        src_->frame_begin();
        Cursor c = src_->cursor();
        if (c.y >= 0 && c.y < g_.rows) {
            draw_row(c.y, c.x, c.x + (c.wide ? 1 : 0));
            draw_cursor();
            r_->flush();
        }
    }
}

void Surface::clear_margins()
{
    uint32_t bg = src_->background();
    int gw = g_.cols * g_.cell_w, gh = g_.rows * g_.cell_h;
    r_->fill_px(0, 0, g_.width, g_.margin_y, bg);
    r_->fill_px(0, g_.margin_y + gh, g_.width, g_.height - g_.margin_y - gh, bg);
    r_->fill_px(0, g_.margin_y, g_.margin_x, gh, bg);
    r_->fill_px(g_.margin_x + gw, g_.margin_y, g_.width - g_.margin_x - gw, gh, bg);
}

void Surface::draw_row(int y, int lo, int hi)
{
    int cols = src_->cols() < g_.cols ? src_->cols() : g_.cols;
    if (y < 0 || y >= g_.rows)
        return;
    if (y >= src_->rows()) {
        /* Below the model (it is smaller than the window for a moment). */
        r_->fill_px(g_.margin_x, g_.margin_y + y * g_.cell_h, g_.cols * g_.cell_w, g_.cell_h,
                    src_->background());
        return;
    }
    if (lo < 0) lo = 0;
    if (hi >= cols) hi = cols - 1;
    if (lo > hi)
        return;
    /* Never split a wide character. */
    if (lo > 0)
        lo--;
    if (hi < cols - 1)
        hi++;
    int n;
    for (;;) {
        n = hi - lo + 1;
        if (n > row_cap_) {
            CellData* p = (CellData*)realloc(row_buf_, (size_t)n * sizeof(CellData));
            if (!p)
                return;
            row_buf_ = p;
            row_cap_ = n;
        }
        /* Sources that know nothing of images leave those fields 0. */
        memset(row_buf_, 0, (size_t)n * sizeof(CellData));
        src_->cells(y, lo, hi, row_buf_);
        /* Starting on the second half of a wide character: take its first
         * half too. */
        if (row_buf_[0].width == 0 && lo > 0) {
            lo--;
            continue;
        }
        break;
    }
    stat_cells_ += n;
    int i = 0;
    while (i < n) {
        const CellData& c = row_buf_[i];
        if (c.image) {
            /* Consecutive tiles of one image row. */
            int j = i + 1;
            while (j < n && row_buf_[j].image == c.image && row_buf_[j].image_row == c.image_row &&
                   row_buf_[j].image_col == c.image_col + (j - i) &&
                   row_buf_[j].style.bg == c.style.bg)
                j++;
            ImageData img;
            if (src_->image(c.image, &img) && img.px && img.cell_w > 0 && img.cell_h > 0)
                r_->draw_image(lo + i, y, row_buf_ + i, j - i, img);
            else
                r_->draw_run(lo + i, y, row_buf_ + i, j - i, &lookup_);
            i = j;
            continue;
        }
        int j = i + 1;
        while (j < n && !row_buf_[j].image && row_buf_[j].style == row_buf_[i].style)
            j++;
        /* A run must not end between a wide head and its tail. */
        if (j < n && row_buf_[j].width == 0)
            j++;
        r_->draw_run(lo + i, y, row_buf_ + i, j - i, &lookup_);
        i = j;
    }
    /* The strip right of the model when the model is narrower. */
    if (hi == cols - 1 && cols < g_.cols)
        r_->fill_px(g_.margin_x + cols * g_.cell_w, g_.margin_y + y * g_.cell_h,
                    (g_.cols - cols) * g_.cell_w, g_.cell_h, src_->background());
}

void Surface::draw_cursor()
{
    Cursor c = src_->cursor();
    if (!c.visible || c.y < 0 || c.y >= g_.rows || c.y >= src_->rows() || c.x >= src_->cols() ||
        c.x >= g_.cols)
        return;
    if (c.blink && focused_ && !blink_on_)
        return;
    CellData under;
    src_->cells(c.y, c.x, c.x, &under);
    r_->draw_cursor(c, under, src_->cursor_color(), focused_, &lookup_);
}

void Surface::frame()
{
    scheduled_ = false;
    if (!src_ || !r_)
        return;
    src_->frame_begin();
    long now = now_ms();
    if (src_->held()) {
        if (hold_since_ < 0)
            hold_since_ = now;
        if (now - hold_since_ < k_hold_timeout_ms) {
            scheduled_ = true;
            sched_->schedule_frame((int)(k_hold_timeout_ms - (now - hold_since_)));
            return;
        }
    } else {
        hold_since_ = -1;
    }
    stat_cells_ = 0;
    int rows = g_.rows;
    r_->begin_frame(src_->background());
    bool retained = r_->retained();
    if (r_->take_full_request())
        full_pending_ = true;
    if (retained) {
        /* The renderer keeps rows: send the changed ones, whole. */
        bool all = full_pending_ || src_->full_damage();
        if (!all)
            for (int i = 0; i < src_->scroll_count(); i++) {
                int top, bottom, count;
                src_->scroll(i, &top, &bottom, &count);
                if (bottom >= rows)
                    bottom = rows - 1;
                if (top <= bottom)
                    r_->scroll(top, bottom, count);
            }
        for (int y = 0; y < rows; y++) {
            int lo, hi;
            if (!all && !(y < src_->rows() && src_->row_dirty(y, &lo, &hi)))
                continue;
            r_->row_begin(y);
            draw_row(y, 0, g_.cols - 1);
            r_->row_end(y);
        }
        full_pending_ = false;
    } else if (full_pending_ || src_->full_damage() || r_->full_frames()) {
        clear_margins();
        for (int y = 0; y < rows; y++)
            draw_row(y, 0, g_.cols - 1);
        full_pending_ = false;
    } else {
        for (int i = 0; i < src_->scroll_count(); i++) {
            int top, bottom, count;
            src_->scroll(i, &top, &bottom, &count);
            if (bottom >= rows)
                bottom = rows - 1;
            if (top <= bottom)
                r_->scroll(top, bottom, count);
        }
        int lim = rows < src_->rows() ? rows : src_->rows();
        for (int y = 0; y < lim; y++) {
            int lo, hi;
            if (src_->row_dirty(y, &lo, &hi))
                draw_row(y, lo, hi);
        }
    }
    draw_cursor();
    src_->frame_done();
    r_->flush();
    last_frame_ = now;
    /* An animated background keeps frames coming. */
    if (r_->animation_ms() > 0 && !scheduled_) {
        scheduled_ = true;
        sched_->schedule_frame(r_->animation_ms());
    }

    /* Keep the cursor blinking while focused. */
    Cursor c = src_->cursor();
    bool want = c.visible && c.blink && focused_ && blink_ms_ > 0;
    if (want && !blink_armed_) {
        blink_armed_ = true;
        sched_->schedule_blink(blink_ms_);
    } else if (!want && blink_armed_) {
        blink_armed_ = false;
        blink_on_ = true;
        sched_->schedule_blink(0);
    }
}

void Surface::blink()
{
    blink_armed_ = false;
    if (!src_ || !r_)
        return;
    Cursor c = src_->cursor();
    if (!(c.visible && c.blink && focused_ && blink_ms_ > 0)) {
        blink_on_ = true;
        return;
    }
    blink_on_ = !blink_on_;
    if (r_->full_frames()) {
        full_pending_ = true;
        damage();
        blink_armed_ = true;
        sched_->schedule_blink(blink_ms_);
        return;
    }
    src_->frame_begin();
    if (c.y >= 0 && c.y < g_.rows) {
        draw_row(c.y, c.x, c.x + (c.wide ? 1 : 0));
        draw_cursor();
        r_->flush();
    }
    blink_armed_ = true;
    sched_->schedule_blink(blink_ms_);
}

void image_row(const ImageData& img, int col, int row, int n, int cw, int ch, int dy,
               uint32_t* out)
{
    int total = n * cw;
    int sy = row * img.cell_h + (ch == img.cell_h ? dy : dy * img.cell_h / ch);
    if (sy < 0 || sy >= img.h || cw <= 0) {
        memset(out, 0, (size_t)total * sizeof(uint32_t));
        return;
    }
    const uint32_t* src = img.px + (size_t)sy * (size_t)img.w;
    int base = col * img.cell_w;
    if (cw == img.cell_w) {
        for (int k = 0; k < total; k++)
            out[k] = base + k < img.w ? src[base + k] : 0;
        return;
    }
    for (int k = 0; k < total; k++) {
        int sx = base + (k / cw) * img.cell_w + (k % cw) * img.cell_w / cw;
        out[k] = sx < img.w ? src[sx] : 0;
    }
}

void image_row_over(const ImageData& img, int col, int row, int n, int cw, int ch, int dy,
                    uint32_t bg, uint32_t* out)
{
    image_row(img, col, row, n, cw, ch, dy, out);
    int total = n * cw;
    for (int k = 0; k < total; k++) {
        uint32_t p = out[k];
        unsigned a = p >> 24;
        if (a == 255) {
            out[k] = p & 0xffffff;
        } else if (a == 0) {
            out[k] = bg & 0xffffff;
        } else {
            uint32_t o = 0;
            for (int s = 0; s < 24; s += 8) {
                unsigned f = (p >> s) & 255, b = (bg >> s) & 255;
                o |= (uint32_t)((f * a + b * (255 - a) + 127) / 255) << s;
            }
            out[k] = o;
        }
    }
}

} /* namespace tw */
