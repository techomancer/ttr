/*
 * ti/term.h - images on terminals: which way the terminal shows them,
 * and the bytes for each (ti/DESIGN.md). Half blocks (U+2580 in two
 * colours a cell) work on every colour terminal and are drawn by the
 * program through its own screen; the rest are escape sequences sent
 * straight to the terminal at the cursor.
 */
#ifndef TI_TERM_H
#define TI_TERM_H

#include "ti.h"

namespace ti {

enum Protocol {
    PROTO_OFF,          /* no images: the viewer shows the bytes */
    PROTO_BLOCKS,       /* half blocks in the program's own cells */
    PROTO_KITTY,        /* kitty's graphics protocol (kitty, ghostty, ttrt) */
    PROTO_SIXEL,        /* DEC sixels (xterm -ti vt340, foot, mlterm, konsole) */
    PROTO_ITERM,        /* iTerm2's inline images (iTerm2, WezTerm) */
    PROTO_COUNT
};

/* "off", "blocks", "kitty", "sixel", "iterm" */
const char* protocol_name(Protocol p);
/* A setting: one of the names, or "auto" / "" for detect_protocol(). */
Protocol protocol_from_setting(const char* s);
/* From the environment ($TERM, $TERM_PROGRAM, $KITTY_WINDOW_ID, ...);
 * blocks when unsure, and inside tmux or screen. */
Protocol detect_protocol();

/* A growing byte buffer. */
class Bytes {
public:
    Bytes();
    ~Bytes();
    void add(const char* s, size_t n);
    void add(const char* s);
    void addf(const char* fmt, ...);
    void clear() { n = 0; }
    char* d;
    size_t n;
    bool failed;                /* out of memory at some point */

private:
    size_t cap_;
    Bytes(const Bytes&);
    Bytes& operator=(const Bytes&);
};

/* The image shown at the cursor, which stays where it is. 'id' names it,
 * for kitty_delete. cols x rows cells: the terminal stretches the image
 * to fill them; with only one of them (the other 0) it keeps the aspect;
 * with neither it shows the pixels one for one. */
void encode_kitty(const Image& im, int cols, int rows, unsigned id, Bytes* out);
void kitty_delete(unsigned id, Bytes* out);
/* Sixels at the image's own pixel size, from the cursor down; 216
 * colours with ordered dithering. Flatten the image first. */
void encode_sixel(const Image& im, Bytes* out);
/* iTerm2's inline file, as a PNG, in cols x rows cells. */
void encode_iterm(const Image& im, int cols, int rows, Bytes* out);
/* An uncompressed PNG of the image. */
void encode_png(const Image& im, Bytes* out);

} /* namespace ti */

#endif
