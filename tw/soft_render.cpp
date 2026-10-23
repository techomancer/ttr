/*
 * SoftRender: a client-side framebuffer for TrueColor visuals (DESIGN.md
 * section 5.2). Cells are painted into an XImage in the visual's pixel
 * format: backgrounds as fills, glyphs from the glyph cache's masks
 * (core font glyphs rasterized once by the server, so text is exactly as
 * sharp as with the core renderer). A frame goes to the server as the
 * bounding boxes of what changed, through MIT-SHM on a local display and
 * XPutImage otherwise; scrolls are a memmove here and an XCopyArea there,
 * so scrolled pixels never cross the wire.
 */
#include "glyphs.h"
#include "palette.h"
#include "renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>

namespace tw {

namespace {

uint32_t mix(uint32_t fg, uint32_t bg, int quarter)
{
    uint32_t out = 0;
    for (int s = 0; s < 24; s += 8) {
        int f = (int)((fg >> s) & 255), b = (int)((bg >> s) & 255);
        out |= (uint32_t)((f * quarter + b * (4 - quarter)) / 4) << s;
    }
    return out;
}

bool g_shm_failed;
int shm_error(Display*, XErrorEvent*)
{
    g_shm_failed = true;
    return 0;
}

bool native_lsb()
{
    unsigned v = 1;
    return *(unsigned char*)&v == 1;
}

uint32_t swap32(uint32_t v)
{
    return (v >> 24) | ((v >> 8) & 0xff00) | ((v << 8) & 0xff0000) | (v << 24);
}

class SoftRender : public Renderer {
public:
    SoftRender()
        : dpy_(0), win_(0), gc_(0), visual_(0), depth_(0), fonts_(0), im_(0), shm_(false),
          swap_(false), bpp_(0), n_dirty_(0), font_serial_(-1)
    {
        memset(&g_, 0, sizeof(g_));
        memset(&shminfo_, 0, sizeof(shminfo_));
    }
    ~SoftRender()
    {
        drop_image();
        if (gc_)
            XFreeGC(dpy_, gc_);
    }

    const char* name() const { return "soft"; }

    bool init(Display* dpy, Window win, Visual* visual, Colormap cmap, int depth,
              const Fonts* fonts)
    {
        if (visual->c_class != TrueColor && visual->c_class != DirectColor)
            return false;
        dpy_ = dpy;
        win_ = win;
        visual_ = visual;
        depth_ = depth;
        fonts_ = fonts;
        palette_.init(dpy, visual, cmap, depth);
        XGCValues v;
        v.graphics_exposures = False;
        gc_ = XCreateGC(dpy, win, GCGraphicsExposures, &v);
        cache_.set_display(dpy, win);
        cache_.configure(fonts, fonts->cell_width(), fonts->cell_height(), fonts->ascent());
        font_serial_ = fonts->serial();
        /* Only 16 and 32 bits per pixel: what every TrueColor server has. */
        int n = 0;
        XPixmapFormatValues* f = XListPixmapFormats(dpy, &n);
        for (int i = 0; f && i < n; i++)
            if (f[i].depth == depth)
                bpp_ = f[i].bits_per_pixel;
        if (f)
            XFree(f);
        swap_ = (ImageByteOrder(dpy) == LSBFirst) != native_lsb();
        return gc_ && (bpp_ == 16 || bpp_ == 32);
    }

    void set_geometry(const Geometry& g)
    {
        bool cell_changed = g.cell_w != g_.cell_w || g.cell_h != g_.cell_h || g.ascent != g_.ascent;
        bool size_changed = g.width != g_.width || g.height != g_.height;
        g_ = g;
        if (cell_changed || font_serial_ != fonts_->serial()) {
            cache_.configure(fonts_, g.cell_w, g.cell_h, g.ascent);
            font_serial_ = fonts_->serial();
        }
        if (size_changed || !im_)
            make_image();
    }

    void fill_px(int x, int y, int w, int h, uint32_t rgb)
    {
        if (!clip(&x, &y, &w, &h))
            return;
        unsigned long p = px(rgb);
        for (int r = 0; r < h; r++) {
            char* row = im_->data + (size_t)(y + r) * (size_t)im_->bytes_per_line;
            if (bpp_ == 32) {
                uint32_t* d = (uint32_t*)row + x;
                for (int c = 0; c < w; c++)
                    d[c] = (uint32_t)p;
            } else {
                uint16_t* d = (uint16_t*)row + x;
                for (int c = 0; c < w; c++)
                    d[c] = (uint16_t)p;
            }
        }
        mark(x, y, w, h);
    }

