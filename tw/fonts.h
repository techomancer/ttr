/*
 * tw/fonts.h - the faces a Surface draws with. Milestone 1: core X fonts
 * (Latin-1 and ISO 10646 encodings) plus builtin glyphs; BDF and
 * FreeType sources come later (DESIGN.md section 4).
 */
#ifndef TW_FONTS_H
#define TW_FONTS_H

#include "tw.h"

#include <X11/Xlib.h>

namespace tw {

class CoreFont {
public:
    CoreFont() : dpy_(0), fs_(0), two_byte_(false), latin1_(true) {}
    ~CoreFont() { unload(); }

    bool load(Display* dpy, const char* name);
    void unload();
    bool loaded() const { return fs_ != 0; }
    XFontStruct* fs() const { return fs_; }
    bool two_byte() const { return two_byte_; }
    /* The glyph exists in the font. */
    bool has(uint32_t cp) const;
    int ascent() const { return fs_ ? fs_->ascent : 0; }
    int descent() const { return fs_ ? fs_->descent : 0; }
    int width() const;
    /* The full XLFD of the loaded font, or "". */
    const char* xlfd() const { return xlfd_; }

private:
    Display* dpy_;
    XFontStruct* fs_;
    bool two_byte_;
    bool latin1_;
    char xlfd_[256];
};

enum Face {
    FACE_REGULAR = 0,
    FACE_BOLD,
    FACE_COUNT
};

class Fonts {
public:
    Fonts();
    /* Load the regular face; derive bold from its XLFD unless one is given.
     * A bold face whose cell differs from the regular one is dropped (bold
     * is then drawn by overstriking). */
    bool load(Display* dpy, const char* regular, const char* bold);
    void unload();

    int cell_width() const { return cw_; }
    int cell_height() const { return ch_; }
    int ascent() const { return ascent_; }
    const CoreFont& face(int f) const { return faces_[f]; }
    bool has_face(int f) const { return faces_[f].loaded(); }

private:
    CoreFont faces_[FACE_COUNT];
    int cw_, ch_, ascent_;
};

/* Builtin glyphs: box drawing, block elements, DEC scan lines. Rectangles
 * in cell coordinates, drawn in the foreground colour; *shade > 0 means
 * "fill the cell with fg mixed into bg at shade/4" instead. */
struct GlyphRect {
    int x, y, w, h;
};
bool builtin_has(uint32_t cp);
int builtin_rects(uint32_t cp, int w, int h, GlyphRect* out, int max, int* shade);

} /* namespace tw */

#endif
