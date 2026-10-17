/*
 * An ARGB icon (non-premultiplied, 0xAARRGGBB), as in _NET_WM_ICON.
 */
#ifndef TTR_CORE_ICON_H
#define TTR_CORE_ICON_H

#include "core/port.h"

namespace ttr {

class Icon {
public:
    Icon(int w, int h);
    ~Icon();

    int width() const { return w_; }
    int height() const { return h_; }
    uint32_t* pixels() { return px_; }
    const uint32_t* pixels() const { return px_; }

    /* Unique per icon; painters key their caches on it. */
    unsigned long serial() const { return serial_; }

    /* A new size x size copy, box filtered when shrinking. */
    Icon* scaled(int size) const;

private:
    Icon(const Icon&);
    Icon& operator=(const Icon&);

    int w_, h_;
    uint32_t* px_;
    unsigned long serial_;
};

}

#endif
