/*
 * tw/sdf.h - signed distance fields of glyphs (DESIGN.md section 4.6).
 *
 * A glyph's distance field at the cell's resolution: each texel the
 * distance from its centre to the glyph's outline, in pixels, positive
 * inside, mapped to 0..255 with the outline at 128 and SPREAD pixels to
 * either end. The outline comes from the same font drawn K times larger
 * when the X server can scale it (TrueType and Type1 faces), so curves
 * and diagonals are exact; otherwise (bitmap fonts, characters only the
 * bundled BDF fonts have) from the cell-size mask scaled up with EPX,
 * which keeps its hinted stems and rounds its stairs.
 *
 * Drawn 1:1, a distance field turned into coverage by a smoothstep around
 * 128 is antialiased text; drawn scaled it stays sharp.
 */
#ifndef TW_SDF_H
#define TW_SDF_H

#include "fonts.h"

namespace tw {

class SdfMaker {
public:
    enum { K = 4, SPREAD = 2 };

    SdfMaker();
    ~SdfMaker();
    void set_display(Display* dpy, Drawable d);
    /* The faces K times larger, where the server scales them. */
    void configure(const Fonts* fonts, int cell_w, int cell_h, int ascent);
    void release();
    /* The face has an outline source (else every glyph comes from EPX). */
    bool scalable(int face) const { return big_[face].loaded(); }

    /* One glyph's field into out (w x h bytes, w = the cell's width, twice
     * for wide; h = the cell's height): from the big face if it has the
     * base character (and the glyph is not a cluster), else from mask, the
     * glyph's 1-bit cell mask (MSB first, 'stride' bytes a row). */
    void make(int face, const uint32_t* cps, int n, bool wide, const uint8_t* mask, int stride,
              uint8_t* out);
    /* The coverage a field value gives with an edge 'edge' pixels wide
     * either side of the outline, moved 'weight' pixels outwards (bolder;
     * negative: lighter): smoothstep, 0..255. */
    static uint8_t coverage(uint8_t v, double edge, double weight);

private:
    bool big_glyph(int face, uint32_t cp, int w, unsigned char* big);
    void field(const unsigned char* big, int bw, int bh, int w, int h, uint8_t* out);

    Display* dpy_;
    Drawable drawable_;
    const Fonts* fonts_;
    int cw_, ch_, ascent_;
    CoreFont big_[FACE_COUNT];
    int big_ascent_[FACE_COUNT];
    int big_dx_[FACE_COUNT];        /* centres the big advance in K cells */
    Pixmap pm_;
    GC gc_;
    int pm_w_, pm_h_;
    unsigned char* work_;           /* K x masks */
    size_t work_cap_;

    SdfMaker(const SdfMaker&);
    SdfMaker& operator=(const SdfMaker&);
};

} /* namespace tw */

#endif
