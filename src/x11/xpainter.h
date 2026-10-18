/*
 * Painter on Xlib: core fonts, XAllocColor (so PseudoColor visuals work),
 * double buffered through a pixmap.
 */
#ifndef TTR_X11_XPAINTER_H
#define TTR_X11_XPAINTER_H

#include "core/painter.h"
#include "core/vector.h"

#include <X11/Xlib.h>

namespace ttr {

class XPainter : public Painter {
public:
    XPainter(Display* dpy, int screen);
    ~XPainter();

    /* Loads the first font that exists. Returns false if none does. */
    bool load_font(const char* const* names);
    /* A second font for emphasis, chosen with set_bold(). */
    bool load_bold_font(const char* const* names);
    void set_bold(bool on);

    /* Paint into a back buffer; end() copies it onto the window. */
    void begin(Window w, int width, int height);
    void end();

    void fill(const Rect& r, const Rgb& c);
    int text_width(const char* s, int len);
    int text_ascent();
    int text_descent();
    void text(int x, int y, const char* s, int len, const Rgb& c);
    void icon(const Icon& ic, int x, int y, int size, const Rgb& bg);

    unsigned long pixel(const Rgb& c);

private:
    struct CachedColor {
        Rgb rgb;
        unsigned long pixel;
    };
    struct CachedIcon {
        unsigned long serial;
        int size;
        Rgb bg;
        Pixmap pixmap;
    };

    Pixmap icon_pixmap(const Icon& ic, int size, const Rgb& bg);
    unsigned long icon_pixel(unsigned long r, unsigned long g,
                             unsigned long b);

    Display* dpy_;
    int screen_;
    GC gc_;
    XFontStruct* font_;         /* the one in use */
    XFontStruct* regular_;
    XFontStruct* bold_;
    Pixmap buf_;
    int buf_w_, buf_h_;
    Window win_;
    int w_, h_;
    Vector<CachedColor> colors_;
    Vector<CachedIcon> icons_;
    bool truecolor_;
    int shift_[3], bits_[3];    /* r, g, b of a TrueColor visual */
};

}

#endif
