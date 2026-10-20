#include "fonts.h"

#include <stdio.h>
#include <string.h>
#include <X11/Xatom.h>

namespace tw {

/* ---- CoreFont ------------------------------------------------------------ */

bool CoreFont::load(Display* dpy, const char* name)
{
    unload();
    dpy_ = dpy;
    fs_ = XLoadQueryFont(dpy, name);
    xlfd_[0] = 0;
    if (!fs_)
        return false;
    unsigned long atom;
    if (XGetFontProperty(fs_, XA_FONT, &atom)) {
        char* n = XGetAtomName(dpy, (Atom)atom);
        if (n) {
            strncpy(xlfd_, n, sizeof(xlfd_) - 1);
            xlfd_[sizeof(xlfd_) - 1] = 0;
            XFree(n);
        }
    }
    if (!xlfd_[0]) {
        strncpy(xlfd_, name, sizeof(xlfd_) - 1);
        xlfd_[sizeof(xlfd_) - 1] = 0;
    }
    two_byte_ = fs_->min_byte1 != 0 || fs_->max_byte1 != 0;
    /* Single-byte fonts are taken as Latin-1 unless they say otherwise. */
    const char* reg = xlfd_;
    int dashes = 0;
    for (const char* p = xlfd_; *p; p++)
        if (*p == '-' && ++dashes == 13)
            reg = p + 1;
    latin1_ = dashes < 13 || strncmp(reg, "iso8859-1", 9) == 0 ||
              strncmp(reg, "iso10646-1", 10) == 0 || strncmp(reg, "ISO8859-1", 9) == 0;
    return true;
}

void CoreFont::unload()
{
    if (fs_)
        XFreeFont(dpy_, fs_);
    fs_ = 0;
}

int CoreFont::width() const
{
    if (!fs_)
        return 0;
    /* The advance of a digit is the cell width of a monospaced font. */
    if (fs_->per_char && '0' >= (int)fs_->min_char_or_byte2 && '0' <= (int)fs_->max_char_or_byte2 &&
        !two_byte_)
        return fs_->per_char['0' - fs_->min_char_or_byte2].width;
    return fs_->max_bounds.width;
}

bool CoreFont::has(uint32_t cp) const
{
    if (!fs_)
        return false;
    unsigned b1, b2;
    if (two_byte_) {
        if (cp > 0xffff)
            return false;
        b1 = cp >> 8;
        b2 = cp & 255;
        if (b1 < fs_->min_byte1 || b1 > fs_->max_byte1)
            return false;
    } else {
        if (cp > 255 || (!latin1_ && cp > 127))
            return false;
        b1 = 0;
        b2 = cp;
    }
    if (b2 < fs_->min_char_or_byte2 || b2 > fs_->max_char_or_byte2)
        return false;
    if (!fs_->per_char)
        return true;
    unsigned cols = fs_->max_char_or_byte2 - fs_->min_char_or_byte2 + 1;
    const XCharStruct& c = fs_->per_char[(b1 - fs_->min_byte1) * cols + (b2 - fs_->min_char_or_byte2)];
    return c.width != 0 || c.lbearing != 0 || c.rbearing != 0 || c.ascent != 0 || c.descent != 0;
}

/* ---- Fonts --------------------------------------------------------------- */

Fonts::Fonts() : cw_(0), ch_(0), ascent_(0), serial_(0) {}

void Fonts::unload()
{
    for (int i = 0; i < FACE_COUNT; i++) {
        faces_[i].unload();
        bdf_[i].unload();
    }
}

bool Fonts::add_fallback_dir(const char* dir)
{
    if (!dir || !*dir || cw_ <= 0)
        return false;
    serial_++;
    /* The misc-fixed sizes, largest first. */
    static const int sizes[][2] = {
        { 10, 20 }, { 9, 18 }, { 9, 15 }, { 8, 13 }, { 7, 14 }, { 7, 13 }, { 6, 13 },
        { 6, 12 }, { 6, 10 }, { 6, 9 }, { 5, 8 }, { 5, 7 }, { 4, 6 }
    };
    char path[700];
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        int w = sizes[i][0], h = sizes[i][1];
        if (w > cw_ || h > ch_)
            continue;
        sprintf(path, "%.600s/%dx%d.bdf", dir, w, h);
        if (!bdf_[FACE_REGULAR].load(path))
            continue;
        sprintf(path, "%.600s/%dx%dB.bdf", dir, w, h);
        bdf_[FACE_BOLD].load(path);
        return true;
    }
    return false;
}

const BdfFont* Fonts::bdf_for(int face, uint32_t cp, bool* synth_bold) const
{
    *synth_bold = false;
    if (face == FACE_BOLD && bdf_[FACE_BOLD].loaded() && bdf_[FACE_BOLD].has(cp))
        return &bdf_[FACE_BOLD];
    if (bdf_[FACE_REGULAR].loaded() && bdf_[FACE_REGULAR].has(cp)) {
        *synth_bold = face == FACE_BOLD;
        return &bdf_[FACE_REGULAR];
    }
    return 0;
}

