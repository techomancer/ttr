/*
 * GLRender: OpenGL 1.1 (DESIGN.md section 5.3), for boards with texture
 * hardware and for Linux with a GPU. Glyph masks from the glyph cache
 * (core font glyphs rasterized once by the server) go into an atlas
 * texture and are drawn as quads one texel per pixel at whole-pixel
 * positions, so text is as sharp as the core renderer's.
 *
 * Two ways to present:
 * - double-buffered: every frame is drawn whole and swapped;
 * - single-buffered (the Indy's 24-bit visual has no back buffer): only
 *   damaged rows are drawn, into the front buffer, and scrolls are
 *   glCopyPixels, like the core renderer's XCopyArea.
 * An animated background (backgrounds.h, from dash) makes every frame
 * whole: cells in the window's background colour are left transparent
 * over it. IMPACT does not render GL_ALPHA textures, so there the atlas
 * is GL_LUMINANCE_ALPHA (found in dash).
 */
#include "renderer.h"

#if defined(TTR_HAVE_GL)

#include "backgrounds.h"
#include "glyphs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <GL/gl.h>
#include <GL/glx.h>

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

long now_ms()
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (long)tv.tv_sec * 1000L + (long)(tv.tv_usec / 1000);
}

struct Quad {
    int x, y, w, h;
    uint32_t rgb;
    float u0, v0, u1, v1;       /* atlas coordinates (textured text) */
    int bm;                     /* the flipped inked box in bm_ (bitmap text) */
};

/* An image run for glDrawPixels, drawn with the quads. */
struct ImageOp {
    int x, y, w, h;
    bool blend;
    unsigned char* rgba;
};

/* A growing array of quads, kept between frames. */
class Quads {
public:
    Quads() : q_(0), n_(0), cap_(0) {}
    ~Quads() { free(q_); }
    void clear() { n_ = 0; }
    int size() const { return n_; }
    void swap(Quads& o)
    {
        Quad* q = q_; q_ = o.q_; o.q_ = q;
        int n = n_; n_ = o.n_; o.n_ = n;
        int c = cap_; cap_ = o.cap_; o.cap_ = c;
    }
    const Quad& operator[](int i) const { return q_[i]; }
    Quad* add()
    {
        if (n_ == cap_) {
            int nc = cap_ ? cap_ * 2 : 64;
            Quad* p = (Quad*)realloc(q_, sizeof(Quad) * (size_t)nc);
            if (!p)
                return 0;
            q_ = p;
            cap_ = nc;
        }
        return &q_[n_++];
    }

private:
    Quad* q_;
    int n_, cap_;
    Quads(const Quads&);
    Quads& operator=(const Quads&);
};

void color(uint32_t rgb)
{
    glColor4ub((GLubyte)(rgb >> 16), (GLubyte)(rgb >> 8), (GLubyte)rgb, 255);
}

class GLRender : public Renderer {
public:
    GLRender()
        : dpy_(0), win_(0), ctx_(0), dbl_(false), impact_(false), atlas_fmt_(GL_ALPHA), atlas_bpp_(1), atlas_cpu_(0), atlas_dirty_(false), fonts_(0), font_serial_(-1),
          atlas_(0), atlas_size_(0), slot_cols_(0), slot_rows_(0), next_slot_(0),
          cache_gen_(-1), bg_(0), last_anim_(0), default_bg_(0), whole_(false), verts_(0), colors_(0), texs_(0), vert_cap_(0),
          bitmap_text_(false), bm_(0), bm_n_(0), bm_cap_(0), ops_(0), n_ops_(0), ops_cap_(0),
          use_arrays_(false), rows_(0), order_(0), n_rows_(0), use_lists_(true), blend_glyphs_(false), list_base_(0),
          atlas_epoch_(0),
          recording_(false), full_request_(false)
    {
        memset(&g_, 0, sizeof(g_));
        bg_name_[0] = 0;
    }
    ~GLRender()
    {
        free_ops(ops_, n_ops_);
        free(ops_);
        free(atlas_cpu_);
        free(verts_);
        free(colors_);
        free(texs_);
        free(bm_);
        if (ctx_) {
            current();
            drop_rows();
            delete bg_;
            if (atlas_)
                glDeleteTextures(1, &atlas_);
            glXMakeCurrent(dpy_, None, 0);
            glXDestroyContext(dpy_, ctx_);
        }
    }

    const char* name() const { return "gl"; }

    /* Why the renderer cannot start (the Surface then falls back). */
    static bool fail(const char* why)
    {
        fprintf(stderr, "tw: the gl renderer cannot start: %s\n", why);
        return false;
    }

