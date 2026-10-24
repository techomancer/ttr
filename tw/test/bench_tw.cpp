/*
 * tw_bench: frames per second of a renderer on a synthetic screen.
 *
 *   tw_bench [-r core|soft|gl] [-m full|scroll|type|idle] [-dynbg name]
 *            [-g COLSxROWS] [-n frames] [-fn font]
 *
 * full: every cell changes each frame; scroll: one line in per frame
 * (cat); type: one cell per frame; idle: nothing but the cursor (with
 * -dynbg, the animated background's cost). Each frame is waited for
 * (XSync, glFinish) so the time is the drawing's, not the queue's.
 */
#include "../surface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#if defined(TTR_HAVE_GL)
#include <GL/gl.h>
#endif

namespace {

long now_us()
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (long)tv.tv_sec * 1000000L + tv.tv_usec;
}

class NoScheduler : public tw::Scheduler {
public:
    void schedule_frame(int) {}
    void schedule_blink(int) {}
};

enum Mode { M_FULL, M_SCROLL, M_TYPE, M_IDLE };

class BenchSource : public tw::Source {
public:
    BenchSource(int cols, int rows, Mode m)
        : cols_(cols), rows_(rows), mode_(m), seed_(12345), frame_(0), full_(true),
          scrolled_(false), dirty_x_(-1), dirty_y_(-1)
    {
        cp_ = (uint32_t*)malloc((size_t)cols * (size_t)rows * sizeof(uint32_t));
        st_ = (unsigned char*)malloc((size_t)cols * (size_t)rows);
        for (int y = 0; y < rows; y++)
            fill_row(y);
    }
    ~BenchSource()
    {
        free(cp_);
        free(st_);
    }

    /* The next frame's change. */
    void step()
    {
        frame_++;
        dirty_x_ = dirty_y_ = -1;
        scrolled_ = false;
        switch (mode_) {
        case M_FULL:
            for (int y = 0; y < rows_; y++)
                fill_row(y);
            full_ = true;
            break;
        case M_SCROLL:
            memmove(cp_, cp_ + cols_, (size_t)cols_ * (size_t)(rows_ - 1) * sizeof(uint32_t));
            memmove(st_, st_ + cols_, (size_t)cols_ * (size_t)(rows_ - 1));
            fill_row(rows_ - 1);
            scrolled_ = true;
            break;
        case M_TYPE:
            dirty_x_ = frame_ % cols_;
            dirty_y_ = (frame_ / cols_) % rows_;
            cp_[dirty_y_ * cols_ + dirty_x_] = (uint32_t)('a' + frame_ % 26);
            break;
        case M_IDLE:
            break;
        }
    }

    int cols() const { return cols_; }
    int rows() const { return rows_; }
    bool held() const { return false; }
    bool full_damage() const { return full_; }
    int scroll_count() const { return scrolled_ ? 1 : 0; }
    void scroll(int, int* top, int* bottom, int* count) const
    {
        *top = 0;
        *bottom = rows_ - 1;
        *count = 1;
    }
    bool row_dirty(int y, int* lo, int* hi) const
    {
        if (scrolled_ && y == rows_ - 1) {
            *lo = 0;
            *hi = cols_ - 1;
            return true;
        }
        if (y == dirty_y_) {
            *lo = *hi = dirty_x_;
            return true;
        }
        return false;
    }
    void cells(int y, int lo, int hi, tw::CellData* out) const
    {
        static const uint32_t fg[8] = { 0xd0d0d0, 0xff5555, 0x55ff55, 0xffff55,
                                        0x5555ff, 0xff55ff, 0x55ffff, 0xffffff };
        for (int x = lo; x <= hi; x++) {
            tw::CellData& d = out[x - lo];
            memset(&d, 0, sizeof(d));
            d.cp = cp_[y * cols_ + x];
            d.width = 1;
            unsigned s = st_[y * cols_ + x];
            d.style.fg = fg[s & 7];
            d.style.bg = (s & 8) ? 0x203040 : 0x000000;
            d.style.deco = d.style.fg;
            d.style.flags = (uint16_t)(((s & 16) ? tw::S_BOLD : 0) |
                                       ((s & 32) ? tw::S_UNDERLINE : 0) |
                                       ((s & 8) ? 0 : tw::S_DEFAULT_BG));
        }
    }
    int cluster(int, int, uint32_t*, int) const { return 0; }
    tw::LineAttr line_attr(int) const { return tw::LINE_SINGLE; }
    tw::Cursor cursor() const
    {
        tw::Cursor c;
        c.x = dirty_x_ >= 0 ? dirty_x_ : 0;
        c.y = dirty_y_ >= 0 ? dirty_y_ : rows_ - 1;
        c.visible = true;
        c.blink = false;
        c.wide = false;
        c.shape = tw::CURSOR_BLOCK;
        return c;
    }
    uint32_t background() const { return 0x000000; }
    uint32_t cursor_color() const { return 0xffffff; }
    void frame_done() { full_ = false; }

private:
    unsigned rnd()
    {
        seed_ = seed_ * 1103515245u + 12345u;
        return seed_ >> 16;
    }
    /* Words of letters in a colour each, some bold, some on a background. */
    void fill_row(int y)
    {
        int x = 0;
        while (x < cols_) {
            int len = 1 + (int)(rnd() % 9);
            unsigned style = rnd() % 64;
            for (int k = 0; k < len && x < cols_; k++, x++) {
                cp_[y * cols_ + x] = (uint32_t)('!' + rnd() % 94);
                st_[y * cols_ + x] = (unsigned char)style;
            }
            if (x < cols_) {
                cp_[y * cols_ + x] = ' ';
                st_[y * cols_ + x] = 0;
                x++;
            }
        }
    }

