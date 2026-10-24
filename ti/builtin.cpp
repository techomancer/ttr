/*
 * The built-in decoders: stb_image (ti/third/stb_image.c), for PNG,
 * JPEG, GIF (the first frame), BMP and PNM.
 */
#include "internal.h"

extern "C" {
unsigned char* ti_stbi_load(const unsigned char* d, int n, int* w, int* h);
int ti_stbi_info(const unsigned char* d, int n, int* w, int* h);
const char* ti_stbi_failure(void);
int ti_stbi_zlib(unsigned char* out, int out_n, const unsigned char* in, int n);
}

namespace ti {
namespace internal {

const char* builtin_name(Format f)
{
    switch (f) {
    case FMT_PNG: case FMT_JPEG: case FMT_GIF: case FMT_BMP: case FMT_PNM:
        return "stb_image 2.30";
    default:
        return 0;
    }
}

bool builtin_decode(Ctx& c, Format f)
{
    if (!builtin_name(f))
        return fail(c, "No built-in %s decoder.", format_name(f));
    if (c.n > 0x7fffffffu)
        return fail(c, "The file is too big.");
    int w = 0, h = 0;
    if (!ti_stbi_info(c.d, (int)c.n, &w, &h))
        return fail(c, "%s: %s.", format_name(f), ti_stbi_failure());
    if (!check_size(c, w, h))
        return false;
    unsigned char* px = ti_stbi_load(c.d, (int)c.n, &w, &h);
    if (!px)
        return fail(c, "%s: %s.", format_name(f), ti_stbi_failure());
    /* stb allocates with malloc, as Image does: keep its buffer. */
    c.out->clear();
    c.out->rgba = px;
    c.out->w = w;
    c.out->h = h;
    return true;
}

} /* namespace internal */

bool inflate(const unsigned char* in, size_t n, unsigned char* out, size_t out_n)
{
    if (n > 0x7fffffffu || out_n > 0x7fffffffu)
        return false;
    return ti_stbi_zlib(out, (int)out_n, in, (int)n) == (int)out_n;
}

} /* namespace ti */
