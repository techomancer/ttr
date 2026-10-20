#include "charset.h"

namespace tl {

/* DEC Special Graphics, 0x5F..0x7E (VT102 user guide, table 5-x). */
static const uint16_t k_dec_special[32] = {
    0x00a0, /* _ blank */
    0x25c6, /* ` diamond */
    0x2592, /* a checkerboard */
    0x2409, /* b HT */
    0x240c, /* c FF */
    0x240d, /* d CR */
    0x240a, /* e LF */
    0x00b0, /* f degree */
    0x00b1, /* g plus/minus */
    0x2424, /* h NL */
    0x240b, /* i VT */
    0x2518, /* j lower right corner */
    0x2510, /* k upper right corner */
    0x250c, /* l upper left corner */
    0x2514, /* m lower left corner */
    0x253c, /* n crossing */
    0x23ba, /* o scan 1 */
    0x23bb, /* p scan 3 */
    0x2500, /* q scan 5, horizontal line */
    0x23bc, /* r scan 7 */
    0x23bd, /* s scan 9 */
    0x251c, /* t left tee */
    0x2524, /* u right tee */
    0x2534, /* v bottom tee */
    0x252c, /* w top tee */
    0x2502, /* x vertical line */
    0x2264, /* y less or equal */
    0x2265, /* z greater or equal */
    0x03c0, /* { pi */
    0x2260, /* | not equal */
    0x00a3, /* } pound */
    0x00b7  /* ~ middle dot */
};

/* DEC Supplemental Graphics (multinational), 0xA0..0xFF, as Latin-1 except
 * for a few positions. */
static uint32_t dec_supplemental(uint32_t c)
{
    switch (c) {
    case 0xa0: return 0xfffd;
    case 0xa4: return 0xfffd;
    case 0xa6: return 0xfffd;
    case 0xa8: return 0xa4;      /* currency sign */
    case 0xac: return 0xfffd;
    case 0xad: return 0xfffd;
    case 0xae: return 0xfffd;
    case 0xaf: return 0xfffd;
    case 0xb4: return 0xfffd;
    case 0xb8: return 0xfffd;
    case 0xbe: return 0xfffd;
    case 0xd0: return 0xfffd;
    case 0xd7: return 0x0152;    /* OE */
    case 0xdd: return 0x0178;    /* Y diaeresis */
    case 0xde: return 0xfffd;
    case 0xf0: return 0xfffd;
    case 0xf7: return 0x0153;    /* oe */
    case 0xfd: return 0x00ff;    /* y diaeresis */
    case 0xfe: return 0xfffd;
    case 0xff: return 0xfffd;
    default: return c;
    }
}

uint32_t charset_map(Charset cs, uint32_t c)
{
    switch (cs) {
    case CS_ASCII:
        return c;
    case CS_DEC_SPECIAL:
        if (c >= 0x5f && c <= 0x7e)
            return k_dec_special[c - 0x5f];
        return c;
    case CS_UK:
        return c == '#' ? 0xa3 : c;
    case CS_DEC_SUPPLEMENTAL:
        if (c >= 0xa0)
            return dec_supplemental(c);
        return dec_supplemental(c | 0x80) ;
    case CS_LATIN1:
        return c < 0x80 ? c | 0x80 : c;
    }
    return c;
}

uint32_t CharsetState::map(uint32_t c)
{
    if (c >= 0x80) {
        /* GR: a 96-set is shifted; a 94-set designated into GR too. */
        Charset cs = (Charset)g[gr];
        if (cs == CS_LATIN1 || cs == CS_DEC_SUPPLEMENTAL)
            return charset_map(cs, c);
        return charset_map(cs, c & 0x7f);
    }
    int set = gl;
    if (single_shift) {
        set = single_shift;
        single_shift = 0;
    }
    Charset cs = (Charset)g[set];
    if (cs == CS_LATIN1 || cs == CS_DEC_SUPPLEMENTAL) {
        if (c == 0x20 || c == 0x7f)
            return c;
        return charset_map(cs, c | 0x80);
    }
    return charset_map(cs, c);
}

int CharsetState::from_final(uint8_t f, bool gr_set)
{
    switch (f) {
    case 'B': return CS_ASCII;
    case '0': return CS_DEC_SPECIAL;
    case 'A': return gr_set ? CS_LATIN1 : CS_UK;
    case '<': return CS_DEC_SUPPLEMENTAL;
    case '1': case '2': return CS_ASCII;    /* VT100 alternate ROM: treated as ASCII */
    default: return -1;
    }
}

} /* namespace tl */
