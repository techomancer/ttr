/*
 * tw/fonts.h - the faces a Surface draws with. Milestone 1: core X fonts
 * (Latin-1 and ISO 10646 encodings) plus builtin glyphs; BDF and
 * FreeType sources come later (DESIGN.md section 4).
 */
#ifndef TW_FONTS_H
#define TW_FONTS_H

#include "bdf.h"
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

    /* Load the bundled BDF fonts that suit the cell size from dir: the
     * misc-fixed WxH.bdf and WxHB.bdf of the cell, or the largest that
     * fits inside it. Call after load(). */
    bool add_fallback_dir(const char* dir);
    /* Use a cell size without any core X font (tests, client-side
     * renderers that draw only bundled fonts). */
    void set_cell(int cell_w, int cell_h, int ascent)
    {
        cw_ = cell_w;
        ch_ = cell_h;
        ascent_ = ascent;
        serial_++;
    }
    bool has_fallback() const { return bdf_[FACE_REGULAR].loaded(); }
    const BdfFont& fallback(int f) const { return bdf_[f]; }
    /* The fallback font that draws cp in this face; *synth_bold is set
     * when bold has to be made from the regular face. */
    const BdfFont* bdf_for(int face, uint32_t cp, bool* synth_bold) const;
    /* Changes whenever the faces change, so renderers can drop glyphs. */
    int serial() const { return serial_; }

private:
    CoreFont faces_[FACE_COUNT];
    BdfFont bdf_[FACE_COUNT];
    int cw_, ch_, ascent_;
    int serial_;
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