/* "-misc-fixed-medium-r-..." with the weight field replaced. */
static bool xlfd_with_weight(const char* xlfd, const char* weight, char* out, size_t len)
{
    if (xlfd[0] != '-')
        return false;
    const char* p = xlfd;
    int dash = 0;
    const char* w0 = 0;
    const char* w1 = 0;
    for (; *p; p++) {
        if (*p != '-')
            continue;
        dash++;
        if (dash == 3)
            w0 = p + 1;
        else if (dash == 4)
            w1 = p;
    }
    if (!w0 || !w1 || dash < 14)
        return false;
    size_t head = (size_t)(w0 - xlfd);
    if (head + strlen(weight) + strlen(w1) + 1 > len)
        return false;
    memcpy(out, xlfd, head);
    strcpy(out + head, weight);
    strcat(out, w1);
    return true;
}

bool Fonts::load(Display* dpy, const char* regular, const char* bold)
{
    unload();
    serial_++;
    if (!faces_[FACE_REGULAR].load(dpy, regular))
        return false;
    const CoreFont& r = faces_[FACE_REGULAR];
    cw_ = r.width();
    ascent_ = r.ascent();
    ch_ = r.ascent() + r.descent();
    if (cw_ <= 0 || ch_ <= 0)
        return false;
    char name[300];
    bool ok = false;
    if (bold && *bold)
        ok = faces_[FACE_BOLD].load(dpy, bold);
    else if (xlfd_with_weight(r.xlfd(), "bold", name, sizeof(name)))
        ok = faces_[FACE_BOLD].load(dpy, name);
    if (ok) {
        const CoreFont& b = faces_[FACE_BOLD];
        if (b.width() != cw_ || b.ascent() + b.descent() != ch_ || b.two_byte() != r.two_byte())
            faces_[FACE_BOLD].unload();
    }
    return true;
}

/* ---- builtin glyphs ------------------------------------------------------ */

/* Box drawing U+2500-257F: arms up, right, down, left; 0 none, 1 light,
 * 2 heavy, 3 double. Dashed forms are drawn solid; diagonals are not
 * builtin. */
static const char* const k_box =
    "0101" "0202" "1010" "2020" "0101" "0202" "1010" "2020"     /* 2500 */
    "0101" "0202" "1010" "2020" "0110" "0210" "0120" "0220"     /* 2508 */
    "0011" "0012" "0021" "0022" "1100" "1200" "2100" "2200"     /* 2510 */
    "1001" "1002" "2001" "2002" "1110" "1210" "2110" "1120"     /* 2518 */
    "2120" "2210" "1220" "2220" "1011" "1012" "2011" "1021"     /* 2520 */
    "2021" "2012" "1022" "2022" "0111" "0112" "0211" "0212"     /* 2528 */
    "0121" "0122" "0221" "0222" "1101" "1102" "1201" "1202"     /* 2530 */
    "2101" "2102" "2201" "2202" "1111" "1112" "1211" "1212"     /* 2538 */
    "2111" "1121" "2121" "2112" "2211" "1122" "1221" "2212"     /* 2540 */
    "1222" "2122" "2221" "2222" "0101" "0202" "1010" "2020"     /* 2548 */
    "0303" "3030" "0310" "0130" "0330" "0013" "0031" "0033"     /* 2550 */
    "1300" "3100" "3300" "1003" "3001" "3003" "1310" "3130"     /* 2558 */
    "3330" "1013" "3031" "3033" "0313" "0131" "0333" "1303"     /* 2560 */
    "3101" "3303" "1313" "3131" "3333" "0110" "0011" "1001"     /* 2568 */
    "1100" "----" "----" "----" "0001" "1000" "0100" "0010"     /* 2570 */
    "0002" "2000" "0200" "0020" "0201" "1020" "0102" "2010";    /* 2578 */

static int add(GlyphRect* out, int n, int max, int x, int y, int w, int h)
{
    if (n >= max || w <= 0 || h <= 0)
        return n;
    out[n].x = x;
    out[n].y = y;
    out[n].w = w;
    out[n].h = h;
    return n + 1;
}

bool builtin_has(uint32_t cp)
{
    if (cp >= 0x2500 && cp <= 0x257f)
        return k_box[(cp - 0x2500) * 4] != '-';
    if (cp >= 0x2580 && cp <= 0x259f)
        return true;
    return cp >= 0x23ba && cp <= 0x23bd;
}

/* The extent [*a, *b) of a stroke of kind k whose light form starts at c. */
static void stroke_span(int k, int c, int t1, int t2, int gap, int* a, int* b)
{
    if (k == 2) {
        *a = c - (t2 - t1) / 2;
        *b = *a + t2;
    } else if (k == 3) {
        *a = c - gap - t1 + t1 / 2;
        *b = c + gap + t1 / 2 + t1;
    } else {
        *a = c;
        *b = c + t1;
    }
}

