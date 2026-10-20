#include "glyphs.h"

#include <stdlib.h>
#include <string.h>

namespace tw {

GlyphCache::GlyphCache()
    : fonts_(0), cw_(0), ch_(0), ascent_(0), table_(0), count_(0), store_(0), store_len_(0),
      store_cap_(0), generation_(0)
{
}

GlyphCache::~GlyphCache()
{
    free(table_);
    free(store_);
}

void GlyphCache::configure(const Fonts* fonts, int cell_w, int cell_h, int ascent)
{
    fonts_ = fonts;
    cw_ = cell_w;
    ch_ = cell_h;
    ascent_ = ascent;
    clear();
}

void GlyphCache::clear()
{
    if (!table_)
        table_ = (CachedGlyph*)calloc(CAP, sizeof(CachedGlyph));
    else
        memset(table_, 0, CAP * sizeof(CachedGlyph));
    count_ = 0;
    store_len_ = 0;
    generation_++;
}

unsigned GlyphCache::hash(int face, const uint32_t* cps, int n, bool wide) const
{
    uint32_t h = (uint32_t)face * 0x9e3779b1u ^ (wide ? 0x85ebca6bu : 0);
    for (int i = 0; i < n; i++)
        h = (h ^ cps[i]) * 0x01000193u;
    h ^= h >> 13;
    return h & (CAP - 1);
}

/* OR one BDF glyph into the mask with its baseline on ours. */
static void blit(uint8_t* mask, int stride, int mw, int mh, const BdfFont* f,
                 const BdfGlyph* g, int ox, int baseline)
{
    const uint8_t* src = f->rows(g);
    int gs = (g->w + 7) / 8;
    int top = baseline - (g->yoff + g->h);
    for (int y = 0; y < g->h; y++) {
        int my = top + y;
        if (my < 0 || my >= mh)
            continue;
        for (int x = 0; x < g->w; x++) {
            if (!(src[y * gs + x / 8] & (0x80 >> (x % 8))))
                continue;
            int mx = ox + g->xoff + x;
            if (mx < 0 || mx >= mw)
                continue;
            mask[my * stride + mx / 8] |= (uint8_t)(0x80 >> (mx % 8));
        }
    }
}

void GlyphCache::compose(CachedGlyph* g)
{
    int w = g->wide ? 2 * cw_ : cw_;
    int stride = (w + 7) / 8;
    size_t need = store_len_ + (size_t)stride * (size_t)ch_;
    if (need > store_cap_) {
        size_t ncap = store_cap_ ? store_cap_ * 2 : 65536;
        while (ncap < need)
            ncap *= 2;
        uint8_t* p = (uint8_t*)realloc(store_, ncap);
        if (!p) {
            g->found = false;
            return;
        }
        store_ = p;
        store_cap_ = ncap;
    }
    g->w = w;
    g->h = ch_;
    g->stride = stride;
    g->bits = (uint32_t)store_len_;
    store_len_ = need;
    uint8_t* mask = store_ + g->bits;
    memset(mask, 0, (size_t)stride * (size_t)ch_);
    g->found = false;
    bool synth = false;
    for (int i = 0; i < g->n; i++) {
        bool s = false;
        const BdfFont* f = fonts_->bdf_for(g->face, g->cps[i], &s);
        if (!f) {
            if (i == 0)
                return;                         /* no base glyph: the renderer draws a box */
            continue;                           /* a mark we cannot draw */
        }
        const BdfGlyph* bg = f->find(g->cps[i]);
        /* Centre a narrower font's glyph in the cell. */
        int fw = g->wide ? 2 * f->cell_width() : f->cell_width();
        int ox = (w - fw) / 2;
        blit(mask, stride, w, ch_, f, bg, ox, ascent_);
        if (i == 0) {
            g->found = true;
            synth = s;
        }
    }
    if (synth) {
        /* Bold from a regular face: thicken by one pixel to the right. */
        for (int y = 0; y < ch_; y++) {
            uint8_t* row = mask + y * stride;
            uint8_t carry = 0;
            for (int b = 0; b < stride; b++) {
                uint8_t v = row[b];
                row[b] = (uint8_t)(v | (v >> 1) | carry);
                carry = (uint8_t)((v & 1) << 7);
            }
        }
    }
}

CachedGlyph* GlyphCache::get(int face, const uint32_t* cps, int n, bool wide)
{
    if (!table_ || !fonts_)
        return 0;
    if (n > TW_CLUSTER_MAX)
        n = TW_CLUSTER_MAX;
    if (count_ >= LIMIT)
        clear();
    unsigned h = hash(face, cps, n, wide);
    for (int probe = 0; probe < CAP; probe++) {
        CachedGlyph* e = &table_[(h + (unsigned)probe) & (CAP - 1)];
        if (!e->used) {
            e->used = 1;
            e->n = (uint8_t)n;
            e->face = (uint8_t)face;
            e->wide = wide ? 1 : 0;
            memcpy(e->cps, cps, (size_t)n * sizeof(uint32_t));
            e->handle_a = e->handle_b = -1;
            compose(e);
            count_++;
            return e;
        }
        if (e->n == n && e->face == face && e->wide == (wide ? 1 : 0) &&
            memcmp(e->cps, cps, (size_t)n * sizeof(uint32_t)) == 0)
            return e;
    }
    return 0;
}

} /* namespace tw */
