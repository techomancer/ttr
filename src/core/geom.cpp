#include "core/geom.h"

namespace ttr {

SizeConstraints::SizeConstraints()
    : min_w(0), min_h(0), max_w(0), max_h(0),
      inc_w(0), inc_h(0), base_w(0), base_h(0)
{
}

static int constrain_axis(int v, int min, int max, int inc, int base)
{
    if (v < min)
        v = min;
    if (max > 0 && v > max)
        v = max;
    if (inc > 1) {
        int b = base > 0 ? base : min;
        if (v > b)
            v = b + ((v - b) / inc) * inc;
    }
    if (v < 1)
        v = 1;
    return v;
}

void SizeConstraints::apply(int* w, int* h) const
{
    *w = constrain_axis(*w, min_w, max_w, inc_w, base_w);
    *h = constrain_axis(*h, min_h, max_h, inc_h, base_h);
}

}
