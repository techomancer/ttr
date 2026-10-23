/*
 * CoreRender: server-side Xlib drawing for any visual, the fast path on
 * SGI boards and PseudoColor (CI8) visuals. DESIGN.md section 5.1.
 *
 * Text in a core font goes out as XDrawImageString, which paints the
 * glyphs and their cell backgrounds in one request; builtin glyphs are
 * rectangles; scrolls are XCopyArea. One GC; its font, foreground and
 * background are changed only when they differ.
 */
#include "glyphs.h"
#include "palette.h"
#include "renderer.h"

#include <stdlib.h>
#include <string.h>
#include <X11/Xutil.h>

namespace tw {

namespace {

uint32_t mix(uint32_t fg, uint32_t bg, int quarter)
{
    uint32_t out = 0;
    for (int s = 0; s < 24; s += 8) {
        int f = (int)((fg >> s) & 255), b = (int)((bg >> s) & 255);
        int v = (f * quarter + b * (4 - quarter)) / 4;
        out |= (uint32_t)v << s;
    }
    return out;
}

class CoreRender : public Renderer {
public:
    CoreRender()
        : dpy_(0), win_(0), gc_(0), gc1_(0), fonts_(0), fid_(0), fg_(~0UL), bg_(~0UL),
          n_pages_(0), cache_gen_(-1), font_serial_(-1)
    {
        memset(&g_, 0, sizeof(g_));
    }
    ~CoreRender()
    {
        drop_pages();
        if (gc1_)
            XFreeGC(dpy_, gc1_);
        if (gc_)
            XFreeGC(dpy_, gc_);
    }

    bool init(Display* dpy, Window win, Visual* visual, Colormap cmap, int depth,
              const Fonts* fonts)
    {
        dpy_ = dpy;
        win_ = win;
        fonts_ = fonts;
        palette_.init(dpy, visual, cmap, depth);
        XGCValues v;
        v.graphics_exposures = True;    /* copies from obscured parts expose */
        gc_ = XCreateGC(dpy, win, GCGraphicsExposures, &v);
        cache_.configure(fonts, fonts->cell_width(), fonts->cell_height(), fonts->ascent());
        font_serial_ = fonts->serial();
        return gc_ != 0;
    }

    void set_geometry(const Geometry& g)
    {
        bool cell_changed = g.cell_w != g_.cell_w || g.cell_h != g_.cell_h || g.ascent != g_.ascent;
        g_ = g;
        if (cell_changed || font_serial_ != fonts_->serial()) {
            cache_.configure(fonts_, g.cell_w, g.cell_h, g.ascent);
            font_serial_ = fonts_->serial();
        }
    }

    void fill_px(int x, int y, int w, int h, uint32_t rgb)
    {
        if (w <= 0 || h <= 0)
            return;
        set_fg(rgb);
        XFillRectangle(dpy_, win_, gc_, x, y, (unsigned)w, (unsigned)h);
    }

    void scroll(int top, int bottom, int count)
    {
        int n = bottom - top + 1;
        int k = count < 0 ? -count : count;
        if (k >= n || k == 0)
            return;
        int x = g_.margin_x, w = g_.cols * g_.cell_w;
        int ch = g_.cell_h;
        if (count > 0)
            XCopyArea(dpy_, win_, win_, gc_, x, g_.margin_y + (top + k) * ch, (unsigned)w,
                      (unsigned)((n - k) * ch), x, g_.margin_y + top * ch);
        else
            XCopyArea(dpy_, win_, win_, gc_, x, g_.margin_y + top * ch, (unsigned)w,
                      (unsigned)((n - k) * ch), x, g_.margin_y + (top + k) * ch);
    }

