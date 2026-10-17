/*
 * Loading icons: from X images (window icon pixmaps, XPM files) and from
 * icon files looked up by name in the XDG icon theme directories.
 *
 * Optional formats, enabled by defines so other platforms can leave the
 * libraries out: TTR_HAVE_PNG (libpng 1.6+), TTR_HAVE_XPM (libXpm).
 */
#ifndef TTR_X11_ICONLOAD_H
#define TTR_X11_ICONLOAD_H

#include "core/str.h"
#include "core/vector.h"

#include <X11/Xlib.h>

namespace ttr {

class Icon;

/* Convert an image of the given depth (1 or the screen depth) to an Icon;
 * 'mask' (depth 1) may be null. */
Icon* icon_from_ximage(Display* dpy, XImage* img, XImage* mask,
                       unsigned int depth);

class IconLoader {
public:
    explicit IconLoader(Display* dpy);

    /* An icon by theme name ("firefox") or file path, preferring sizes
     * near 'size'. Null if not found; the caller owns the result. */
    Icon* load(const char* name, int size);

private:
    Icon* load_file(const char* path);
    bool find(const char* name, int size, char* path, int len);

    Display* dpy_;
    Vector<Str> bases_;     /* .../icons and pixmap directories */
};

}

#endif