    void scroll(int top, int bottom, int count)
    {
        int n = bottom - top + 1;
        int k = count < 0 ? -count : count;
        if (k >= n || k == 0 || !im_)
            return;
        /* What was painted goes out first: the copy moves the window. */
        flush();
        int ch = g_.cell_h;
        int x = g_.margin_x, w = g_.cols * g_.cell_w;
        int src = g_.margin_y + (count > 0 ? top + k : top) * ch;
        int dst = g_.margin_y + (count > 0 ? top : top + k) * ch;
        int h = (n - k) * ch;
        if (src < 0 || dst < 0 || src + h > im_->height || dst + h > im_->height)
            return;
        size_t bpl = (size_t)im_->bytes_per_line;
        memmove(im_->data + (size_t)dst * bpl, im_->data + (size_t)src * bpl, (size_t)h * bpl);
        XCopyArea(dpy_, win_, win_, gc_, x, src, (unsigned)w, (unsigned)h, x, dst);
    }

    void draw_run(int x, int y, const CellData* cells, int n, const ClusterLookup* clusters)
    {
        const CellStyle& st = cells[0].style;
        int px = g_.margin_x + x * g_.cell_w;
        int py = g_.margin_y + y * g_.cell_h;
        /* Backgrounds first, in one fill (wide tails included). */
        fill_px(px, py, n * g_.cell_w, g_.cell_h, st.bg);
        for (int i = 0; i < n; i++) {
            const CellData& c = cells[i];
            if (c.width == 0 || (st.flags & S_INVISIBLE) || c.cp == 0 || c.cp == ' ')
                continue;
            int cw = c.width == 2 ? 2 : 1;
            int cx = px + i * g_.cell_w;
            if (builtin_has(c.cp) && !c.cluster) {
                draw_builtin(c.cp, cx, py, cw * g_.cell_w, st);
                continue;
            }
            uint32_t cps[TW_CLUSTER_MAX];
            int ncp = 0;
            if (c.cluster && clusters)
                ncp = clusters->cluster(y, x + i, cps, TW_CLUSTER_MAX);
            if (ncp <= 0) {
                cps[0] = c.cp;
                ncp = 1;
            }
            int face = (st.flags & S_BOLD) ? FACE_BOLD : FACE_REGULAR;
            CachedGlyph* gl = cache_.get(face, cps, ncp, cw == 2);
            if (gl && gl->found)
                blit(gl, cx, py, st.fg);
            else
                missing(cx, py, cw, st);
        }
        decorate(px, py, n, st);
    }

    void draw_cursor(const Cursor& c, const CellData& under, uint32_t color, bool focused,
                     const ClusterLookup* clusters)
    {
        int cw = c.wide ? 2 : 1;
        int px = g_.margin_x + c.x * g_.cell_w;
        int py = g_.margin_y + c.y * g_.cell_h;
        int w = cw * g_.cell_w, h = g_.cell_h;
        if (!focused) {
            fill_px(px, py, w, 1, color);
            fill_px(px, py + h - 1, w, 1, color);
            fill_px(px, py, 1, h, color);
            fill_px(px + w - 1, py, 1, h, color);
            return;
        }
        if (c.shape == CURSOR_UNDERLINE) {
            int t = h >= 16 ? 2 : 1;
            fill_px(px, py + h - t, w, t, color);
            return;
        }
        if (c.shape == CURSOR_BAR) {
            fill_px(px, py, g_.cell_w >= 12 ? 2 : 1, h, color);
            return;
        }
        CellData cell[2];
        cell[0] = under;
        cell[0].style.fg = under.style.bg == color ? under.style.fg : under.style.bg;
        if (cell[0].style.fg == color)
            cell[0].style.fg = 0xffffff ^ color;
        cell[0].style.bg = color;
        cell[0].style.flags &= (uint16_t)~(S_INVISIBLE | S_UNDERLINE | S_DOUBLE_UNDERLINE |
                                           S_CURLY_UNDERLINE | S_STRIKE | S_OVERLINE);
        int n = 1;
        if (cw == 2) {
            cell[1] = cell[0];
            cell[1].width = 0;
            n = 2;
        }
        draw_run(c.x, c.y, cell, n, clusters);
    }