    void draw_run(int x, int y, const CellData* cells, int n, const ClusterLookup* clusters)
    {
        (void)clusters;                 /* clusters draw their base glyph for now */
        const CellStyle& st = cells[0].style;
        int px = g_.margin_x + x * g_.cell_w;
        int py = g_.margin_y + y * g_.cell_h;
        int i = 0;
        while (i < n) {
            const CellData& c = cells[i];
            if (c.width == 0) {                     /* tail of a wide char drawn by its head */
                i++;
                continue;
            }
            int cw = c.width == 2 ? 2 : 1;
            int cx = px + i * g_.cell_w;
            uint32_t cp = c.cp;
            if ((st.flags & S_INVISIBLE) || cp == 0 || cp == ' ') {
                /* A stretch of blanks: one fill. */
                int j = i;
                while (j < n && cells[j].width != 0 &&
                       ((st.flags & S_INVISIBLE) || cells[j].cp == 0 || cells[j].cp == ' '))
                    j++;
                fill_px(cx, py, (j - i) * g_.cell_w, g_.cell_h, st.bg);
                i = j;
                continue;
            }
            if (builtin_has(cp) && !c.cluster) {
                draw_builtin(cp, cx, py, cw * g_.cell_w, st);
                i++;
                continue;
            }
            if (c.cluster) {
                /* Combining marks: compose base and marks from the bundled
                 * fonts, or at least draw the base. */
                uint32_t cps[TW_CLUSTER_MAX];
                int ncp = clusters ? clusters->cluster(y, x + i, cps, TW_CLUSTER_MAX) : 0;
                if (ncp <= 0) {
                    cps[0] = cp;
                    ncp = 1;
                }
                int face = (st.flags & S_BOLD) ? FACE_BOLD : FACE_REGULAR;
                if (!cached_glyph(face, cps, ncp, cw == 2, cx, py, st))
                    missing(cx, py, cw, st);
                i++;
                continue;
            }
            int face = (st.flags & S_BOLD) && fonts_->has_face(FACE_BOLD) ? FACE_BOLD : FACE_REGULAR;
            const CoreFont& f = fonts_->face(face);
            if (cw == 1 && f.has(cp)) {
                /* A stretch of narrow glyphs in this font: one image string. */
                int j = i;
                while (j < n && cells[j].width == 1 && cells[j].cp > ' ' && !cells[j].cluster &&
                       !builtin_has(cells[j].cp) && f.has(cells[j].cp))
                    j++;
                image_text(f, cells + i, j - i, cx, py, st, face == FACE_REGULAR && (st.flags & S_BOLD));
                i = j;
                continue;
            }
            if (cw == 2 && f.has(cp)) {
                fill_px(cx, py, 2 * g_.cell_w, g_.cell_h, st.bg);
                plain_text(f, cp, cx, py, st, false);
                i++;
                continue;
            }
            /* Not in the server font: the bundled fonts, else a box. */
            int bface = (st.flags & S_BOLD) ? FACE_BOLD : FACE_REGULAR;
            if (!cached_glyph(bface, &cp, 1, cw == 2, cx, py, st))
                missing(cx, py, cw, st);
            i++;
        }
        decorate(px, py, n, st);
    }

