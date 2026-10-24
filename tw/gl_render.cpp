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
    float u0, v0, u1, v1;
};

/* A growing array of quads, kept between frames. */
class Quads {
public:
    Quads() : q_(0), n_(0), cap_(0) {}
    ~Quads() { free(q_); }
    void clear() { n_ = 0; }
    int size() const { return n_; }
    const Quad& operator[](int i) const { return q_[i]; }
    Quad* add()
    {
        if (n_ == cap_) {
            int nc = cap_ ? cap_ * 2 : 1024;
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
        : dpy_(0), win_(0), ctx_(0), dbl_(false), impact_(false), fonts_(0), font_serial_(-1),
          atlas_(0), atlas_size_(0), slot_cols_(0), slot_rows_(0), next_slot_(0),
          cache_gen_(-1), bg_(0), last_anim_(0), default_bg_(0), whole_(false), pix_(0), pix_cap_(0)
    {
        memset(&g_, 0, sizeof(g_));
        bg_name_[0] = 0;
    }
    ~GLRender()
    {
        free(pix_);
        if (ctx_) {
            current();
            delete bg_;
            if (atlas_)
                glDeleteTextures(1, &atlas_);
            glXMakeCurrent(dpy_, None, 0);
            glXDestroyContext(dpy_, ctx_);
        }
    }

    const char* name() const { return "gl"; }

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
            return false;
        int use_gl = 0, rgba = 0, dbl = 0;
        glXGetConfig(dpy, vi, GLX_USE_GL, &use_gl);
        glXGetConfig(dpy, vi, GLX_RGBA, &rgba);
        glXGetConfig(dpy, vi, GLX_DOUBLEBUFFER, &dbl);
        if (use_gl && rgba)
            ctx_ = glXCreateContext(dpy, vi, 0, True);
        XFree(vi);
        if (!ctx_)
            return false;
        dbl_ = dbl != 0;
        if (!glXMakeCurrent(dpy, win, ctx_))
            return false;
        const char* r = (const char*)glGetString(GL_RENDERER);
        impact_ = r && strstr(r, "IMPACT");
        GLint max_tex = 256;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
        atlas_size_ = max_tex >= 1024 ? 1024 : max_tex >= 512 ? 512 : 256;
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
        GLenum fmt = impact_ ? GL_LUMINANCE_ALPHA : GL_ALPHA;
        size_t bytes = (size_t)atlas_size_ * (size_t)atlas_size_ * (impact_ ? 2 : 1);
        unsigned char* zero = (unsigned char*)calloc(bytes, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, (GLint)fmt, atlas_size_, atlas_size_, 0, fmt,
                     GL_UNSIGNED_BYTE, zero);
        free(zero);
        if (glGetError() != GL_NO_ERROR)
            return false;
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
        /* Over an animated background, the window's own colour is the
         * background showing through. */
        if (bg_ && whole_ && rgb == default_bg_)
            return;
        add(fills_, x, y, w, h, rgb);
    }

    void scroll(int top, int bottom, int count)
    {
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
        size_t need = (size_t)w * (size_t)(h + 1) * 4;
        if (need > pix_cap_) {
            unsigned char* p = (unsigned char*)realloc(pix_, need);
            if (!p)
                return;
            pix_ = p;
            pix_cap_ = need;
        }
        current();
        draw_lists();
        bool blend = whole_ && bg_ && cells[0].style.bg == default_bg_;
        uint32_t* line = (uint32_t*)(pix_ + (size_t)w * (size_t)h * 4);
        for (int dy = 0; dy < h; dy++) {
            if (blend)
                image_row(img, cells[0].image_col, cells[0].image_row, n, g_.cell_w, g_.cell_h,
                          dy, line);
            else
                image_row_over(img, cells[0].image_col, cells[0].image_row, n, g_.cell_w,
                               g_.cell_h, dy, cells[0].style.bg, line);
            unsigned char* o = pix_ + (size_t)dy * (size_t)w * 4;
            for (int k = 0; k < w; k++, o += 4) {
                uint32_t p = line[k];
                o[0] = (unsigned char)(p >> 16);
                o[1] = (unsigned char)(p >> 8);
                o[2] = (unsigned char)p;
                o[3] = blend ? (unsigned char)(p >> 24) : 255;
            }
        }
        glDisable(GL_TEXTURE_2D);
        if (blend)
            glEnable(GL_BLEND);
        else
            glDisable(GL_BLEND);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelZoom(1.0f, -1.0f);
        glRasterPos2i(px0, py0);
        glDrawPixels(w, h, GL_RGBA, GL_UNSIGNED_BYTE, pix_);
        glPixelZoom(1.0f, 1.0f);
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
        draw_lists();
        if (dbl_)
            glXSwapBuffers(dpy_, win_);
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

    /* Backgrounds, then glyphs, then lines and builtins: the order cells
     * need however the runs came in. */
    void draw_lists()
    {
        if (fills_.size()) {
            glDisable(GL_TEXTURE_2D);
            glDisable(GL_BLEND);
            glBegin(GL_QUADS);
            for (int i = 0; i < fills_.size(); i++) {
                const Quad& q = fills_[i];
                color(q.rgb);
                glVertex2i(q.x, q.y);
                glVertex2i(q.x + q.w, q.y);
                glVertex2i(q.x + q.w, q.y + q.h);
                glVertex2i(q.x, q.y + q.h);
            }
            glEnd();
        }
        if (glyphs_.size()) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, atlas_);
            glEnable(GL_BLEND);
            glBegin(GL_QUADS);
            for (int i = 0; i < glyphs_.size(); i++) {
                const Quad& q = glyphs_[i];
                color(q.rgb);
                glTexCoord2f(q.u0, q.v0);
                glVertex2i(q.x, q.y);
                glTexCoord2f(q.u1, q.v0);
                glVertex2i(q.x + q.w, q.y);
                glTexCoord2f(q.u1, q.v1);
                glVertex2i(q.x + q.w, q.y + q.h);
                glTexCoord2f(q.u0, q.v1);
                glVertex2i(q.x, q.y + q.h);
            }
            glEnd();
            glDisable(GL_TEXTURE_2D);
        }
        if (solids_.size()) {
            glDisable(GL_TEXTURE_2D);
            glDisable(GL_BLEND);
            glBegin(GL_QUADS);
            for (int i = 0; i < solids_.size(); i++) {
                const Quad& q = solids_[i];
                color(q.rgb);
                glVertex2i(q.x, q.y);
                glVertex2i(q.x + q.w, q.y);
                glVertex2i(q.x + q.w, q.y + q.h);
                glVertex2i(q.x, q.y + q.h);
            }
            glEnd();
        }
        fills_.clear();
        glyphs_.clear();
        solids_.clear();
    }

