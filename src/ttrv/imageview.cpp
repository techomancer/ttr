/* ttrv's image view (imageview.h). */
#include "imageview.h"
#include "render.h"

#include "../../tw/renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <Xm/DrawingA.h>

namespace ttrv {

namespace {

void expose_cb(Widget, XtPointer closure, XtPointer)
{
    ((ImageView*)closure)->exposed();
}

void resize_cb(Widget, XtPointer closure, XtPointer)
{
    ((ImageView*)closure)->resized();
}

/* The child window's exposures: Xt hands them to the drawing area's
 * event handlers (XtRegisterDrawable). */
void child_event(Widget, XtPointer closure, XEvent* ev, Boolean*)
{
    if (ev->type == Expose && ev->xexpose.count == 0)
        ((ImageView*)closure)->exposed();
}

/* Put the window on WM_COLORMAP_WINDOWS of its shell, after the shell's
 * own, so window managers install its colormap when it has focus. */
void add_colormap_window(Widget w, Window win)
{
    Widget shell = w;
    while (XtParent(shell) && !XtIsShell(shell))
        shell = XtParent(shell);
    if (!XtIsRealized(shell))
        return;
    Display* dpy = XtDisplay(w);
    Window* old = 0;
    int n = 0;
    XGetWMColormapWindows(dpy, XtWindow(shell), &old, &n);
    Window* list = (Window*)malloc(sizeof(Window) * (size_t)(n + 2));
    if (!list)
        return;
    int k = 0;
    for (int i = 0; i < n; i++)
        if (old[i] != win)
            list[k++] = old[i];
    if (k == 0)
        list[k++] = XtWindow(shell);
    list[k++] = win;
    XSetWMColormapWindows(dpy, XtWindow(shell), list, k);
    free(list);
    if (old)
        XFree(old);
}

bool g_default_visual = false;
enum { R_AUTO, R_SOFT, R_GL };
int g_renderer = R_AUTO;
int g_texture_mb = 0;

} /* namespace */

void ImageView::set_visual_choice(const char* choice)
{
    g_default_visual = choice && strcmp(choice, "default") == 0;
}

void ImageView::set_renderer(const char* renderer, int texture_mb)
{
    g_renderer = !renderer ? R_AUTO : strcmp(renderer, "soft") == 0 ? R_SOFT
                                   : strcmp(renderer, "gl") == 0    ? R_GL
                                                                    : R_AUTO;
    g_texture_mb = texture_mb > 0 ? texture_mb : 0;
}

ImageView::ImageView(Widget parent, const char* name, ImageViewHost* host, bool gl)
    : host_(host), dpy_(XtDisplay(parent)), win_(0), own_window_(false), visual_(0), depth_(0),
      cmap_(0), own_cmap_(false), gc_(0), font_(0), dirty_(true), im_(0), at_(false), zoom_(1),
      x0_(0), y0_(0), may_gl_(gl), gl_(0), bg_(0x808080)
{
    Arg a[4];
    int n = 0;
    XtSetArg(a[n], XmNresizePolicy, XmRESIZE_NONE); n++;
    XtSetArg(a[n], XmNmarginWidth, 0); n++;
    XtSetArg(a[n], XmNmarginHeight, 0); n++;
    da_ = XmCreateDrawingArea(parent, (char*)name, a, n);
    XtAddCallback(da_, XmNexposeCallback, expose_cb, this);
    XtAddCallback(da_, XmNresizeCallback, resize_cb, this);
    XtManageChild(da_);
}

ImageView::~ImageView()
{
    delete gl_;
    frame_.destroy();
    fmt_.release();
    if (gc_)
        XFreeGC(dpy_, gc_);
    if (font_)
        XFreeFont(dpy_, font_);
    if (own_window_ && win_) {
        XtUnregisterDrawable(dpy_, win_);
        XDestroyWindow(dpy_, win_);
    }
    if (own_cmap_)
        XFreeColormap(dpy_, cmap_);
}

void ImageView::size(int* w, int* h) const
{
    Dimension dw = 0, dh = 0;
    XtVaGetValues(da_, XmNwidth, &dw, XmNheight, &dh, (char*)0);
    *w = dw;
    *h = dh;
}

const char* ImageView::renderer() const
{
    return gl_ ? gl_->name() : win_ ? "soft" : 0;
}

/* A child window in a GLX visual with a context: false (and nothing
 * made) if there is none, or "auto" and it is not worth it. */
bool ImageView::make_gl_window()
{
    int screen = XScreenNumberOfScreen(XtScreen(da_));
    XVisualInfo vi;
    char why[200] = "the display has no GLX visual";
    if (GLPaint::choose_visual(dpy_, screen, &vi)) {
        Colormap cm = XCreateColormap(dpy_, RootWindow(dpy_, screen), vi.visual, AllocNone);
        int w, h;
        size(&w, &h);
        XSetWindowAttributes sa;
        sa.colormap = cm;
        sa.background_pixmap = None;
        sa.border_pixel = 0;
        sa.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
        Window win = XCreateWindow(dpy_, XtWindow(da_), 0, 0, (unsigned)(w > 0 ? w : 1),
                                   (unsigned)(h > 0 ? h : 1), 0, vi.depth, InputOutput, vi.visual,
                                   CWColormap | CWBackPixmap | CWBorderPixel | CWEventMask, &sa);
        GLPaint* gl = new GLPaint;
        bool ok = gl->init(dpy_, win, vi, why, sizeof(why));
        if (ok && getenv("TTRV_VERBOSE"))
            fprintf(stderr, "ttrv: GL is %s (%s)\n", gl->name(),
                    gl->accelerated() ? "accelerated" : "not accelerated");
        if (ok && (g_renderer == R_GL || gl->accelerated())) {
            gl_ = gl;
            visual_ = vi.visual;
            depth_ = vi.depth;
            cmap_ = cm;
            own_cmap_ = true;
            win_ = win;
            own_window_ = true;
            if (g_texture_mb)
                gl_->set_budget((long)g_texture_mb * 1024 * 1024);
            XtRegisterDrawable(dpy_, win_, da_);
            XtAddEventHandler(da_, ExposureMask, False, child_event, this);
            XMapWindow(dpy_, win_);
            add_colormap_window(da_, win_);
            return true;
        }
        delete gl;
        XDestroyWindow(dpy_, win);
        XFreeColormap(dpy_, cm);
    }
    static bool told = false;
    if (g_renderer == R_GL && !told) {
        fprintf(stderr, "ttrv: no GL (%s); drawing in software\n", why);
        told = true;
    }
    return false;
}

/* The window to draw in, made on first use once the widget has one. */
bool ImageView::ensure_window()
{
    if (win_)
        return true;
    if (!XtIsRealized(da_))
        return false;
    bool verbose = may_gl_ && getenv("TTRV_VERBOSE");
    if (may_gl_ && g_renderer != R_SOFT && make_gl_window()) {
        if (verbose)
            fprintf(stderr, "ttrv: the viewer draws with GL: %s\n", gl_->name());
        XGCValues gv;
        gv.graphics_exposures = False;
        gc_ = XCreateGC(dpy_, win_, GCGraphicsExposures, &gv);
        font_ = XLoadQueryFont(dpy_, "-*-helvetica-medium-r-normal--12-*-*-*-*-*-iso8859-1");
        if (!font_)
            font_ = XLoadQueryFont(dpy_, "fixed");
        gl_->set_font(font_);
        dirty_ = true;
        return true;
    }
    int screen = XScreenNumberOfScreen(XtScreen(da_));
    Visual* parent_visual = 0;
    Widget sh = da_;
    while (XtParent(sh) && !XtIsShell(sh))
        sh = XtParent(sh);
    XtVaGetValues(sh, XmNvisual, &parent_visual, (char*)0);
    if (!parent_visual)
        parent_visual = DefaultVisual(dpy_, screen);
    int parent_depth = 0;
    Colormap parent_cmap = 0;
    XtVaGetValues(da_, XmNdepth, &parent_depth, XmNcolormap, &parent_cmap, (char*)0);

    /* The deepest TrueColor visual (tw's choice for its soft renderer),
     * else Motif's own through a colour cube. */
    XVisualInfo vi;
    bool tc = !g_default_visual && tw::choose_visual("soft", dpy_, screen, &vi);
    if (tc && vi.visual != parent_visual) {
        visual_ = vi.visual;
        depth_ = vi.depth;
        cmap_ = XCreateColormap(dpy_, RootWindow(dpy_, screen), visual_, AllocNone);
        own_cmap_ = true;
        int w, h;
        size(&w, &h);
        XSetWindowAttributes sa;
        sa.colormap = cmap_;
        sa.background_pixmap = None;
        sa.border_pixel = 0;
        sa.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
        win_ = XCreateWindow(dpy_, XtWindow(da_), 0, 0, (unsigned)(w > 0 ? w : 1),
                             (unsigned)(h > 0 ? h : 1), 0, depth_, InputOutput, visual_,
                             CWColormap | CWBackPixmap | CWBorderPixel | CWEventMask, &sa);
        own_window_ = true;
        XtRegisterDrawable(dpy_, win_, da_);
        XtAddEventHandler(da_, ExposureMask, False, child_event, this);
        XMapWindow(dpy_, win_);
        add_colormap_window(da_, win_);
    } else {
        visual_ = tc ? vi.visual : parent_visual;
        depth_ = parent_depth;
        cmap_ = parent_cmap;
        win_ = XtWindow(da_);
        /* The pixels are all ours: no background clearing. */
        XSetWindowBackgroundPixmap(dpy_, win_, None);
    }
    if (!fmt_.init(dpy_, visual_, cmap_, depth_))
        return false;
    if (verbose)
        fprintf(stderr, "ttrv: the viewer draws in software, %d bits\n", depth_);
    XGCValues gv;
    gv.graphics_exposures = False;
    gc_ = XCreateGC(dpy_, win_, GCGraphicsExposures, &gv);
    font_ = XLoadQueryFont(dpy_, "-*-helvetica-medium-r-normal--12-*-*-*-*-*-iso8859-1");
    if (!font_)
        font_ = XLoadQueryFont(dpy_, "fixed");
    if (font_)
        XSetFont(dpy_, gc_, font_->fid);
    dirty_ = true;
    return true;
}

void ImageView::show(const ti::Image* im, const char* key)
{
    im_ = im && !im->empty() ? im : 0;
    at_ = false;
    key_ = key ? key : "";
    if (!im_) {
        scaled_.clear();
        if (gl_)
            gl_->forget();
    }
    dirty_ = true;
    paint();
}

void ImageView::show_at(const ti::Image* im, const char* key, double zoom, double x0, double y0)
{
    if (!im || im->empty() || zoom <= 0) {
        show(0);
        return;
    }
    im_ = im;
    at_ = true;
    key_ = key ? key : "";
    zoom_ = zoom;
    x0_ = x0;
    y0_ = y0;
    dirty_ = true;
    paint();
}

void ImageView::set_text(const char* text)
{
    const char* t = text ? text : "";
    if (strcmp(t, text_.c_str()) == 0)
        return;
    text_ = t;
    dirty_ = true;
    paint();
}

void ImageView::set_background(unsigned long rgb)
{
    if (rgb == bg_)
        return;
    bg_ = rgb;
    dirty_ = true;
    paint();
}

void ImageView::exposed()
{
    paint();
}

void ImageView::resized()
{
    if (own_window_ && win_) {
        int w, h;
        size(&w, &h);
        XResizeWindow(dpy_, win_, (unsigned)(w > 0 ? w : 1), (unsigned)(h > 0 ? h : 1));
    }
    dirty_ = true;
    if (host_)
        host_->view_resized(this);
    paint();
}

/* frame_: the background and the image, the window's size. */
void ImageView::rebuild()
{
    int w, h;
    size(&w, &h);
    if (w < 1 || h < 1)
        return;
    if (frame_.width() != w || frame_.height() != h || !frame_.image()) {
        if (!frame_.create(dpy_, visual_, depth_, w, h))
            return;
    }
    if (im_ && at_ && render_view(*im_, zoom_, x0_, y0_, w, h, bg_, &scaled_)) {
        fmt_.convert(scaled_, bg_, &frame_, 0, 0);
        dirty_ = false;
        return;
    }
    /* The background: one row converted, copied down. */
    {
        ti::Image line;
        if (line.alloc(w, 1)) {
            for (int x = 0; x < w; x++) {
                unsigned char* p = line.rgba + x * 4;
                p[0] = (unsigned char)(bg_ >> 16);
                p[1] = (unsigned char)(bg_ >> 8);
                p[2] = (unsigned char)bg_;
                p[3] = 255;
            }
            if (fmt_.true_color()) {
                fmt_.convert(line, bg_, &frame_, 0, 0);
                size_t bpl = (size_t)frame_.image()->bytes_per_line;
                for (int y = 1; y < h; y++)
                    memcpy(frame_.row(y), frame_.row(0), bpl);
            } else {
                /* Dithered: the pattern repeats every four rows. */
                for (int y = 0; y < h && y < 4; y++)
                    fmt_.convert(line, bg_, &frame_, 0, y);
                size_t bpl = (size_t)frame_.image()->bytes_per_line;
                for (int y = 4; y < h; y++)
                    memcpy(frame_.row(y), frame_.row(y & 3), bpl);
            }
        }
    }
    if (im_ && !at_)
        fmt_.convert(*im_, bg_, &frame_, (w - im_->w) / 2, (h - im_->h) / 2);
    dirty_ = false;
}

/* Where the text line goes, and in which colour: black on light
 * backgrounds, white on dark. */
void ImageView::text_at(int w, int h, int* x, int* y, unsigned long* fg) const
{
    int len = (int)strlen(text_.c_str());
    int tw = XTextWidth(font_, text_.c_str(), len);
    if (im_) {
        *x = 6;
        *y = h - 6 - font_->descent;
    } else {
        *x = (w - tw) / 2;
        *y = h / 2;
    }
    if (*x < 2)
        *x = 2;
    long lum = ((bg_ >> 16) & 255) * 3 + ((bg_ >> 8) & 255) * 6 + (bg_ & 255);
    *fg = lum > 1280 ? 0x000000 : 0xffffff;
}

void ImageView::paint_gl()
{
    int w, h;
    size(&w, &h);
    if (w < 1 || h < 1)
        return;
    gl_->begin(w, h, bg_);
    if (im_) {
        double z = 1, x0 = -((w - im_->w) / 2), y0 = -((h - im_->h) / 2);
        if (at_) {
            z = zoom_;
            x0 = x0_;
            y0 = y0_;
        }
        /* Not in textures (no memory), or zoomed past what they keep:
         * the soft renderer's frame, drawn 1:1. */
        if (!gl_->image(*im_, key_.c_str(), z, x0, y0) &&
            render_view(*im_, z, x0, y0, w, h, bg_, &scaled_))
            gl_->image(scaled_, "", 1, 0, 0);
    }
    if (!text_.empty() && font_) {
        int x, y;
        unsigned long fg;
        text_at(w, h, &x, &y, &fg);
        gl_->text(x, y, text_.c_str(), (int)strlen(text_.c_str()), fg);
    }
    gl_->end();
    dirty_ = false;
}

void ImageView::paint()
{
    if (!ensure_window())
        return;
    if (gl_) {
        paint_gl();
        return;
    }
    if (dirty_)
        rebuild();
    if (!frame_.image())
        return;
    frame_.put(win_, gc_, 0, 0, 0, 0, frame_.width(), frame_.height());
    if (!text_.empty() && font_) {
        int x, y;
        unsigned long fg;
        text_at(frame_.width(), frame_.height(), &x, &y, &fg);
        XSetForeground(dpy_, gc_, fmt_.pixel(fg));
        XDrawString(dpy_, win_, gc_, x, y, text_.c_str(), (int)strlen(text_.c_str()));
    }
    frame_.wait();
}

} /* namespace ttrv */