static int box(uint32_t cp, int w, int h, GlyphRect* out, int max)
{
    const char* a = k_box + (cp - 0x2500) * 4;
    int arm[4];
    for (int i = 0; i < 4; i++)
        arm[i] = a[i] - '0';
    int t1 = w >= 14 ? w / 7 : 1;           /* light */
    int t2 = t1 * 2 + (t1 == 1 ? 1 : 0);    /* heavy: 3 px at the smallest */
    int gap = t1;                           /* between the strokes of a double */
    int cx = (w - t1) / 2, cy = (h - t1) / 2;

    /* The centre box the arms meet in: the union of the spans of the
     * strokes crossing it, or the light stroke's square when none do. */
    int hy0 = cy, hy1 = cy + t1, vx0 = cx, vx1 = cx + t1;
    bool any_h = false, any_v = false;
    for (int i = 0; i < 4; i++) {
        if (!arm[i])
            continue;
        int s0, s1;
        if (i == 1 || i == 3) {
            stroke_span(arm[i], cy, t1, t2, gap, &s0, &s1);
            if (!any_h || s0 < hy0) hy0 = s0;
            if (!any_h || s1 > hy1) hy1 = s1;
            any_h = true;
        } else {
            stroke_span(arm[i], cx, t1, t2, gap, &s0, &s1);
            if (!any_v || s0 < vx0) vx0 = s0;
            if (!any_v || s1 > vx1) vx1 = s1;
            any_v = true;
        }
    }
    int n = 0;
    for (int i = 0; i < 4; i++) {
        int k = arm[i];
        if (!k)
            continue;
        if (i == 0 || i == 2) {
            int y0 = i == 0 ? 0 : hy0;
            int y1 = i == 0 ? hy1 : h;
            if (k == 3) {
                n = add(out, n, max, cx - gap - t1 + t1 / 2, y0, t1, y1 - y0);
                n = add(out, n, max, cx + gap + t1 / 2, y0, t1, y1 - y0);
            } else {
                int s0, s1;
                stroke_span(k, cx, t1, t2, gap, &s0, &s1);
                n = add(out, n, max, s0, y0, s1 - s0, y1 - y0);
            }
        } else {
            int x0 = i == 3 ? 0 : vx0;
            int x1 = i == 3 ? vx1 : w;
            if (k == 3) {
                n = add(out, n, max, x0, cy - gap - t1 + t1 / 2, x1 - x0, t1);
                n = add(out, n, max, x0, cy + gap + t1 / 2, x1 - x0, t1);
            } else {
                int s0, s1;
                stroke_span(k, cy, t1, t2, gap, &s0, &s1);
                n = add(out, n, max, x0, s0, x1 - x0, s1 - s0);
            }
        }
    }
    return n;
}

int builtin_rects(uint32_t cp, int w, int h, GlyphRect* out, int max, int* shade)
{
    *shade = 0;
    if (cp >= 0x2500 && cp <= 0x257f)
        return box(cp, w, h, out, max);
    if (cp >= 0x23ba && cp <= 0x23bd) {
        /* Scan lines 1, 3, 7, 9 of a 9-line VT100 character cell. */
        static const int line[4] = { 0, 2, 6, 8 };
        int t = h >= 18 ? h / 9 : 1;
        int y = h * line[cp - 0x23ba] / 9 + (h / 9 - t) / 2;
        return add(out, 0, max, 0, y, w, t);
    }
    if (cp < 0x2580 || cp > 0x259f)
        return 0;
    int n = 0;
    int hw = w / 2, hh = h / 2;
    switch (cp) {
    case 0x2580: return add(out, 0, max, 0, 0, w, hh);
    case 0x2590: return add(out, 0, max, hw, 0, w - hw, h);
    case 0x2591: *shade = 1; return 0;
    case 0x2592: *shade = 2; return 0;
    case 0x2593: *shade = 3; return 0;
    case 0x2594: return add(out, 0, max, 0, 0, w, h / 8 > 0 ? h / 8 : 1);
    case 0x2595: {
        int t = w / 8 > 0 ? w / 8 : 1;
        return add(out, 0, max, w - t, 0, t, h);
    }
    default:
        break;
    }
    if (cp >= 0x2581 && cp <= 0x2588) {
        int k = (int)(cp - 0x2580);
        int bh = h * k / 8;
        return add(out, 0, max, 0, h - bh, w, bh);
    }
    if (cp >= 0x2589 && cp <= 0x258f) {
        int k = 8 - (int)(cp - 0x2588);
        return add(out, 0, max, 0, 0, w * k / 8, h);
    }
    /* Quadrants U+2596-259F: bits upper-left 1, upper-right 2, lower-left
     * 4, lower-right 8. */
    static const int quad[10] = { 4, 8, 1, 1 | 4 | 8, 1 | 8, 1 | 2 | 4, 1 | 2 | 8, 2, 2 | 4, 2 | 4 | 8 };
    int q = quad[cp - 0x2596];
    if (q & 1) n = add(out, n, max, 0, 0, hw, hh);
    if (q & 2) n = add(out, n, max, hw, 0, w - hw, hh);
    if (q & 4) n = add(out, n, max, 0, hh, hw, h - hh);
    if (q & 8) n = add(out, n, max, hw, hh, w - hw, h - hh);
    return n;
}

} /* namespace tw */