    void flush()
    {
        if (!im_) {
            XFlush(dpy_);
            return;
        }
        for (int i = 0; i < n_dirty_; i++) {
            const Rect& r = dirty_[i];
            if (shm_)
                XShmPutImage(dpy_, win_, gc_, im_, r.x, r.y, r.x, r.y, (unsigned)r.w,
                             (unsigned)r.h, False);
            else
                XPutImage(dpy_, win_, gc_, im_, r.x, r.y, r.x, r.y, (unsigned)r.w, (unsigned)r.h);
        }
        n_dirty_ = 0;
        /* The server reads the shared image later: wait, so the next
         * frame does not paint into what it is still reading. */
        if (shm_)
            XSync(dpy_, False);
        else
            XFlush(dpy_);
    }

private:
    struct Rect {
        int x, y, w, h;
    };
    enum { MAX_DIRTY = 8 };

    /* A pixel as stored: in the server's byte order, so the image goes
     * out as it is (Xlib's own swapping of 32-bit images to a big-endian
     * server mangled the channels). */
    unsigned long px(uint32_t rgb)
    {
        unsigned long p = palette_.pixel(rgb);
        if (!swap_)
            return p;
        if (bpp_ == 32)
            return swap32((uint32_t)p);
        return ((p >> 8) & 0xff) | ((p & 0xff) << 8);
    }

    bool clip(int* x, int* y, int* w, int* h) const
    {
        if (!im_)
            return false;
        if (*x < 0) { *w += *x; *x = 0; }
        if (*y < 0) { *h += *y; *y = 0; }
        if (*x + *w > im_->width) *w = im_->width - *x;
        if (*y + *h > im_->height) *h = im_->height - *y;
        return *w > 0 && *h > 0;
    }

    /* Damage as at most a few rectangles: rows of cells merge into one
     * span per stretch of rows. */
    void mark(int x, int y, int w, int h)
    {
        for (int i = 0; i < n_dirty_; i++) {
            Rect& r = dirty_[i];
            /* Touching or overlapping vertically, overlapping in x: grow. */
            if (y <= r.y + r.h && y + h >= r.y && x <= r.x + r.w && x + w >= r.x) {
                int x2 = r.x + r.w > x + w ? r.x + r.w : x + w;
                int y2 = r.y + r.h > y + h ? r.y + r.h : y + h;
                r.x = r.x < x ? r.x : x;
                r.y = r.y < y ? r.y : y;
                r.w = x2 - r.x;
                r.h = y2 - r.y;
                return;
            }
        }
        if (n_dirty_ == MAX_DIRTY) {
            /* Too many: one box around everything. */
            Rect& r = dirty_[0];
            for (int i = 1; i < n_dirty_; i++) {
                const Rect& o = dirty_[i];
                int x2 = r.x + r.w > o.x + o.w ? r.x + r.w : o.x + o.w;
                int y2 = r.y + r.h > o.y + o.h ? r.y + r.h : o.y + o.h;
                r.x = r.x < o.x ? r.x : o.x;
                r.y = r.y < o.y ? r.y : o.y;
                r.w = x2 - r.x;
                r.h = y2 - r.y;
            }
            n_dirty_ = 1;
            mark(x, y, w, h);
            return;
        }
        Rect& r = dirty_[n_dirty_++];
        r.x = x;
        r.y = y;
        r.w = w;
        r.h = h;
    }

    void drop_image()
    {
        if (!im_)
            return;
        if (shm_) {
            XShmDetach(dpy_, &shminfo_);
            XSync(dpy_, False);
            im_->data = 0;
            XDestroyImage(im_);
            shmdt(shminfo_.shmaddr);
            shm_ = false;
        } else {
            XDestroyImage(im_);         /* frees data */
        }
        im_ = 0;
        n_dirty_ = 0;
    }

    void make_image()
    {
        drop_image();
        int w = g_.width > 0 ? g_.width : 1, h = g_.height > 0 ? g_.height : 1;
        /* MIT-SHM where the server shares our memory: a local display. */
        int major, minor;
        Bool pixmaps;
        if (XShmQueryVersion(dpy_, &major, &minor, &pixmaps)) {
            im_ = XShmCreateImage(dpy_, visual_, (unsigned)depth_, ZPixmap, 0, &shminfo_,
                                  (unsigned)w, (unsigned)h);
            if (im_) {
                shminfo_.shmid = shmget(IPC_PRIVATE, (size_t)im_->bytes_per_line * (size_t)h,
                                        IPC_CREAT | 0600);
                shminfo_.shmaddr = shminfo_.shmid >= 0 ? (char*)shmat(shminfo_.shmid, 0, 0)
                                                       : (char*)-1;
                bool ok = shminfo_.shmaddr != (char*)-1;
                if (ok) {
                    im_->data = shminfo_.shmaddr;
                    shminfo_.readOnly = False;
                    g_shm_failed = false;
                    XErrorHandler old = XSetErrorHandler(shm_error);
                    XShmAttach(dpy_, &shminfo_);
                    XSync(dpy_, False);
                    XSetErrorHandler(old);
                    ok = !g_shm_failed;
                }
                if (shminfo_.shmid >= 0)
                    shmctl(shminfo_.shmid, IPC_RMID, 0);     /* gone when both detach */
                if (ok) {
                    shm_ = true;
                    return;
                }
                if (shminfo_.shmaddr != (char*)-1)
                    shmdt(shminfo_.shmaddr);
                im_->data = 0;
                XDestroyImage(im_);
                im_ = 0;
            }
        }
        im_ = XCreateImage(dpy_, visual_, (unsigned)depth_, ZPixmap, 0, 0, (unsigned)w,
                           (unsigned)h, 32, 0);
        if (!im_)
            return;
        im_->data = (char*)calloc((size_t)im_->bytes_per_line, (size_t)h);
    }

