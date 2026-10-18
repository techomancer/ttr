#include "core/scale.h"

namespace ttr {

static int g_scale = 100;

void set_ui_scale(int percent)
{
    if (percent < k_min_scale)
        percent = k_min_scale;
    if (percent > k_max_scale)
        percent = k_max_scale;
    g_scale = percent;
}

int ui_scale()
{
    return g_scale;
}

int scaled(int px)
{
    if (px == 0)
        return 0;
    int neg = px < 0;
    if (neg)
        px = -px;
    int r = (px * g_scale + 50) / 100;
    if (r < 1)
        r = 1;
    return neg ? -r : r;
}

int scaled_line(int px)
{
    /* Round down: 1 stays 1 up to 199%. */
    int r = px * g_scale / 100;
    return r < 1 && px > 0 ? 1 : r;
}

}