    bool init(Display* dpy, Window win, Visual* visual, Colormap, int, const Fonts* fonts)
    {
        dpy_ = dpy;
        win_ = win;
        fonts_ = fonts;
        XVisualInfo tmpl;
        tmpl.visualid = XVisualIDFromVisual(visual);
        int n = 0;
        XVisualInfo* vi = XGetVisualInfo(dpy, VisualIDMask, &tmpl, &n);
        if (!vi)
            return fail("the window's visual is unknown");
        int use_gl = 0, rgba = 0, dbl = 0;
        glXGetConfig(dpy, vi, GLX_USE_GL, &use_gl);
        glXGetConfig(dpy, vi, GLX_RGBA, &rgba);
        glXGetConfig(dpy, vi, GLX_DOUBLEBUFFER, &dbl);
        if (use_gl && rgba)
            ctx_ = glXCreateContext(dpy, vi, 0, True);
        XFree(vi);
        if (!ctx_)
            return fail(use_gl && rgba ? "glXCreateContext failed" : "the visual has no RGBA GL");
        dbl_ = dbl != 0;
        if (!glXMakeCurrent(dpy, win, ctx_))
            return fail("glXMakeCurrent failed");
        const char* r = (const char*)glGetString(GL_RENDERER);
        impact_ = r && strstr(r, "IMPACT");
        /* Boards that texture in software draw text as bitmaps:
         * Newport (Indy, Indigo2 XL) and the XZ/Elan family. */
        const char* force = getenv("TW_GL_TEXT");
        bitmap_text_ = r && (strstr(r, "NEWPORT") || strstr(r, "GR2") || strstr(r, "GR3") ||
                             strstr(r, "GU1") || strstr(r, "XZ") || strstr(r, "ELAN"));
#if defined(__sgi)
        use_arrays_ = false;
#else
        use_arrays_ = true;
#endif
        const char* blend = getenv("TW_GL_BLEND");
        if (blend)
            blend_glyphs_ = blend[0] == '1';
        const char* lists = getenv("TW_GL_LISTS");
        if (lists)
            use_lists_ = lists[0] == '1';
        const char* arr = getenv("TW_GL_ARRAYS");
        if (arr)
            use_arrays_ = arr[0] == '1';
        if (force && strcmp(force, "bitmap") == 0)
            bitmap_text_ = true;
        else if (force && strcmp(force, "texture") == 0)
            bitmap_text_ = false;
        GLint max_tex = 256;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
        atlas_size_ = max_tex >= 1024 ? 1024 : max_tex >= 512 ? 512 : 256;
        const char* atlas = getenv("TW_GL_ATLAS");     /* to measure */
        if (atlas && atoi(atlas) >= 64 && atoi(atlas) <= atlas_size_)
            atlas_size_ = atoi(atlas);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DITHER);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (!dbl_) {
            glDrawBuffer(GL_FRONT);
            glReadBuffer(GL_FRONT);
        }
        glGenTextures(1, &atlas_);
        glBindTexture(GL_TEXTURE_2D, atlas_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        /* The atlas is GL_ALPHA, except on IMPACT, which refuses it
         * (GL_INVALID_VALUE): GL_LUMINANCE_ALPHA there, luminance 255.
         * TW_GL_ATLASFMT=alpha|la|rgba chooses, to try. */
        atlas_fmt_ = impact_ ? GL_LUMINANCE_ALPHA : GL_ALPHA;
        /* The atlas is kept here and loaded whole (glTexImage2D) before a
         * frame's glyphs when glyphs were added: IMPACT does not show
         * glTexSubImage2D changes (a High IMPACT drew no text), and glyphs
         * are added only while a screen warms up, so whole loads cost
         * nothing to speak of anywhere. IMPACT gets a smaller atlas. */
        if (impact_ && atlas_size_ > 512 && !getenv("TW_GL_ATLAS"))
            atlas_size_ = 512;
        const char* af = getenv("TW_GL_ATLASFMT");
        if (af && strcmp(af, "alpha") == 0)
            atlas_fmt_ = GL_ALPHA;
        else if (af && strcmp(af, "la") == 0)
            atlas_fmt_ = GL_LUMINANCE_ALPHA;
        else if (af && strcmp(af, "rgba") == 0)
            atlas_fmt_ = GL_RGBA;
        atlas_bpp_ = atlas_fmt_ == GL_RGBA ? 4 : atlas_fmt_ == GL_LUMINANCE_ALPHA ? 2 : 1;
        GLenum fmt = atlas_fmt_;
        size_t bytes = (size_t)atlas_size_ * (size_t)atlas_size_ * (size_t)atlas_bpp_;
        atlas_cpu_ = (unsigned char*)calloc(bytes, 1);
        if (!atlas_cpu_)
            return fail("no memory for the glyph atlas");
        glTexImage2D(GL_TEXTURE_2D, 0, (GLint)fmt, atlas_size_, atlas_size_, 0, fmt,
                     GL_UNSIGNED_BYTE, atlas_cpu_);
        GLenum e = glGetError();
        if (e != GL_NO_ERROR) {
            char m[80];
            sprintf(m, "the %dx%d glyph atlas was refused (GL error 0x%x)", atlas_size_, atlas_size_,
                    (unsigned)e);
            return fail(m);
        }
        cache_.set_display(dpy, win);
        cache_.configure(fonts, fonts->cell_width(), fonts->cell_height(), fonts->ascent());
        font_serial_ = fonts->serial();
        return true;
    }

    void set_geometry(const Geometry& g)
    {
        bool cell_changed = g.cell_w != g_.cell_w || g.cell_h != g_.cell_h || g.ascent != g_.ascent;
        g_ = g;
        current();
        if (cell_changed || font_serial_ != fonts_->serial()) {
            cache_.configure(fonts_, g.cell_w, g.cell_h, g.ascent);
            font_serial_ = fonts_->serial();
            reset_atlas();
        }
        if (!rows_ || n_rows_ != g.rows)
            make_rows(g.rows);
        invalidate_rows();
        glViewport(0, 0, g.width, g.height);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, g.width, g.height, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        if (bg_)
            bg_->resize(g.width, g.height, g.cols, g.rows, g.cell_w, g.cell_h, g.margin_x,
                        g.margin_y);
    }