    void blit(const CachedGlyph* gl, int x, int y, uint32_t rgb)
    {
        const uint8_t* bits = cache_.bits(gl);
        unsigned long p = px(rgb);
        int w = gl->w, h = gl->h;
        if (x + w > im_->width)
            w = im_->width - x;
        if (y + h > im_->height)
            h = im_->height - y;
        for (int r = 0; r < h; r++) {
            const uint8_t* m = bits + r * gl->stride;
            char* row = im_->data + (size_t)(y + r) * (size_t)im_->bytes_per_line;
            for (int c = 0; c < w; c++) {
                if (!(m[c >> 3] & (0x80 >> (c & 7))))
                    continue;
                if (bpp_ == 32)
                    ((uint32_t*)row)[x + c] = (uint32_t)p;
                else
                    ((uint16_t*)row)[x + c] = (uint16_t)p;
            }
        }
    }

    void missing(int x, int y, int cw, const CellStyle& st)
    {
        int w = cw * g_.cell_w, h = g_.cell_h;
        fill_px(x + 1, y + 1, w - 2, 1, st.fg);
        fill_px(x + 1, y + h - 2, w - 2, 1, st.fg);
        fill_px(x + 1, y + 1, 1, h - 2, st.fg);
        fill_px(x + w - 2, y + 1, 1, h - 2, st.fg);
    }

    void draw_builtin(uint32_t cp, int x, int y, int w, const CellStyle& st)
    {
        GlyphRect r[16];
        int shade;
        int n = builtin_rects(cp, w, g_.cell_h, r, 16, &shade);
        if (shade) {
            fill_px(x, y, w, g_.cell_h, mix(st.fg, st.bg, shade));
            return;
        }
        for (int i = 0; i < n; i++)
            fill_px(x + r[i].x, y + r[i].y, r[i].w, r[i].h, st.fg);
    }

    void decorate(int px, int py, int n, const CellStyle& st)
    {
        if (st.flags & S_INVISIBLE)
            return;
        int w = n * g_.cell_w;
        int t = g_.cell_h >= 20 ? 2 : 1;
        int base = py + g_.ascent;
        uint32_t c = st.deco;
        if (st.flags & (S_UNDERLINE | S_CURLY_UNDERLINE)) {
            int uy = base + 1;
            if (uy + t > py + g_.cell_h)
                uy = py + g_.cell_h - t;
            fill_px(px, uy, w, t, c);
        }
        if (st.flags & S_DOUBLE_UNDERLINE) {
            fill_px(px, py + g_.cell_h - 1 - 2 * t, w, t, c);
            fill_px(px, py + g_.cell_h - t, w, t, c);
        }
        if (st.flags & S_STRIKE)
            fill_px(px, py + g_.ascent * 2 / 3, w, t, c);
        if (st.flags & S_OVERLINE)
            fill_px(px, py, w, t, c);
        if (st.flags & S_LEFT_LINE)
            fill_px(px, py, 1, g_.cell_h, c);
    }

    Display* dpy_;
    Window win_;
    GC gc_;
    Visual* visual_;
    int depth_;
    const Fonts* fonts_;
    Palette palette_;
    Geometry g_;
    GlyphCache cache_;
    XImage* im_;
    XShmSegmentInfo shminfo_;
    bool shm_;
    bool swap_;                 /* the server's byte order is not ours */
    int bpp_;
    Rect dirty_[MAX_DIRTY];
    int n_dirty_;
    int font_serial_;
};

} /* namespace */

Renderer* create_soft_renderer()
{
    return new SoftRender();
}

} /* namespace tw */
