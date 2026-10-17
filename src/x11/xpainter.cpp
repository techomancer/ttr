#include "x11/xpainter.h"
#include "core/icon.h"

#include <X11/Xutil.h>

#include <stdlib.h>

namespace ttr {

static void mask_shift(unsigned long mask, int* shift, int* bits)
{
    *shift = 0;
    *bits = 0;
    if (!mask)
        return;
    while (!(mask & 1)) {
        mask >>= 1;
        (*shift)++;
    }
    while (mask & 1) {
        mask >>= 1;
        (*bits)++;
    }
}

/* Icons kept as server pixmaps; plenty for a taskbar and switcher. */
static const int k_icon_cache = 128;

XPainter::XPainter(Display* dpy, int screen)
    : dpy_(dpy), screen_(screen), font_(0), buf_(None),
      buf_w_(0), buf_h_(0), win_(None), w_(0), h_(0)
{
    gc_ = XCreateGC(dpy_, RootWindow(dpy_, screen_), 0, 0);
    Visual* vis = DefaultVisual(dpy_, screen_);
    truecolor_ = vis->c_class == TrueColor;
    mask_shift(vis->red_mask, &shift_[0], &bits_[0]);
    mask_shift(vis->green_mask, &shift_[1], &bits_[1]);
    mask_shift(vis->blue_mask, &shift_[2], &bits_[2]);
}

XPainter::~XPainter()
{
    int i;
    for (i = 0; i < icons_.size(); i++)
        XFreePixmap(dpy_, icons_[i].pixmap);
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

void XPainter::icon(const Icon& ic, int x, int y, int size, const Rgb& bg)
{
    Pixmap pm = icon_pixmap(ic, size, bg);
    if (pm != None)
        XCopyArea(dpy_, pm, buf_, gc_, 0, 0, size, size, x, y);
}

unsigned long XPainter::icon_pixel(unsigned long r, unsigned long g,
                                   unsigned long b)
{
    if (truecolor_) {
        unsigned long c[3];
        c[0] = r;
        c[1] = g;
        c[2] = b;
        unsigned long p = 0;
        int i;
        for (i = 0; i < 3; i++) {
            unsigned long v = bits_[i] >= 8 ? c[i] << (bits_[i] - 8)
                                            : c[i] >> (8 - bits_[i]);
            p |= v << shift_[i];
        }
        return p;
    }
    /* Colormapped (an 8-bit Indy): snap to a 6x6x6 cube so an icon
     * allocates at most 216 colors. */
    r = (r + 25) / 51 * 51;
    g = (g + 25) / 51 * 51;
    b = (b + 25) / 51 * 51;
    return pixel(Rgb((unsigned short)(r * 257), (unsigned short)(g * 257),
                     (unsigned short)(b * 257)));
}

Pixmap XPainter::icon_pixmap(const Icon& ic, int size, const Rgb& bg)
{
    int i;
    for (i = 0; i < icons_.size(); i++) {
        CachedIcon& c = icons_[i];
        if (c.serial == ic.serial() && c.size == size && c.bg == bg)
            return c.pixmap;
    }

    Icon* scaled = 0;
    const Icon* src = &ic;
    if (ic.width() != size || ic.height() != size) {
        scaled = ic.scaled(size);
        src = scaled;
    }

    int depth = DefaultDepth(dpy_, screen_);
    XImage* img = XCreateImage(dpy_, DefaultVisual(dpy_, screen_), depth,
                               ZPixmap, 0, 0, size, size, 32, 0);
    if (!img) {
        delete scaled;
        return None;
    }
    img->data = (char*)malloc(img->bytes_per_line * size);

    unsigned long br = bg.r >> 8, bgc = bg.g >> 8, bb = bg.b >> 8;
    const uint32_t* px = src->pixels();
    int x, y;
    for (y = 0; y < size; y++) {
        for (x = 0; x < size; x++) {
            uint32_t p = px[y * size + x];
            unsigned long a = (p >> 24) & 0xff;
            unsigned long r = (((p >> 16) & 0xff) * a + br * (255 - a)) / 255;
            unsigned long g = (((p >> 8) & 0xff) * a + bgc * (255 - a)) / 255;
            unsigned long b = ((p & 0xff) * a + bb * (255 - a)) / 255;
            XPutPixel(img, x, y, icon_pixel(r, g, b));
        }
    }
    delete scaled;

    Pixmap pm = XCreatePixmap(dpy_, RootWindow(dpy_, screen_), size, size,
                              depth);
    XPutImage(dpy_, pm, gc_, img, 0, 0, 0, 0, size, size);
    XDestroyImage(img);

    if (icons_.size() >= k_icon_cache) {
        XFreePixmap(dpy_, icons_[0].pixmap);
        icons_.remove_at(0);
    }
    CachedIcon c;
    c.serial = ic.serial();
    c.size = size;
    c.bg = bg;
    c.pixmap = pm;
    icons_.push_back(c);
    return pm;
}

}
