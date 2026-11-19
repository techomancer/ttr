/*
 * ti's decoders, one per format and backend (ti/DESIGN.md). Each takes
 * the file from a Ctx and fills ctx.out and ctx.info, or calls fail().
 */
#ifndef TI_INTERNAL_H
#define TI_INTERNAL_H

#include "ti.h"

namespace ti {
namespace internal {

struct Ctx {
    const unsigned char* d;
    size_t n;
    Image* out;
    Info* info;
    char* err;
    size_t errlen;
    int reduce_to;
};

/* The message (printf-style) into ctx.err; returns false. */
bool fail(Ctx& c, const char* fmt, ...);
/* Records the file's size; false (with a message) if it is too big. */
bool check_size(Ctx& c, long w, long h);

/* The system libraries' decoders, and their names: 0 when the build
 * does not have the library. */
const char* png_system_name();
bool png_system(Ctx& c);
const char* jpeg_system_name();
bool jpeg_system(Ctx& c);
const char* gif_system_name();
bool gif_system(Ctx& c);
const char* tiff_system_name();
bool tiff_system(Ctx& c);

/* ti's own SGI and XPM readers. */
bool sgi_decode(Ctx& c);
bool xpm_decode(Ctx& c);
/* An X colour name ("light gray", "Gray50"; case and spaces ignored) as
 * 0xRRGGBB, from the system's rgb.txt or a few built-in ones; -1 if
 * unknown. */
long xpm_color_name(const char* name, size_t len);

/* set_lock()'s lock; no-ops without one. */
void lock();
void unlock();

/* stb_image: PNG, JPEG, GIF, BMP and PNM; the name is 0 for the rest. */
const char* builtin_name(Format f);
bool builtin_decode(Ctx& c, Format f);

} /* namespace internal */
} /* namespace ti */

#endif
