/*
 * ttrv's GL renderer (ttrv/DESIGN.md section 5.2): an image drawn from
 * textures, so zoom and pan cost the GPU a few quads and nothing more.
 *
 * OpenGL 1.1 is the floor (VPro's 1.2, Mesa's anything). The image goes
 * up once, cut in power-of-two tiles no larger than GL_MAX_TEXTURE_SIZE
 * (1.1 has no other sizes) that overlap their neighbours by two pixels,
 * so filtering across a seam reads the same pixels either side. Each
 * tile has mipmaps, made by the GPU where it can (1.4) and by a box
 * filter here where it cannot, and shrinking is GL_LINEAR_MIPMAP_LINEAR.
 * Enlarging is bilinear below twice, a Catmull-Rom shader where there is
 * GLSL (Linux), and the nearest pixel from twice, as the soft renderer.
 *
 * Textures fit a budget: a larger image goes up reduced to it, and a
 * zoom that asks more pixels than the reduced one has is drawn by the
 * caller (image() says so) from a frame the soft renderer made. Two
 * named images stay up (the viewer's fitted one and its full one).
 *
 * Built without TTR_HAVE_GL, init() always fails.
 */
#ifndef TTRV_GLPAINT_H
#define TTRV_GLPAINT_H

#include "../../ti/ti.h"

#include "core/str.h"

#include <stddef.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace ttrv {

class GLPaint {
public:
    GLPaint();
    ~GLPaint();

    /* A GLX visual for the window (tw's choice: double-buffered 8 bits a
     * channel, else single-buffered), false if the display has no GL. */
    static bool choose_visual(Display* dpy, int screen, XVisualInfo* out);

    /* A context for win, a window in vi's visual. On failure 'why' says
     * why. */
    bool init(Display* dpy, Window win, const XVisualInfo& vi, char* why, size_t n);
    /* Direct rendering with textures in hardware: what "auto" asks. */
    bool accelerated() const { return accelerated_; }
    /* "VPRO/B/32 (GL 1.2)", for the About box. */
    const char* name() const { return name_.c_str(); }
    /* The bytes textures may take (0: this board's default). */
    void set_budget(long bytes);
    void set_font(XFontStruct* font);
    /* Drop every texture (the image is gone). */
    void forget();

    /* One frame: begin, then images and text, then end. */
    void begin(int w, int h, unsigned long bg);
    /* im at 'zoom' window pixels per image pixel, the window's top left
     * at image position (x0, y0), over the background. 'key' names the
     * image so it goes up once ("": a new one every call, kept apart
     * from the named one). False if it was not drawn: out of texture
     * memory, or reduced to the budget below what the zoom shows. */
    bool image(const ti::Image& im, const char* key, double zoom, double x0, double y0);
    void text(int x, int y, const char* s, int len, unsigned long rgb);
    /* The frame drawn so far (before end()), for tests. */
    bool read(ti::Image* out);
    void end();
    /* Wait for the GPU (timing). */
    void finish();

    struct Tile {
        unsigned int id;
        int x, y, w, h;     /* the pixels it shows, in the uploaded image */
        int tw, th;         /* the texture's size */
    };
    struct Set {
        Tile* tiles;
        int count;
        int w, h;           /* the uploaded image's size */
        double scale;       /* uploaded pixels per image pixel (<= 1) */
        ttr::Str key;
        unsigned long bg;
        bool failed;
        unsigned long used;     /* when last drawn (clock_) */
        Set() : tiles(0), count(0), w(0), h(0), scale(1), bg(0), failed(false), used(0) {}
    };

private:
    GLPaint(const GLPaint&);
    GLPaint& operator=(const GLPaint&);

    bool upload(Set* s, const ti::Image& im, const char* key);
    void release(Set* s);
    void draw(const Set& s, double zoom, double x0, double y0);
    void make_shader();

    Display* dpy_;
    Window win_;
    void* ctx_;
    bool dbl_, accelerated_, edge_clamp_, gen_mipmap_;
    int max_tex_, w_, h_;
    long budget_;
    unsigned long bg_;
    ttr::Str name_;
    unsigned int font_lists_;
    Font font_;
    Set named_[2], frame_;
    unsigned long clock_;
    unsigned int prog_;     /* the bicubic program, 0 if none */
    int u_size_;
    struct Api;
    Api* api_;
};

} /* namespace ttrv */

#endif
