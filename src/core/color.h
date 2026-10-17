#ifndef TTR_CORE_COLOR_H
#define TTR_CORE_COLOR_H

namespace ttr {

/* 16 bits per channel, like XColor. */
struct Rgb {
    unsigned short r, g, b;

    Rgb() : r(0), g(0), b(0) {}
    Rgb(unsigned short r_, unsigned short g_, unsigned short b_)
        : r(r_), g(g_), b(b_) {}

    /* From 0xRRGGBB. */
    static Rgb hex(unsigned long v);

    bool operator==(const Rgb& o) const
    {
        return r == o.r && g == o.g && b == o.b;
    }
};

/* A background color and the colors Motif derives from it. */
struct ColorSet {
    Rgb bg, fg, top, bottom, select;
};

/* Derive shadow, select and foreground colors the way Motif's XmGetColors
 * does, so our frames match Motif widgets drawn with the same background. */
ColorSet motif_colors(const Rgb& bg);

}

#endif