    bool full_frames() const { return dbl_ || bg_ != 0; }
    bool retained() const { return full_frames() && rows_ != 0; }

    void row_begin(int)
    {
        current();
        draw_lists();       /* nothing transient may go into the row */
        recording_ = true;
    }

    void row_end(int y)
    {
        recording_ = false;
        if (y < 0 || y >= n_rows_) {
            draw_lists();
            return;
        }
        RowStore& r = rows_[order_[y]];
        r.fills.swap(fills_);
        r.glyphs.swap(glyphs_);
        r.solids.swap(solids_);
        fills_.clear();
        glyphs_.clear();
        solids_.clear();
        free_ops(r.ops, r.n_ops);
        free(r.ops);
        r.ops = ops_;
        r.n_ops = n_ops_;
        ops_ = 0;
        n_ops_ = ops_cap_ = 0;
        r.y = y;
        r.epoch = atlas_epoch_;
        r.stable = 0;
        r.listed = false;
    }

    bool take_full_request()
    {
        bool r = full_request_;
        full_request_ = false;
        return r;
    }

    int animation_ms() const
    {
#if defined(__sgi)
        return bg_ ? 100 : 0;
#else
        return bg_ ? 33 : 0;
#endif
    }

    bool set_background(const char* name)
    {
        current();
        /* Retained or not may change with it: every row again. */
        full_request_ = true;
        invalidate_rows();
        delete bg_;
        bg_ = 0;
        bg_name_[0] = 0;
        if (!name || !*name || strcmp(name, "none") == 0)
            return true;
        bg_ = create_background(name, g_.width, g_.height, impact_, 1.0f);
        if (!bg_)
            return false;
        strncpy(bg_name_, name, sizeof(bg_name_) - 1);
        bg_name_[sizeof(bg_name_) - 1] = 0;
        bg_->resize(g_.width, g_.height, g_.cols, g_.rows, g_.cell_w, g_.cell_h, g_.margin_x,
                    g_.margin_y);
        last_anim_ = now_ms();
        return true;
    }

    void begin_frame(uint32_t bg)
    {
        current();
        default_bg_ = bg;
        whole_ = full_frames();
        if (!whole_)
            return;
        glClearColor((float)((bg >> 16) & 255) / 255.0f, (float)((bg >> 8) & 255) / 255.0f,
                     (float)(bg & 255) / 255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (bg_) {
            long t = now_ms();
            float dt = (float)(t - last_anim_) / 1000.0f;
            if (dt > 0.1f)
                dt = 0.1f;
            last_anim_ = t;
            bg_->update(dt);
            glDisable(GL_TEXTURE_2D);
            glEnable(GL_BLEND);
            bg_->draw();
        }
    }

    void fill_px(int x, int y, int w, int h, uint32_t rgb)
    {
        if (w <= 0 || h <= 0)
            return;
        /* A whole frame starts cleared to the window's colour (or with
         * the animated background, which shows through): no need to
         * paint it again. On IMPACT that was a third of a frame. */
        if (whole_ && rgb == default_bg_)
            return;
        add(fills_, x, y, w, h, rgb);
    }

    void scroll(int top, int bottom, int count)
    {
        if (retained()) {
            /* Rows move, and what they keep with them. Rows that come in
             * are empty until the Surface sends them. */
            int n = bottom - top + 1;
            int k = count < 0 ? -count : count;
            if (k == 0 || top < 0 || bottom >= n_rows_)
                return;
            if (k > n)
                k = n;
            int* tmp = (int*)malloc(sizeof(int) * (size_t)n);
            if (!tmp)
                return;
            for (int i = 0; i < n; i++) {
                int from = count > 0 ? (i + k) % n : (i - k + n) % n;
                tmp[i] = order_[top + from];
            }
            for (int i = 0; i < n; i++) {
                order_[top + i] = tmp[i];
                bool came_in = count > 0 ? i >= n - k : i < k;
                if (came_in) {
                    rows_[tmp[i]].epoch = -1;
                    rows_[tmp[i]].listed = false;
                }
            }
            free(tmp);
            return;
        }
        if (whole_)
            return;             /* the whole frame is drawn anyway */
        int n = bottom - top + 1;
        int k = count < 0 ? -count : count;
        if (k >= n || k == 0)
            return;
        current();
        draw_lists();
        int ch = g_.cell_h;
        int x = g_.margin_x, w = g_.cols * g_.cell_w, h = (n - k) * ch;
        int src = g_.margin_y + (count > 0 ? top + k : top) * ch;
        int dst = g_.margin_y + (count > 0 ? top : top + k) * ch;
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_BLEND);
        glRasterPos2i(x, dst + h);
        glCopyPixels(x, g_.height - (src + h), w, h, GL_COLOR);
    }

