/*
 * ti's camera raw decoder: libraw (TTR_HAVE_RAW), its thread-safe C API
 * (libraw_r). Not in libti.a: the programs that want raw files link
 * ti_raw.o and RAW_LIBS and call raw_init() (ti.h).
 */
#include "internal.h"
#include "ti.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef TTR_HAVE_RAW
#include <libraw/libraw.h>
#endif

namespace ti {

using namespace internal;

#ifdef TTR_HAVE_RAW

namespace {

char g_name[40];

const char* raw_name_fn()
{
    return g_name;
}

/* libraw's flip as EXIF's orientation. */
int orientation(int flip)
{
    switch (flip) {
    case 3: return 3;
    case 5: return 8;
    case 6: return 6;
    default: return 1;
    }
}

int bits_of(libraw_data_t* lr)
{
    int b = lr->color.raw_bps;
    if (b <= 0) {
        unsigned m = lr->color.maximum;
        for (b = 0; m; m >>= 1)
            b++;
    }
    return b > 0 ? b : 0;
}

void fill_info(libraw_data_t* lr, Info* info)
{
    int w = lr->sizes.width, h = lr->sizes.height;
    if (lr->sizes.flip & 4) {
        int t = w;
        w = h;
        h = t;
    }
    info->format = FMT_RAW;
    info->width = w;
    info->height = h;
    info->bits = bits_of(lr);
    info->colors = 0;
}

void copy(char* out, size_t n, const char* s)
{
    size_t l = 0;
    while (s[l] && l + 1 < n) {
        out[l] = s[l];
        l++;
    }
    out[l] = 0;
    /* Cameras pad with spaces. */
    while (l > 0 && out[l - 1] == ' ')
        out[--l] = 0;
}

void fill_exif(libraw_data_t* lr, Exif* x)
{
    *x = Exif();
    x->present = true;
    copy(x->make, sizeof(x->make), lr->idata.make);
    copy(x->model, sizeof(x->model), lr->idata.model);
    copy(x->lens, sizeof(x->lens), lr->lens.Lens[0] ? lr->lens.Lens : lr->lens.makernotes.Lens);
    copy(x->software, sizeof(x->software), lr->idata.software);
    copy(x->artist, sizeof(x->artist), lr->other.artist);
    copy(x->description, sizeof(x->description), lr->other.desc);
    if (lr->other.timestamp > 0) {
        time_t t = lr->other.timestamp;
        struct tm* tm = localtime(&t);
        if (tm)
            strftime(x->date, sizeof(x->date), "%Y:%m:%d %H:%M:%S", tm);
    }
    x->orientation = 0;     /* the images come out upright */
    x->exposure = lr->other.shutter;
    x->fnumber = lr->other.aperture;
    x->focal = lr->other.focal_len;
    x->iso = (int)(lr->other.iso_speed + 0.5f);
    x->focal35 = lr->lens.FocalLengthIn35mmFormat;
    x->flash = -1;
    const libraw_gps_info_t& g = lr->other.parsed_gps;
    if (g.gpsparsed) {
        x->has_gps = true;
        x->latitude = g.latitude[0] + g.latitude[1] / 60.0 + g.latitude[2] / 3600.0;
        x->longitude = g.longitude[0] + g.longitude[1] / 60.0 + g.longitude[2] / 3600.0;
        if (g.latref == 'S')
            x->latitude = -x->latitude;
        if (g.longref == 'W')
            x->longitude = -x->longitude;
        if (g.altitude != 0) {
            x->has_altitude = true;
            x->altitude = g.altref ? -g.altitude : g.altitude;
        }
    }
}

/* RGB (or grey) 8-bit rows into RGBA. */
bool from_rgb(const unsigned char* d, int w, int h, int colors, Image* out)
{
    if (!out->alloc(w, h))
        return false;
    for (int y = 0; y < h; y++) {
        const unsigned char* s = d + (size_t)y * w * colors;
        unsigned char* o = out->row(y);
        for (int x = 0; x < w; x++, o += 4, s += colors) {
            o[0] = s[0];
            o[1] = colors >= 3 ? s[1] : s[0];
            o[2] = colors >= 3 ? s[2] : s[0];
            o[3] = 255;
        }
    }
    return true;
}

/* The camera's preview, upright, if it has one at least 'want' pixels on
 * its longer side. */
bool preview(Ctx& c, libraw_data_t* lr, int want)
{
    int tw = lr->thumbnail.twidth, th = lr->thumbnail.theight;
    if ((tw > th ? tw : th) < want || libraw_unpack_thumb(lr) != LIBRAW_SUCCESS)
        return false;
    int e = 0;
    libraw_processed_image_t* t = libraw_dcraw_make_mem_thumb(lr, &e);
    if (!t)
        return false;
    Image im;
    bool ok = false;
    if (t->type == LIBRAW_IMAGE_JPEG) {
        DecodeOptions o;
        o.reduce_to = want;
        Info pi;
        char err[100];
        ok = decode(t->data, t->data_size, &im, &pi, err, sizeof(err), o);
    } else if (t->type == LIBRAW_IMAGE_BITMAP && t->bits == 8 &&
               (t->colors == 3 || t->colors == 1)) {
        ok = from_rgb(t->data, t->width, t->height, t->colors, &im);
    }
    libraw_dcraw_clear_mem(t);
    /* A thumbnail smaller than it said is no use. */
    if (!ok || (im.w > im.h ? im.w : im.h) < want / 2)
        return false;
    int o = orientation(lr->sizes.flip);
    Image up;
    if (o > 1 && orient(im, o, &up))
        c.out->take(up);
    else
        c.out->take(im);
    return true;
}

bool decode_raw(Ctx& c)
{
    libraw_data_t* lr = libraw_init(0);
    if (!lr)
        return fail(c, "Out of memory.");
    int r = libraw_open_buffer(lr, (void*)c.d, c.n);
    if (r != LIBRAW_SUCCESS) {
        fail(c, "libraw: %s.", libraw_strerror(r));
        libraw_close(lr);
        return false;
    }
    fill_info(lr, c.info);
    bool ok = false;
    long w = c.info->width, h = c.info->height;
    long longer = w > h ? w : h;
    /* Reduced: the camera's preview, when it is big enough. */
    if (c.reduce_to > 0 && preview(c, lr, c.reduce_to < longer ? c.reduce_to : longer)) {
        libraw_close(lr);
        return true;
    }
    lr->params.use_camera_wb = 1;
    lr->params.output_bps = 8;
    lr->params.output_color = 1;        /* sRGB */
    /* Half size (each 2x2 of the sensor one pixel, no demosaicing: four
     * times as fast) when that is all that is wanted, or the full one is
     * too big. */
    lr->params.half_size = (c.reduce_to > 0 && c.reduce_to * 2 <= longer) ||
                           (double)w * h > (double)k_max_pixels;
    r = libraw_unpack(lr);
    if (r == LIBRAW_SUCCESS)
        r = libraw_dcraw_process(lr);
    libraw_processed_image_t* im = 0;
    if (r == LIBRAW_SUCCESS)
        im = libraw_dcraw_make_mem_image(lr, &r);
    if (!im) {
        fail(c, "libraw: %s.", libraw_strerror(r));
    } else if (im->type != LIBRAW_IMAGE_BITMAP || im->bits != 8 ||
               (im->colors != 3 && im->colors != 1)) {
        fail(c, "libraw gave an image ti cannot use.");
    } else if (check_size(c, im->width, im->height)) {
        ok = from_rgb(im->data, im->width, im->height, im->colors, c.out);
        if (!ok)
            fail(c, "Out of memory.");
        /* check_size wrote the image's size; the file's is the raw's
         * (libraw halved its sizes for half_size). */
        c.info->width = (int)w;
        c.info->height = (int)h;
    }
    if (im)
        libraw_dcraw_clear_mem(im);
    libraw_close(lr);
    return ok;
}

const RawDecoder g_libraw = { raw_name_fn, decode_raw };

} /* namespace */

void raw_init()
{
    snprintf(g_name, sizeof(g_name), "libraw %s", libraw_version());
    char* dash = strchr(g_name, '-');
    if (dash)
        *dash = 0;
    g_raw = &g_libraw;
}

bool raw_probe_file(const char* path, Info* info, Exif* exif)
{
    libraw_data_t* lr = libraw_init(0);
    if (!lr)
        return false;
    bool ok = libraw_open_file(lr, path) == LIBRAW_SUCCESS;
    if (ok) {
        fill_info(lr, info);
        info->decoder = g_raw ? g_raw->name() : 0;
        if (exif)
            fill_exif(lr, exif);
    }
    libraw_close(lr);
    return ok;
}

bool raw_exif(const unsigned char* d, size_t n, Exif* exif)
{
    libraw_data_t* lr = libraw_init(0);
    if (!lr)
        return false;
    bool ok = libraw_open_buffer(lr, (void*)d, n) == LIBRAW_SUCCESS;
    if (ok)
        fill_exif(lr, exif);
    libraw_close(lr);
    return ok;
}

#else /* !TTR_HAVE_RAW */

void raw_init()
{
}

bool raw_probe_file(const char*, Info*, Exif*)
{
    return false;
}

bool raw_exif(const unsigned char*, size_t, Exif*)
{
    return false;
}

#endif

} /* namespace ti */
