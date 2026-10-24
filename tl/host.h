/*
 * tl/host.h - what the terminal asks of the program embedding it.
 *
 * Every member has a default that does nothing (or declines), so a host
 * implements only what it supports. The terminal calls these only from
 * inside Terminal::feed(), resize() and the input encoders, on the
 * caller's thread. Strings are valid only during the call.
 */
#ifndef TL_HOST_H
#define TL_HOST_H

#include "port.h"

namespace tl {

enum TitleKind {
    TITLE_WINDOW = 1,
    TITLE_ICON = 2,
    TITLE_BOTH = 3
};

class Host {
public:
    virtual ~Host() {}

    /* Bytes for the application: replies to queries, encoded keys. */
    virtual void write_pty(const uint8_t* /*data*/, size_t /*n*/) {}
    virtual void bell() {}
    virtual void title_changed(TitleKind /*kind*/, const char* /*utf8*/) {}
    /* The application asks for a new size in cells (DECCOLM, XTWINOPS 8).
     * Return true if the host will resize; it then calls
     * Terminal::resize() itself, now or later. */
    virtual bool request_resize(int /*cols*/, int /*rows*/) { return false; }
    /* An image file the terminal does not decode itself (PNG, kitty's
     * f=100) into a malloc'd w * h buffer of 0xAARRGGBB pixels. False:
     * the host cannot, and the image is refused. */
    virtual bool decode_image(const uint8_t* /*data*/, size_t /*n*/, uint32_t** /*px*/,
                              int* /*w*/, int* /*h*/)
    {
        return false;
    }
    /* zlib data (kitty's o=z) into out, which must come out exactly
     * out_n bytes long. */
    virtual bool inflate(const uint8_t* /*data*/, size_t /*n*/, uint8_t* /*out*/,
                         size_t /*out_n*/)
    {
        return false;
    }
};

} /* namespace tl */

#endif
