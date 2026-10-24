/*
 * The kitty graphics protocol (APC G keys ; payload ST), on the image
 * store and image cells (DESIGN.md section 5.8). Direct transmission of
 * RGB, RGBA (both optionally zlib-compressed) and PNG (decoded by the
 * host), in chunks; placement in a box of cells with a source rectangle
 * and a pixel offset; deletion by the protocol's selectors; replies.
 * Images become image cells, so text written over one replaces it, and
 * z-index, animation, relative placements and Unicode placeholders are
 * not supported.
 */
#include "terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace tl {

namespace {

/* A transfer larger than this is refused (EFBIG). */
const size_t k_kitty_max = 64u << 20;
const int k_kitty_side = 10000;

} /* namespace */

void KittyCommand::clear()
{
    memset(this, 0, sizeof(*this));
    action = 't';
    medium = 'd';
    del = 'a';
    format = 32;
}

bool KittyCommand::parse(const uint8_t* s, size_t n)
{
    clear();
    size_t i = 0;
    while (i < n) {
        uint8_t key = s[i];
        if (i + 1 >= n || s[i + 1] != '=')
            return false;
        i += 2;
        size_t start = i;
        while (i < n && s[i] != ',')
            i++;
        size_t len = i - start;
        if (i < n)
            i++;                    /* the comma */
        if (key == 'a' || key == 't' || key == 'o' || key == 'd') {
            if (len != 1)
                return false;
            char c = (char)s[start];
            if (key == 'a') action = c;
            else if (key == 't') medium = c;
            else if (key == 'o') compression = c;
            else del = c;
            continue;
        }
        /* A number, possibly negative (z). */
        bool neg = len > 0 && s[start] == '-';
        unsigned long v = 0;
        size_t k = start + (neg ? 1 : 0);
        if (k == start + len)
            return false;
        for (; k < start + len; k++) {
            if (s[k] < '0' || s[k] > '9')
                return false;
            if (v < 0x10000000UL)
                v = v * 10 + (unsigned long)(s[k] - '0');
        }
        int iv = v > 0x7fffffffUL ? 0x7fffffff : (int)v;
        if (neg)
            iv = -iv;
        switch (key) {
        case 'f': format = iv; break;
        case 'i': id = (uint32_t)v; break;
        case 'I': number = (uint32_t)v; break;
        case 'p': placement = (uint32_t)v; break;
        case 's': src_w = iv; break;
        case 'v': src_h = iv; break;
        case 'x': x = iv; break;
        case 'y': y = iv; break;
        case 'w': w = iv; break;
        case 'h': h = iv; break;
        case 'X': off_x = iv; break;
        case 'Y': off_y = iv; break;
        case 'c': cols = iv; break;
        case 'r': rows = iv; break;
        case 'm': more = iv; break;
        case 'q': quiet = iv; break;
        case 'C': cursor = iv; break;
        case 'z': z = iv; break;
        default: break;         /* S, O, U, P, Q, H, V: not supported */
        }
    }
    return true;
}

void Terminal::kitty_reply(const KittyCommand& k, const char* msg)
{
    bool ok = strcmp(msg, "OK") == 0;
    if ((!k.id && !k.number) || (ok && k.quiet >= 1) || (!ok && k.quiet >= 2))
        return;
    char b[200];
    int n = sprintf(b, "\x1b_Gi=%lu", (unsigned long)k.id);
    if (k.number)
        n += sprintf(b + n, ",I=%lu", (unsigned long)k.number);
    if (k.placement)
        n += sprintf(b + n, ",p=%lu", (unsigned long)k.placement);
    sprintf(b + n, ";%.150s\x1b\\", msg);
    reply(b);
}

