/* Shadow colours: th's derivation (Motif's CalculateColorsRGB). */
#include "core/color.h"

#include "../../th/th.h"

namespace ttr {

Rgb Rgb::hex(unsigned long v)
{
    unsigned short r = (unsigned short)((v >> 16) & 0xff);
    unsigned short g = (unsigned short)((v >> 8) & 0xff);
    unsigned short b = (unsigned short)(v & 0xff);
    return Rgb((unsigned short)(r * 257), (unsigned short)(g * 257),
               (unsigned short)(b * 257));
}

ColorSet motif_colors(const Rgb& bg)
{
    th::ColorSet t = th::motif_colors(((th::Color)(bg.r >> 8) << 16) |
                                      ((th::Color)(bg.g >> 8) << 8) | (th::Color)(bg.b >> 8));
    ColorSet cs;
    cs.bg = Rgb::hex(t.bg);
    cs.fg = Rgb::hex(t.fg);
    cs.top = Rgb::hex(t.top);
    cs.bottom = Rgb::hex(t.bottom);
    cs.select = Rgb::hex(t.select);
    return cs;
}

}
