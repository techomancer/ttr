/*
 * probe(): an image's size, bits a pixel and palette from its header,
 * without decoding it. Each format's own reader, bounds-checked against
 * the bytes given (the start of the file may be all there is).
 */
#include "internal.h"

#include <string.h>

namespace ti {

namespace {

unsigned long be16(const unsigned char* p)
{
    return ((unsigned long)p[0] << 8) | p[1];
}

unsigned long be32(const unsigned char* p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) |
           p[3];
}

unsigned long le16(const unsigned char* p)
{
    return ((unsigned long)p[1] << 8) | p[0];
}

unsigned long le32(const unsigned char* p)
{
    return ((unsigned long)p[3] << 24) | ((unsigned long)p[2] << 16) | ((unsigned long)p[1] << 8) |
           p[0];
}

bool sized(Info* in, long w, long h)
{
    in->width = (int)w;
    in->height = (int)h;
    return w > 0 && h > 0 && w <= 0x7fffffffL && h <= 0x7fffffffL;
}

bool png(const unsigned char* d, size_t n, Info* in)
{
    if (n < 33 || memcmp(d + 12, "IHDR", 4) != 0)
        return false;
    int depth = d[24], type = d[25];
    switch (type) {
    case 0: in->bits = depth; break;
    case 2: in->bits = depth * 3; break;
    case 3: in->bits = depth; break;
    case 4: in->bits = depth * 2; break;
    case 6: in->bits = depth * 4; break;
    default: return false;
    }
    if (type == 3) {
        /* The palette's size: PLTE comes before the first IDAT. */
        size_t at = 8;
        while (at + 8 <= n) {
            unsigned long len = be32(d + at);
            if (memcmp(d + at + 4, "PLTE", 4) == 0) {
                in->colors = (int)(len / 3);
                break;
            }
            if (memcmp(d + at + 4, "IDAT", 4) == 0 || len > n)
                break;
            at += 12 + len;
        }
        if (!in->colors)
            in->colors = 1 << depth;
    }
    return sized(in, (long)be32(d + 16), (long)be32(d + 20));
}

bool jpeg(const unsigned char* d, size_t n, Info* in)
{
    size_t at = 2;
    while (at + 4 <= n) {
        if (d[at] != 0xff)
            return false;
        int m = d[at + 1];
        if (m == 0xff) {        /* fill byte */
            at++;
            continue;
        }
        if (m == 0xd8 || m == 0x01 || (m >= 0xd0 && m <= 0xd7)) {
            at += 2;
            continue;
        }
        if (m == 0xd9 || m == 0xda)
            return false;       /* no frame header before the scan */
        size_t len = be16(d + at + 2);
        if (len < 2)
            return false;
        /* Frame headers: SOF0 to SOF15 but DHT, JPG and DAC. */
        if (m >= 0xc0 && m <= 0xcf && m != 0xc4 && m != 0xc8 && m != 0xcc) {
            if (at + 10 > n)
                return false;
            in->bits = d[at + 4] * d[at + 9];
            return sized(in, (long)be16(d + at + 7), (long)be16(d + at + 5));
        }
        at += 2 + len;
    }
    return false;
}

bool gif(const unsigned char* d, size_t n, Info* in)
{
    if (n < 13)
        return false;
    bool ok = sized(in, (long)le16(d + 6), (long)le16(d + 8));
    int bits = 0;
    size_t at = 13;
    if (d[10] & 0x80) {
        bits = (d[10] & 7) + 1;
        at += (size_t)3 << bits;
    } else {
        /* No global table: the first image's local one. */
        while (at < n && !bits) {
            if (d[at] == 0x21) {            /* an extension: skip its blocks */
                at += 2;
                while (at < n && d[at])
                    at += 1 + d[at];
                at++;
            } else if (d[at] == 0x2c && at + 10 <= n) {
                if (d[at + 9] & 0x80)
                    bits = (d[at + 9] & 7) + 1;
                break;
            } else {
                break;
            }
        }
    }
    if (!bits)
        bits = 8;
    in->bits = bits;
    in->colors = 1 << bits;
    return ok;
}

bool bmp(const unsigned char* d, size_t n, Info* in)
{
    if (n < 26)
        return false;
    unsigned long hs = le32(d + 14);
    long w, h;
    int bits;
    unsigned long used = 0;
    if (hs == 12) {
        w = (long)le16(d + 18);
        h = (long)le16(d + 20);
        bits = (int)le16(d + 24);
    } else {
        if (n < 34)
            return false;
        w = (long)le32(d + 18);
        unsigned long uh = le32(d + 22);
        /* Top-down images have a negative height. */
        h = uh & 0x80000000UL ? (long)(0xffffffffUL - uh) + 1 : (long)uh;
        bits = (int)le16(d + 28);
        if (n >= 50)
            used = le32(d + 46);
    }
    in->bits = bits;
    if (bits <= 8)
        in->colors = used && used <= 256 ? (int)used : 1 << bits;
    return bits > 0 && sized(in, w, h);
}

/* The next number of a PNM header, past white space and comments. */
bool pnm_number(const unsigned char* d, size_t n, size_t* at, long* v)
{
    size_t i = *at;
    for (;;) {
        while (i < n && (d[i] == ' ' || d[i] == '\t' || d[i] == '\r' || d[i] == '\n'))
            i++;
        if (i < n && d[i] == '#') {
            while (i < n && d[i] != '\n')
                i++;
            continue;
        }
        break;
    }
    if (i >= n || d[i] < '0' || d[i] > '9')
        return false;
    long r = 0;
    while (i < n && d[i] >= '0' && d[i] <= '9' && r < 100000000L)
        r = r * 10 + (d[i++] - '0');
    *at = i;
    *v = r;
    return true;
}

bool pnm(const unsigned char* d, size_t n, Info* in)
{
    size_t at = 2;
    long w, h, maxval;
    if (!pnm_number(d, n, &at, &w) || !pnm_number(d, n, &at, &h) ||
        !pnm_number(d, n, &at, &maxval))
        return false;
    int per = maxval > 255 ? 16 : 8;
    in->bits = d[1] == '6' ? per * 3 : per;
    return sized(in, w, h);
}

bool sgi(const unsigned char* d, size_t n, Info* in)
{
    if (n < 12)
        return false;
    int bpc = d[3];
    unsigned long dim = be16(d + 4), z = be16(d + 10);
    long w = (long)be16(d + 6), h = dim >= 2 ? (long)be16(d + 8) : 1;
    if (dim < 3)
        z = 1;
    in->bits = bpc * 8 * (int)z;
    return sized(in, w, h);
}

bool tiff(const unsigned char* d, size_t n, Info* in)
{
    bool le = d[0] == 'I';
    unsigned long off = le ? le32(d + 4) : be32(d + 4);
    if (off + 2 > n)
        return false;
    unsigned long count = le ? le16(d + off) : be16(d + off);
    long w = 0, h = 0;
    int bps = 1, spp = 1, photometric = -1;
    for (unsigned long i = 0; i < count; i++) {
        size_t e = off + 2 + i * 12;
        if (e + 12 > n)
            break;
        unsigned long tag = le ? le16(d + e) : be16(d + e);
        unsigned long type = le ? le16(d + e + 2) : be16(d + e + 2);
        unsigned long cnt = le ? le32(d + e + 4) : be32(d + e + 4);
        /* The first value: in the entry when it fits in 4 bytes. */
        unsigned long v;
        if (type == 3) {
            const unsigned char* p = d + e + 8;
            if (cnt > 2) {
                unsigned long at = le ? le32(p) : be32(p);
                p = at + 2 <= n ? d + at : 0;
            }
            v = p ? (le ? le16(p) : be16(p)) : 0;
        } else {
            v = le ? le32(d + e + 8) : be32(d + e + 8);
        }
        switch (tag) {
        case 256: w = (long)v; break;
        case 257: h = (long)v; break;
        case 258: bps = (int)v; break;
        case 262: photometric = (int)v; break;
        case 277: spp = (int)v; break;
        default: break;
        }
    }
    in->bits = bps * spp;
    if (photometric == 3 && bps <= 8)
        in->colors = 1 << bps;
    return sized(in, w, h);
}

/* XPM: the first string, "width height ncolors chars_per_pixel". */
bool xpm(const unsigned char* d, size_t n, Info* in)
{
    size_t i = 9;
    bool comment = false;
    for (; i < n; i++) {
        if (comment) {
            if (d[i] == '*' && i + 1 < n && d[i + 1] == '/') {
                comment = false;
                i++;
            }
        } else if (d[i] == '/' && i + 1 < n && d[i + 1] == '*') {
            comment = true;
            i++;
        } else if (d[i] == '"') {
            break;
        }
    }
    if (i >= n)
        return false;
    i++;
    long v[3];
    for (int k = 0; k < 3; k++) {
        while (i < n && (d[i] == ' ' || d[i] == '\t'))
            i++;
        if (i >= n || d[i] < '0' || d[i] > '9')
            return false;
        long r = 0;
        while (i < n && d[i] >= '0' && d[i] <= '9' && r < 100000000L)
            r = r * 10 + (d[i++] - '0');
        v[k] = r;
    }
    in->colors = (int)v[2];
    int bits = 1;
    while (bits < 24 && (1L << bits) < v[2])
        bits++;
    in->bits = bits;
    return sized(in, v[0], v[1]);
}

} /* namespace */

bool probe(const unsigned char* d, size_t n, Info* info)
{
    *info = Info();
    Format f = sniff(d, n);
    info->format = f;
    switch (f) {
    case FMT_PNG: return png(d, n, info);
    case FMT_JPEG: return jpeg(d, n, info);
    case FMT_GIF: return gif(d, n, info);
    case FMT_TIFF: return tiff(d, n, info);
    case FMT_BMP: return bmp(d, n, info);
    case FMT_PNM: return pnm(d, n, info);
    case FMT_SGI: return sgi(d, n, info);
    case FMT_XPM: return xpm(d, n, info);
    default: return false;
    }
}

} /* namespace ti */