    void draw_run(int x, int y, const CellData* cells, int n, const ClusterLookup* clusters)
    {
        const CellStyle& st = cells[0].style;
        int px = g_.margin_x + x * g_.cell_w;
        int py = g_.margin_y + y * g_.cell_h;
        bool keep_bg = (st.flags & S_DEFAULT_BG) && bg_ && whole_;
        if (!keep_bg)
            add(fills_, px, py, n * g_.cell_w, g_.cell_h, st.bg);
        for (int i = 0; i < n; i++) {
            const CellData& c = cells[i];
            if (c.width == 0 || (st.flags & S_INVISIBLE) || c.cp == 0 || c.cp == ' ')
                continue;
            int cw = c.width == 2 ? 2 : 1;
            int cx = px + i * g_.cell_w;
            if (builtin_has(c.cp) && !c.cluster) {
                builtin(c.cp, cx, py, cw * g_.cell_w, st);
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
            if (!glyph(face, cps, ncp, cw == 2, cx, py, st.fg))
                missing(cx, py, cw, st.fg);
        }
        decorate(px, py, n, st);
    }

    /* Image runs go out at once with glDrawPixels (GL 1.1, no texture
     * size limits), after what is queued so far. Over an animated
     * background transparent pixels let it through. */
    void draw_image(int x, int y, const CellData* cells, int n, const ImageData& img)
    {
        int w = n * g_.cell_w, h = g_.cell_h;
        int px0 = g_.margin_x + x * g_.cell_w;
        int py0 = g_.margin_y + y * g_.cell_h;
        if (px0 < 0 || py0 < 0 || px0 + w > g_.width || py0 + h > g_.height)
            return;
        /* Over an animated background transparent pixels let it through. */
        bool blend = full_frames() && bg_ && cells[0].style.bg == default_bg_;
        unsigned char* rgba = (unsigned char*)malloc((size_t)w * (size_t)h * 4);
        uint32_t* line = (uint32_t*)malloc((size_t)w * 4);
        if (!rgba || !line || !op_slot()) {
            free(rgba);
            free(line);
            return;
        }
        for (int dy = 0; dy < h; dy++) {
            if (blend)
                image_row(img, cells[0].image_col, cells[0].image_row, n, g_.cell_w, g_.cell_h,
                          dy, line);
            else
                image_row_over(img, cells[0].image_col, cells[0].image_row, n, g_.cell_w,
                               g_.cell_h, dy, cells[0].style.bg, line);
            unsigned char* o = rgba + (size_t)dy * (size_t)w * 4;
            for (int k = 0; k < w; k++, o += 4) {
                uint32_t p = line[k];
                o[0] = (unsigned char)(p >> 16);
                o[1] = (unsigned char)(p >> 8);
                o[2] = (unsigned char)p;
                o[3] = blend ? (unsigned char)(p >> 24) : 255;
            }
        }
        free(line);
        ImageOp& op = ops_[n_ops_++];
        op.x = px0;
        op.y = py0;
        op.w = w;
        op.h = h;
        op.blend = blend;
        op.rgba = rgba;
    }

    void draw_cursor(const Cursor& c, const CellData& under, uint32_t col, bool focused,
                     const ClusterLookup* clusters)
    {
        int cw = c.wide ? 2 : 1;
        int px = g_.margin_x + c.x * g_.cell_w;
        int py = g_.margin_y + c.y * g_.cell_h;
        int w = cw * g_.cell_w, h = g_.cell_h;
        if (!focused) {
            add(solids_, px, py, w, 1, col);
            add(solids_, px, py + h - 1, w, 1, col);
            add(solids_, px, py, 1, h, col);
            add(solids_, px + w - 1, py, 1, h, col);
            return;
        }
        if (c.shape == CURSOR_UNDERLINE) {
            int t = h >= 16 ? 2 : 1;
            add(solids_, px, py + h - t, w, t, col);
            return;
        }
        if (c.shape == CURSOR_BAR) {
            add(solids_, px, py, g_.cell_w >= 12 ? 2 : 1, h, col);
            return;
        }
        CellData cell[2];
        cell[0] = under;
        cell[0].style.fg = under.style.bg == col ? under.style.fg : under.style.bg;
        if (cell[0].style.fg == col)
            cell[0].style.fg = 0xffffff ^ col;
        cell[0].style.bg = col;
        cell[0].style.flags &= (uint16_t)~(S_INVISIBLE | S_UNDERLINE | S_DOUBLE_UNDERLINE |
                                           S_CURLY_UNDERLINE | S_STRIKE | S_OVERLINE |
                                           S_DEFAULT_BG);
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
        current();
        if (retained()) {
            for (int pass = PASS_FILL; pass <= PASS_SOLID; pass++)
                replay(pass);

        }
        /* What is not in a row: the cursor, and everything when not
         * retained. */
        draw_lists();
        /* TW_GL_NOSWAP: never show the back buffer, so a benchmark times
         * the drawing and not the wait for the vertical blank. */
        static bool noswap = getenv("TW_GL_NOSWAP") != 0;
        if (dbl_ && noswap)
            glFinish();
        else if (dbl_) {
            glXSwapBuffers(dpy_, win_);
            /* IRIX (VPro) keeps the swap in its command buffer until
             * something flushes it: without this the last frame before
             * the output stops is never shown. */
            glFlush();
        }
        else
            glFlush();
    }

private:
    void current()
    {
        if (glXGetCurrentContext() != ctx_ || glXGetCurrentDrawable() != win_)
            glXMakeCurrent(dpy_, win_, ctx_);
    }

    void add(Quads& list, int x, int y, int w, int h, uint32_t rgb)
    {
        Quad* q = list.add();
        if (!q)
            return;
        q->x = x;
        q->y = y;
        q->w = w;
        q->h = h;
        q->rgb = rgb;
        q->u0 = q->v0 = q->u1 = q->v1 = 0;
    }

    /* A list of quads. IRIX's immediate mode feeds the graphics FIFO
     * directly and measured fastest there (VPro: 13 ms a 200x60 frame,
     * against 17-18 with client arrays of any vertex format); elsewhere
     * client vertex arrays, one glDrawArrays a list. TW_GL_ARRAYS=0 or 1
     * chooses, to measure. Inside a display list either is compiled. */
    void draw_quads(const Quads& list, bool textured)
    {
        int n = list.size();
        if (!n)
            return;
        if (!use_arrays_ || !arrays(n * 4)) {
            glBegin(GL_QUADS);
            for (int i = 0; i < n; i++) {
                const Quad& q = list[i];
                color(q.rgb);
                if (textured)
                    glTexCoord2f(q.u0, q.v0);
                glVertex2i(q.x, q.y);
                if (textured)
                    glTexCoord2f(q.u1, q.v0);
                glVertex2i(q.x + q.w, q.y);
                if (textured)
                    glTexCoord2f(q.u1, q.v1);
                glVertex2i(q.x + q.w, q.y + q.h);
                if (textured)
                    glTexCoord2f(q.u0, q.v1);
                glVertex2i(q.x, q.y + q.h);
            }
            glEnd();
            return;
        }
        GLint* v = verts_;
        GLubyte* c = colors_;
        GLfloat* t = texs_;
        for (int i = 0; i < n; i++) {
            const Quad& q = list[i];
            v[0] = q.x;         v[1] = q.y;
            v[2] = q.x + q.w;   v[3] = q.y;
            v[4] = q.x + q.w;   v[5] = q.y + q.h;
            v[6] = q.x;         v[7] = q.y + q.h;
            v += 8;
            for (int k = 0; k < 4; k++, c += 4) {
                c[0] = (GLubyte)(q.rgb >> 16);
                c[1] = (GLubyte)(q.rgb >> 8);
                c[2] = (GLubyte)q.rgb;
                c[3] = 255;
            }
            if (textured) {
                t[0] = q.u0; t[1] = q.v0;
                t[2] = q.u1; t[3] = q.v0;
                t[4] = q.u1; t[5] = q.v1;
                t[6] = q.u0; t[7] = q.v1;
                t += 8;
            }
        }
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(2, GL_INT, 0, verts_);
        glColorPointer(4, GL_UNSIGNED_BYTE, 0, colors_);
        if (textured) {
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glTexCoordPointer(2, GL_FLOAT, 0, texs_);
        }
        glDrawArrays(GL_QUADS, 0, n * 4);
        if (textured)
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
    }

    /* Glyphs as glBitmap: no texturing, which boards without texture
     * hardware (the Indy's Newport) do in software, slowly. */
    void draw_bitmaps(const Quads& glyphs)
    {
        uint32_t rgb = 0;
        bool have = false;
        for (int i = 0; i < glyphs.size(); i++) {
            const Quad& q = glyphs[i];
            if (!have || q.rgb != rgb) {
                color(q.rgb);
                rgb = q.rgb;
                have = true;
            }
            /* The raster position is the inked box's bottom left; its
             * rows were stored bottom first, each padded to a byte. */
            glRasterPos2i(q.x, q.y + q.h);
            glBitmap(q.w, q.h, 0, 0, 0, 0, bm_ + q.bm);
        }
    }

    /* Three passes, each with its GL state set once: backgrounds and
     * image runs, then glyphs, then lines and builtins. The order cells
     * need however the runs came in; rows do not overlap, so a frame of
     * retained rows can run each pass over all of them. */
    enum { PASS_FILL, PASS_GLYPH, PASS_SOLID };

    void pass_begin(int pass)
    {
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_BLEND);
        if (pass == PASS_GLYPH && !bitmap_text_) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, atlas_);
            if (atlas_dirty_) {
                glTexImage2D(GL_TEXTURE_2D, 0, (GLint)atlas_fmt_, atlas_size_, atlas_size_, 0,
                             atlas_fmt_, GL_UNSIGNED_BYTE, atlas_cpu_);
                atlas_dirty_ = false;
            }
            /* The masks are 1-bit: an alpha test draws the same pixels as
             * blending without reading the frame buffer (IMPACT textured
             * and blended glyphs at 19 Mpixels/s). TW_GL_BLEND=1 blends. */
            if (blend_glyphs_) {
                glEnable(GL_BLEND);
            } else {
                glAlphaFunc(GL_GREATER, 0.5f);
                glEnable(GL_ALPHA_TEST);
            }
        }
    }

    void pass_end(int pass)
    {
        if (pass == PASS_GLYPH && !bitmap_text_) {
            glDisable(GL_TEXTURE_2D);
            glDisable(GL_BLEND);
            glDisable(GL_ALPHA_TEST);
        }
    }

    /* One pass of one set, in the state pass_begin set. */
    void draw_pass(int pass, const Quads& fills, const ImageOp* ops, int n_ops,
                   const Quads& glyphs, const Quads& solids)
    {
        if (pass == PASS_FILL) {
            draw_quads(fills, false);
            for (int i = 0; i < n_ops; i++) {
                const ImageOp& op = ops[i];
                if (op.blend)
                    glEnable(GL_BLEND);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glPixelZoom(1.0f, -1.0f);
                glRasterPos2i(op.x, op.y);
                glDrawPixels(op.w, op.h, GL_RGBA, GL_UNSIGNED_BYTE, op.rgba);
                glPixelZoom(1.0f, 1.0f);
                if (op.blend)
                    glDisable(GL_BLEND);
            }
        } else if (pass == PASS_GLYPH) {
            if (bitmap_text_)
                draw_bitmaps(glyphs);
            else
                draw_quads(glyphs, true);
        } else {
            draw_quads(solids, false);
        }
    }

    void draw_set(const Quads& fills, const ImageOp* ops, int n_ops, const Quads& glyphs,
                  const Quads& solids)
    {
        for (int pass = PASS_FILL; pass <= PASS_SOLID; pass++) {
            pass_begin(pass);
            draw_pass(pass, fills, ops, n_ops, glyphs, solids);
            pass_end(pass);
        }
    }

    /* Draw what is queued and forget it. */
    void draw_lists()
    {
        draw_set(fills_, ops_, n_ops_, glyphs_, solids_);
        free_ops(ops_, n_ops_);
        n_ops_ = 0;
        fills_.clear();
        glyphs_.clear();
        solids_.clear();
    }

    static void free_ops(ImageOp* ops, int n)
    {
        for (int i = 0; i < n; i++)
            free(ops[i].rgba);
    }

    bool op_slot()
    {
        if (n_ops_ < ops_cap_)
            return true;
        int cap = ops_cap_ ? ops_cap_ * 2 : 16;
        ImageOp* p = (ImageOp*)realloc(ops_, sizeof(ImageOp) * (size_t)cap);
        if (!p)
            return false;
        ops_ = p;
        ops_cap_ = cap;
        return true;
    }

    bool arrays(int vertices)
    {
        if (vertices <= vert_cap_)
            return true;
        int cap = vert_cap_ ? vert_cap_ : 4096;
        while (cap < vertices)
            cap *= 2;
        GLint* v = (GLint*)realloc(verts_, sizeof(GLint) * 2 * (size_t)cap);
        if (v)
            verts_ = v;
        GLubyte* c = (GLubyte*)realloc(colors_, 4 * (size_t)cap);
        if (c)
            colors_ = c;
        GLfloat* t = (GLfloat*)realloc(texs_, sizeof(GLfloat) * 2 * (size_t)cap);
        if (t)
            texs_ = t;
        if (!v || !c || !t)
            return false;
        vert_cap_ = cap;
        return true;
    }

    /* Retained rows (full-frame modes). Each screen row keeps the quads
     * and image runs it was last sent with, in the coordinates of the row
     * it was sent at; scrolling moves rows between screen positions and a
     * translation puts them in place. Every frame replays them all, so
     * the model is not walked again for rows that did not change. A row
     * unchanged for a few frames is compiled into a display list, which
     * replays cheaper; one that keeps changing is never compiled (on VPro
     * compiling cost three times a direct draw). TW_GL_LISTS=0 turns the
     * lists off. */
    struct RowStore {
        Quads fills, glyphs, solids;
        ImageOp* ops;
        int n_ops;
        int y;              /* the row its coordinates are for */
        int epoch;          /* atlas_epoch_ it was made under; -1: empty */
        int stable;         /* frames since it changed */
        GLuint list;        /* three lists, one a pass, from here */
        bool listed;        /* the lists hold it */
        RowStore() : ops(0), n_ops(0), y(0), epoch(-1), stable(0), list(0), listed(false) {}
        ~RowStore() { free_ops(ops, n_ops); free(ops); }
    };

    void make_rows(int rows)
    {
        drop_rows();
        if (rows <= 0)
            return;
        rows_ = new RowStore[rows];
        order_ = (int*)malloc(sizeof(int) * (size_t)rows);
        list_base_ = glGenLists(rows * 3);
        if (!order_) {
            drop_rows();
            return;
        }
        n_rows_ = rows;
        for (int i = 0; i < rows; i++) {
            order_[i] = i;
            rows_[i].list = list_base_ ? list_base_ + (GLuint)(i * 3) : 0;
        }
    }

    void drop_rows()
    {
        if (list_base_)
            glDeleteLists(list_base_, n_rows_ * 3);
        delete[] rows_;
        free(order_);
        rows_ = 0;
        order_ = 0;
        n_rows_ = 0;
        list_base_ = 0;
    }

    void invalidate_rows()
    {
        for (int i = 0; i < n_rows_; i++) {
            rows_[i].epoch = -1;
            rows_[i].listed = false;
        }
    }

    /* One pass of every retained row: from its list, or drawn (and,
     * once it has stayed the same a few frames, compiled). */
    void replay(int pass)
    {
        pass_begin(pass);
        for (int y = 0; y < n_rows_; y++) {
            RowStore& r = rows_[order_[y]];
            if (r.epoch != atlas_epoch_)
                continue;
            bool moved = r.y != y;
            if (moved) {
                glPushMatrix();
                glTranslatef(0.0f, (float)((y - r.y) * g_.cell_h), 0.0f);
            }
            if (r.listed) {
                glCallList(r.list + (GLuint)pass);
            } else {
                draw_pass(pass, r.fills, r.ops, r.n_ops, r.glyphs, r.solids);
                if (pass == PASS_SOLID && use_lists_ && r.list && ++r.stable >= 3) {
                    for (int p = PASS_FILL; p <= PASS_SOLID; p++) {
                        glNewList(r.list + (GLuint)p, GL_COMPILE);
                        draw_pass(p, r.fills, r.ops, r.n_ops, r.glyphs, r.solids);
                        glEndList();
                    }
                    r.listed = true;
                }
            }
            if (moved)
                glPopMatrix();
        }
        pass_end(pass);
    }

    void reset_atlas()
    {
        int sw = 2 * (g_.cell_w > 0 ? g_.cell_w : 8), sh = g_.cell_h > 0 ? g_.cell_h : 16;
        slot_cols_ = atlas_size_ / sw;
        slot_rows_ = atlas_size_ / sh;
        next_slot_ = 0;
        bm_n_ = 0;
        cache_gen_ = cache_.generation();
        /* Compiled rows point into the old atlas. */
        atlas_epoch_++;
        if (recording_)
            full_request_ = true;
    }

    /* Put a glyph's mask into the atlas (or, for bitmap text, bm_);
     * false when it is full. */
    bool upload(CachedGlyph* gl)
    {
        if (cache_gen_ != cache_.generation())
            reset_atlas();
        /* Only the inked box is drawn: a letter covers half its cell or
         * less, and pixels cost on IMPACT and the Indy. */
        {
            const uint8_t* bits = cache_.bits(gl);
            int x0 = gl->w, y0 = gl->h, x1 = -1, y1 = -1;
            for (int y = 0; y < gl->h; y++)
                for (int x = 0; x < gl->w; x++)
                    if (bits[y * gl->stride + x / 8] & (0x80 >> (x % 8))) {
                        if (x < x0) x0 = x;
                        if (x > x1) x1 = x;
                        if (y < y0) y0 = y;
                        if (y > y1) y1 = y;
                    }
            bool ink = x1 >= 0;
            gl->ink_x = (int16_t)(ink ? x0 : 0);
            gl->ink_y = (int16_t)(ink ? y0 : 0);
            gl->ink_w = (int16_t)(ink ? x1 - x0 + 1 : 0);      /* 0: nothing to draw */
            gl->ink_h = (int16_t)(ink ? y1 - y0 + 1 : 0);
        }
        if (bitmap_text_) {
            /* The inked box only, flipped, rows padded to bytes: IRIX's
             * glBitmap ignores GL_UNPACK_SKIP_PIXELS, so the box cannot be
             * cut out of the whole mask at draw time. */
            int bw = gl->ink_w, bh = gl->ink_h, bstride = (bw + 7) / 8;
            size_t need = (size_t)bstride * (size_t)(bh > 0 ? bh : 1);
            if (bm_n_ + need > bm_cap_) {
                size_t cap = bm_cap_ ? bm_cap_ * 2 : 65536;
                while (cap < bm_n_ + need)
                    cap *= 2;
                unsigned char* p = (unsigned char*)realloc(bm_, cap);
                if (!p)
                    return false;
                bm_ = p;
                bm_cap_ = cap;
            }
            const uint8_t* bits = cache_.bits(gl);
            unsigned char* out = bm_ + bm_n_;
            memset(out, 0, need);
            for (int y = 0; y < bh; y++) {
                const uint8_t* src = bits + (size_t)(gl->ink_y + bh - 1 - y) * (size_t)gl->stride;
                unsigned char* dst = out + (size_t)y * (size_t)bstride;
                for (int x = 0; x < bw; x++) {
                    int sx = gl->ink_x + x;
                    if (src[sx / 8] & (0x80 >> (sx % 8)))
                        dst[x / 8] |= (unsigned char)(0x80 >> (x % 8));
                }
            }
            gl->handle_a = (int)bm_n_;
            gl->handle_b = 0;
            bm_n_ += need;
            return true;
        }
        if (next_slot_ >= slot_cols_ * slot_rows_)
            return false;
        int slot = next_slot_++;
        int sx = (slot % slot_cols_) * 2 * g_.cell_w, sy = (slot / slot_cols_) * g_.cell_h;
        int w = gl->w, h = gl->h, bpp = atlas_bpp_;
        unsigned char* buf = (unsigned char*)malloc((size_t)(w * h * bpp));
        if (!buf)
            return false;
        const uint8_t* bits = cache_.bits(gl);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                unsigned char a = (bits[y * gl->stride + x / 8] & (0x80 >> (x % 8))) ? 255 : 0;
                unsigned char* o = buf + (size_t)(y * w + x) * (size_t)bpp;
                for (int k = 0; k < bpp - 1; k++)
                    o[k] = 255;         /* white, modulated by the run's colour */
                o[bpp - 1] = a;
            }
        for (int y = 0; y < h; y++)
            memcpy(atlas_cpu_ + ((size_t)(sy + y) * (size_t)atlas_size_ + (size_t)sx) * (size_t)bpp,
                   buf + (size_t)y * (size_t)w * (size_t)bpp, (size_t)w * (size_t)bpp);
        atlas_dirty_ = true;
        free(buf);
        gl->handle_a = sx;
        gl->handle_b = sy;
        return true;
    }

    bool glyph(int face, const uint32_t* cps, int n, bool wide, int x, int y, uint32_t fg)
    {
        CachedGlyph* gl = cache_.get(face, cps, n, wide);
        if (!gl || !gl->found)
            return false;
        if (cache_gen_ != cache_.generation() || gl->handle_a < 0) {
            if (!upload(gl)) {
                /* The atlas is full: draw what uses it now, start over.
                 * A row being recorded cannot be drawn half: the Surface
                 * sends everything again next frame. */
                if (recording_) {
                    fills_.clear();
                    glyphs_.clear();
                    solids_.clear();
                    full_request_ = true;
                } else {
                    draw_lists();
                }
                cache_.clear();
                reset_atlas();
                gl = cache_.get(face, cps, n, wide);
                if (!gl || !gl->found || !upload(gl))
                    return false;
            }
        }
        if (gl->ink_w <= 0 || gl->ink_h <= 0)
            return true;            /* nothing to draw */
        Quad* q = glyphs_.add();
        if (!q)
            return true;
        float s = (float)atlas_size_;
        q->x = x + gl->ink_x;
        q->y = y + gl->ink_y;
        q->w = gl->ink_w;
        q->h = gl->ink_h;
        q->rgb = fg;
        q->u0 = (gl->handle_a + gl->ink_x) / s;
        q->v0 = (gl->handle_b + gl->ink_y) / s;
        q->u1 = (gl->handle_a + gl->ink_x + gl->ink_w) / s;
        q->v1 = (gl->handle_b + gl->ink_y + gl->ink_h) / s;
        q->bm = gl->handle_a;
        return true;
    }

    void missing(int x, int y, int cw, uint32_t fg)
    {
        int w = cw * g_.cell_w, h = g_.cell_h;
        add(solids_, x + 1, y + 1, w - 2, 1, fg);
        add(solids_, x + 1, y + h - 2, w - 2, 1, fg);
        add(solids_, x + 1, y + 1, 1, h - 2, fg);
        add(solids_, x + w - 2, y + 1, 1, h - 2, fg);
    }

    void builtin(uint32_t cp, int x, int y, int w, const CellStyle& st)
    {
        GlyphRect r[16];
        int shade;
        int n = builtin_rects(cp, w, g_.cell_h, r, 16, &shade);
        if (shade) {
            add(fills_, x, y, w, g_.cell_h, mix(st.fg, st.bg, shade));
            return;
        }
        for (int i = 0; i < n; i++)
            add(solids_, x + r[i].x, y + r[i].y, r[i].w, r[i].h, st.fg);
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
            add(solids_, px, uy, w, t, c);
        }
        if (st.flags & S_DOUBLE_UNDERLINE) {
            add(solids_, px, py + g_.cell_h - 1 - 2 * t, w, t, c);
            add(solids_, px, py + g_.cell_h - t, w, t, c);
        }
        if (st.flags & S_STRIKE)
            add(solids_, px, py + g_.ascent * 2 / 3, w, t, c);
        if (st.flags & S_OVERLINE)
            add(solids_, px, py, w, t, c);
        if (st.flags & S_LEFT_LINE)
            add(solids_, px, py, 1, g_.cell_h, c);
    }

    Display* dpy_;
    Window win_;
    GLXContext ctx_;
    bool dbl_, impact_;
    GLenum atlas_fmt_;
    int atlas_bpp_;
    unsigned char* atlas_cpu_;  /* the atlas, loaded whole when dirty */
    bool atlas_dirty_;
    const Fonts* fonts_;
    int font_serial_;
    Geometry g_;
    GlyphCache cache_;
    GLuint atlas_;
    int atlas_size_, slot_cols_, slot_rows_, next_slot_;
    int cache_gen_;
    Background* bg_;
    char bg_name_[32];
    long last_anim_;
    uint32_t default_bg_;
    bool whole_;                /* this frame is drawn whole */
    Quads fills_, glyphs_, solids_;
    GLint* verts_;              /* draw_quads' client arrays */
    GLubyte* colors_;
    GLfloat* texs_;
    int vert_cap_;
    bool bitmap_text_;          /* glyphs as glBitmap, not textured quads */
    unsigned char* bm_;         /* flipped glyph masks for glBitmap */
    size_t bm_n_, bm_cap_;
    ImageOp* ops_;              /* image runs queued with the lists */
    int n_ops_, ops_cap_;
    bool use_arrays_;
    RowStore* rows_;
    int* order_;                /* screen row -> rows_ index */
    int n_rows_;
    bool use_lists_;
    bool blend_glyphs_;
    GLuint list_base_;
    int atlas_epoch_;
    bool recording_;
    bool full_request_;
};

} /* namespace */

Renderer* create_gl_renderer()
{
    return new GLRender();
}

/* A GLX visual: double-buffered with 8 bits a channel, else single-
 * buffered with 8 bits (the Indy's 24-bit visual), else whatever RGBA. */
bool gl_choose_visual(Display* dpy, int screen, XVisualInfo* out)
{
    static int attrs[4][12] = {
        { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, None },
        { GLX_RGBA, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, None },
        { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 1, GLX_GREEN_SIZE, 1, GLX_BLUE_SIZE, 1, None },
        { GLX_RGBA, None }
    };
    int base, event;
    if (!glXQueryExtension(dpy, &base, &event))
        return false;
    /* TW_GL_SINGLE: a single-buffered visual (damage only), to measure. */
    for (int i = getenv("TW_GL_SINGLE") ? 1 : 0; i < 4; i++) {
        XVisualInfo* vi = glXChooseVisual(dpy, screen, attrs[i]);
        if (vi) {
            *out = *vi;
            XFree(vi);
            return true;
        }
    }
    return false;
}

} /* namespace tw */

#else

namespace tw {

Renderer* create_gl_renderer()
{
    return 0;
}

bool gl_choose_visual(Display*, int, XVisualInfo*)
{
    return false;
}

} /* namespace tw */

#endif
