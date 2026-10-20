/*
 * tl/unicode.h - character width and grapheme cluster boundaries.
 *
 * The tables are generated from the Unicode Character Database by
 * tools/gen_unicode.py and checked in as unicode_data.cpp, so no locale
 * or system wcwidth is involved: IRIX's would be useless anyway.
 */
#ifndef TL_UNICODE_H
#define TL_UNICODE_H

#include "port.h"

namespace tl {

enum WidthClass {
    WIDTH_ZERO = 0,       /* combining marks, format characters, jamo V/T */
    WIDTH_NARROW = 1,
    WIDTH_WIDE = 2,
    WIDTH_AMBIGUOUS = 3   /* East Asian Ambiguous: 1, or 2 for CJK hosts */
};

enum GraphemeClass {
    GB_OTHER = 0,
    GB_CR,
    GB_LF,
    GB_CONTROL,
    GB_EXTEND,
    GB_ZWJ,
    GB_REGIONAL_INDICATOR,
    GB_PREPEND,
    GB_SPACINGMARK,
    GB_L,
    GB_V,
    GB_T,
    GB_LV,
    GB_LVT,
    GB_EXTENDED_PICTOGRAPHIC
};

enum IncbClass {          /* Indic conjunct break (UAX #29 GB9c) */
    INCB_NONE = 0,
    INCB_CONSONANT,
    INCB_LINKER,
    INCB_EXTEND
};

extern const char* const k_unicode_version;
extern const uint16_t k_unicode_stage1[];
extern const uint8_t k_unicode_stage2[];

/* The raw property byte; see tools/gen_unicode.py for the layout. */
inline uint8_t unicode_props(uint32_t cp)
{
    if (cp >= 0x110000)
        return WIDTH_NARROW;
    return k_unicode_stage2[k_unicode_stage1[cp >> 7] * 128 + (cp & 127)];
}

inline WidthClass unicode_width_class(uint32_t cp)
{
    return (WidthClass)(unicode_props(cp) & 3);
}

/* Columns a code point takes: 0, 1 or 2. Controls are 0. */
inline int unicode_width(uint32_t cp, bool ambiguous_wide)
{
    int w = unicode_props(cp) & 3;
    if (w == WIDTH_AMBIGUOUS)
        return ambiguous_wide ? 2 : 1;
    return w;
}

inline GraphemeClass unicode_grapheme_class(uint32_t cp)
{
    return (GraphemeClass)((unicode_props(cp) >> 2) & 15);
}

inline IncbClass unicode_incb(uint32_t cp)
{
    return (IncbClass)(unicode_props(cp) >> 6);
}

/* Incremental grapheme cluster segmentation (UAX #29, extended grapheme
 * clusters). Start from a zeroed state; grapheme_boundary() says whether
 * a cluster boundary lies before the given code point, and advances. */
struct GraphemeState {
    uint8_t prev;         /* GraphemeClass of the previous code point, or 255 at start */
    uint8_t ri_odd;       /* an odd run of regional indicators is open */
    uint8_t pict;         /* 0: none; 1: ExtPict Extend* seen; 2: ... then ZWJ */
    uint8_t incb;         /* 2: an InCB linker, then only InCB extends, so far */
    void reset() { prev = 255; ri_odd = 0; pict = 0; incb = 0; }
};

bool grapheme_boundary(GraphemeState& st, uint32_t cp);

} /* namespace tl */

#endif