    void reset_atlas()
    {
        int sw = 2 * (g_.cell_w > 0 ? g_.cell_w : 8), sh = g_.cell_h > 0 ? g_.cell_h : 16;
        slot_cols_ = atlas_size_ / sw;
        slot_rows_ = atlas_size_ / sh;
        next_slot_ = 0;
        cache_gen_ = cache_.generation();
    }

    /* Put a glyph's mask into the atlas; false when it is full. */
    bool upload(CachedGlyph* gl)
    {
        if (cache_gen_ != cache_.generation())
            reset_atlas();
        if (next_slot_ >= slot_cols_ * slot_rows_)
            return false;
        int slot = next_slot_++;
        int sx = (slot % slot_cols_) * 2 * g_.cell_w, sy = (slot / slot_cols_) * g_.cell_h;
        int w = gl->w, h = gl->h, bpp = impact_ ? 2 : 1;
        unsigned char* buf = (unsigned char*)malloc((size_t)(w * h * bpp));
        if (!buf)
            return false;
        const uint8_t* bits = cache_.bits(gl);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                unsigned char a = (bits[y * gl->stride + x / 8] & (0x80 >> (x % 8))) ? 255 : 0;
                if (impact_) {
                    buf[(y * w + x) * 2] = 255;
                    buf[(y * w + x) * 2 + 1] = a;
                } else {
                    buf[y * w + x] = a;
                }
            }
        glBindTexture(GL_TEXTURE_2D, atlas_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, sx, sy, w, h, impact_ ? GL_LUMINANCE_ALPHA : GL_ALPHA,
                        GL_UNSIGNED_BYTE, buf);
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
                /* The atlas is full: draw what uses it now, start over. */
                draw_lists();
                cache_.clear();
                reset_atlas();
                gl = cache_.get(face, cps, n, wide);
                if (!gl || !gl->found || !upload(gl))
                    return false;
            }
        }
        Quad* q = glyphs_.add();
        if (!q)
            return true;
        float s = (float)atlas_size_;
        q->x = x;
        q->y = y;
        q->w = gl->w;
        q->h = gl->h;
        q->rgb = fg;
        q->u0 = gl->handle_a / s;
        q->v0 = gl->handle_b / s;
        q->u1 = (gl->handle_a + gl->w) / s;
        q->v1 = (gl->handle_b + gl->h) / s;
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
    unsigned char* pix_;        /* image runs for glDrawPixels */
    size_t pix_cap_;
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
    for (int i = 0; i < 4; i++) {
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
