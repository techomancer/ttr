/*
 * tl/style.h - character attributes and the table that interns them.
 *
 * Cells carry a 16-bit style id; the Style itself lives once in the
 * StyleTable. Id 0 is the default style. Unused entries are reclaimed by
 * a mark-and-sweep pass that the screen runs when the table fills up.
 */
#ifndef TL_STYLE_H
#define TL_STYLE_H

#include "port.h"

namespace tl {

/* A colour: 2-bit kind in the top bits, value below. */
enum ColorKind {
    COLOR_DEFAULT = 0,
    COLOR_PALETTE = 1,    /* low 8 bits: index 0-255 */
    COLOR_RGB = 2         /* low 24 bits: 0xRRGGBB */
};

inline uint32_t color_default() { return 0; }
inline uint32_t color_palette(int i) { return (1u << 30) | (uint32_t)(i & 255); }
inline uint32_t color_rgb(int r, int g, int b)
{
    return (2u << 30) | ((uint32_t)(r & 255) << 16) | ((uint32_t)(g & 255) << 8) | (uint32_t)(b & 255);
}
inline ColorKind color_kind(uint32_t c) { return (ColorKind)(c >> 30); }
inline int color_index(uint32_t c) { return (int)(c & 255); }
inline int color_r(uint32_t c) { return (int)((c >> 16) & 255); }
inline int color_g(uint32_t c) { return (int)((c >> 8) & 255); }
inline int color_b(uint32_t c) { return (int)(c & 255); }

enum StyleAttr {
    ATTR_BOLD = 1,
    ATTR_FAINT = 2,
    ATTR_ITALIC = 4,
    ATTR_BLINK = 8,
    ATTR_INVERSE = 16,
    ATTR_INVISIBLE = 32,
    ATTR_STRIKE = 64,
    ATTR_OVERLINE = 128,
    ATTR_UL_SHIFT = 8,            /* 3 bits: Underline */
    ATTR_UL_MASK = 7 << 8
};

enum Underline {
    UL_NONE = 0,
    UL_SINGLE,
    UL_DOUBLE,
    UL_CURLY,
    UL_DOTTED,
    UL_DASHED
};

struct Style {
    uint32_t fg;
    uint32_t bg;
    uint32_t ul;                  /* underline colour; default = same as fg */
    uint16_t attrs;
    uint16_t reserved;

    void clear() { fg = bg = ul = 0; attrs = 0; reserved = 0; }
    Underline underline() const { return (Underline)((attrs & ATTR_UL_MASK) >> ATTR_UL_SHIFT); }
    void set_underline(Underline u)
    {
        attrs = (uint16_t)((attrs & ~ATTR_UL_MASK) | ((unsigned)u << ATTR_UL_SHIFT));
    }
    bool is_default() const { return fg == 0 && bg == 0 && ul == 0 && attrs == 0; }
    bool operator==(const Style& o) const
    {
        return fg == o.fg && bg == o.bg && ul == o.ul && attrs == o.attrs;
    }
    bool operator!=(const Style& o) const { return !(*this == o); }
};

class StyleTable {
public:
    StyleTable();
    ~StyleTable();

    /* The id for a style, adding it if needed. Returns 0 (default) when the
     * table is full; the caller should then run a mark-and-sweep and retry
     * (see Screen). */
    uint16_t intern(const Style& s);
    const Style& get(uint16_t id) const { return entries_[id].style; }
    int count() const { return used_; }
    bool full() const { return used_ >= cap_; }

    /* Mark-and-sweep: clear all marks, mark the ids in use, sweep. */
    void mark_begin();
    void mark(uint16_t id) { if (id) marks_[id] = 1; }
    void sweep();

private:
    struct Entry {
        Style style;
        uint16_t next;            /* hash chain */
        uint8_t used;
    };
    enum { HASH_SIZE = 4096 };

    unsigned hash(const Style& s) const;
    void unlink(uint16_t id);

    Entry* entries_;
    uint16_t* heads_;
    uint8_t* marks_;
    int cap_;
    int used_;
    uint16_t free_;               /* free list head, 0 = none */
    int scan_;                    /* next never-used id */

    StyleTable(const StyleTable&);
    StyleTable& operator=(const StyleTable&);
};

} /* namespace tl */

#endif
