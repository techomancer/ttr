#include "image.h"
#include "grid.h"

#include <stdlib.h>
#include <string.h>

namespace tl {

/* ------------------------------------------------------------------ */
/* ImageStore                                                          */

namespace {

/* What images may hold before the oldest go: an Indy has 64 to 256 MB. */
#if defined(__sgi)
const size_t k_default_budget = 32u << 20;
#else
const size_t k_default_budget = 256u << 20;
#endif

} /* namespace */

ImageStore::ImageStore() : count_(0), next_(1), serial_(0), bytes_(0), budget_(k_default_budget)
{
    memset(slots_, 0, sizeof(slots_));
}

ImageStore::~ImageStore()
{
    clear();
}

void ImageStore::clear()
{
    for (int i = 1; i <= MAX_ID; i++)
        if (slots_[i].px)
            drop(i);
}

void ImageStore::drop(int id)
{
    Image& im = slots_[id];
    bytes_ -= (size_t)im.w * (size_t)im.h * 4;
    free(im.px);
    memset(&im, 0, sizeof(im));
    count_--;
}

int ImageStore::oldest() const
{
    int best = 0;
    for (int i = 1; i <= MAX_ID; i++)
        if (slots_[i].px && (!best || slots_[i].serial < slots_[best].serial))
            best = i;
    return best;
}

int ImageStore::add(uint32_t* px, int w, int h, int cell_w, int cell_h)
{
    size_t size = (size_t)w * (size_t)h * 4;
    if (!px || w <= 0 || h <= 0 || size > budget_) {
        free(px);
        return 0;
    }
    /* Room: the oldest images go (their cells then show nothing). */
    while (count_ > 0 && (count_ >= MAX_ID || bytes_ + size > budget_))
        drop(oldest());
    int id = 0;
    for (int k = 0; k < MAX_ID; k++) {
        int i = 1 + (next_ - 1 + k) % MAX_ID;
        if (!slots_[i].px) {
            id = i;
            break;
        }
    }
    if (!id) {
        free(px);
        return 0;
    }
    Image& im = slots_[id];
    im.w = w;
    im.h = h;
    im.px = px;
    im.cell_w = cell_w;
    im.cell_h = cell_h;
    im.serial = ++serial_;
    bytes_ += size;
    count_++;
    next_ = id % MAX_ID + 1;
    return id;
}

const Image* ImageStore::get(int id) const
{
    if (id < 1 || id > MAX_ID || !slots_[id].px)
        return 0;
    return &slots_[id];
}

void ImageStore::collect(const Grid* const* grids, int n)
{
    if (!count_)
        return;
    bool used[MAX_ID + 1];
    memset(used, 0, sizeof(used));
    for (int g = 0; g < n; g++) {
        const Grid* grid = grids[g];
        uint64_t end = grid->base() + grid->total_rows();
        for (uint64_t a = grid->base(); a < end; a++) {
            const Row& r = grid->abs_row(a);
            if (!r.cells || !(r.flags & ROW_HAS_IMAGES))
                continue;
            for (int x = 0; x < r.cols; x++)
                if (r.cells[x].is_image())
                    used[r.cells[x].image_id()] = true;
        }
    }
    for (int i = 1; i <= MAX_ID; i++)
        if (slots_[i].px && !used[i])
            drop(i);
}

/* ------------------------------------------------------------------ */
/* KittyImages                                                         */

namespace {

#if defined(__sgi)
const size_t k_kitty_budget = 32u << 20;
#else
const size_t k_kitty_budget = 256u << 20;
#endif

} /* namespace */

KittyImages::KittyImages() : n_(0), age_(0), bytes_(0), budget_(k_kitty_budget)
{
    memset(e_, 0, sizeof(e_));
}

KittyImages::~KittyImages()
{
    clear();
}

void KittyImages::clear()
{
    while (n_)
        drop(n_ - 1);
}

void KittyImages::drop(int i)
{
    bytes_ -= (size_t)e_[i].w * (size_t)e_[i].h * 4;
    free(e_[i].px);
    e_[i] = e_[--n_];
    memset(&e_[n_], 0, sizeof(e_[n_]));
}

bool KittyImages::add(uint32_t id, uint32_t number, uint32_t* px, int w, int h)
{
    size_t size = (size_t)w * (size_t)h * 4;
    if (!px || w <= 0 || h <= 0 || size > budget_) {
        free(px);
        return false;
    }
    remove(id);
    while (n_ && (n_ == MAX || bytes_ + size > budget_)) {
        int old = 0;
        for (int i = 1; i < n_; i++)
            if (e_[i].age < e_[old].age)
                old = i;
        drop(old);
    }
    Entry& e = e_[n_++];
    e.id = id;
    e.number = number;
    e.px = px;
    e.w = w;
    e.h = h;
    e.age = ++age_;
    bytes_ += size;
    return true;
}

const KittyImages::Entry* KittyImages::get(uint32_t id) const
{
    for (int i = 0; i < n_; i++)
        if (e_[i].id == id)
            return &e_[i];
    return 0;
}

const KittyImages::Entry* KittyImages::by_number(uint32_t number) const
{
    const Entry* best = 0;
    for (int i = 0; i < n_; i++)
        if (e_[i].number == number && (!best || e_[i].age > best->age))
            best = &e_[i];
    return best;
}

void KittyImages::remove(uint32_t id)
{
    for (int i = 0; i < n_; i++)
        if (e_[i].id == id) {
            drop(i);
            return;
        }
}

uint32_t KittyImages::free_id() const
{
    /* Ours count down from the top, away from what clients choose. */
    uint32_t id = 0xffffffffu;
    while (get(id))
        id--;
    return id;
}

uint32_t* resample(const uint32_t* src, int sw, int sh, int x, int y, int w, int h, int tw,
                   int th, int pad_x, int pad_y)
{
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > sw || y + h > sh || tw <= 0 || th <= 0 ||
        pad_x < 0 || pad_y < 0)
        return 0;
    int ow = pad_x + tw, oh = pad_y + th;
    if ((double)ow * (double)oh > 4096.0 * 4096.0)
        return 0;
    uint32_t* out = (uint32_t*)calloc((size_t)ow * (size_t)oh, 4);
    if (!out)
        return 0;
    for (int ty = 0; ty < th; ty++) {
        int y0 = y + (int)((double)ty * h / th), y1 = y + (int)((double)(ty + 1) * h / th);
        if (y1 <= y0)
            y1 = y0 + 1;
        uint32_t* o = out + (size_t)(pad_y + ty) * (size_t)ow + pad_x;
        for (int tx = 0; tx < tw; tx++) {
            int x0 = x + (int)((double)tx * w / tw), x1 = x + (int)((double)(tx + 1) * w / tw);
            if (x1 <= x0)
                x1 = x0 + 1;
            if (x1 - x0 == 1 && y1 - y0 == 1) {
                o[tx] = src[(size_t)y0 * (size_t)sw + x0];
                continue;
            }
            /* Weighted by alpha, so transparent pixels lend no colour. */
            unsigned long r = 0, g = 0, b = 0, a = 0, cnt = 0;
            for (int yy = y0; yy < y1; yy++) {
                const uint32_t* p = src + (size_t)yy * (size_t)sw;
                for (int xx = x0; xx < x1; xx++) {
                    uint32_t v = p[xx];
                    unsigned long al = v >> 24;
                    r += ((v >> 16) & 255) * al;
                    g += ((v >> 8) & 255) * al;
                    b += (v & 255) * al;
                    a += al;
                    cnt++;
                }
            }
            if (a)
                o[tx] = (uint32_t)(a / cnt) << 24 | (uint32_t)(r / a) << 16 |
                        (uint32_t)(g / a) << 8 | (uint32_t)(b / a);
        }
    }
    return out;
}

