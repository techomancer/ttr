/*
 * ti: ttr's images (ti/DESIGN.md). Decodes PNG, JPEG, GIF, TIFF, BMP,
 * PNM and SGI images from memory into 8-bit RGBA, scales them, and
 * encodes them for terminals (ti/term.h).
 *
 * Each format has two decoders. The system library, when the build has
 * it (TTR_HAVE_PNG libpng 1.2 to 1.6, TTR_HAVE_JPEG libjpeg 6a to
 * libjpeg-turbo 3, TTR_HAVE_GIF giflib 4 or 5, TTR_HAVE_TIFF libtiff 3.4
 * to 4), written against the API every one of those versions has; and
 * the built-in one (stb_image, third_party/stb), used where the library
 * is missing, as IRIX has no giflib. TIFF has no built-in decoder, SGI
 * no library: ti reads SGI images itself.
 *
 * C++98, no STL, no exceptions. Errors are messages, never aborts.
 */
#ifndef TI_TI_H
#define TI_TI_H

#include <stddef.h>

namespace ti {

enum Format {
    FMT_UNKNOWN,
    FMT_PNG,
    FMT_JPEG,
    FMT_GIF,
    FMT_TIFF,
    FMT_BMP,
    FMT_PNM,
    FMT_SGI,
    FMT_COUNT
};

/* "PNG", "JPEG", ... */
const char* format_name(Format f);
/* The format from the first bytes (16 are enough). */
Format sniff(const unsigned char* d, size_t n);

/* Which decoder to use: the library if there is one (else the built-in
 * one), or only one of them (tests compare the two). */
enum Backend {
    BACKEND_ANY,
    BACKEND_SYSTEM,
    BACKEND_BUILTIN
};

/* "libpng 1.6.51", "stb_image 2.30", "ti"; 0 if this backend has none. */
const char* decoder_name(Format f, Backend b = BACKEND_ANY);

/* Pixels: w * h * 4 bytes, R G B A, rows from the top, alpha not
 * premultiplied. Not copyable; take() moves. */
class Image {
public:
    Image();
    ~Image();
    bool alloc(int w, int h);   /* false if too big or out of memory */
    void clear();
    void take(Image& o);        /* o becomes empty */
    bool empty() const { return !rgba; }
    unsigned char* row(int y) const { return rgba + (size_t)y * (size_t)w * 4; }

    int w, h;
    unsigned char* rgba;

private:
    Image(const Image&);
    Image& operator=(const Image&);
};

/* Images larger than this many pixels are refused. */
const long k_max_pixels = 1L << 25;

struct Info {
    Format format;
    int width, height;          /* of the file; the image may be reduced */
    const char* decoder;
    Info() : format(FMT_UNKNOWN), width(0), height(0), decoder(0) {}
};

struct DecodeOptions {
    Backend backend;
    /* Nonzero: the image may come out smaller, down to this many pixels
     * on its longer side, where the decoder can do that for free
     * (JPEG's DCT scaling). */
    int reduce_to;
    DecodeOptions() : backend(BACKEND_ANY), reduce_to(0) {}
};

/* Decode a whole file held in memory. On failure 'err' says why. */
bool decode(const unsigned char* d, size_t n, Image* out, Info* info, char* err,
            size_t errlen, const DecodeOptions& opt = DecodeOptions());

/* Scale to w x h: an area average when shrinking, nearest pixel when
 * growing. */
bool scale(const Image& in, int w, int h, Image* out);
/* The largest w x h that fits in max_w x max_h at the image's aspect,
 * where a pixel of the target is 'aspect' (width / height) as wide as it
 * is tall. Never larger than the image unless 'grow'. */
void fit(int iw, int ih, int max_w, int max_h, double aspect, bool grow, int* w, int* h);
/* Put the image over a solid 0xRRGGBB background; alpha becomes 255. */
void flatten(Image* im, unsigned long bg);

} /* namespace ti */

#endif
