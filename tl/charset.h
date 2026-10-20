/*
 * tl/charset.h - ISO 2022 / DEC character set designation and mapping.
 * Internally the grid holds Unicode; the sets map single bytes to it.
 */
#ifndef TL_CHARSET_H
#define TL_CHARSET_H

#include "port.h"

namespace tl {

enum Charset {
    CS_ASCII = 0,          /* ESC ( B */
    CS_DEC_SPECIAL,        /* ESC ( 0: line drawing */
    CS_UK,                 /* ESC ( A: '#' is the pound sign */
    CS_DEC_SUPPLEMENTAL,   /* ESC ( < or ESC - ... : DEC multinational, GR */
    CS_LATIN1              /* ESC - A: ISO Latin-1 as GR */
};

struct CharsetState {
    uint8_t g[4];          /* designations of G0..G3 */
    uint8_t gl;            /* 0..3: set invoked into GL */
    uint8_t gr;            /* set invoked into GR */
    uint8_t single_shift;  /* 0, or 2/3 for the next character only */

    void reset()
    {
        g[0] = g[1] = CS_ASCII;
        g[2] = g[3] = CS_LATIN1;   /* GR in 8-bit mode is Latin-1, as in xterm */
        gl = 0;
        gr = 2;
        single_shift = 0;
    }
    /* Map a 7-bit (0x20-0x7E) or 8-bit (0xA0-0xFF) input character. */
    uint32_t map(uint32_t c);
    /* The set a final byte designates, or -1. */
    static int from_final(uint8_t final_byte, bool gr_set);
};

uint32_t charset_map(Charset cs, uint32_t c);

} /* namespace tl */

#endif
