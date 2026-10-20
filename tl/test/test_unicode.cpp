#include "tltest.h"
#include "../unicode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace tl;

namespace {

/* Run every case in GraphemeBreakTest.txt: lines like
 *   ÷ 0020 × 0308 ÷ 0020 ÷   # comment
 * where ÷ marks a boundary and × none. The marks are UTF-8 C3 B7 and
 * C3 97. */
void grapheme_break_file()
{
    char path[1024];
    sprintf(path, "%s/GraphemeBreakTest.txt", tl_test_data_dir);
    FILE* fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "%s: cannot open, skipping grapheme break tests\n", path);
        return;
    }
    char line[2048];
    int cases = 0, bad = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n')
            continue;
        char* hash = strchr(line, '#');
        if (hash)
            *hash = 0;
        uint32_t cps[64];
        bool brk[64];
        int n = 0;
        bool pending = true;
        const char* p = line;
        bool ok = true;
        while (*p && n < 64) {
            if ((uint8_t)p[0] == 0xc3 && ((uint8_t)p[1] == 0xb7 || (uint8_t)p[1] == 0x97)) {
                pending = (uint8_t)p[1] == 0xb7;
                p += 2;
            } else if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
                p++;
            } else {
                char* end;
                cps[n] = (uint32_t)strtoul(p, &end, 16);
                if (end == p) {
                    ok = false;
                    break;
                }
                brk[n] = pending;
                n++;
                p = end;
            }
        }
        if (!ok || n == 0)
            continue;
        cases++;
        GraphemeState st;
        st.reset();
        bool good = true;
        for (int i = 0; i < n; i++) {
            bool b = grapheme_boundary(st, cps[i]);
            if (b != brk[i])
                good = false;
        }
        if (!good) {
            bad++;
            if (bad <= 10)
                fprintf(stderr, "grapheme break mismatch: %s", line);
        }
    }
    fclose(fp);
    TL_CHECK(cases > 500);
    TL_CHECK(bad == 0);
    if (bad)
        fprintf(stderr, "grapheme break: %d of %d cases failed\n", bad, cases);
}

} /* namespace */

void tl_test_unicode()
{
    TL_CHECK(strcmp(k_unicode_version, "") != 0);

    TL_CHECK(unicode_width('A', false) == 1);
    TL_CHECK(unicode_width(0x20, false) == 1);
    TL_CHECK(unicode_width(0x07, false) == 0);          /* control */
    TL_CHECK(unicode_width(0x7f, false) == 0);
    TL_CHECK(unicode_width(0x9b, false) == 0);
    TL_CHECK(unicode_width(0xad, false) == 1);          /* soft hyphen is drawn */
    TL_CHECK(unicode_width(0xe9, false) == 1);
    TL_CHECK(unicode_width(0x301, false) == 0);         /* combining acute */
    TL_CHECK(unicode_width(0x200b, false) == 0);        /* zero width space */
    TL_CHECK(unicode_width(0x200d, false) == 0);        /* ZWJ */
    TL_CHECK(unicode_width(0xfe0f, false) == 0);        /* VS16 */
    TL_CHECK(unicode_width(0x1160, false) == 0);        /* jamo V */
    TL_CHECK(unicode_width(0x11a8, false) == 0);        /* jamo T */
    TL_CHECK(unicode_width(0x1100, false) == 2);        /* jamo L */
    TL_CHECK(unicode_width(0xac00, false) == 2);        /* hangul syllable */
    TL_CHECK(unicode_width(0x4e2d, false) == 2);        /* CJK */
    TL_CHECK(unicode_width(0x3000, false) == 2);        /* ideographic space */
    TL_CHECK(unicode_width(0xff21, false) == 2);        /* fullwidth A */
    TL_CHECK(unicode_width(0x1f600, false) == 2);       /* emoji */
    TL_CHECK(unicode_width(0x2603, false) == 1);        /* snowman: text presentation */
    TL_CHECK(unicode_width(0x20000, false) == 2);       /* CJK ext B */
    TL_CHECK(unicode_width(0x2a6e0, false) == 2);       /* unassigned inside a W range */
    TL_CHECK(unicode_width(0xe000, false) == 1);        /* PUA is ambiguous: narrow */
    TL_CHECK(unicode_width(0xe000, true) == 2);
    TL_CHECK(unicode_width(0xa1, false) == 1);          /* inverted ! is ambiguous */
    TL_CHECK(unicode_width(0xa1, true) == 2);
    TL_CHECK(unicode_width_class(0xa1) == WIDTH_AMBIGUOUS);
    TL_CHECK(unicode_width(0x2500, true) == 2);         /* box drawing is ambiguous */
    TL_CHECK(unicode_width(0xfffd, false) == 1);
    TL_CHECK(unicode_width(0x10ffff, false) == 1);
    TL_CHECK(unicode_width(0x110000, false) == 1);      /* out of range: narrow */
    TL_CHECK(unicode_width(0x0600, false) == 0);        /* Arabic number sign, Cf */

    TL_CHECK(unicode_grapheme_class('a') == GB_OTHER);
    TL_CHECK(unicode_grapheme_class(0x0d) == GB_CR);
    TL_CHECK(unicode_grapheme_class(0x0a) == GB_LF);
    TL_CHECK(unicode_grapheme_class(0x301) == GB_EXTEND);
    TL_CHECK(unicode_grapheme_class(0x200d) == GB_ZWJ);
    TL_CHECK(unicode_grapheme_class(0x1f1e6) == GB_REGIONAL_INDICATOR);
    TL_CHECK(unicode_grapheme_class(0x1100) == GB_L);
    TL_CHECK(unicode_grapheme_class(0x1f600) == GB_EXTENDED_PICTOGRAPHIC);
    TL_CHECK(unicode_incb(0x915) == INCB_CONSONANT);    /* devanagari ka */
    TL_CHECK(unicode_incb(0x94d) == INCB_LINKER);       /* virama */

    /* A few clusters by hand. */
    {
        GraphemeState st;
        st.reset();
        TL_CHECK(grapheme_boundary(st, 'e'));
        TL_CHECK(!grapheme_boundary(st, 0x301));
        TL_CHECK(grapheme_boundary(st, 'x'));
        /* flag: two regional indicators */
        TL_CHECK(grapheme_boundary(st, 0x1f1e9));
        TL_CHECK(!grapheme_boundary(st, 0x1f1ea));
        TL_CHECK(grapheme_boundary(st, 0x1f1e9));
        /* family: man ZWJ woman */
        TL_CHECK(grapheme_boundary(st, 0x1f468));
        TL_CHECK(!grapheme_boundary(st, 0x200d));
        TL_CHECK(!grapheme_boundary(st, 0x1f469));
        TL_CHECK(grapheme_boundary(st, 0x0d));
        TL_CHECK(!grapheme_boundary(st, 0x0a));
        TL_CHECK(grapheme_boundary(st, 'a'));
    }

    grapheme_break_file();
}
