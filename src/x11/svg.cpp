#include "x11/svg.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* nanosvg uses the C99 float math functions. IRIX 6.5 predates C99, so
 * map them to the double versions there. <math.h> is already included, so
 * its own declarations are not affected. Unverified on real IRIX. */
#if defined(__sgi)
#undef sqrtf
#undef fabsf
#undef sinf
#undef cosf
#undef tanf
#undef acosf
#undef ceilf
#undef floorf
#undef fmodf
#undef roundf
#define sqrtf(x) ((float)sqrt((double)(x)))
#define fabsf(x) ((float)fabs((double)(x)))
#define sinf(x) ((float)sin((double)(x)))
#define cosf(x) ((float)cos((double)(x)))
#define tanf(x) ((float)tan((double)(x)))
#define acosf(x) ((float)acos((double)(x)))
#define ceilf(x) ((float)ceil((double)(x)))
#define floorf(x) ((float)floor((double)(x)))
#define fmodf(x, y) ((float)fmod((double)(x), (double)(y)))
#define roundf(x) ((float)floor((double)(x) + 0.5))
#endif

#define NANOSVG_IMPLEMENTATION
#define NANOSVG_ALL_COLOR_KEYWORDS
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

namespace ttr {

bool svg_render(const char* path, int size, uint32_t* argb)
{
    NSVGimage* img = nsvgParseFromFile(path, "px", 96.0f);
    if (!img)
        return false;
    if (img->width <= 0 || img->height <= 0 || !img->shapes) {
        nsvgDelete(img);
        return false;
    }
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        nsvgDelete(img);
        return false;
    }

    float longest = img->width > img->height ? img->width : img->height;
    float scale = (float)size / longest;
    float tx = ((float)size - img->width * scale) / 2;
    float ty = ((float)size - img->height * scale) / 2;

    unsigned char* rgba = new unsigned char[size * size * 4];
    memset(rgba, 0, size * size * 4);
    nsvgRasterize(rast, img, tx, ty, scale, rgba, size, size, size * 4);

    /* Byte order RGBA, packed by hand so it works on either endianness. */
    int i;
    for (i = 0; i < size * size; i++) {
        const unsigned char* p = rgba + i * 4;
        argb[i] = ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) |
                  ((uint32_t)p[1] << 8) | (uint32_t)p[2];
    }

    delete [] rgba;
    nsvgDeleteRasterizer(rast);
    nsvgDelete(img);
    return true;
}

}
