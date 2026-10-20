#include "unicode.h"

namespace tl {

/* UAX #29 extended grapheme cluster rules GB1-GB999 as of Unicode 16
 * (GB9c without a leading consonant). Returns true when a boundary lies
 * before cp. */
bool grapheme_boundary(GraphemeState& st, uint32_t cp)
{
    GraphemeClass c = unicode_grapheme_class(cp);
    IncbClass ic = unicode_incb(cp);
    uint8_t prev = st.prev;
    bool brk;

    if (prev == 255) {
        brk = true;                                         /* GB1 */
    } else if (prev == GB_CR && c == GB_LF) {
        brk = false;                                        /* GB3 */
    } else if (prev == GB_CONTROL || prev == GB_CR || prev == GB_LF) {
        brk = true;                                         /* GB4 */
    } else if (c == GB_CONTROL || c == GB_CR || c == GB_LF) {
        brk = true;                                         /* GB5 */
    } else if (prev == GB_L && (c == GB_L || c == GB_V || c == GB_LV || c == GB_LVT)) {
        brk = false;                                        /* GB6 */
    } else if ((prev == GB_LV || prev == GB_V) && (c == GB_V || c == GB_T)) {
        brk = false;                                        /* GB7 */
    } else if ((prev == GB_LVT || prev == GB_T) && c == GB_T) {
        brk = false;                                        /* GB8 */
    } else if (c == GB_EXTEND || c == GB_ZWJ) {
        brk = false;                                        /* GB9 */
    } else if (c == GB_SPACINGMARK) {
        brk = false;                                        /* GB9a */
    } else if (prev == GB_PREPEND) {
        brk = false;                                        /* GB9b */
    } else if (st.incb == 2 && ic == INCB_CONSONANT) {
        brk = false;                                        /* GB9c */
    } else if (st.pict == 2 && c == GB_EXTENDED_PICTOGRAPHIC) {
        brk = false;                                        /* GB11 */
    } else if (prev == GB_REGIONAL_INDICATOR && c == GB_REGIONAL_INDICATOR && st.ri_odd) {
        brk = false;                                        /* GB12, GB13 */
    } else {
        brk = true;                                         /* GB999 */
    }

    /* Advance the run states. */
    if (c == GB_REGIONAL_INDICATOR)
        st.ri_odd = brk ? 1 : !st.ri_odd;
    else
        st.ri_odd = 0;

    if (c == GB_EXTENDED_PICTOGRAPHIC)
        st.pict = 1;
    else if (st.pict == 1 && c == GB_EXTEND)
        st.pict = 1;
    else if (st.pict == 1 && c == GB_ZWJ)
        st.pict = 2;
    else
        st.pict = 0;

    /* GB9c as of Unicode 16: Linker [Extend Linker]* x Consonant. */
    if (ic == INCB_LINKER)
        st.incb = 2;
    else if (st.incb == 2 && ic == INCB_EXTEND)
        ;                         /* stays */
    else
        st.incb = 0;

    st.prev = (uint8_t)c;
    return brk;
}

} /* namespace tl */
