#include "x11/iconload.h"
#include "core/icon.h"

#include <X11/Xutil.h>
#if defined(TTR_HAVE_XPM)
#include <X11/xpm.h>
#endif
#if defined(TTR_HAVE_PNG)
#include <png.h>
#endif
#if defined(TTR_HAVE_SVG)
#include "x11/svg.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace ttr {

/* ------------------------------------------------------------------ */
/* X images                                                            */

/* Bit position and width of a TrueColor channel mask. */
static void mask_shift(unsigned long mask, int* shift, int* bits)
{
    *shift = 0;
    *bits = 0;
    if (!mask)
        return;
    while (!(mask & 1)) {
        mask >>= 1;
        (*shift)++;
    }
    while (mask & 1) {
        mask >>= 1;
        (*bits)++;
    }
}

static unsigned long channel8(unsigned long pixel, int shift, int bits)
{
    if (bits == 0)
        return 0;
    unsigned long v = (pixel >> shift) & ((1UL << bits) - 1);
    return bits >= 8 ? v >> (bits - 8) : v * 255 / ((1UL << bits) - 1);
}

Icon* icon_from_ximage(Display* dpy, XImage* img, XImage* mask,
                       unsigned int depth)
{
    unsigned int w = img->width, h = img->height;
    int screen = DefaultScreen(dpy);
    Visual* vis = DefaultVisual(dpy, screen);
    bool truecolor = vis->c_class == TrueColor || vis->c_class == DirectColor;
    int rs, rb, gs, gb, bs, bb;
    mask_shift(vis->red_mask, &rs, &rb);
    mask_shift(vis->green_mask, &gs, &gb);
    mask_shift(vis->blue_mask, &bs, &bb);

    /* Colormapped visuals: look all pixels up in one request. */
    XColor* colors = 0;
    if (depth != 1 && !truecolor) {
        colors = new XColor[w * h];
        unsigned int i, j;
        for (j = 0; j < h; j++)
            for (i = 0; i < w; i++)
                colors[j * w + i].pixel = XGetPixel(img, i, j);
        XQueryColors(dpy, DefaultColormap(dpy, screen), colors, w * h);
    }

    Icon* ic = new Icon(w, h);
    uint32_t* px = ic->pixels();
    unsigned int i, j;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            unsigned long p = XGetPixel(img, i, j);
            unsigned long r, g, b;
            if (depth == 1) {
                r = g = b = p ? 0 : 255;
            } else if (truecolor) {
                r = channel8(p, rs, rb);
                g = channel8(p, gs, gb);
                b = channel8(p, bs, bb);
            } else {
                XColor& c = colors[j * w + i];
                r = c.red >> 8;
                g = c.green >> 8;
                b = c.blue >> 8;
            }
            unsigned long alpha = (!mask || XGetPixel(mask, i, j)) ? 255 : 0;
            px[j * w + i] = (uint32_t)((alpha << 24) | (r << 16) | (g << 8) |
                                       b);
        }
    }
    delete [] colors;
    return ic;
}

/* ------------------------------------------------------------------ */
/* Files                                                               */

static const int k_max_file_icon = 1024;

#if defined(TTR_HAVE_PNG)
static Icon* load_png(const char* path)
{
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&image, path))
        return 0;
    if (image.width == 0 || image.height == 0 ||
        image.width > (unsigned)k_max_file_icon ||
        image.height > (unsigned)k_max_file_icon) {
        png_image_free(&image);
        return 0;
    }
    /* Byte order RGBA, packed by hand so it works on either endianness. */
    image.format = PNG_FORMAT_RGBA;
    unsigned char* buf = new unsigned char[PNG_IMAGE_SIZE(image)];
    if (!png_image_finish_read(&image, 0, buf, 0, 0)) {
        png_image_free(&image);
        delete [] buf;
        return 0;
    }
    Icon* ic = new Icon(image.width, image.height);
    uint32_t* px = ic->pixels();
    unsigned long i, n = (unsigned long)image.width * image.height;
    for (i = 0; i < n; i++) {
        const unsigned char* p = buf + i * 4;
        px[i] = ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) |
                ((uint32_t)p[1] << 8) | (uint32_t)p[2];
    }
    delete [] buf;
    return ic;
}
#endif

#if defined(TTR_HAVE_XPM)
static Icon* load_xpm(Display* dpy, const char* path)
{
    XImage* img = 0;
    XImage* shape = 0;
    if (XpmReadFileToImage(dpy, (char*)path, &img, &shape, 0) != XpmSuccess ||
        !img)
        return 0;
    Icon* ic = 0;
    if (img->width <= k_max_file_icon && img->height <= k_max_file_icon)
        ic = icon_from_ximage(dpy, img, shape, img->depth);
    XDestroyImage(img);
    if (shape)
        XDestroyImage(shape);
    return ic;
}
#endif

