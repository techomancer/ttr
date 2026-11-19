/*
 * tw/ximage.h - an XImage the client draws into and sends to a window:
 * shared memory (MIT-SHM) where the server can attach it (a local
 * display), plain memory and XPutImage otherwise. Used by tw's soft
 * renderer and by ttrv's image views.
 *
 * Pixels are to be written in the server's byte order (swapped() says
 * whether that is not ours), so the image goes out as it is: Xlib's own
 * swapping of 32-bit images to a big-endian server mangled the channels.
 */
#ifndef TW_XIMAGE_H
#define TW_XIMAGE_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>

namespace tw {

class ClientImage {
public:
    ClientImage();
    ~ClientImage();

    /* w x h pixels (at least 1 x 1) of the visual and depth, cleared to
     * zero; any earlier image is dropped. False if Xlib has none. */
    bool create(Display* dpy, Visual* visual, int depth, int w, int h);
    void destroy();

    XImage* image() const { return im_; }
    bool shared() const { return shm_; }
    int width() const { return im_ ? im_->width : 0; }
    int height() const { return im_ ? im_->height : 0; }
    int bits_per_pixel() const { return im_ ? im_->bits_per_pixel : 0; }
    char* row(int y) const { return im_->data + (size_t)y * (size_t)im_->bytes_per_line; }
    /* The server's byte order is not this machine's. */
    bool swapped() const;

    /* Part of the image to a drawable; no flush. After shared puts, the
     * image must not be drawn into before wait(): the server reads it
     * later. */
    void put(Drawable d, GC gc, int sx, int sy, int dx, int dy, int w, int h);
    /* The server has read everything put (XSync for a shared image,
     * XFlush otherwise). */
    void wait();

private:
    ClientImage(const ClientImage&);
    ClientImage& operator=(const ClientImage&);

    Display* dpy_;
    XImage* im_;
    XShmSegmentInfo shminfo_;
    bool shm_;
};

/* Bits a pixel the server uses for images of this depth, or 0. */
int pixmap_bits_per_pixel(Display* dpy, int depth);

} /* namespace tw */

#endif
