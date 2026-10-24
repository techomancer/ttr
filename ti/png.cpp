/*
 * PNG through libpng's classic API (png_create_read_struct and its
 * transforms), which every version from 1.2 (IRIX 6.5 has 1.2.5) to 1.6
 * has. The simplified png_image API is 1.6 only.
 */
#include "internal.h"

#if defined(TTR_HAVE_PNG)

#include <png.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ti {
namespace internal {

namespace {

struct Src {
    Ctx* c;
    size_t pos;
    char msg[200];
};

void read_fn(png_structp p, png_bytep out, png_size_t len)
{
    Src* s = (Src*)png_get_io_ptr(p);
    if (s->c->n - s->pos < len)
        png_error(p, "The file ends early.");
    memcpy(out, s->c->d + s->pos, len);
    s->pos += len;
}

void error_fn(png_structp p, png_const_charp msg)
{
    Src* s = (Src*)png_get_error_ptr(p);
    snprintf(s->msg, sizeof(s->msg), "%s", msg);
    longjmp(png_jmpbuf(p), 1);
}

void warning_fn(png_structp, png_const_charp)
{
}

} /* namespace */

const char* png_system_name()
{
    static char name[40];
    if (!name[0])
        snprintf(name, sizeof(name), "libpng %s", png_get_libpng_ver(0));
    return name;
}

bool png_system(Ctx& c)
{
    Src src;
    src.c = &c;
    src.pos = 0;
    src.msg[0] = 0;
    png_structp p = png_create_read_struct(PNG_LIBPNG_VER_STRING, &src, error_fn, warning_fn);
    if (!p)
        return fail(c, "Out of memory.");
    png_infop info = png_create_info_struct(p);
    if (!info) {
        png_destroy_read_struct(&p, 0, 0);
        return fail(c, "Out of memory.");
    }
    png_bytep* volatile rows = 0;
    if (setjmp(png_jmpbuf(p))) {
        free(rows);
        png_destroy_read_struct(&p, &info, 0);
        return fail(c, "PNG: %s", src.msg);
    }
    png_set_read_fn(p, &src, read_fn);
    png_read_info(p, info);
    png_uint_32 w, h;
    int depth, type, interlace;
    png_get_IHDR(p, info, &w, &h, &depth, &type, &interlace, 0, 0);
    if (!check_size(c, (long)w, (long)h)) {
        png_destroy_read_struct(&p, &info, 0);
        return false;
    }
    /* Everything to 8-bit RGBA. */
    png_set_expand(p);
    png_set_strip_16(p);
    if (type == PNG_COLOR_TYPE_GRAY || type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(p);
    png_set_filler(p, 0xff, PNG_FILLER_AFTER);
    png_set_interlace_handling(p);
    png_read_update_info(p, info);
    if (png_get_rowbytes(p, info) != (png_uint_32)w * 4 || !c.out->alloc((int)w, (int)h)) {
        png_destroy_read_struct(&p, &info, 0);
        return fail(c, c.out->empty() ? "Out of memory." : "PNG: unexpected pixel layout.");
    }
    rows = (png_bytep*)malloc(sizeof(png_bytep) * h);
    if (!rows) {
        png_destroy_read_struct(&p, &info, 0);
        return fail(c, "Out of memory.");
    }
    for (png_uint_32 y = 0; y < h; y++)
        rows[y] = c.out->row((int)y);
    png_read_image(p, rows);
    png_read_end(p, 0);
    free(rows);
    png_destroy_read_struct(&p, &info, 0);
    return true;
}

} /* namespace internal */
} /* namespace ti */

#else

namespace ti {
namespace internal {

const char* png_system_name()
{
    return 0;
}

bool png_system(Ctx& c)
{
    return fail(c, "No libpng in this build.");
}

} /* namespace internal */
} /* namespace ti */

#endif
