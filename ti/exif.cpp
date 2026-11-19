/*
 * ti's EXIF reader: the TIFF structure in a JPEG's APP1 ("Exif\0\0"), a
 * PNG's eXIf chunk, or a TIFF file itself. Bounds-checked against the
 * bytes given; anything damaged is skipped, never trusted.
 */
#include "ti.h"

#include <stdio.h>
#include <string.h>

namespace ti {

namespace {

struct Tiff {
    const unsigned char* d;
    size_t n;
    bool le;
    unsigned long u16(size_t at) const
    {
        if (at + 2 > n)
            return 0;
        return le ? (unsigned long)d[at] | ((unsigned long)d[at + 1] << 8)
                  : ((unsigned long)d[at] << 8) | d[at + 1];
    }
    unsigned long u32(size_t at) const
    {
        if (at + 4 > n)
            return 0;
        return le ? (unsigned long)d[at] | ((unsigned long)d[at + 1] << 8) |
                        ((unsigned long)d[at + 2] << 16) | ((unsigned long)d[at + 3] << 24)
                  : ((unsigned long)d[at] << 24) | ((unsigned long)d[at + 1] << 16) |
                        ((unsigned long)d[at + 2] << 8) | d[at + 3];
    }
};

/* An entry's values: in the entry when they fit in 4 bytes, else at the
 * offset it holds. 0 if they are not all there. */
size_t value_at(const Tiff& t, size_t e, size_t size)
{
    unsigned long count = t.u32(e + 4);
    if (count == 0 || count > 0x100000UL)
        return 0;
    unsigned long bytes = count * size;
    if (bytes <= 4)
        return e + 8;
    size_t at = (size_t)t.u32(e + 8);
    if (at == 0 || at + bytes > t.n || at + bytes < at)
        return 0;
    return at;
}

void ascii(const Tiff& t, size_t e, char* out, size_t len)
{
    out[0] = 0;
    if (t.u16(e + 2) != 2)
        return;
    size_t at = value_at(t, e, 1);
    if (!at)
        return;
    size_t count = t.u32(e + 4), k = 0;
    for (size_t i = 0; i < count && k + 1 < len; i++) {
        unsigned char c = t.d[at + i];
        if (!c)
            break;
        out[k++] = c < 32 ? ' ' : (char)c;
    }
    /* Cameras pad with spaces. */
    while (k > 0 && out[k - 1] == ' ')
        k--;
    out[k] = 0;
}

/* The first value as a number: SHORT, LONG, RATIONAL or SRATIONAL. */
double number(const Tiff& t, size_t e, int index = 0)
{
    unsigned long type = t.u16(e + 2);
    if ((unsigned long)index >= t.u32(e + 4))
        return 0;
    switch (type) {
    case 3: {
        size_t at = value_at(t, e, 2);
        return at ? (double)t.u16(at + 2 * (size_t)index) : 0;
    }
    case 4: {
        size_t at = value_at(t, e, 4);
        return at ? (double)t.u32(at + 4 * (size_t)index) : 0;
    }
    case 5: case 10: {
        size_t at = value_at(t, e, 8);
        if (!at)
            return 0;
        unsigned long nu = t.u32(at + 8 * (size_t)index), de = t.u32(at + 8 * (size_t)index + 4);
        if (!de)
            return 0;
        if (type == 10)
            return (double)(int)(unsigned int)nu / (double)(int)(unsigned int)de;
        return (double)nu / (double)de;
    }
    default:
        return 0;
    }
}

enum Ifd { IFD_MAIN, IFD_EXIF, IFD_GPS };

void read_ifd(const Tiff& t, size_t off, Ifd which, Exif* x, int depth)
{
    if (depth > 3 || off + 2 > t.n)
        return;
    unsigned long count = t.u16(off);
    double lat[3] = { 0, 0, 0 }, lon[3] = { 0, 0, 0 };
    char lat_ref = 0, lon_ref = 0;
    bool has_lat = false, has_lon = false;
    for (unsigned long i = 0; i < count && i < 1000; i++) {
        size_t e = off + 2 + i * 12;
        if (e + 12 > t.n)
            break;
        unsigned long tag = t.u16(e);
        if (which == IFD_GPS) {
            char ref[4];
            switch (tag) {
            case 1: ascii(t, e, ref, sizeof(ref)); lat_ref = ref[0]; break;
            case 2:
                for (int k = 0; k < 3; k++)
                    lat[k] = number(t, e, k);
                has_lat = t.u32(e + 4) >= 3;
                break;
            case 3: ascii(t, e, ref, sizeof(ref)); lon_ref = ref[0]; break;
            case 4:
                for (int k = 0; k < 3; k++)
                    lon[k] = number(t, e, k);
                has_lon = t.u32(e + 4) >= 3;
                break;
            case 6: x->altitude = number(t, e); x->has_altitude = true; break;
            default: break;
            }
            continue;
        }
        switch (tag) {
        case 0x010e: ascii(t, e, x->description, sizeof(x->description)); break;
        case 0x010f: ascii(t, e, x->make, sizeof(x->make)); break;
        case 0x0110: ascii(t, e, x->model, sizeof(x->model)); break;
        case 0x0112: x->orientation = (int)number(t, e); break;
        case 0x0131: ascii(t, e, x->software, sizeof(x->software)); break;
        case 0x0132: if (!x->date[0]) ascii(t, e, x->date, sizeof(x->date)); break;
        case 0x013b: ascii(t, e, x->artist, sizeof(x->artist)); break;
        case 0x8298: ascii(t, e, x->copyright, sizeof(x->copyright)); break;
        case 0x829a: x->exposure = number(t, e); break;
        case 0x829d: x->fnumber = number(t, e); break;
        case 0x8827: x->iso = (int)number(t, e); break;
        case 0x9003: ascii(t, e, x->date, sizeof(x->date)); break;
        case 0x9204: x->bias = number(t, e); x->has_bias = true; break;
        case 0x9209: x->flash = (int)number(t, e); break;
        case 0x920a: x->focal = number(t, e); break;
        case 0xa405: x->focal35 = (int)number(t, e); break;
        case 0xa434: ascii(t, e, x->lens, sizeof(x->lens)); break;
        case 0x8769: read_ifd(t, t.u32(e + 8), IFD_EXIF, x, depth + 1); break;
        case 0x8825: read_ifd(t, t.u32(e + 8), IFD_GPS, x, depth + 1); break;
        default: break;
        }
    }
    if (has_lat && has_lon) {
        x->latitude = lat[0] + lat[1] / 60 + lat[2] / 3600;
        x->longitude = lon[0] + lon[1] / 60 + lon[2] / 3600;
        if (lat_ref == 'S')
            x->latitude = -x->latitude;
        if (lon_ref == 'W')
            x->longitude = -x->longitude;
        x->has_gps = true;
    }
}

bool read_tiff(const unsigned char* d, size_t n, Exif* x)
{
    if (n < 8)
        return false;
    Tiff t;
    t.d = d;
    t.n = n;
    if (d[0] == 'I' && d[1] == 'I')
        t.le = true;
    else if (d[0] == 'M' && d[1] == 'M')
        t.le = false;
    else
        return false;
    if (t.u16(2) != 42)
        return false;
    read_ifd(t, t.u32(4), IFD_MAIN, x, 0);
    x->present = true;
    if (x->orientation < 1 || x->orientation > 8)
        x->orientation = 0;
    return true;
}

} /* namespace */

Exif::Exif()
{
    memset(this, 0, sizeof(*this));
    flash = -1;
}

bool read_exif(const unsigned char* d, size_t n, Exif* x)
{
    *x = Exif();
    Format f = sniff(d, n);
    if (f == FMT_TIFF)
        return read_tiff(d, n, x);
    if (f == FMT_JPEG) {
        size_t at = 2;
        while (at + 4 <= n) {
            if (d[at] != 0xff)
                return false;
            int m = d[at + 1];
            if (m == 0xff) {
                at++;
                continue;
            }
            if (m == 0xd9 || m == 0xda)
                return false;
            size_t len = ((size_t)d[at + 2] << 8) | d[at + 3];
            if (len < 2)
                return false;
            if (m == 0xe1 && len >= 8 && at + 10 <= n && memcmp(d + at + 4, "Exif\0\0", 6) == 0) {
                size_t end = at + 2 + len;
                if (end > n)
                    end = n;
                return read_tiff(d + at + 10, end - (at + 10), x);
            }
            at += 2 + len;
        }
        return false;
    }
    if (f == FMT_PNG) {
        size_t at = 8;
        while (at + 12 <= n) {
            unsigned long cl = ((unsigned long)d[at] << 24) | ((unsigned long)d[at + 1] << 16) |
                               ((unsigned long)d[at + 2] << 8) | d[at + 3];
            if (cl > n - at - 12)
                return false;
            if (memcmp(d + at + 4, "eXIf", 4) == 0)
                return read_tiff(d + at + 8, cl, x);
            if (memcmp(d + at + 4, "IDAT", 4) == 0)
                return false;
            at += 12 + cl;
        }
    }
    return false;
}

bool orient(const Image& in, int o, Image* out)
{
    if (o < 2 || o > 8 || in.empty())
        return false;
    bool swap = o >= 5;
    int w = swap ? in.h : in.w, h = swap ? in.w : in.h;
    if (!out->alloc(w, h))
        return false;
    for (int y = 0; y < in.h; y++) {
        const unsigned char* s = in.row(y);
        for (int x = 0; x < in.w; x++, s += 4) {
            int dx, dy;
            switch (o) {
            case 2: dx = in.w - 1 - x; dy = y; break;                       /* mirrored */
            case 3: dx = in.w - 1 - x; dy = in.h - 1 - y; break;            /* 180 */
            case 4: dx = x; dy = in.h - 1 - y; break;                       /* flipped */
            case 5: dx = y; dy = x; break;                                  /* transposed */
            case 6: dx = in.h - 1 - y; dy = x; break;                       /* 90 clockwise */
            case 7: dx = in.h - 1 - y; dy = in.w - 1 - x; break;            /* transversed */
            default: dx = y; dy = in.w - 1 - x; break;                      /* 8: 90 counter */
            }
            memcpy(out->row(dy) + dx * 4, s, 4);
        }
    }
    return true;
}

void exif_summary(const Exif& x, char* out, size_t len)
{
    out[0] = 0;
    size_t n = 0;
    if (x.exposure > 0) {
        if (x.exposure < 0.5)
            n += snprintf(out + n, len - n, "1/%.0fs", 1.0 / x.exposure);
        else
            n += snprintf(out + n, len - n, "%.1fs", x.exposure);
    }
    if (x.fnumber > 0 && n < len)
        n += snprintf(out + n, len - n, "%sf/%.1f", n ? " " : "", x.fnumber);
    if (x.iso > 0 && n < len)
        n += snprintf(out + n, len - n, "%sISO %d", n ? " " : "", x.iso);
    if (x.focal > 0 && n < len) {
        n += snprintf(out + n, len - n, "%s%.0fmm", n ? " " : "", x.focal);
        if (x.focal35 > 0 && n < len)
            n += snprintf(out + n, len - n, " (%dmm)", x.focal35);
    }
}

} /* namespace ti */
