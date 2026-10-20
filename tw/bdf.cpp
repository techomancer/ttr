#include "bdf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace tw {

BdfFont::BdfFont()
    : glyphs_(0), n_(0), cap_(0), bits_(0), bits_len_(0), bits_cap_(0), ascent_(0),
      descent_(0), cell_w_(0), bold_(false)
{
    path_[0] = 0;
}

BdfFont::~BdfFont()
{
    unload();
}

void BdfFont::unload()
{
    free(glyphs_);
    free(bits_);
    glyphs_ = 0;
    bits_ = 0;
    n_ = cap_ = 0;
    bits_len_ = bits_cap_ = 0;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int cmp_glyph(const void* a, const void* b)
{
    uint32_t x = ((const BdfGlyph*)a)->cp, y = ((const BdfGlyph*)b)->cp;
    return x < y ? -1 : x > y ? 1 : 0;
}

bool BdfFont::load(const char* path)
{
    unload();
    FILE* f = fopen(path, "r");
    if (!f)
        return false;
    strncpy(path_, path, sizeof(path_) - 1);
    path_[sizeof(path_) - 1] = 0;
    char line[1024];
    int fbb_w = 0;
    bool in_char = false, in_bitmap = false;
    long enc = -1;
    BdfGlyph g;
    memset(&g, 0, sizeof(g));
    int row = 0, stride = 0;
    bold_ = false;
    while (fgets(line, sizeof(line), f)) {
        if (in_bitmap) {
            if (strncmp(line, "ENDCHAR", 7) == 0) {
                in_bitmap = in_char = false;
                if (enc >= 0 && row == g.h) {
                    if (n_ == cap_) {
                        int ncap = cap_ ? cap_ * 2 : 1024;
                        BdfGlyph* p = (BdfGlyph*)realloc(glyphs_, (size_t)ncap * sizeof(BdfGlyph));
                        if (!p)
                            break;
                        glyphs_ = p;
                        cap_ = ncap;
                    }
                    g.cp = (uint32_t)enc;
                    glyphs_[n_++] = g;
                } else {
                    bits_len_ = g.bits;     /* discard */
                }
                continue;
            }
            if (row >= g.h)
                continue;
            uint8_t* out = bits_ + g.bits + (size_t)row * (size_t)stride;
            for (int i = 0; i < stride; i++) {
                int hi = hexval((unsigned char)line[2 * i]);
                int lo = hi < 0 ? -1 : hexval((unsigned char)line[2 * i + 1]);
                out[i] = (uint8_t)(hi < 0 || lo < 0 ? 0 : (hi << 4) | lo);
            }
            row++;
            continue;
        }
        if (strncmp(line, "STARTCHAR", 9) == 0) {
            in_char = true;
            enc = -1;
            memset(&g, 0, sizeof(g));
            continue;
        }
        if (in_char) {
            int a, b, c, d;
            if (strncmp(line, "ENCODING", 8) == 0) {
                enc = strtol(line + 8, 0, 10);
            } else if (strncmp(line, "DWIDTH", 6) == 0 && sscanf(line + 6, "%d", &a) == 1) {
                g.dwidth = (int16_t)a;
            } else if (strncmp(line, "BBX", 3) == 0 && sscanf(line + 3, "%d %d %d %d", &a, &b, &c, &d) == 4) {
                g.w = (int16_t)a;
                g.h = (int16_t)b;
                g.xoff = (int16_t)c;
                g.yoff = (int16_t)d;
            } else if (strncmp(line, "BITMAP", 6) == 0) {
                stride = (g.w + 7) / 8;
                size_t need = bits_len_ + (size_t)stride * (size_t)g.h;
                if (need > bits_cap_) {
                    size_t ncap = bits_cap_ ? bits_cap_ * 2 : 65536;
                    while (ncap < need)
                        ncap *= 2;
                    uint8_t* p = (uint8_t*)realloc(bits_, ncap);
                    if (!p)
                        break;
                    bits_ = p;
                    bits_cap_ = ncap;
                }
                g.bits = (uint32_t)bits_len_;
                bits_len_ = need;
                row = 0;
                in_bitmap = true;
            }
            continue;
        }
        int a, b;
        if (strncmp(line, "FONT_ASCENT", 11) == 0 && sscanf(line + 11, "%d", &a) == 1)
            ascent_ = a;
        else if (strncmp(line, "FONT_DESCENT", 12) == 0 && sscanf(line + 12, "%d", &a) == 1)
            descent_ = a;
        else if (strncmp(line, "FONTBOUNDINGBOX", 15) == 0 && sscanf(line + 15, "%d %d", &a, &b) == 2)
            fbb_w = a;
        else if (strncmp(line, "WEIGHT_NAME", 11) == 0 && strstr(line, "Bold"))
            bold_ = true;
    }
    fclose(f);
    if (!n_) {
        unload();
        return false;
    }
    qsort(glyphs_, (size_t)n_, sizeof(BdfGlyph), cmp_glyph);
    /* The cell is the advance of a digit (all of a fixed font's). */
    const BdfGlyph* z = find('0');
    cell_w_ = z && z->dwidth > 0 ? z->dwidth : fbb_w;
    return true;
}

const BdfGlyph* BdfFont::find(uint32_t cp) const
{
    int lo = 0, hi = n_ - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t m = glyphs_[mid].cp;
        if (m == cp)
            return &glyphs_[mid];
        if (m < cp)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}

} /* namespace tw */
