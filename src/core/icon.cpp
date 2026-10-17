#include "core/icon.h"

#include <string.h>

namespace ttr {

static unsigned long g_next_serial = 1;

Icon::Icon(int w, int h)
    : w_(w > 0 ? w : 1), h_(h > 0 ? h : 1), serial_(g_next_serial++)
{
    px_ = new uint32_t[w_ * h_];
    memset(px_, 0, sizeof(uint32_t) * w_ * h_);
}

Icon::~Icon()
{
    delete [] px_;
}

Icon* Icon::scaled(int size) const
{
    Icon* out = new Icon(size, size);
    int x, y;
    for (y = 0; y < size; y++) {
        int sy0 = y * h_ / size;
        int sy1 = (y + 1) * h_ / size;
        if (sy1 <= sy0)
            sy1 = sy0 + 1;
        for (x = 0; x < size; x++) {
            int sx0 = x * w_ / size;
            int sx1 = (x + 1) * w_ / size;
            if (sx1 <= sx0)
                sx1 = sx0 + 1;

            /* Average with premultiplied alpha so transparent pixels do not
             * darken the edges. */
            unsigned long a = 0, r = 0, g = 0, b = 0, n = 0;
            int i, j;
            for (j = sy0; j < sy1; j++) {
                for (i = sx0; i < sx1; i++) {
                    uint32_t p = px_[j * w_ + i];
                    unsigned long pa = (p >> 24) & 0xff;
                    a += pa;
                    r += ((p >> 16) & 0xff) * pa;
                    g += ((p >> 8) & 0xff) * pa;
                    b += (p & 0xff) * pa;
                    n++;
                }
            }
            uint32_t o = 0;
            if (a > 0) {
                o = (uint32_t)((a / n) << 24) |
                    (uint32_t)((r / a) << 16) |
                    (uint32_t)((g / a) << 8) |
                    (uint32_t)(b / a);
            }
            out->px_[y * size + x] = o;
        }
    }
    return out;
}

}
