/*
 * The wrapper through which ti compiles stb_image (third_party/stb,
 * pinned; AGENTS.md: third-party code is never edited, only compiled
 * through wrapper files). ti's built-in decoders: PNG, JPEG, GIF, BMP
 * and PNM from memory (stb allocates with malloc and ti frees with free), for formats whose system library the build does
 * not have.
 *
 * stb_image includes <stdint.h>, which MIPSpro's cc has only in C99
 * mode: the IRIX build compiles this file with STB_CFLAGS=-c99.
 */
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_ONLY_PNM
#define STBI_MAX_DIMENSIONS 32768
#include "../../third_party/stb/stb_image.h"

/* The few calls ti makes, with names of its own (STB_IMAGE_STATIC keeps
 * stb's to this file). */
unsigned char* ti_stbi_load(const unsigned char* d, int n, int* w, int* h)
{
    int comp;
    return stbi_load_from_memory(d, n, w, h, &comp, 4);
}

int ti_stbi_info(const unsigned char* d, int n, int* w, int* h)
{
    int comp;
    return stbi_info_from_memory(d, n, w, h, &comp);
}

int ti_stbi_zlib(unsigned char* out, int out_n, const unsigned char* in, int n)
{
    return stbi_zlib_decode_buffer((char*)out, out_n, (const char*)in, n);
}

const char* ti_stbi_failure(void)
{
    return stbi_failure_reason();
}

