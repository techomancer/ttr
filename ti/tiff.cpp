/*
 * TIFF through libtiff's TIFFClientOpen and TIFFReadRGBAImage, which
 * 3.4 (IRIX 6.5) and 4.x share. 4.x widened toff_t to 64 bits, so the
 * procedures use libtiff's own typedefs; its RGBA raster is bottom-up
 * in both (TIFFReadRGBAImageOriented is newer), so rows are flipped,
 * and premultiplied by alpha, which is undone.
 */
#include "internal.h"

#if defined(TTR_HAVE_TIFF)

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tiffio.h>

namespace ti {
namespace internal {

namespace {

#if defined(TIFF_VERSION_BIG)
typedef uint32_t U32;
#else
typedef uint32 U32;
#endif

struct Src {
    Ctx* c;
    size_t pos;
};

/* libtiff reports through process-wide handlers; ti runs on one thread. */
char g_msg[200];

void error_fn(const char*, const char* fmt, va_list ap)
{
    vsnprintf(g_msg, sizeof(g_msg), fmt, ap);
}

void warning_fn(const char*, const char*, va_list)
{
}

tsize_t read_fn(thandle_t h, tdata_t buf, tsize_t n)
{
    Src* s = (Src*)h;
    size_t left = s->c->n - s->pos;
    size_t k = (size_t)n < left ? (size_t)n : left;
    memcpy(buf, s->c->d + s->pos, k);
    s->pos += k;
    return (tsize_t)k;
}

tsize_t write_fn(thandle_t, tdata_t, tsize_t)
{
    return 0;
}

toff_t seek_fn(thandle_t h, toff_t off, int whence)
{
    Src* s = (Src*)h;
    double to = whence == SEEK_SET ? (double)off
              : whence == SEEK_CUR ? (double)s->pos + (double)off
              : (double)s->c->n + (double)off;
    if (to < 0 || to > (double)s->c->n)
        return (toff_t)-1;
    s->pos = (size_t)to;
    return (toff_t)s->pos;
}

int close_fn(thandle_t)
{
    return 0;
}

toff_t size_fn(thandle_t h)
{
    return (toff_t)((Src*)h)->c->n;
}

int map_fn(thandle_t, tdata_t*, toff_t*)
{
    return 0;
}

void unmap_fn(thandle_t, tdata_t, toff_t)
{
}

/* The first value of a SHORT or LONG entry in the first directory; 'dflt'
 * if absent, -1 if the file is broken. */
long ifd_value(const unsigned char* d, size_t n, bool le, unsigned tag, long dflt)
{
#define TI_U16(p) (le ? (unsigned)(p)[0] | (unsigned)(p)[1] << 8 : (unsigned)(p)[0] << 8 | (p)[1])
#define TI_U32(p) (le ? (unsigned long)TI_U16(p) | (unsigned long)TI_U16((p) + 2) << 16 \
                      : (unsigned long)TI_U16(p) << 16 | (unsigned long)TI_U16((p) + 2))
    if (n < 8)
        return -1;
    unsigned long ifd = TI_U32(d + 4);
    if (ifd + 2 > n)
        return -1;
    unsigned count = TI_U16(d + ifd);
    if (ifd + 2 + (unsigned long)count * 12 > n)
        return -1;
    for (unsigned i = 0; i < count; i++) {
        const unsigned char* e = d + ifd + 2 + i * 12;
        if (TI_U16(e) != tag)
            continue;
        unsigned type = TI_U16(e + 2);
        unsigned long cnt = TI_U32(e + 4);
        size_t size = type == 3 ? 2 : type == 4 ? 4 : 0;
        if (!size || !cnt)
            return -1;
        const unsigned char* v = e + 8;
        if (size * cnt > 4) {
            unsigned long at = TI_U32(e + 8);
            if (at + size > n)
                return -1;
            v = d + at;
        }
        return size == 2 ? (long)TI_U16(v) : (long)TI_U32(v);
    }
    return dflt;
#undef TI_U32
#undef TI_U16
}

/* libtiff 3.4 (IRIX) divides by the scanline size while it reads the
 * directory, and traps when that is 0: refuse such files first. */
bool sane(const unsigned char* d, size_t n)
{
    bool le = d[0] == 'I';
    long w = ifd_value(d, n, le, 256, -1), h = ifd_value(d, n, le, 257, -1);
    long bits = ifd_value(d, n, le, 258, 1), spp = ifd_value(d, n, le, 277, 1);
    long tw = ifd_value(d, n, le, 322, 0), th = ifd_value(d, n, le, 323, 0);
    if (w <= 0 || h <= 0 || bits <= 0 || spp <= 0 || tw < 0 || th < 0)
        return false;
    double line = (double)w * (double)bits * (double)spp;
    double tile = (double)tw * (double)bits * (double)spp * (double)th;
    return line >= 1 && line < 4294967296.0 && tile < 4294967296.0;
}

} /* namespace */

const char* tiff_system_name()
{
    /* "LIBTIFF, Version 4.7.0\n...": "libtiff 4.7.0". */
    static char name[40];
    if (!name[0]) {
        const char* v = TIFFGetVersion();
        const char* p = v ? strstr(v, "Version ") : 0;
        int n = 0;
        if (p) {
            p += 8;
            while (p[n] && p[n] != '\n' && p[n] != ' ' && n < 20)
                n++;
        }
        sprintf(name, "libtiff %.*s", n, p ? p : "");
    }
    return name;
}

bool tiff_system(Ctx& c)
{
    if (!sane(c.d, c.n))
        return fail(c, "TIFF: the image's size or layout is damaged.");
    Src src;
    src.c = &c;
    src.pos = 0;
    g_msg[0] = 0;
    TIFFErrorHandler old_err = TIFFSetErrorHandler(error_fn);
    TIFFErrorHandler old_warn = TIFFSetWarningHandler(warning_fn);
    bool ok = false;
    /* 'c': no strip chopping. libtiff 3.4 divides by zero chopping a
     * single uncompressed strip whose rows are over 8 KB; the whole file
     * is in memory anyway. */
    TIFF* t = TIFFClientOpen("image", "rmc", (thandle_t)&src, read_fn, write_fn, seek_fn, close_fn,
                             size_fn, map_fn, unmap_fn);
    if (!t) {
        fail(c, "TIFF: %s", g_msg[0] ? g_msg : "not a TIFF file.");
    } else {
        U32 w = 0, h = 0;
        TIFFGetField(t, TIFFTAG_IMAGEWIDTH, &w);
        TIFFGetField(t, TIFFTAG_IMAGELENGTH, &h);
        if (check_size(c, (long)w, (long)h)) {
            if (!c.out->alloc((int)w, (int)h)) {
                fail(c, "Out of memory.");
            } else if (!TIFFReadRGBAImage(t, w, h, (U32*)c.out->rgba, 0)) {
                fail(c, "TIFF: %s", g_msg[0] ? g_msg : "cannot read this kind of TIFF.");
            } else {
                /* ABGR words, bottom row first: RGBA bytes from the top. */
                for (U32 y = 0; y < h; y++) {
                    U32* r = (U32*)c.out->row((int)y);
                    for (U32 x = 0; x < w; x++) {
                        U32 v = r[x];
                        unsigned char* o = (unsigned char*)(r + x);
                        o[0] = (unsigned char)TIFFGetR(v);
                        o[1] = (unsigned char)TIFFGetG(v);
                        o[2] = (unsigned char)TIFFGetB(v);
                        unsigned al = TIFFGetA(v);
                        o[3] = (unsigned char)al;
                        /* libtiff premultiplies by alpha; ti does not. */
                        if (al && al < 255)
                            for (int k = 0; k < 3; k++) {
                                unsigned cv = (o[k] * 255u + al / 2) / al;
                                o[k] = (unsigned char)(cv > 255 ? 255 : cv);
                            }
                    }
                }
                size_t rb = (size_t)w * 4;
                unsigned char* tmp = (unsigned char*)malloc(rb);
                if (tmp) {
                    for (U32 y = 0; y < h / 2; y++) {
                        unsigned char* a = c.out->row((int)y);
                        unsigned char* b = c.out->row((int)(h - 1 - y));
                        memcpy(tmp, a, rb);
                        memcpy(a, b, rb);
                        memcpy(b, tmp, rb);
                    }
                    free(tmp);
                    ok = true;
                } else {
                    fail(c, "Out of memory.");
                }
            }
        }
        TIFFClose(t);
    }
    TIFFSetErrorHandler(old_err);
    TIFFSetWarningHandler(old_warn);
    return ok;
}

} /* namespace internal */
} /* namespace ti */

#else

namespace ti {
namespace internal {

const char* tiff_system_name()
{
    return 0;
}

bool tiff_system(Ctx& c)
{
    return fail(c, "No libtiff in this build.");
}

} /* namespace internal */
} /* namespace ti */

#endif