size_t base64_decode(const uint8_t* s, size_t n, uint8_t* out)
{
    size_t k = 0;
    unsigned long acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t c = s[i];
        int v;
        if (c >= 'A' && c <= 'Z')
            v = c - 'A';
        else if (c >= 'a' && c <= 'z')
            v = c - 'a' + 26;
        else if (c >= '0' && c <= '9')
            v = c - '0' + 52;
        else if (c == '+')
            v = 62;
        else if (c == '/')
            v = 63;
        else
            continue;
        acc = (acc << 6 | (unsigned long)v) & 0xffffff;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[k++] = (uint8_t)(acc >> bits);
        }
    }
    return k;
}

/* ------------------------------------------------------------------ */
/* SixelDecoder                                                        */

namespace {

/* The VT340's colour registers, in percent. */
const unsigned char k_vt340[16][3] = {
    { 0, 0, 0 }, { 20, 20, 80 }, { 80, 13, 13 }, { 20, 80, 20 },
    { 80, 20, 80 }, { 20, 80, 80 }, { 80, 80, 20 }, { 53, 53, 53 },
    { 26, 26, 26 }, { 33, 33, 60 }, { 60, 26, 26 }, { 33, 60, 33 },
    { 60, 33, 60 }, { 33, 60, 60 }, { 60, 60, 33 }, { 80, 80, 80 }
};

int pct(int v)
{
    if (v < 0)
        v = 0;
    if (v > 100)
        v = 100;
    return (v * 255 + 50) / 100;
}

uint32_t rgb_pct(int r, int g, int b)
{
    return 0xff000000u | (uint32_t)pct(r) << 16 | (uint32_t)pct(g) << 8 | (uint32_t)pct(b);
}

double hue_part(double p, double q, double t)
{
    if (t < 0)
        t += 1;
    if (t > 1)
        t -= 1;
    if (t < 1.0 / 6)
        return p + (q - p) * 6 * t;
    if (t < 0.5)
        return q;
    if (t < 2.0 / 3)
        return p + (q - p) * (2.0 / 3 - t) * 6;
    return p;
}

/* DEC's HLS: hue 0 is blue, 120 red, 240 green. */
uint32_t rgb_hls(int h, int l, int s)
{
    double hh = (double)((h % 360 + 360 + 240) % 360) / 360.0;
    double ll = (l < 0 ? 0 : l > 100 ? 100 : l) / 100.0;
    double ss = (s < 0 ? 0 : s > 100 ? 100 : s) / 100.0;
    double r = ll, g = ll, b = ll;
    if (ss > 0) {
        double q = ll < 0.5 ? ll * (1 + ss) : ll + ss - ll * ss;
        double p = 2 * ll - q;
        r = hue_part(p, q, hh + 1.0 / 3);
        g = hue_part(p, q, hh);
        b = hue_part(p, q, hh - 1.0 / 3);
    }
    return 0xff000000u | (uint32_t)(r * 255 + 0.5) << 16 | (uint32_t)(g * 255 + 0.5) << 8 |
           (uint32_t)(b * 255 + 0.5);
}

} /* namespace */

