/*
 * The wrapper through which ti compiles stb_image_write (third_party/stb,
 * pinned; AGENTS.md: third-party code is never edited, only compiled
 * through wrapper files): PNG into memory, for ti::encode_png. Its
 * compressor keeps no state between calls, so threads may share it.
 */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#include <stdlib.h>
#include "../../third_party/stb/stb_image_write.h"

int ti_stbiw_png(void (*fn)(void* ctx, void* data, int n), void* ctx, int w, int h,
                 const unsigned char* rgba)
{
    return stbi_write_png_to_func(fn, ctx, w, h, 4, rgba, w * 4);
}