    void draw_cursor(const Cursor& c, const CellData& under, uint32_t color, bool focused,
                     const ClusterLookup* clusters)
    {
        int cw = c.wide ? 2 : 1;
        int px = g_.margin_x + c.x * g_.cell_w;
        int py = g_.margin_y + c.y * g_.cell_h;
        int w = cw * g_.cell_w;
        if (!focused) {
            set_fg(color);
            XDrawRectangle(dpy_, win_, gc_, px, py, (unsigned)(w - 1), (unsigned)(g_.cell_h - 1));
            return;
        }
        if (c.shape == CURSOR_UNDERLINE) {
            int t = g_.cell_h >= 16 ? 2 : 1;
            fill_px(px, py + g_.cell_h - t, w, t, color);
            return;
        }
        if (c.shape == CURSOR_BAR) {
            fill_px(px, py, g_.cell_w >= 12 ? 2 : 1, g_.cell_h, color);
            return;
        }
        /* Block: the cell again in cursor colours. */
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

    void flush() { XFlush(dpy_); }
    const char* name() const { return "core"; }

private:
    /* Glyph masks live on the server in depth-1 pixmap pages of fixed
     * slots (two cells wide, for wide glyphs); a glyph is drawn with one
     * XCopyPlane, which paints foreground and background like image text. */
    enum { PAGE_COLS = 32, MAX_PAGES = 24 };
    struct Page {
        Pixmap pm;
        int used;
    };

    int slot_w() const { return 2 * g_.cell_w; }
    int page_rows() const { int r = 512 / (g_.cell_h > 0 ? g_.cell_h : 16); return r < 1 ? 1 : r; }

    void drop_pages()
    {
        for (int i = 0; i < n_pages_; i++)
            XFreePixmap(dpy_, pages_[i].pm);
        n_pages_ = 0;
    }

    /* Put a mask on the server; false if there is no room left. */
    bool upload(CachedGlyph* gl)
    {
        if (cache_gen_ != cache_.generation()) {
            drop_pages();
            cache_gen_ = cache_.generation();
        }
        int per_page = PAGE_COLS * page_rows();
        if (n_pages_ == 0 || pages_[n_pages_ - 1].used >= per_page) {
            if (n_pages_ == MAX_PAGES) {
                cache_.clear();                 /* start over; this glyph is gone too */
                return false;
            }
            Page& p = pages_[n_pages_];
            p.pm = XCreatePixmap(dpy_, win_, (unsigned)(PAGE_COLS * slot_w()),
                                 (unsigned)(page_rows() * g_.cell_h), 1);
            p.used = 0;
            if (!gc1_) {
                XGCValues v;
                v.foreground = 1;
                v.background = 0;
                v.graphics_exposures = False;
                gc1_ = XCreateGC(dpy_, p.pm, GCForeground | GCBackground | GCGraphicsExposures, &v);
            }
            n_pages_++;
        }
        Page& p = pages_[n_pages_ - 1];
        int slot = p.used++;
        int sx = (slot % PAGE_COLS) * slot_w(), sy = (slot / PAGE_COLS) * g_.cell_h;
        XImage* im = XCreateImage(dpy_, 0, 1, XYBitmap, 0, (char*)cache_.bits(gl),
                                  (unsigned)gl->w, (unsigned)gl->h, 8, gl->stride);
        if (!im)
            return false;
        im->byte_order = MSBFirst;
        im->bitmap_bit_order = MSBFirst;
        XPutImage(dpy_, p.pm, gc1_, im, 0, 0, sx, sy, (unsigned)gl->w, (unsigned)gl->h);
        im->data = 0;                           /* the cache owns the bits */
        XDestroyImage(im);
        gl->handle_a = n_pages_ - 1;
        gl->handle_b = slot;
        return true;
    }

    bool cached_glyph(int face, const uint32_t* cps, int n, bool wide, int x, int y,
                      const CellStyle& st)
    {
        CachedGlyph* gl = cache_.get(face, cps, n, wide);
        if (!gl || !gl->found)
            return false;
        if (cache_gen_ != cache_.generation() || gl->handle_a < 0) {
            if (!upload(gl)) {
                gl = cache_.get(face, cps, n, wide);
                if (!gl || !gl->found || !upload(gl))
                    return false;
            }
        }
        set_fg(st.fg);
        set_bg(st.bg);
        const Page& p = pages_[gl->handle_a];
        int sx = (gl->handle_b % PAGE_COLS) * slot_w(), sy = (gl->handle_b / PAGE_COLS) * g_.cell_h;
        XCopyPlane(dpy_, p.pm, win_, gc_, sx, sy, (unsigned)gl->w, (unsigned)gl->h, x, y, 1);
        return true;
    }

    void missing(int x, int y, int cw, const CellStyle& st)
    {
        fill_px(x, y, cw * g_.cell_w, g_.cell_h, st.bg);
        set_fg(st.fg);
        XDrawRectangle(dpy_, win_, gc_, x + 1, y + 1, (unsigned)(cw * g_.cell_w - 3),
                       (unsigned)(g_.cell_h - 3));
    }

    void set_fg(uint32_t rgb)
    {
        unsigned long p = palette_.pixel(rgb);
        if (p != fg_) {
            XSetForeground(dpy_, gc_, p);
            fg_ = p;
        }
    }
    void set_bg(uint32_t rgb)
    {
        unsigned long p = palette_.pixel(rgb);
        if (p != bg_) {
            XSetBackground(dpy_, gc_, p);
            bg_ = p;
        }
    }
    void set_font(const CoreFont& f)
    {
        Font id = f.fs()->fid;
        if (id != fid_) {
            XSetFont(dpy_, gc_, id);
            fid_ = id;
        }
    }

    void image_text(const CoreFont& f, const CellData* cells, int n, int x, int y,
                    const CellStyle& st, bool overstrike)
    {
        set_font(f);
        set_fg(st.fg);
        set_bg(st.bg);
        int base = y + fonts_->ascent();
        if (f.two_byte()) {
            XChar2b buf[256];
            for (int done = 0; done < n;) {
                int k = n - done > 256 ? 256 : n - done;
                for (int i = 0; i < k; i++) {
                    buf[i].byte1 = (unsigned char)(cells[done + i].cp >> 8);
                    buf[i].byte2 = (unsigned char)(cells[done + i].cp & 255);
                }
                XDrawImageString16(dpy_, win_, gc_, x + done * g_.cell_w, base, buf, k);
                if (overstrike)
                    XDrawString16(dpy_, win_, gc_, x + done * g_.cell_w + 1, base, buf, k);
                done += k;
            }
        } else {
            char buf[256];
            for (int done = 0; done < n;) {
                int k = n - done > 256 ? 256 : n - done;
                for (int i = 0; i < k; i++)
                    buf[i] = (char)cells[done + i].cp;
                XDrawImageString(dpy_, win_, gc_, x + done * g_.cell_w, base, buf, k);
                if (overstrike)
                    XDrawString(dpy_, win_, gc_, x + done * g_.cell_w + 1, base, buf, k);
                done += k;
            }
        }
        /* Fonts whose ascent and descent fall short of the cell height
         * leave a strip; the cell height is taken from the regular face,
         * so this only matters for the bold face, which must match. */
    }

    void plain_text(const CoreFont& f, uint32_t cp, int x, int y, const CellStyle& st, bool bold)
    {
        set_font(f);
        set_fg(st.fg);
        int base = y + fonts_->ascent();
        if (f.two_byte()) {
            XChar2b c;
            c.byte1 = (unsigned char)(cp >> 8);
            c.byte2 = (unsigned char)(cp & 255);
            XDrawString16(dpy_, win_, gc_, x, base, &c, 1);
            if (bold)
                XDrawString16(dpy_, win_, gc_, x + 1, base, &c, 1);
        } else {
            char c = (char)cp;
            XDrawString(dpy_, win_, gc_, x, base, &c, 1);
            if (bold)
                XDrawString(dpy_, win_, gc_, x + 1, base, &c, 1);
        }
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
        fill_px(x, y, w, g_.cell_h, st.bg);
        if (n == 0)
            return;
        set_fg(st.fg);
        XRectangle xr[16];
        for (int i = 0; i < n; i++) {
            xr[i].x = (short)(x + r[i].x);
            xr[i].y = (short)(y + r[i].y);
            xr[i].width = (unsigned short)r[i].w;
            xr[i].height = (unsigned short)r[i].h;
        }
        XFillRectangles(dpy_, win_, gc_, xr, n);
    }

    void decorate(int px, int py, int n, const CellStyle& st)
    {
        if (!(st.flags & (S_UNDERLINE | S_DOUBLE_UNDERLINE | S_CURLY_UNDERLINE | S_STRIKE |
                          S_OVERLINE | S_LEFT_LINE)) ||
            (st.flags & S_INVISIBLE))
            return;
        int w = n * g_.cell_w;
        int t = g_.cell_h >= 20 ? 2 : 1;
        int base = py + fonts_->ascent();
        uint32_t c = st.deco;
        if (st.flags & (S_UNDERLINE | S_CURLY_UNDERLINE)) {
            int uy = base + 1;
            if (uy + t > py + g_.cell_h)
                uy = py + g_.cell_h - t;
            fill_px(px, uy, w, t, c);
        }
        if (st.flags & S_DOUBLE_UNDERLINE) {
            int uy = py + g_.cell_h - 1 - t;
            if (uy < base + 1)
                uy = base + 1;
            fill_px(px, uy - t - 1 > base ? uy - t - 1 : base, w, t, c);
            fill_px(px, uy + 1 > py + g_.cell_h - t ? py + g_.cell_h - t : uy + 1, w, t, c);
        }
        if (st.flags & S_STRIKE)
            fill_px(px, py + fonts_->ascent() * 2 / 3, w, t, c);
        if (st.flags & S_OVERLINE)
            fill_px(px, py, w, t, c);
        if (st.flags & S_LEFT_LINE)
            fill_px(px, py, 1, g_.cell_h, c);
    }

    Display* dpy_;
    Window win_;
    GC gc_;
    GC gc1_;                    /* for the depth-1 pages */
    const Fonts* fonts_;
    Palette palette_;
    Geometry g_;
    Font fid_;
    unsigned long fg_, bg_;
    GlyphCache cache_;
    Page pages_[MAX_PAGES];
    int n_pages_;
    int cache_gen_;
    int font_serial_;
};

} /* namespace */

Renderer* create_core_renderer()
{
    return new CoreRender();
}

} /* namespace tw */
