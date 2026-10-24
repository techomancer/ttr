/*
 * GIF through giflib's record API (DGifGetRecordType, DGifGetLine),
 * which 4.x and 5.x share; only opening and closing changed (an error
 * argument in 5.0 and 5.1). The first frame, over the logical screen.
 */
#include "internal.h"

#if defined(TTR_HAVE_GIF)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include <gif_lib.h>
}

#if defined(GIFLIB_MAJOR) && GIFLIB_MAJOR >= 5
#define TI_GIF5 1
#if GIFLIB_MAJOR > 5 || GIFLIB_MINOR >= 1
#define TI_GIF51 1
#endif
#endif

namespace ti {
namespace internal {

namespace {

struct Src {
    Ctx* c;
    size_t pos;
};

int read_fn(GifFileType* g, GifByteType* out, int len)
{
    Src* s = (Src*)g->UserData;
    size_t left = s->c->n - s->pos;
    size_t n = (size_t)len < left ? (size_t)len : left;
    memcpy(out, s->c->d + s->pos, n);
    s->pos += n;
    return (int)n;
}

void close_gif(GifFileType* g)
{
#if defined(TI_GIF51)
    int e;
    DGifCloseFile(g, &e);
#else
    DGifCloseFile(g);
#endif
}

} /* namespace */

const char* gif_system_name()
{
#if defined(GIFLIB_MAJOR)
    static char name[40];
    if (!name[0])
        sprintf(name, "giflib %d.%d", GIFLIB_MAJOR, GIFLIB_MINOR);
    return name;
#else
    return "giflib 4";
#endif
}

bool gif_system(Ctx& c)
{
    Src src;
    src.c = &c;
    src.pos = 0;
#if defined(TI_GIF5)
    int e = 0;
    GifFileType* g = DGifOpen(&src, read_fn, &e);
#else
    GifFileType* g = DGifOpen(&src, read_fn);
#endif
    if (!g)
        return fail(c, "GIF: not a GIF file.");
    if (!check_size(c, g->SWidth, g->SHeight)) {
        close_gif(g);
        return false;
    }
    if (!c.out->alloc(g->SWidth, g->SHeight)) {
        close_gif(g);
        return fail(c, "Out of memory.");
    }
    memset(c.out->rgba, 0, (size_t)c.out->w * (size_t)c.out->h * 4);
    int transparent = -1;
    GifRecordType rt;
    bool ok = false;
    GifPixelType* line = 0;
    do {
        if (DGifGetRecordType(g, &rt) == GIF_ERROR)
            break;
        if (rt == EXTENSION_RECORD_TYPE) {
            int code;
            GifByteType* ext;
            if (DGifGetExtension(g, &code, &ext) == GIF_ERROR)
                break;
            /* The graphic control extension: bit 0, index 3. */
            if (code == 0xf9 && ext && ext[0] >= 4 && (ext[1] & 1))
                transparent = ext[4];
            while (ext)
                if (DGifGetExtensionNext(g, &ext) == GIF_ERROR)
                    break;
        } else if (rt == IMAGE_DESC_RECORD_TYPE) {
            if (DGifGetImageDesc(g) == GIF_ERROR)
                break;
            const GifImageDesc& d = g->Image;
            ColorMapObject* map = d.ColorMap ? d.ColorMap : g->SColorMap;
            if (!map || d.Width <= 0 || d.Height <= 0)
                break;
            line = (GifPixelType*)malloc((size_t)d.Width);
            if (!line)
                break;
            /* Interlaced rows come in four passes. */
            static const int start[4] = { 0, 4, 2, 1 }, step[4] = { 8, 8, 4, 2 };
            int passes = d.Interlace ? 4 : 1;
            bool bad = false;
            for (int pass = 0; pass < passes && !bad; pass++) {
                for (int y = d.Interlace ? start[pass] : 0; y < d.Height;
                     y += d.Interlace ? step[pass] : 1) {
                    if (DGifGetLine(g, line, d.Width) == GIF_ERROR) {
                        bad = true;
                        break;
                    }
                    int sy = d.Top + y;
                    if (sy < 0 || sy >= c.out->h)
                        continue;
                    unsigned char* o = c.out->row(sy);
                    for (int x = 0; x < d.Width; x++) {
                        int sx = d.Left + x;
                        int i = line[x];
                        if (sx < 0 || sx >= c.out->w || i == transparent || i >= map->ColorCount)
                            continue;
                        unsigned char* px = o + sx * 4;
                        px[0] = map->Colors[i].Red;
                        px[1] = map->Colors[i].Green;
                        px[2] = map->Colors[i].Blue;
                        px[3] = 255;
                    }
                }
            }
            /* Rows decoded before damage still count, as browsers do. */
            ok = true;
            break;
        }
    } while (rt != TERMINATE_RECORD_TYPE);
    free(line);
    close_gif(g);
    if (!ok)
        return fail(c, "GIF: no image in the file.");
    return true;
}

} /* namespace internal */
} /* namespace ti */

#else

namespace ti {
namespace internal {

const char* gif_system_name()
{
    return 0;
}

bool gif_system(Ctx& c)
{
    return fail(c, "No giflib in this build.");
}

} /* namespace internal */
} /* namespace ti */

#endif