    int cols_, rows_;
    Mode mode_;
    unsigned seed_;
    int frame_;
    bool full_, scrolled_;
    int dirty_x_, dirty_y_;
    uint32_t* cp_;
    unsigned char* st_;
};

void usage()
{
    fprintf(stderr, "usage: tw_bench [-r core|soft|gl] [-m full|scroll|type|idle] "
                    "[-dynbg name] [-g COLSxROWS] [-n frames] [-fn font]\n");
    exit(2);
}

} /* namespace */

int main(int argc, char** argv)
{
    const char* kind = "gl";
    const char* mode_name = "full";
    const char* dynbg = 0;
    const char* font = "fixed";
    int cols = 120, rows = 40, frames = 200;
    for (int i = 1; i < argc; i++) {
        if (i + 1 >= argc)
            usage();
        if (strcmp(argv[i], "-r") == 0)
            kind = argv[++i];
        else if (strcmp(argv[i], "-m") == 0)
            mode_name = argv[++i];
        else if (strcmp(argv[i], "-dynbg") == 0)
            dynbg = argv[++i];
        else if (strcmp(argv[i], "-g") == 0) {
            if (sscanf(argv[++i], "%dx%d", &cols, &rows) != 2)
                usage();
        } else if (strcmp(argv[i], "-n") == 0)
            frames = atoi(argv[++i]);
        else if (strcmp(argv[i], "-fn") == 0)
            font = argv[++i];
        else
            usage();
    }
    Mode mode = strcmp(mode_name, "scroll") == 0 ? M_SCROLL
              : strcmp(mode_name, "type") == 0 ? M_TYPE
              : strcmp(mode_name, "idle") == 0 ? M_IDLE : M_FULL;
    Display* dpy = XOpenDisplay(0);
    if (!dpy) {
        fprintf(stderr, "tw_bench: no display\n");
        return 1;
    }
    int scr = DefaultScreen(dpy);
    XVisualInfo vi;
    Visual* visual = DefaultVisual(dpy, scr);
    int depth = DefaultDepth(dpy, scr);
    Colormap cmap = DefaultColormap(dpy, scr);
    if (tw::choose_visual(kind, dpy, scr, &vi) && vi.visual != visual) {
        visual = vi.visual;
        depth = vi.depth;
        cmap = XCreateColormap(dpy, RootWindow(dpy, scr), visual, AllocNone);
    }
    /* Freed before the display closes (its fonts are the server's). */
    tw::Surface* sp = new tw::Surface;
    sp->set_fallback_dirs("third_party/misc-misc:/usr/share/ttr/fonts");
    if (!sp->set_fonts(dpy, font, 0)) {
        fprintf(stderr, "tw_bench: no font %s\n", font);
        return 1;
    }
    int w = cols * sp->cell_width() + 4, h = rows * sp->cell_height() + 4;
    XSetWindowAttributes a;
    a.colormap = cmap;
    a.background_pixel = 0;
    a.border_pixel = 0;
    Window win = XCreateWindow(dpy, RootWindow(dpy, scr), 0, 0, (unsigned)w, (unsigned)h, 0, depth,
                               InputOutput, visual, CWColormap | CWBackPixel | CWBorderPixel, &a);
    XStoreName(dpy, win, "tw_bench");
    /* Wait until it is on the screen (asking first: a fast server
     * exposes it before a late XSelectInput would see it). */
    XSelectInput(dpy, win, ExposureMask);
    XMapWindow(dpy, win);
    XEvent ev;
    XWindowEvent(dpy, win, ExposureMask, &ev);
    NoScheduler sched;
    if (!sp->attach(dpy, win, visual, cmap, depth, &sched, kind)) {
        fprintf(stderr, "tw_bench: cannot attach the %s renderer\n", kind);
        return 1;
    }
    if (dynbg && !sp->set_background(dynbg))
        fprintf(stderr, "tw_bench: no background %s with %s\n", dynbg, sp->renderer_name());
    /* The model is what fits (margins take a little). */
    int fc, fr;
    sp->resize(w, h, &fc, &fr);
    cols = fc;
    rows = fr;
    BenchSource src(cols, rows, mode);
    sp->set_source(&src);
    sp->resize(w, h, &fc, &fr);
    bool gl = strcmp(sp->renderer_name(), "gl") == 0;
#if defined(TTR_HAVE_GL)
    if (gl)
        printf("GL_RENDERER %s, GL_VERSION %s\n", (const char*)glGetString(GL_RENDERER),
               (const char*)glGetString(GL_VERSION));
#endif
    /* A few frames to warm the glyph cache. */
    for (int i = 0; i < 5; i++) {
        src.step();
        sp->frame();
    }
    XSync(dpy, False);
    long cells = 0;
    long t0 = now_us();
    for (int i = 0; i < frames; i++) {
        src.step();
        sp->frame();
        cells += sp->last_frame_cells();
#if defined(TTR_HAVE_GL)
        if (gl)
            glFinish();
#endif
        XSync(dpy, False);
    }
    long t = now_us() - t0;
    (void)gl;
    printf("%s %s%s%s %dx%d: %d frames, %.2f ms a frame, %.1f fps, %ld cells a frame\n",
           sp->renderer_name(), mode_name, dynbg ? " +" : "", dynbg ? dynbg : "", cols, rows, frames,
           t / 1000.0 / frames, frames * 1e6 / (double)t, cells / frames);
    fflush(stdout);
    sp->detach();
    delete sp;
    XCloseDisplay(dpy);
    return 0;
}
