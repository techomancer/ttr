/*
 * tw/glyphs.h - glyph masks for cells, composed from the bundled BDF
 * fonts (DESIGN.md section 4.4). Renderer-independent: a renderer keeps
 * its own copy of each mask (a pixmap slot, a texture region) in the
 * entry's handle fields.
 */
#ifndef TW_GLYPHS_H
#define TW_GLYPHS_H

#include "fonts.h"
#include "tw.h"

namespace tw {

enum { TW_CLUSTER_MAX = 8 };

struct CachedGlyph {
    uint32_t cps[TW_CLUSTER_MAX];
    uint8_t n;
    uint8_t face;
    uint8_t wide;
    uint8_t used;
    bool found;                 /* false: no source has the base character */
    int w, h, stride;           /* mask: w x h bits, MSB first */
    uint32_t bits;              /* offset in the cache's bit store */
    int handle_a, handle_b;     /* renderer's: -1 until uploaded */
};

class GlyphCache {
public:
    GlyphCache();
    ~GlyphCache();

    /* Start over for this cell size; the renderer must drop its handles
     * whenever generation() changes. */
    void configure(const Fonts* fonts, int cell_w, int cell_h, int ascent);
    void clear();
    int generation() const { return generation_; }

    /* The mask for a cell holding these code points (base first). */
    CachedGlyph* get(int face, const uint32_t* cps, int n, bool wide);
    const uint8_t* bits(const CachedGlyph* g) const { return store_ + g->bits; }
    int count() const { return count_; }

private:
    enum { CAP = 4096, LIMIT = 3000 };

    unsigned hash(int face, const uint32_t* cps, int n, bool wide) const;
    void compose(CachedGlyph* g);

    const Fonts* fonts_;
    int cw_, ch_, ascent_;
    CachedGlyph* table_;
    int count_;
    uint8_t* store_;
    size_t store_len_, store_cap_;
    int generation_;

    GlyphCache(const GlyphCache&);
    GlyphCache& operator=(const GlyphCache&);
};

} /* namespace tw */

#endif