SixelDecoder::SixelDecoder()
    : px_(0), cap_w_(0), cap_h_(0), w_(0), h_(0), raster_w_(0), raster_h_(0), x_(0), y_(0),
      color_(0), bg_(0), transparent_(false), cmd_(0), n_nums_(0), failed_(false)
{
}

SixelDecoder::~SixelDecoder()
{
    free(px_);
}

void SixelDecoder::begin(int p2, uint32_t bg)
{
    free(px_);
    px_ = 0;
    cap_w_ = cap_h_ = w_ = h_ = raster_w_ = raster_h_ = x_ = y_ = 0;
    transparent_ = p2 == 1;
    bg_ = 0xff000000u | (bg & 0xffffff);
    for (int i = 0; i < REGISTERS; i++)
        regs_[i] = i < 16 ? rgb_pct(k_vt340[i][0], k_vt340[i][1], k_vt340[i][2]) : 0xff000000u;
    color_ = regs_[0];
    cmd_ = 0;
    n_nums_ = 0;
    failed_ = false;
}

bool SixelDecoder::grow(int w, int h)
{
    if (w > MAX_SIDE)
        w = MAX_SIDE;
    if (h > MAX_SIDE)
        h = MAX_SIDE;
    if (w <= cap_w_ && h <= cap_h_)
        return true;
    int nw = cap_w_ ? cap_w_ : 64, nh = cap_h_ ? cap_h_ : 64;
    while (nw < w)
        nw *= 2;
    while (nh < h)
        nh *= 2;
    if (nw > MAX_SIDE)
        nw = MAX_SIDE;
    if (nh > MAX_SIDE)
        nh = MAX_SIDE;
    uint32_t* n = (uint32_t*)calloc((size_t)nw * (size_t)nh, 4);
    if (!n) {
        failed_ = true;
        return false;
    }
    for (int y = 0; y < cap_h_; y++)
        memcpy(n + (size_t)y * (size_t)nw, px_ + (size_t)y * (size_t)cap_w_, (size_t)cap_w_ * 4);
    free(px_);
    px_ = n;
    cap_w_ = nw;
    cap_h_ = nh;
    return true;
}

void SixelDecoder::set_register(int i, int space, int a, int b, int c)
{
    if (i < 0 || i >= REGISTERS)
        return;
    if (space == 1)
        regs_[i] = rgb_hls(a, b, c);
    else if (space == 2)
        regs_[i] = rgb_pct(a, b, c);
}

