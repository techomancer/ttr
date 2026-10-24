/*
 * SGI images (.rgb, .rgba, .bw, .sgi; Paul Haeberli's format, IRIX's
 * own): a 512-byte big-endian header, then each channel's rows from the
 * bottom, verbatim or run-length encoded, at 1 or 2 bytes a sample.
 */
#include "internal.h"

#include <string.h>

namespace ti {
namespace internal {

namespace {

unsigned be16(const unsigned char* p)
{
    return (unsigned)p[0] << 8 | p[1];
}

unsigned long be32(const unsigned char* p)
{
    return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

/* One RLE row of one channel into out (w samples, the high byte of
 * 16-bit ones). False if it runs past the file or the row. */
bool rle_row(const unsigned char* d, size_t n, unsigned long at, int bpc, unsigned char* out,
             int w, int stride)
{
    int x = 0;
    for (;;) {
        if (at + (unsigned long)bpc > n)
            return false;
        unsigned v = bpc == 1 ? d[at] : be16(d + at);
        at += (unsigned long)bpc;
        int count = (int)(v & 0x7f);
        if (!count)
            return true;
        if (x + count > w)
            return false;
        if (v & 0x80) {
            if (at + (unsigned long)count * (unsigned long)bpc > n)
                return false;
            for (int k = 0; k < count; k++, at += (unsigned long)bpc)
                out[(x + k) * stride] = d[at];
        } else {
            if (at + (unsigned long)bpc > n)
                return false;
            unsigned char s = d[at];
            at += (unsigned long)bpc;
            for (int k = 0; k < count; k++)
                out[(x + k) * stride] = s;
        }
        x += count;
    }
}

} /* namespace */

bool sgi_decode(Ctx& c)
{
    const unsigned char* d = c.d;
    if (c.n < 512)
        return fail(c, "SGI: the header is cut short.");
    int storage = d[2], bpc = d[3];
    unsigned dim = be16(d + 4);
    int w = (int)be16(d + 6), h = (int)be16(d + 8), z = (int)be16(d + 10);
    if (dim == 1)
        h = 1;
    if (dim < 3)
        z = 1;
    if (be32(d + 104) != 0)
        return fail(c, "SGI: colour-mapped images are not supported.");
    if (z < 1 || z > 4)
        z = z < 1 ? 1 : 4;
    if (!check_size(c, w, h))
        return false;
    if (!c.out->alloc(w, h))
        return fail(c, "Out of memory.");
    /* Unset channels: opaque black, or grey copied below. */
    memset(c.out->rgba, 0, (size_t)w * (size_t)h * 4);
    int chans = z == 2 ? 2 : z;     /* channels in the file that we read */
    unsigned long rows = (unsigned long)h * (unsigned long)z;
    bool ok = true;
    for (int ch = 0; ch < chans && ok; ch++) {
        /* The channel's place in RGBA: grey goes to R (copied after),
         * grey + alpha's alpha to A. */
        int dst = z == 2 && ch == 1 ? 3 : ch;
        for (int y = 0; y < h && ok; y++) {
            unsigned char* o = c.out->row(h - 1 - y) + dst;
            unsigned long r = (unsigned long)ch * (unsigned long)h + (unsigned long)y;
            if (storage == 1) {
                if (512 + rows * 8 > c.n) {
                    ok = false;
                    break;
                }
                unsigned long at = be32(d + 512 + r * 4);
                ok = rle_row(d, c.n, at, bpc, o, w, 4);
            } else {
                unsigned long at = 512 + r * (unsigned long)w * (unsigned long)bpc;
                if (at + (unsigned long)w * (unsigned long)bpc > c.n) {
                    ok = false;
                    break;
                }
                for (int x = 0; x < w; x++)
                    o[x * 4] = d[at + (unsigned long)x * (unsigned long)bpc];
            }
        }
    }
    if (!ok)
        return fail(c, "SGI: the file is cut short or damaged.");
    size_t np = (size_t)w * (size_t)h;
    unsigned char* p = c.out->rgba;
    for (size_t i = 0; i < np; i++, p += 4) {
        if (z <= 2)
            p[1] = p[2] = p[0];
        if (z != 2 && z != 4)
            p[3] = 255;
    }
    return true;
}

} /* namespace internal */
} /* namespace ti */
