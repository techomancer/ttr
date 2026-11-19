/* An XImage the client draws into (ximage.h). */
#include "ximage.h"

#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>

namespace tw {

namespace {

bool g_shm_failed;
int shm_error(Display*, XErrorEvent*)
{
    g_shm_failed = true;
    return 0;
}

bool native_lsb()
{
    unsigned v = 1;
    return *(unsigned char*)&v == 1;
}

} /* namespace */

ClientImage::ClientImage() : dpy_(0), im_(0), shm_(false)
{
    memset(&shminfo_, 0, sizeof(shminfo_));
}

ClientImage::~ClientImage()
{
    destroy();
}

bool ClientImage::swapped() const
{
    return dpy_ && (ImageByteOrder(dpy_) == LSBFirst) != native_lsb();
}

void ClientImage::destroy()
{
    if (!im_)
        return;
    if (shm_) {
        XShmDetach(dpy_, &shminfo_);
        XSync(dpy_, False);
        im_->data = 0;
        XDestroyImage(im_);
        shmdt(shminfo_.shmaddr);
        shm_ = false;
    } else {
        XDestroyImage(im_);         /* frees data */
    }
    im_ = 0;
}

bool ClientImage::create(Display* dpy, Visual* visual, int depth, int w, int h)
{
    destroy();
    dpy_ = dpy;
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;
    /* MIT-SHM where the server shares our memory: a local display. */
    int major, minor;
    Bool pixmaps;
    if (XShmQueryVersion(dpy, &major, &minor, &pixmaps)) {
        im_ = XShmCreateImage(dpy, visual, (unsigned)depth, ZPixmap, 0, &shminfo_, (unsigned)w,
                              (unsigned)h);
        if (im_) {
            shminfo_.shmid = shmget(IPC_PRIVATE, (size_t)im_->bytes_per_line * (size_t)h,
                                    IPC_CREAT | 0600);
            shminfo_.shmaddr =
                shminfo_.shmid >= 0 ? (char*)shmat(shminfo_.shmid, 0, 0) : (char*)-1;
            bool ok = shminfo_.shmaddr != (char*)-1;
            if (ok) {
                im_->data = shminfo_.shmaddr;
                memset(im_->data, 0, (size_t)im_->bytes_per_line * (size_t)h);
                shminfo_.readOnly = False;
                g_shm_failed = false;
                XErrorHandler old = XSetErrorHandler(shm_error);
                XShmAttach(dpy, &shminfo_);
                XSync(dpy, False);
                XSetErrorHandler(old);
                ok = !g_shm_failed;
            }
            if (shminfo_.shmid >= 0)
                shmctl(shminfo_.shmid, IPC_RMID, 0);     /* gone when both detach */
            if (ok) {
                shm_ = true;
                return true;
            }
            if (shminfo_.shmaddr != (char*)-1)
                shmdt(shminfo_.shmaddr);
            im_->data = 0;
            XDestroyImage(im_);
            im_ = 0;
        }
    }
    im_ = XCreateImage(dpy, visual, (unsigned)depth, ZPixmap, 0, 0, (unsigned)w, (unsigned)h, 32,
                       0);
    if (!im_)
        return false;
    im_->data = (char*)calloc((size_t)im_->bytes_per_line, (size_t)h);
    if (!im_->data) {
        XDestroyImage(im_);
        im_ = 0;
        return false;
    }
    return true;
}

void ClientImage::put(Drawable d, GC gc, int sx, int sy, int dx, int dy, int w, int h)
{
    if (!im_ || w <= 0 || h <= 0)
        return;
    if (shm_)
        XShmPutImage(dpy_, d, gc, im_, sx, sy, dx, dy, (unsigned)w, (unsigned)h, False);
    else
        XPutImage(dpy_, d, gc, im_, sx, sy, dx, dy, (unsigned)w, (unsigned)h);
}

void ClientImage::wait()
{
    if (!dpy_)
        return;
    if (shm_)
        XSync(dpy_, False);
    else
        XFlush(dpy_);
}

int pixmap_bits_per_pixel(Display* dpy, int depth)
{
    int n = 0, bpp = 0;
    XPixmapFormatValues* f = XListPixmapFormats(dpy, &n);
    for (int i = 0; f && i < n; i++)
        if (f[i].depth == depth)
            bpp = f[i].bits_per_pixel;
    if (f)
        XFree(f);
    return bpp;
}

} /* namespace tw */