void Terminal::apc_dispatch(const uint8_t* s, size_t n, bool truncated)
{
    if (!n || s[0] != 'G')
        return;
    size_t semi = 1;
    while (semi < n && s[semi] != ';')
        semi++;
    KittyCommand k;
    if (!k.parse(s + 1, semi - 1)) {
        kloading_ = false;
        return;
    }
    const uint8_t* payload = semi < n ? s + semi + 1 : s + n;
    size_t plen = semi < n ? n - semi - 1 : 0;
    bool continuing = kloading_;
    if (!continuing) {
        if (k.action == 'p') {
            const KittyImages::Entry* e = k.id ? kitty_.get(k.id)
                                               : k.number ? kitty_.by_number(k.number) : 0;
            if (e && !k.id)
                k.id = e->id;
            kitty_reply(k, e ? kitty_put(k, *e) : "ENOENT:no such image");
            return;
        }
        if (k.action == 'd') {
            kitty_delete(k);
            return;
        }
        if (k.action != 't' && k.action != 'T' && k.action != 'q') {
            kitty_reply(k, "EINVAL:unsupported action");
            return;
        }
        kcmd_ = k;
        kdata_n_ = 0;
        kfailed_ = false;
    }
    /* Payload: base64, appended decoded. */
    if (truncated)
        kfailed_ = true;
    size_t need = kdata_n_ + plen / 4 * 3 + 3;
    if (!kfailed_ && need > k_kitty_max)
        kfailed_ = true;
    if (!kfailed_ && need > kdata_cap_) {
        size_t cap = kdata_cap_ ? kdata_cap_ : 65536;
        while (cap < need)
            cap *= 2;
        uint8_t* nd = (uint8_t*)realloc(kdata_, cap);
        if (nd) {
            kdata_ = nd;
            kdata_cap_ = cap;
        } else {
            kfailed_ = true;
        }
    }
    if (!kfailed_)
        kdata_n_ += base64_decode(payload, plen, kdata_ + kdata_n_);
    /* A later chunk's m (and q) are its only keys. */
    if (continuing && k.quiet)
        kcmd_.quiet = k.quiet;
    if (k.more) {
        kloading_ = true;
        return;
    }
    kloading_ = false;
    kitty_finish(kcmd_);
    if (kdata_cap_ > (1u << 20)) {
        free(kdata_);
        kdata_ = 0;
        kdata_cap_ = 0;
    }
    kdata_n_ = 0;
}

