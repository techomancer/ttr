/*
 * tw/bdf.h - a reader for BDF bitmap fonts (Glyph Bitmap Distribution
 * Format 2.1), the source format of the X.org misc-fixed fonts tw bundles
 * for Unicode on servers without ISO 10646 fonts (IRIX).
 */
#ifndef TW_BDF_H
#define TW_BDF_H

#include "tw.h"

namespace tw {

struct BdfGlyph {
    uint32_t cp;
    int16_t w, h;               /* bitmap size */
    int16_t xoff, yoff;         /* from the origin; yoff up from the baseline */
    int16_t dwidth;             /* advance */
    uint32_t bits;              /* offset of the rows in the bit store */
};

class BdfFont {
public:
    BdfFont();
    ~BdfFont();

    bool load(const char* path);
    void unload();
    bool loaded() const { return n_ != 0; }

    int ascent() const { return ascent_; }
    int descent() const { return descent_; }
    int cell_width() const { return cell_w_; }
    int cell_height() const { return ascent_ + descent_; }
    bool bold() const { return bold_; }
    const char* path() const { return path_; }

    const BdfGlyph* find(uint32_t cp) const;
    bool has(uint32_t cp) const { return find(cp) != 0; }
    /* Rows of a glyph, MSB first, (w + 7) / 8 bytes each. */
    const uint8_t* rows(const BdfGlyph* g) const { return bits_ + g->bits; }
    int count() const { return n_; }

private:
    BdfGlyph* glyphs_;          /* sorted by cp */
    int n_, cap_;
    uint8_t* bits_;
    size_t bits_len_, bits_cap_;
    int ascent_, descent_, cell_w_;
    bool bold_;
    char path_[512];

    BdfFont(const BdfFont&);
    BdfFont& operator=(const BdfFont&);
};

} /* namespace tw */

#endif
