/* ttrv's image view (imageview.h). */
#include "imageview.h"

#include "../../tw/renderer.h"

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

} /* namespace */

ImageView::ImageView(Widget parent, const char* name, ImageViewHost* host)
    : host_(host), dpy_(XtDisplay(parent)), win_(0), own_window_(false), visual_(0), depth_(0),
      cmap_(0), own_cmap_(false), gc_(0), font_(0), dirty_(true), im_(0), bg_(0x808080)
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

/* The window to draw in, made on first use once the widget has one. */
bool ImageView::ensure_window()
{
    if (win_)
        return true;
    if (!XtIsRealized(da_))
        return false;
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
    bool tc = tw::choose_visual("soft", dpy_, screen, &vi);
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

void ImageView::show(const ti::Image* im)
{
    im_ = im && !im->empty() ? im : 0;
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
    if (im_)
        fmt_.convert(*im_, bg_, &frame_, (w - im_->w) / 2, (h - im_->h) / 2);
    dirty_ = false;
}

void ImageView::paint()
{
    if (!ensure_window())
        return;
    if (dirty_)
        rebuild();
    if (!frame_.image())
        return;
    frame_.put(win_, gc_, 0, 0, 0, 0, frame_.width(), frame_.height());
    if (!text_.empty() && font_) {
        int len = (int)strlen(text_.c_str());
        int tw = XTextWidth(font_, text_.c_str(), len);
        int x, y;
        if (im_) {
            x = 6;
            y = frame_.height() - 6 - font_->descent;
        } else {
            x = (frame_.width() - tw) / 2;
            y = frame_.height() / 2;
        }
        long lum = ((bg_ >> 16) & 255) * 3 + ((bg_ >> 8) & 255) * 6 + (bg_ & 255);
        unsigned long fg = lum > 1280 ? 0x000000 : 0xffffff;
        XSetForeground(dpy_, gc_, fmt_.pixel(fg));
        XDrawString(dpy_, win_, gc_, x < 2 ? 2 : x, y, text_.c_str(), len);
    }
    frame_.wait();
}

} /* namespace ttrv */