static bool ends_with(const char* s, const char* suffix)
{
    int n = (int)strlen(s), m = (int)strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

static void add_list(Vector<Str>* out, const char* list, const char* sub)
{
    char buf[1024];
    const char* p = list;
    while (*p) {
        const char* e = strchr(p, ':');
        int n = e ? (int)(e - p) : (int)strlen(p);
        if (n > 0 && n + strlen(sub) + 1 < sizeof(buf)) {
            memcpy(buf, p, n);
            strcpy(buf + n, sub);
            out->push_back(Str(buf));
        }
        if (!e)
            break;
        p = e + 1;
    }
}

IconLoader::IconLoader(Display* dpy) : dpy_(dpy)
{
    /* Icon theme base directories, in XDG order. */
    char buf[1024];
    const char* home = getenv("HOME");
    if (home && strlen(home) < 900) {
        sprintf(buf, "%s/.icons", home);
        bases_.push_back(Str(buf));
    }
    const char* data_home = getenv("XDG_DATA_HOME");
    if (data_home && *data_home) {
        add_list(&bases_, data_home, "/icons");
    } else if (home && strlen(home) < 900) {
        sprintf(buf, "%s/.local/share/icons", home);
        bases_.push_back(Str(buf));
    }
    const char* dirs = getenv("XDG_DATA_DIRS");
    add_list(&bases_, dirs && *dirs ? dirs : "/usr/local/share:/usr/share",
             "/icons");
}

Icon* IconLoader::load_file(const char* path, int size)
{
#if defined(TTR_HAVE_SVG)
    if (ends_with(path, ".svg")) {
        Icon* ic = new Icon(size, size);
        if (svg_render(path, size, ic->pixels()))
            return ic;
        delete ic;
        return 0;
    }
#endif
#if defined(TTR_HAVE_PNG)
    if (ends_with(path, ".png"))
        return load_png(path);
#endif
#if defined(TTR_HAVE_XPM)
    if (ends_with(path, ".xpm"))
        return load_xpm(dpy_, path);
#endif
    (void)path;
    (void)size;
    return 0;
}

/* hicolor is every theme's fallback; the others are common on Linux.
 * (Following each theme's Inherits= chain would be more thorough.) */
static const char* const k_themes[] = {
    "hicolor", "Adwaita", "AdwaitaLegacy", "Tango", 0
};
static const char* const k_contexts[] = {
    "apps", "places", "actions", "categories", "devices", "status",
    "legacy", 0
};
static const char* const k_bitmap_exts[] = {
#if defined(TTR_HAVE_PNG)
    ".png",
#endif
#if defined(TTR_HAVE_XPM)
    ".xpm",
#endif
    0
};

/* Look for name in <base>/<theme>/<dir>/<context>/ with one of exts. */
bool IconLoader::find_in(const char* dir, const char* name,
                         const char* const* exts, char* path, int len)
{
    char buf[1200];
    int b, t, c, e;
    for (b = 0; b < bases_.size(); b++) {
        const char* base = bases_[b].c_str();
        if (strlen(base) + strlen(dir) + strlen(name) + 64 > sizeof(buf))
            continue;
        for (t = 0; k_themes[t]; t++)
            for (c = 0; k_contexts[c]; c++)
                for (e = 0; exts[e]; e++) {
                    sprintf(buf, "%s/%s/%s/%s/%s%s", base, k_themes[t], dir,
                            k_contexts[c], name, exts[e]);
                    if (access(buf, R_OK) == 0 && (int)strlen(buf) < len) {
                        strcpy(path, buf);
                        return true;
                    }
                }
    }
    return false;
}

/* Bitmaps drawn for the exact size first (artists tune small sizes by
 * hand), then scalable SVG, then other bitmap sizes scaled down, then the
 * legacy pixmaps directories. */
bool IconLoader::find(const char* name, int size, char* path, int len)
{
    char dir[32];
    sprintf(dir, "%dx%d", size, size);
    if (find_in(dir, name, k_bitmap_exts, path, len))
        return true;
#if defined(TTR_HAVE_SVG)
    static const char* const svg[] = { ".svg", 0 };
    if (find_in("scalable", name, svg, path, len))
        return true;
#endif
    static const int sizes[] = { 22, 24, 32, 48, 64, 128, 256, 16 };
    int k;
    for (k = 0; k < (int)(sizeof(sizes) / sizeof(sizes[0])); k++) {
        if (sizes[k] == size)
            continue;
        sprintf(dir, "%dx%d", sizes[k], sizes[k]);
        if (find_in(dir, name, k_bitmap_exts, path, len))
            return true;
    }

    /* <datadir>/pixmaps, next to each <datadir>/icons. */
    static const char* const pixmap_exts[] = {
#if defined(TTR_HAVE_PNG)
        ".png",
#endif
#if defined(TTR_HAVE_XPM)
        ".xpm",
#endif
#if defined(TTR_HAVE_SVG)
        ".svg",
#endif
        0
    };
    char buf[1200];
    int b, e;
    for (b = 0; b < bases_.size(); b++) {
        const char* base = bases_[b].c_str();
        int n = (int)strlen(base);
        if (n < 6 || strcmp(base + n - 6, "/icons") != 0 ||
            n + strlen(name) + 16 > sizeof(buf))
            continue;
        for (e = 0; pixmap_exts[e]; e++) {
            sprintf(buf, "%.*s/pixmaps/%s%s", n - 6, base, name,
                    pixmap_exts[e]);
            if (access(buf, R_OK) == 0 && (int)strlen(buf) < len) {
                strcpy(path, buf);
                return true;
            }
        }
    }
    return false;
}

Icon* IconLoader::load(const char* name, int size)
{
    if (!name || !*name)
        return 0;
    if (strchr(name, '/'))
        return access(name, R_OK) == 0 ? load_file(name, size) : 0;

    /* Names sometimes carry an extension they should not have. */
    char base[256];
    if (strlen(name) >= sizeof(base))
        return 0;
    strcpy(base, name);
    int n = (int)strlen(base);
    if (n > 4 && (ends_with(base, ".png") || ends_with(base, ".xpm") ||
                  ends_with(base, ".svg")))
        base[n - 4] = 0;

    char path[1200];
    if (!find(base, size, path, sizeof(path)))
        return 0;
    return load_file(path, size);
}

}
