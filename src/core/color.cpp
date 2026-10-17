/*
 * Shadow color derivation, following the structure of Motif's
 * CalculateColorsRGB (lib/Xm/Visual.c): classify the background as dark,
 * medium or light by brightness, then scale each channel by a percentage.
 * Medium backgrounds interpolate the factors by brightness.
 */
#include "core/color.h"

namespace ttr {

static const long MAXC = 65535;

static const int DARK_THRESHOLD = 20;   /* percent */
static const int LIGHT_THRESHOLD = 93;
static const int FG_THRESHOLD = 70;

static const int LITE_SEL = 15, LITE_BS = 45, LITE_TS = 50;
static const int LO_SEL = 15, LO_BS = 60, LO_TS = 50;
static const int HI_SEL = 15, HI_BS = 40, HI_TS = 60;
static const int DARK_SEL = 15, DARK_BS = 30, DARK_TS = 50;

Rgb Rgb::hex(unsigned long v)
{
    unsigned short r = (unsigned short)((v >> 16) & 0xff);
    unsigned short g = (unsigned short)((v >> 8) & 0xff);
    unsigned short b = (unsigned short)(v & 0xff);
    return Rgb((unsigned short)(r * 257), (unsigned short)(g * 257),
               (unsigned short)(b * 257));
}

static long brightness(const Rgb& c)
{
    long r = c.r, g = c.g, b = c.b;
    long intensity = (r + g + b) / 3;
    long luminosity = (r * 30 + g * 59 + b * 11) / 100;
    long maxp = r, minp = r;
    if (g > maxp) maxp = g;
    if (b > maxp) maxp = b;
    if (g < minp) minp = g;
    if (b < minp) minp = b;
    long light = (maxp + minp) / 2;
    return (intensity * 75 + light * 0 + luminosity * 25) / 100;
}

static unsigned short darker(unsigned short v, long pct)
{
    return (unsigned short)(v - (long)v * pct / 100);
}

static unsigned short lighter(unsigned short v, long pct)
{
    return (unsigned short)(v + (MAXC - v) * pct / 100);
}

static Rgb darker(const Rgb& c, long pct)
{
    return Rgb(darker(c.r, pct), darker(c.g, pct), darker(c.b, pct));
}

static Rgb lighter(const Rgb& c, long pct)
{
    return Rgb(lighter(c.r, pct), lighter(c.g, pct), lighter(c.b, pct));
}

ColorSet motif_colors(const Rgb& bg)
{
    ColorSet cs;
    long br = brightness(bg);
    cs.bg = bg;

    if (br < DARK_THRESHOLD * MAXC / 100) {
        cs.select = lighter(bg, DARK_SEL);
        cs.bottom = lighter(bg, DARK_BS);
        cs.top = lighter(bg, DARK_TS);
    } else if (br > LIGHT_THRESHOLD * MAXC / 100) {
        cs.select = darker(bg, LITE_SEL);
        cs.bottom = darker(bg, LITE_BS);
        cs.top = lighter(bg, LITE_TS);
    } else {
        long f;
        f = LO_SEL + br * (HI_SEL - LO_SEL) / MAXC;
        cs.select = darker(bg, f);
        f = LO_BS + br * (HI_BS - LO_BS) / MAXC;
        cs.bottom = darker(bg, f);
        f = LO_TS + br * (HI_TS - LO_TS) / MAXC;
        cs.top = lighter(bg, f);
    }

    if (br > FG_THRESHOLD * MAXC / 100)
        cs.fg = Rgb(0, 0, 0);
    else
        cs.fg = Rgb((unsigned short)MAXC, (unsigned short)MAXC,
                    (unsigned short)MAXC);
    return cs;
}

}
