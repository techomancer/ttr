/*
 * JPEG through libjpeg, written for the API of 6a (IRIX 6.5) that every
 * later libjpeg and libjpeg-turbo keeps: our own source manager (6a has
 * no jpeg_mem_src) and error manager. CMYK and YCCK files (Adobe's) and
 * grey ones are converted here; libjpeg 6a turns only YCbCr into RGB.
 */
#include "internal.h"

#if defined(TTR_HAVE_JPEG)

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include <jpeglib.h>
}

namespace ti {
namespace internal {

namespace {

struct Err {
    struct jpeg_error_mgr mgr;
    jmp_buf jump;
    char msg[JMSG_LENGTH_MAX];
};

void error_exit(j_common_ptr j)
{
    Err* e = (Err*)j->err;
    (*j->err->format_message)(j, e->msg);
    longjmp(e->jump, 1);
}

void output_message(j_common_ptr)
{
}

const JOCTET k_eoi[2] = { 0xff, JPEG_EOI };

void init_source(j_decompress_ptr)
{
}

/* The whole file is in the buffer from the start: running out means a
 * truncated file, which gets an end marker so the rows so far show. */
boolean fill_input_buffer(j_decompress_ptr j)
{
    j->src->next_input_byte = k_eoi;
    j->src->bytes_in_buffer = 2;
    return TRUE;
}

void skip_input_data(j_decompress_ptr j, long n)
{
    if (n <= 0)
        return;
    if ((size_t)n > j->src->bytes_in_buffer) {
        fill_input_buffer(j);
        return;
    }
    j->src->next_input_byte += n;
    j->src->bytes_in_buffer -= (size_t)n;
}

void term_source(j_decompress_ptr)
{
}

} /* namespace */

const char* jpeg_system_name()
{
#if defined(LIBJPEG_TURBO_VERSION)
#define TI_STR2(x) #x
#define TI_STR(x) TI_STR2(x)
    return "libjpeg-turbo " TI_STR(LIBJPEG_TURBO_VERSION);
#else
    static char name[40];
    if (!name[0])
        sprintf(name, "libjpeg %d%c", JPEG_LIB_VERSION / 10,
                JPEG_LIB_VERSION % 10 ? 'a' + JPEG_LIB_VERSION % 10 - 1 : ' ');
    return name;
#endif
}

bool jpeg_system(Ctx& c)
{
    struct jpeg_decompress_struct j;
    Err err;
    unsigned char* volatile line = 0;
    j.err = jpeg_std_error(&err.mgr);
    err.mgr.error_exit = error_exit;
    err.mgr.output_message = output_message;
    err.msg[0] = 0;
    if (setjmp(err.jump)) {
        free(line);
        jpeg_destroy_decompress(&j);
        return fail(c, "JPEG: %s", err.msg);
    }
    jpeg_create_decompress(&j);
    struct jpeg_source_mgr src;
    src.init_source = init_source;
    src.fill_input_buffer = fill_input_buffer;
    src.skip_input_data = skip_input_data;
    src.resync_to_restart = jpeg_resync_to_restart;
    src.term_source = term_source;
    src.next_input_byte = (const JOCTET*)c.d;
    src.bytes_in_buffer = c.n;
    j.src = &src;
    jpeg_read_header(&j, TRUE);
    if (!check_size(c, (long)j.image_width, (long)j.image_height)) {
        jpeg_destroy_decompress(&j);
        return false;
    }
    bool cmyk = j.jpeg_color_space == JCS_CMYK || j.jpeg_color_space == JCS_YCCK;
    /* 6a cannot turn grey into RGB: ti does. */
    bool grey = j.jpeg_color_space == JCS_GRAYSCALE;
    j.out_color_space = cmyk ? JCS_CMYK : grey ? JCS_GRAYSCALE : JCS_RGB;
    /* DCT scaling: 1/2, 1/4 or 1/8 while the result stays big enough. */
    if (c.reduce_to > 0) {
        long side = j.image_width > j.image_height ? (long)j.image_width : (long)j.image_height;
        unsigned denom = 1;
        while (denom < 8 && side / (long)(denom * 2) >= c.reduce_to)
            denom *= 2;
        j.scale_num = 1;
        j.scale_denom = denom;
    }
    jpeg_start_decompress(&j);
    int w = (int)j.output_width, h = (int)j.output_height, comps = j.output_components;
    if ((comps != 1 && comps != 3 && comps != 4) || !c.out->alloc(w, h)) {
        jpeg_destroy_decompress(&j);
        return fail(c, c.out->empty() ? "Out of memory." : "JPEG: unexpected colour layout.");
    }
    line = (unsigned char*)malloc((size_t)w * (size_t)comps);
    if (!line) {
        jpeg_destroy_decompress(&j);
        return fail(c, "Out of memory.");
    }
    /* Adobe writes CMYK inverted. */
    bool inverted = cmyk && j.saw_Adobe_marker;
    while (j.output_scanline < j.output_height) {
        int y = (int)j.output_scanline;
        JSAMPROW rp = (JSAMPROW)line;
        jpeg_read_scanlines(&j, &rp, 1);
        unsigned char* o = c.out->row(y);
        const unsigned char* p = line;
        for (int x = 0; x < w; x++, o += 4, p += comps) {
            if (comps == 1) {
                o[0] = o[1] = o[2] = p[0];
            } else if (comps == 3) {
                o[0] = p[0];
                o[1] = p[1];
                o[2] = p[2];
            } else {
                unsigned cc = p[0], m = p[1], yy = p[2], k = p[3];
                if (!inverted) {
                    cc = 255 - cc;
                    m = 255 - m;
                    yy = 255 - yy;
                    k = 255 - k;
                }
                o[0] = (unsigned char)(cc * k / 255);
                o[1] = (unsigned char)(m * k / 255);
                o[2] = (unsigned char)(yy * k / 255);
            }
            o[3] = 255;
        }
    }
    jpeg_finish_decompress(&j);
    jpeg_destroy_decompress(&j);
    free(line);
    return true;
}

} /* namespace internal */
} /* namespace ti */

#else

namespace ti {
namespace internal {

const char* jpeg_system_name()
{
    return 0;
}

bool jpeg_system(Ctx& c)
{
    return fail(c, "No libjpeg in this build.");
}

} /* namespace internal */
} /* namespace ti */

#endif