bool Terminal::kitty_decode(const KittyCommand& k, uint32_t** px, int* w, int* h,
                            const char** err)
{
    *px = 0;
    if (k.medium != 'd') {
        *err = "EINVAL:only direct transmission (t=d)";
        return false;
    }
    if (k.format == 100) {
        if (k.compression) {
            *err = "EINVAL:compressed PNG";
            return false;
        }
        if (!host_->decode_image(kdata_, kdata_n_, px, w, h) || !*px) {
            *err = "EBADPNG:cannot decode the PNG";
            return false;
        }
        if (*w <= 0 || *h <= 0 || *w > k_kitty_side || *h > k_kitty_side) {
            free(*px);
            *px = 0;
            *err = "EINVAL:image too big";
            return false;
        }
        return true;
    }
    if (k.format != 24 && k.format != 32) {
        *err = "EINVAL:unknown format";
        return false;
    }
    if (k.src_w <= 0 || k.src_h <= 0 || k.src_w > k_kitty_side || k.src_h > k_kitty_side) {
        *err = "EINVAL:bad size";
        return false;
    }
    int bpp = k.format / 8;
    size_t bytes = (size_t)k.src_w * (size_t)k.src_h * (size_t)bpp;
    const uint8_t* raw = kdata_;
    uint8_t* inflated = 0;
    if (k.compression == 'z') {
        inflated = (uint8_t*)malloc(bytes);
        if (!inflated || !host_->inflate(kdata_, kdata_n_, inflated, bytes)) {
            free(inflated);
            *err = "EINVAL:cannot inflate";
            return false;
        }
        raw = inflated;
    } else if (k.compression) {
        *err = "EINVAL:unknown compression";
        return false;
    } else if (kdata_n_ < bytes) {
        *err = "ENODATA:too little data";
        return false;
    }
    uint32_t* out = (uint32_t*)malloc((size_t)k.src_w * (size_t)k.src_h * 4);
    if (!out) {
        free(inflated);
        *err = "ENOMEM:out of memory";
        return false;
    }
    size_t np = (size_t)k.src_w * (size_t)k.src_h;
    for (size_t i = 0; i < np; i++) {
        const uint8_t* p = raw + i * (size_t)bpp;
        uint32_t a = bpp == 4 ? p[3] : 255;
        out[i] = a << 24 | (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
    }
    free(inflated);
    *px = out;
    *w = k.src_w;
    *h = k.src_h;
    return true;
}

void Terminal::kitty_finish(const KittyCommand& k0)
{
    KittyCommand k = k0;
    if (kfailed_) {
        kitty_reply(k, "EFBIG:too much data");
        return;
    }
    uint32_t* px;
    int w, h;
    const char* err = "OK";
    if (!kitty_decode(k, &px, &w, &h, &err)) {
        kitty_reply(k, err);
        return;
    }
    if (k.action == 'q') {
        free(px);
        kitty_reply(k, "OK");
        return;
    }
    bool anonymous = !k.id && !k.number;
    uint32_t id = k.id ? k.id : kitty_.free_id();
    if (!kitty_.add(id, k.number, px, w, h)) {
        kitty_reply(k, "ENOMEM:image too big");
        return;
    }
    if (k.action == 'T') {
        const KittyImages::Entry* e = kitty_.get(id);
        const char* r = e ? kitty_put(k, *e) : "ENOMEM:image dropped";
        if (anonymous)
            kitty_.remove(id);      /* nothing can refer to it again */
        if (k.number && !k.id)
            k.id = id;
        kitty_reply(k, r);
        return;
    }
    if (k.number && !k.id)
        k.id = id;
    kitty_reply(k, "OK");
}

void Terminal::kitty_forget_placement(int store_id)
{
    for (int i = 0; i < n_kplace_; i++)
        if (kplace_[i].store == store_id) {
            kplace_[i] = kplace_[--n_kplace_];
            return;
        }
}

/* Show image e as k says; returns the reply. */
const char* Terminal::kitty_put(const KittyCommand& k, const KittyImages::Entry& e)
{
    int cw = cell_w_ > 0 ? cell_w_ : 10, ch = cell_h_ > 0 ? cell_h_ : 20;
    int sx = k.x, sy = k.y;
    if (sx < 0 || sy < 0 || sx >= e.w || sy >= e.h)
        return "EINVAL:source rectangle outside the image";
    int sw = k.w > 0 && sx + k.w <= e.w ? k.w : e.w - sx;
    int sh = k.h > 0 && sy + k.h <= e.h ? k.h : e.h - sy;
    int ox = k.off_x < 0 ? 0 : k.off_x >= cw ? cw - 1 : k.off_x;
    int oy = k.off_y < 0 ? 0 : k.off_y >= ch ? ch - 1 : k.off_y;
    int cols = k.cols > 0 ? k.cols : 0, rows = k.rows > 0 ? k.rows : 0;
    if (cols > 1000)
        cols = 1000;
    if (rows > 1000)
        rows = 1000;
    int tw, th;
    if (cols && rows) {
        tw = cols * cw - ox;        /* kitty stretches to the box */
        th = rows * ch - oy;
    } else if (cols) {
        tw = cols * cw - ox;
        th = (int)((double)sh * tw / sw + 0.5);
        rows = (oy + th + ch - 1) / ch;
    } else if (rows) {
        th = rows * ch - oy;
        tw = (int)((double)sw * th / sh + 0.5);
        cols = (ox + tw + cw - 1) / cw;
    } else {
        tw = sw;
        th = sh;
        cols = (ox + tw + cw - 1) / cw;
        rows = (oy + th + ch - 1) / ch;
    }
    if (tw < 1)
        tw = 1;
    if (th < 1)
        th = 1;
    uint32_t* buf = resample(e.px, e.w, e.h, sx, sy, sw, sh, tw, th, ox, oy);
    if (!buf)
        return "ENOMEM:cannot scale the image";
    /* The same image and placement id replace the earlier placement. */
    if (k.placement) {
        bool ids[ImageStore::MAX_ID + 1];
        memset(ids, 0, sizeof(ids));
        bool any = false;
        for (int i = 0; i < n_kplace_; i++)
            if (kplace_[i].image == e.id && kplace_[i].placement == k.placement) {
                ids[kplace_[i].store] = true;
                any = true;
            }
        if (any) {
            wipe_images(ids);
            for (int i = 1; i <= ImageStore::MAX_ID; i++)
                if (ids[i])
                    kitty_forget_placement(i);
        }
    }
    int id = place_image(buf, ox + tw, oy + th, cw, ch, cols, rows,
                         k.cursor == 1 ? MOVE_NONE : MOVE_KITTY);
    if (!id)
        return "ENOMEM:no room for the image";
    kitty_forget_placement(id);     /* a reused store id */
    if (n_kplace_ < ImageStore::MAX_ID) {
        KittyPlacement& p = kplace_[n_kplace_++];
        p.image = e.id;
        p.placement = k.placement;
        p.store = id;
        const Image* im = images_.get(id);
        p.serial = im ? im->serial : 0;
    }
    return "OK";
}

void Terminal::wipe_images(const bool* ids)
{
    Grid& g = screen().grid;
    uint64_t end = g.base() + g.total_rows(), top = g.active_top();
    for (uint64_t a = g.base(); a < end; a++) {
        Row& r = g.abs_row(a);
        if (!r.cells || !(r.flags & ROW_HAS_IMAGES))
            continue;
        for (int x = 0; x < r.cols; x++) {
            Cell& c = r.cells[x];
            if (!c.is_image() || !ids[c.image_id()])
                continue;
            c = Cell();
            if (a >= top)
                r.damage(x, x);
            else
                full_damage_ = true;
        }
    }
}

void Terminal::kitty_delete(const KittyCommand& k)
{
    /* Which of the store's images are kitty placements still shown. */
    bool kitty[ImageStore::MAX_ID + 1];
    memset(kitty, 0, sizeof(kitty));
    for (int i = 0; i < n_kplace_;) {
        const Image* im = images_.get(kplace_[i].store);
        if (!im || im->serial != kplace_[i].serial) {
            kplace_[i] = kplace_[--n_kplace_];
            continue;
        }
        kitty[kplace_[i].store] = true;
        i++;
    }
    bool del[ImageStore::MAX_ID + 1];
    memset(del, 0, sizeof(del));
    char d = k.del;
    bool free_data = d >= 'A' && d <= 'Z';
    char lower = free_data ? (char)(d - 'A' + 'a') : d;
    Grid& g = screen().grid;
    /* Cells to look at: on the screen, or one cell, row or column. */
    int x0 = 0, x1 = cols_ - 1, y0 = 0, y1 = rows_ - 1;
    bool by_cells = true;
    switch (lower) {
    case 'a': case 'z': break;
    case 'c': x0 = x1 = cur_.x; y0 = y1 = cur_.y; break;
    case 'p': x0 = x1 = k.x - 1; y0 = y1 = k.y - 1; break;
    case 'x': x0 = x1 = k.x - 1; break;
    case 'y': y0 = y1 = k.y - 1; break;
    default: by_cells = false; break;
    }
    uint32_t image_id = k.id;
    if (lower == 'n') {
        const KittyImages::Entry* e = kitty_.by_number(k.number);
        image_id = e ? e->id : 0;
    }
    uint32_t freed[KittyImages::MAX];
    int n_freed = 0;
    if (by_cells) {
        if (x0 < 0 || y0 < 0 || x1 >= cols_ || y1 >= rows_)
            return;
        for (int y = y0; y <= y1; y++) {
            const Row& r = g.row(y);
            for (int x = x0; x <= x1 && x < r.cols; x++)
                if (r.cells[x].is_image() && kitty[r.cells[x].image_id()])
                    del[r.cells[x].image_id()] = true;
        }
    }
    for (int i = 0; i < n_kplace_; i++) {
        const KittyPlacement& p = kplace_[i];
        bool hit;
        if (by_cells)
            hit = del[p.store];
        else if (lower == 'i' || lower == 'n')
            hit = p.image == image_id && (!k.placement || p.placement == k.placement);
        else if (lower == 'r')
            hit = p.image >= (uint32_t)k.x && p.image <= (uint32_t)k.y;
        else
            hit = false;
        if (!hit)
            continue;
        del[p.store] = true;
        if (free_data && n_freed < KittyImages::MAX)
            freed[n_freed++] = p.image;
    }
    if (free_data && (lower == 'i' || lower == 'n') && image_id)
        freed[n_freed < KittyImages::MAX ? n_freed++ : 0] = image_id;
    wipe_images(del);
    for (int i = 1; i <= ImageStore::MAX_ID; i++)
        if (del[i])
            kitty_forget_placement(i);
    for (int i = 0; i < n_freed; i++)
        kitty_.remove(freed[i]);
}

} /* namespace tl */