void SixelDecoder::sixel(int bits, int count)
{
    if (failed_ || count <= 0)
        return;
    if (bits) {
        int top = 0;
        for (int b = 0; b < 6; b++)
            if (bits & (1 << b))
                top = b;
        if (x_ >= MAX_SIDE || y_ + top >= MAX_SIDE) {
            x_ += count;
            return;
        }
        int x1 = x_ + count > MAX_SIDE ? MAX_SIDE : x_ + count;
        if (!grow(x1, y_ + top + 1))
            return;
        for (int b = 0; b <= top; b++) {
            if (!(bits & (1 << b)))
                continue;
            uint32_t* row = px_ + (size_t)(y_ + b) * (size_t)cap_w_;
            for (int x = x_; x < x1; x++)
                row[x] = color_;
        }
        if (x1 > w_)
            w_ = x1;
        if (y_ + top + 1 > h_)
            h_ = y_ + top + 1;
    }
    x_ += count;
}

/* The command in cmd_ with its numbers is complete. */
void SixelDecoder::command(uint8_t)
{
    switch (cmd_) {
    case '#':
        if (n_nums_ >= 5)
            set_register(nums_[0], nums_[1], nums_[2], nums_[3], nums_[4]);
        if (n_nums_ >= 1 && nums_[0] >= 0 && nums_[0] < REGISTERS)
            color_ = regs_[nums_[0]];
        break;
    case '"':
        /* Pan;Pad;Ph;Pv: the size, filled with the background. */
        if (n_nums_ >= 4 && nums_[2] > 0 && nums_[3] > 0) {
            raster_w_ = nums_[2] > MAX_SIDE ? MAX_SIDE : nums_[2];
            raster_h_ = nums_[3] > MAX_SIDE ? MAX_SIDE : nums_[3];
            grow(raster_w_, raster_h_);
        }
        break;
    default:
        break;
    }
    cmd_ = 0;
    n_nums_ = 0;
}

void SixelDecoder::put(const uint8_t* s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        uint8_t c = s[i];
        if (cmd_) {
            if (c >= '0' && c <= '9') {
                if (n_nums_ == 0)
                    n_nums_ = 1, nums_[0] = 0;
                int& v = nums_[n_nums_ - 1];
                if (v < 100000)
                    v = v * 10 + (c - '0');
                continue;
            }
            if (c == ';') {
                if (n_nums_ == 0)
                    n_nums_ = 1, nums_[0] = 0;
                if (n_nums_ < 5)
                    nums_[n_nums_++] = 0;
                continue;
            }
            if (cmd_ == '!') {
                /* The repeat count applies to this sixel. */
                int count = n_nums_ ? nums_[0] : 1;
                cmd_ = 0;
                n_nums_ = 0;
                if (c >= 0x3f && c <= 0x7e) {
                    sixel(c - 0x3f, count ? count : 1);
                    continue;
                }
            } else {
                command(c);
            }
        }
        if (c >= 0x3f && c <= 0x7e) {
            sixel(c - 0x3f, 1);
        } else if (c == '#' || c == '!' || c == '"') {
            cmd_ = c;
            n_nums_ = 0;
        } else if (c == '$') {
            x_ = 0;
        } else if (c == '-') {
            x_ = 0;
            y_ += 6;
        }
    }
}

bool SixelDecoder::finish(uint32_t** px, int* w, int* h)
{
    if (cmd_ && cmd_ != '!')
        command(0);
    int fw = w_ > raster_w_ ? w_ : raster_w_;
    int fh = h_ > raster_h_ ? h_ : raster_h_;
    if (fw > cap_w_)
        fw = cap_w_;
    if (fh > cap_h_)
        fh = cap_h_;
    if (failed_ || !px_ || fw <= 0 || fh <= 0) {
        free(px_);
        px_ = 0;
        cap_w_ = cap_h_ = 0;
        return false;
    }
    uint32_t* out = (uint32_t*)malloc((size_t)fw * (size_t)fh * 4);
    if (!out) {
        free(px_);
        px_ = 0;
        cap_w_ = cap_h_ = 0;
        return false;
    }
    for (int y = 0; y < fh; y++) {
        uint32_t* o = out + (size_t)y * (size_t)fw;
        const uint32_t* p = px_ + (size_t)y * (size_t)cap_w_;
        for (int x = 0; x < fw; x++)
            o[x] = p[x] ? p[x] : transparent_ ? 0 : bg_;
    }
    free(px_);
    px_ = 0;
    cap_w_ = cap_h_ = 0;
    *px = out;
    *w = fw;
    *h = fh;
    return true;
}

} /* namespace tl */
