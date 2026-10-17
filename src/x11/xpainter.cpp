#include "x11/xpainter.h"

namespace ttr {

XPainter::XPainter(Display* dpy, int screen)
    : dpy_(dpy), screen_(screen), font_(0), buf_(None),
      buf_w_(0), buf_h_(0), win_(None), w_(0), h_(0)
{
    gc_ = XCreateGC(dpy_, RootWindow(dpy_, screen_), 0, 0);
}

XPainter::~XPainter()
{
    if (buf_ != None)
        XFreePixmap(dpy_, buf_);
    if (font_)
        XFreeFont(dpy_, font_);
    XFreeGC(dpy_, gc_);
}

bool XPainter::load_font(const char* const* names)
{
    int i;
    for (i = 0; names[i]; i++) {
        XFontStruct* f = XLoadQueryFont(dpy_, names[i]);
        if (f) {
            if (font_)
                XFreeFont(dpy_, font_);
            font_ = f;
            XSetFont(dpy_, gc_, font_->fid);
            return true;
        }
    }
    return false;
}

void XPainter::begin(Window w, int width, int height)
{
    if (width > buf_w_ || height > buf_h_) {
        if (buf_ != None)
            XFreePixmap(dpy_, buf_);
        if (width > buf_w_)
            buf_w_ = width;
        if (height > buf_h_)
            buf_h_ = height;
        buf_ = XCreatePixmap(dpy_, RootWindow(dpy_, screen_), buf_w_, buf_h_,
                             DefaultDepth(dpy_, screen_));
    }
    win_ = w;
    w_ = width;
    h_ = height;
}

void XPainter::end()
{
    XCopyArea(dpy_, buf_, win_, gc_, 0, 0, w_, h_, 0, 0);
}

void XPainter::fill(const Rect& r, const Rgb& c)
{
    if (r.w <= 0 || r.h <= 0)
        return;
    XSetForeground(dpy_, gc_, pixel(c));
    XFillRectangle(dpy_, buf_, gc_, r.x, r.y, r.w, r.h);
}

int XPainter::text_width(const char* s, int len)
{
    return font_ ? XTextWidth(font_, s, len) : 0;
}

int XPainter::text_ascent()
{
    return font_ ? font_->ascent : 0;
}

int XPainter::text_descent()
{
    return font_ ? font_->descent : 0;
}

void XPainter::text(int x, int y, const char* s, int len, const Rgb& c)
{
    if (!font_)
        return;
    XSetForeground(dpy_, gc_, pixel(c));
    XDrawString(dpy_, buf_, gc_, x, y, s, len);
}

unsigned long XPainter::pixel(const Rgb& c)
{
    int i;
    for (i = 0; i < colors_.size(); i++)
        if (colors_[i].rgb == c)
            return colors_[i].pixel;

    XColor xc;
    xc.red = c.r;
    xc.green = c.g;
    xc.blue = c.b;
    xc.flags = DoRed | DoGreen | DoBlue;
    unsigned long p;
    if (XAllocColor(dpy_, DefaultColormap(dpy_, screen_), &xc))
        p = xc.pixel;
    else if ((long)c.r + c.g + c.b > 3L * 32768)
        p = WhitePixel(dpy_, screen_);
    else
        p = BlackPixel(dpy_, screen_);

    CachedColor cc;
    cc.rgb = c;
    cc.pixel = p;
    colors_.push_back(cc);
    return p;
}

}
