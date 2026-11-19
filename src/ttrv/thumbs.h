/*
 * ttrv's thumbnails on disk (ttrv/DESIGN.md section 4.3): the freedesktop
 * thumbnail spec, so other programs share them. A thumbnail of
 * /a/b.jpg is ~/.cache/thumbnails/normal/MD5(uri).png (128 pixels) or
 * large/ (256), a PNG carrying Thumb::URI and Thumb::MTime; one whose
 * MTime is not the file's is stale. IRIX keeps the same layout under
 * ~/.ttrv/thumbnails. Files inside archives get none.
 *
 * Plain stdio on local paths, never tf: decoder threads call these.
 */
#ifndef TTRV_THUMBS_H
#define TTRV_THUMBS_H

#include "../../ti/ti.h"

#include <stddef.h>

namespace ttrv {

/* The directory above normal/ and large/, made if missing: false if it
 * cannot be. Call once before threads start. */
bool thumbs_init();
/* "file:///a/b%20c.jpg". */
void file_uri(const char* path, char* out, size_t n);
/* The thumbnail's path for a file and a size class (128 or 256). */
bool thumb_path(const char* path, int size, char* out, size_t n);
/* 128 for cells up to 128 pixels, 256 above. */
int thumb_class(int cell);
/* A valid thumbnail's bytes (malloc'd), or 0: missing or stale. 'w' and
 * 'h' get the image's size from it when it says (else 0). */
unsigned char* thumb_read(const char* path, long mtime, int size, size_t* len, int* w, int* h);
/* Save a thumbnail of the file (im fitted to the size class already). */
bool thumb_write(const char* path, long mtime, int size, const ti::Image& im, int image_w,
                 int image_h);

/* RFC 1321: 16 bytes of digest as 32 hex digits and a NUL. */
void md5_hex(const unsigned char* d, size_t n, char out[33]);

} /* namespace ttrv */

#endif
