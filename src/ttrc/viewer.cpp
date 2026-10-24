#include "viewer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ttrc {

namespace {

const size_t k_max_view = 64u << 20;
const unsigned k_kitty_id = 4711;
const int k_tab = 8;

/* One character at p: its bytes, columns and what to draw. Control
 * characters are shown as '.' in the control style. */
int char_at(const char* p, const char* end, uint32_t* cp, int col, int* cols, bool* ctrl)
{
    char tmp[8];
    size_t avail = (size_t)(end - p);
    size_t n = avail < 4 ? avail : 4;
    memcpy(tmp, p, n);
    tmp[n] = 0;
    int used = tu::decode_char(tmp, cp);
    if (used <= 0)
        used = 1;
    *ctrl = false;
    if (*cp == '\t') {
        *cols = k_tab - col % k_tab;
    } else if (*cp < 0x20 || *cp == 0x7f || (*cp >= 0x80 && *cp < 0xa0)) {
        *cols = 1;
        *ctrl = true;
    } else {
        char one[8];
        memcpy(one, p, (size_t)used);
        one[used] = 0;
        *cols = tu::Screen::text_width(one);
        if (*cols <= 0)
            *cols = 0;
    }
    return used;
}

bool match_at(const char* d, size_t len, size_t i, const char* t, size_t tl, bool fold)
{
    if (i + tl > len)
        return false;
    if (!fold)
        return memcmp(d + i, t, tl) == 0;
    for (size_t k = 0; k < tl; k++)
        if (tolower((unsigned char)d[i + k]) != tolower((unsigned char)t[k]))
            return false;
    return true;
}

int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* "de ad be ef", "deadbeef", "0xde,0xad": the bytes, or -1 when a
 * group has an odd number of digits or something else is in it. */
long parse_hex_bytes(const char* t, char* out, size_t cap)
{
    size_t n = 0;
    const char* p = t;
    while (*p) {
        if (*p == ' ' || *p == '\t' || *p == ',') {
            p++;
            continue;
        }
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
            p += 2;
        const char* g = p;
        while (hex_digit(*p) >= 0)
            p++;
        size_t digits = (size_t)(p - g);
        if (!digits || digits % 2 || (*p && *p != ' ' && *p != '\t' && *p != ','))
            return -1;
        for (size_t k = 0; k < digits; k += 2) {
            if (n == cap)
                return -1;
            out[n++] = (char)(hex_digit(g[k]) * 16 + hex_digit(g[k + 1]));
        }
    }
    return (long)n;
}

/* 0x1f40, 1f40h, $1f40, or 8000 (hex if it has a-f in it). False if it
 * is not a number. */
bool parse_offset(const char* t, size_t* out)
{
    while (*t == ' ')
        t++;
    bool hex = false;
    if (t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
        hex = true;
        t += 2;
    } else if (*t == '$') {
        hex = true;
        t++;
    }
    const char* e = t;
    while (hex_digit(*e) >= 0) {
        if (!isdigit((unsigned char)*e))
            hex = true;
        e++;
    }
    if (e == t)
        return false;
    const char* rest = e;
    if (*rest == 'h' || *rest == 'H') {
        hex = true;
        rest++;
    }
    while (*rest == ' ')
        rest++;
    if (*rest)
        return false;
    size_t v = 0;
    for (const char* p = t; p < e; p++) {
        int d = hex ? hex_digit(*p) : *p - '0';
        if (!hex && (d < 0 || d > 9))
            return false;
        v = v * (hex ? 16 : 10) + (size_t)d;
    }
    *out = v;
    return true;
}

void key_bar(tu::Screen& s, const Theme& t, const char* const* labels)
{
    int w = s.cols(), y = s.rows() - 1;
    int slot = w / 10;
    if (slot < 3)
        return;
    s.fill(0, y, w, 1, ' ', t.key_num);
    for (int i = 0; i < 10; i++) {
        int x = i * slot;
        char num[4];
        sprintf(num, "%d", i + 1);
        int nw = (int)strlen(num);
        s.text(x, y, num, t.key_num);
        int lw = (i == 9 ? w - x : slot) - nw;
        s.fill(x + nw, y, lw, 1, ' ', t.key_label);
        s.text(x + nw, y, labels[i], t.key_label, lw);
    }
}

} /* namespace */

void FullScreen::set_note(const char* n)
{
    snprintf(note_, sizeof(note_), "%.250s", n);
}

/* ------------------------------------------------------------------ */
/* Viewer                                                              */

Viewer::Viewer()
    : data_(0), len_(0), truncated_(false), hex_(false), wrap_(true), top_line_(0), top_sub_(0),
      left_(0), hex_top_(0), rows_(20), width_(80), bpr_(16), hit_(0), hit_len_(0), pat_len_(0),
      proto_(ti::PROTO_OFF), term_(0), image_(false), img_x_(0), img_y_(0), img_cols_(0),
      img_rows_(0), img_bg_(0), cell_known_(false), over_dirty_(true), over_shown_(false)
{
    name_[0] = 0;
}

Viewer::~Viewer()
{
    free(data_);
}

void Viewer::set_images(ti::Protocol p, const tu::Term* term)
{
    proto_ = p;
    term_ = term;
}

int Viewer::load(tf::Fs* fs, const char* path)
{
    int err = 0;
    tf::Stream* s = fs->open(path, tf::OPEN_READ, 0, &err);
    if (!s)
        return err;
    size_t cap = 65536;
    data_ = (char*)malloc(cap);
    len_ = 0;
    while (data_) {
        if (len_ == cap) {
            if (cap >= k_max_view) {
                truncated_ = true;
                break;
            }
            char* n = (char*)realloc(data_, cap * 2);
            if (!n)
                break;
            data_ = n;
            cap *= 2;
        }
        long r = s->read(data_ + len_, cap - len_);
        if (r < 0) {
            err = (int)-r;
            break;
        }
        if (r == 0)
            break;
        len_ += (size_t)r;
    }
    delete s;
    if (err)
        return err;
    lines_.push_back(0);
    for (size_t i = 0; i < len_; i++)
        if (data_[i] == '\n' && i + 1 < len_)
            lines_.push_back(i + 1);
    const char* b = strrchr(path, '/');
    snprintf(name_, sizeof(name_), "%s", b ? b + 1 : path);
    /* Binary (control bytes other than tabs and line ends in more than
     * one byte of twenty): start in hex. */
    size_t bad = 0, look = len_ < 4096 ? len_ : 4096;
    for (size_t i = 0; i < look; i++) {
        unsigned char c = (unsigned char)data_[i];
        if ((c < 0x20 && c != '\t' && c != '\n' && c != '\r' && c != '\f') || c == 0x7f)
            bad++;
    }
    hex_ = look > 0 && bad > look / 20;
    if (proto_ != ti::PROTO_OFF && !truncated_ &&
        ti::sniff((const unsigned char*)data_, len_) != ti::FMT_UNKNOWN) {
        char err[200];
        ti::DecodeOptions o;
        /* No terminal shows more; JPEG decodes this much faster. */
        o.reduce_to = 2048;
        if (ti::decode((const unsigned char*)data_, len_, &img_, &img_info_, err, sizeof(err), o)) {
            image_ = true;
        } else {
            char n[260];
            snprintf(n, sizeof(n), "%.200s Showing the bytes.", err);
            set_note(n);
            hex_ = true;
        }
    }
    return 0;
}

size_t Viewer::line_start(long i) const
{
    return lines_[(int)i];
}

size_t Viewer::line_end(long i) const
{
    size_t e = i + 1 < lines_.size() ? lines_[(int)i + 1] - 1 : len_;
    if (e > len_)
        e = len_;
    if (i + 1 >= lines_.size() && e > 0 && e == len_ && data_[e - 1] == '\n')
        e--;
    return e;
}

long Viewer::line_of(size_t off) const
{
    long lo = 0, hi = lines_.size() - 1;
    while (lo < hi) {
        long mid = (lo + hi + 1) / 2;
        if (lines_[(int)mid] <= off)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

int Viewer::rows_of(long i, int w, size_t* starts, int max) const
{
    size_t s = line_start(i), e = line_end(i);
    starts[0] = s;
    if (!wrap_ || w <= 0)
        return 1;
    int n = 1, col = 0;
    const char* p = data_ + s;
    const char* end = data_ + e;
    while (p < end) {
        uint32_t cp;
        int cols;
        bool ctrl;
        int used = char_at(p, end, &cp, col, &cols, &ctrl);
        if (col + cols > w && col > 0) {
            if (n < max)
                starts[n] = (size_t)(p - data_);
            n++;
            col = 0;
            continue;
        }
        col += cols;
        p += used;
    }
    return n < max ? n : max;
}

void Viewer::scroll(int rows)
{
    if (hex_) {
        long last = len_ ? (long)((len_ - 1) / (size_t)bpr_) : 0;
        hex_top_ += rows;
        if (hex_top_ > last - rows_ + 1)
            hex_top_ = last - rows_ + 1;
        if (hex_top_ < 0)
            hex_top_ = 0;
        return;
    }
    size_t starts[512];
    while (rows > 0) {
        int n = rows_of(top_line_, width_, starts, 512);
        if (top_sub_ + 1 < n) {
            top_sub_++;
        } else if (top_line_ + 1 < lines_.size()) {
            top_line_++;
            top_sub_ = 0;
        } else {
            break;
        }
        rows--;
    }
    while (rows < 0) {
        if (top_sub_ > 0) {
            top_sub_--;
        } else if (top_line_ > 0) {
            top_line_--;
            top_sub_ = rows_of(top_line_, width_, starts, 512) - 1;
        } else {
            break;
        }
        rows++;
    }
}

void Viewer::top_to(size_t off, int above)
{
    if (hex_) {
        hex_top_ = (long)(off / (size_t)bpr_) - above;
        scroll(0);
        return;
    }
    top_line_ = line_of(off);
    top_sub_ = 0;
    size_t starts[512];
    int n = rows_of(top_line_, width_, starts, 512);
    for (int i = 0; i < n; i++)
        if (starts[i] <= off)
            top_sub_ = i;
    scroll(-above);
}

void Viewer::draw(tu::Screen& s, const Theme& t)
{
    int w = s.cols(), h = s.rows();
    width_ = w;
    rows_ = h - 2;
    if (rows_ < 1)
        rows_ = 1;
    s.fill(0, 0, w, h, ' ', t.text);
    /* The status line. */
    char st[700], where[80];
    int pct = len_ ? (int)((hex_ ? (double)hex_top_ * bpr_ : (double)line_start(top_line_)) * 100 /
                            (double)len_) : 100;
    if (image_)
        sprintf(where, "%s %d x %d (%s)", ti::format_name(img_info_.format), img_info_.width,
                img_info_.height, img_info_.decoder);
    else if (hex_)
        sprintf(where, "offset 0x%lx", (unsigned long)hex_top_ * (unsigned long)bpr_);
    else
        sprintf(where, "line %ld of %d", top_line_ + 1, lines_.size());
    char pcts[16] = "";
    if (!image_)
        sprintf(pcts, "%d%%   ", pct);
    snprintf(st, sizeof(st), " %s   %s   %s%lu bytes%s%s%s", name_, where, pcts,
             (unsigned long)len_, image_ ? "   [image]" : hex_ ? "   [hex]" : "",
             !image_ && !hex_ && !wrap_ ? "   [unwrapped]" : "",
             truncated_ ? "   [first 64 MB]" : "");
    s.fill(0, 0, w, 1, ' ', t.text_status);
    s.text(0, 0, note_[0] ? note_ : st, t.text_status, w);

    if (image_) {
        draw_image(s, t);
    } else if (hex_) {
        bpr_ = w >= 75 ? 16 : 8;
        for (int r = 0; r < rows_; r++) {
            size_t off = (size_t)(hex_top_ + r) * (size_t)bpr_;
            if (off >= len_)
                break;
            char b[32];
            sprintf(b, "%08lx", (unsigned long)off);
            s.text(0, r + 1, b, t.text_ctrl);
            for (int k = 0; k < bpr_ && off + (size_t)k < len_; k++) {
                size_t i = off + (size_t)k;
                bool hit = hit_len_ && i >= hit_ && i < hit_ + hit_len_;
                const tu::Style& hs = hit ? t.text_select : t.text;
                sprintf(b, "%02x", (unsigned char)data_[i]);
                int x = 10 + k * 3 + (k >= 8 ? 1 : 0);
                s.text(x, r + 1, b, hs);
                unsigned char c = (unsigned char)data_[i];
                char a[2] = { (char)(c >= 0x20 && c < 0x7f ? c : '.'), 0 };
                s.text(10 + bpr_ * 3 + 2 + k, r + 1, a, hit ? t.text_select : c >= 0x20 && c < 0x7f ? t.text : t.text_ctrl);
            }
        }
    } else {
        long ln = top_line_;
        int sub = top_sub_;
        size_t starts[512];
        for (int r = 0; r < rows_ && ln < lines_.size();) {
            int n = rows_of(ln, w, starts, 512);
            for (; sub < n && r < rows_; sub++, r++) {
                size_t a = starts[sub];
                size_t e = sub + 1 < n ? starts[sub + 1] : line_end(ln);
                const char* p = data_ + a;
                const char* end = data_ + e;
                /* Columns before this row of the line (for tabs). */
                int col = 0, x = 0;
                if (!wrap_) {
                    /* Skip left_ columns. */
                    while (p < end && col < left_) {
                        uint32_t cp;
                        int cols;
                        bool ctrl;
                        p += char_at(p, end, &cp, col, &cols, &ctrl);
                        col += cols;
                    }
                    x = col - left_;
                }
                while (p < end && x < w) {
                    uint32_t cp;
                    int cols;
                    bool ctrl;
                    size_t off = (size_t)(p - data_);
                    int used = char_at(p, end, &cp, col, &cols, &ctrl);
                    bool hit = hit_len_ && off >= hit_ && off < hit_ + hit_len_;
                    const tu::Style& cs = hit ? t.text_select : ctrl ? t.text_ctrl : t.text;
                    if (cp == '\t' || ctrl) {
                        s.fill(x, r + 1, cols, 1, ' ', cs);
                        if (ctrl)
                            s.put(x, r + 1, '.', cs);
                    } else {
                        char one[8];
                        memcpy(one, p, (size_t)used);
                        one[used] = 0;
                        s.text(x, r + 1, one, cs, w - x);
                    }
                    x += cols;
                    col += cols;
                    p += used;
                }
            }
            ln++;
            sub = 0;
        }
    }
    const char* const keys[10] = {
        "Help", image_ ? "" : wrap_ ? "UnWrap" : "Wrap", "Quit", hex_ && !image_ ? "Ascii" : "Hex",
        "Goto", img_.empty() ? "" : image_ ? "Text" : "Image", "Search", "", "", "Quit"
    };
    key_bar(s, t, keys);
    s.set_cursor(0, 0, false);
}

ToolRequest Viewer::handle(const tu::Event& ev)
{
    note_[0] = 0;
    if (image_) {
        /* Only leaving the image means anything here. */
        bool fkey = ev.key >= tu::KEY_F1 && ev.key <= tu::KEY_F1 + 9;
        int n = fkey ? ev.key - tu::KEY_F1 + 1 : 0;
        int ch = ev.key == tu::KEY_CHAR && !(ev.mods & tu::MOD_CTRL) ? (int)ev.ch : 0;
        if (n == 1) {
            set_note("F4 or h: the bytes in hex. F6 or i: the image again from text or hex. q quits.");
            return REQ_NONE;
        }
        if (n == 4 || ch == 'h' || ch == 'H') {
            image_to_hex();
            return REQ_NONE;
        }
        if (n == 6 || ch == 'i') {
            image_to_hex();
            hex_ = false;
            return REQ_NONE;
        }
        if (n == 5 || ch == 'g') {
            image_to_hex();
            return REQ_GOTO;
        }
        if (n == 7 || ch == '/') {
            image_to_hex();
            return REQ_SEARCH;
        }
        if (n == 3 || n == 10 || ch == 'q' || ev.key == tu::KEY_ESC)
            return REQ_CLOSE;
        return REQ_NONE;
    }
    if (ev.key == tu::KEY_MOUSE) {
        if (ev.action == tu::MOUSE_WHEEL_UP)
            scroll(-3);
        else if (ev.action == tu::MOUSE_WHEEL_DOWN)
            scroll(3);
        return REQ_NONE;
    }
    int page = rows_ > 1 ? rows_ - 1 : 1;
    switch (ev.key) {
    case tu::KEY_UP: scroll(-1); return REQ_NONE;
    case tu::KEY_DOWN: case tu::KEY_ENTER: scroll(1); return REQ_NONE;
    case tu::KEY_PGUP: scroll(-page); return REQ_NONE;
    case tu::KEY_PGDN: scroll(page); return REQ_NONE;
    case tu::KEY_HOME:
        top_line_ = 0;
        top_sub_ = 0;
        hex_top_ = 0;
        return REQ_NONE;
    case tu::KEY_END:
        top_line_ = lines_.size() - 1;
        top_sub_ = 0;
        hex_top_ = len_ ? (long)((len_ - 1) / (size_t)bpr_) : 0;
        {
            size_t starts[512];
            top_sub_ = rows_of(top_line_, width_, starts, 512) - 1;
        }
        scroll(-(rows_ - 1));
        return REQ_NONE;
    case tu::KEY_LEFT:
        if (!wrap_ && left_ > 0)
            left_ = left_ > 8 ? left_ - 8 : 0;
        return REQ_NONE;
    case tu::KEY_RIGHT:
        if (!wrap_)
            left_ += 8;
        return REQ_NONE;
    case tu::KEY_ESC:
        return REQ_CLOSE;
    default:
        break;
    }
    if (ev.key >= tu::KEY_F1 && ev.key <= tu::KEY_F1 + 9) {
        int n = ev.key - tu::KEY_F1 + 1;
        switch (n) {
        case 1: set_note("Arrows, PgUp/PgDn, Home/End move. F2 wrap. F4 hex. F5 go to. F6 image. F7 or / search, n next. q quit."); return REQ_NONE;
        case 2:
            if (!hex_) {
                wrap_ = !wrap_;
                top_sub_ = 0;
                left_ = 0;
            }
            return REQ_NONE;
        case 4: {
            size_t at = hex_ ? (size_t)hex_top_ * (size_t)bpr_ : line_start(top_line_);
            hex_ = !hex_;
            top_to(at, 0);
            return REQ_NONE;
        }
        case 5: return REQ_GOTO;
        case 6:
            if (!img_.empty()) {
                image_ = true;
                over_dirty_ = true;
            }
            return REQ_NONE;
        case 7:
            if (ev.mods & tu::MOD_SHIFT) {
                search_again();
                return REQ_NONE;
            }
            return REQ_SEARCH;
        case 3: case 10: return REQ_CLOSE;
        default: return REQ_NONE;
        }
    }
    if (ev.key == tu::KEY_CHAR && !(ev.mods & tu::MOD_CTRL)) {
        switch (ev.ch) {
        case 'q': return REQ_CLOSE;
        case ' ': scroll(page); return REQ_NONE;
        case 'b': scroll(-page); return REQ_NONE;
        case '/': return REQ_SEARCH;
        case 'n': search_again(); return REQ_NONE;
        case 'h': case 'H': {
            size_t at = hex_ ? (size_t)hex_top_ * (size_t)bpr_ : line_start(top_line_);
            hex_ = !hex_;
            top_to(at, 0);
            return REQ_NONE;
        }
        case 'w': wrap_ = !wrap_; top_sub_ = 0; left_ = 0; return REQ_NONE;
        case 'g': return REQ_GOTO;
        case 'i':
            if (!img_.empty()) {
                image_ = true;
                over_dirty_ = true;
            }
            return REQ_NONE;
        default: return REQ_NONE;
        }
    }
    return REQ_NONE;
}

void Viewer::image_to_hex()
{
    if (!image_)
        return;
    image_ = false;
    hex_ = true;
    hex_top_ = 0;
    if (over_shown_)
        redraw_ = true;
}

void Viewer::draw_image(tu::Screen& s, const Theme& t)
{
    int w = s.cols(), area = rows_;
    if (term_ && term_->tier() == tu::TIER_BASIC && proto_ == ti::PROTO_BLOCKS) {
        const char* m = "This terminal has no colours for the image: F4 shows its bytes.";
        s.text(2, 1 + area / 2, m, t.text, w - 4);
        return;
    }
    unsigned long bg = 0;
    if (t.text.bg != tu::COLOR_DEFAULT && !tu::is_indexed(t.text.bg))
        bg = t.text.bg & 0xffffffUL;
    /* A cell's pixels: the terminal's, or the usual 1:2. */
    int cw = 8, ch = 16;
    cell_known_ = term_ && term_->cell_pixels(&cw, &ch);
    if (!cell_known_) {
        cw = 8;
        ch = 16;
    }
    bool blocks = proto_ == ti::PROTO_BLOCKS || proto_ == ti::PROTO_OFF;
    bool half = !term_ || term_->utf8();
    int vp = blocks && half ? 2 : 1;    /* image rows a cell */
    int tw, th, cols, rows;
    if (blocks) {
        ti::fit(img_.w, img_.h, w, area * vp, (double)cw * vp / ch, false, &tw, &th);
        cols = tw;
        rows = (th + vp - 1) / vp;
    } else {
        ti::fit(img_.w, img_.h, w * cw, area * ch, 1.0, false, &tw, &th);
        cols = (tw + cw - 1) / cw;
        rows = (th + ch - 1) / ch;
    }
    if (cols > w)
        cols = w;
    if (rows > area)
        rows = area;
    int x0 = (w - cols) / 2, y0 = 1 + (area - rows) / 2;
    if (shown_.w != tw || shown_.h != th || bg != img_bg_ || shown_.empty()) {
        if (!ti::scale(img_, tw, th, &shown_)) {
            s.text(2, 1 + area / 2, "Not enough memory to scale the image.", t.text, w - 4);
            return;
        }
        ti::flatten(&shown_, bg);
        img_bg_ = bg;
        over_dirty_ = true;
    }
    if (x0 != img_x_ || y0 != img_y_ || cols != img_cols_ || rows != img_rows_)
        over_dirty_ = true;
    img_x_ = x0;
    img_y_ = y0;
    img_cols_ = cols;
    img_rows_ = rows;
    if (!blocks)
        return;     /* paint_over sends it */
    for (int r = 0; r < rows; r++) {
        int top = r * vp, bot = top + 1;
        for (int x = 0; x < cols && x < shown_.w; x++) {
            const unsigned char* a = shown_.row(top) + x * 4;
            tu::Color ca = (tu::Color)a[0] << 16 | (tu::Color)a[1] << 8 | a[2];
            if (vp == 2) {
                tu::Color cb = (tu::Color)bg;
                if (bot < shown_.h) {
                    const unsigned char* b = shown_.row(bot) + x * 4;
                    cb = (tu::Color)b[0] << 16 | (tu::Color)b[1] << 8 | b[2];
                }
                /* U+2580 UPPER HALF BLOCK: the top pixel in front. */
                s.text(x0 + x, y0 + r, "\xe2\x96\x80", tu::Style(ca, cb));
            } else {
                s.put(x0 + x, y0 + r, ' ', tu::Style(ca, ca));
            }
        }
    }
}

void Viewer::paint_over(tu::Term& term, bool all)
{
    bool graphics = proto_ == ti::PROTO_KITTY || proto_ == ti::PROTO_SIXEL || proto_ == ti::PROTO_ITERM;
    if (!graphics)
        return;
    if (!image_ || shown_.empty()) {
        if (over_shown_)
            hide_over(term);
        return;
    }
    if (!all && !over_dirty_ && over_shown_)
        return;
    ti::Bytes b;
    if (proto_ == ti::PROTO_KITTY && over_shown_)
        ti::kitty_delete(k_kitty_id, &b);
    b.addf("\033[%d;%dH", img_y_ + 1, img_x_ + 1);
    if (proto_ == ti::PROTO_KITTY)
        /* Pixels one for one where the cell size is known (the image is
         * already that size); else a width in cells, keeping the aspect:
         * a whole box of cells would stretch it. */
        ti::encode_kitty(shown_, cell_known_ ? 0 : img_cols_, 0, k_kitty_id, &b);
    else if (proto_ == ti::PROTO_SIXEL)
        ti::encode_sixel(shown_, &b);
    else
        ti::encode_iterm(shown_, img_cols_, img_rows_, &b);
    if (b.failed) {
        set_note("Not enough memory to send the image.");
        return;
    }
    term.write_raw(b.d, b.n);
    over_shown_ = true;
    over_dirty_ = false;
}

void Viewer::hide_over(tu::Term& term)
{
    if (over_shown_ && proto_ == ti::PROTO_KITTY) {
        ti::Bytes b;
        ti::kitty_delete(k_kitty_id, &b);
        term.write_raw(b.d, b.n);
    }
    over_shown_ = false;
    over_dirty_ = true;
}

void Viewer::search_defaults(SearchQuery* q) const
{
    *q = last_;
    /* A new search starts in the mode the view is in. */
    if (last_.text.empty())
        q->hex = hex_;
}

void Viewer::search(const SearchQuery& q)
{
    long n;
    if (q.hex) {
        n = parse_hex_bytes(q.text.c_str(), pat_, sizeof(pat_));
        if (n <= 0) {
            set_note(n < 0 ? "Hex search: give bytes as pairs of digits, like de ad be ef."
                           : "Hex search: no bytes given.");
            pat_len_ = 0;
            return;
        }
    } else {
        n = (long)strlen(q.text.c_str());
        if (n >= (long)sizeof(pat_))
            n = (long)sizeof(pat_) - 1;
        memcpy(pat_, q.text.c_str(), (size_t)n);
    }
    pat_len_ = (size_t)n;
    last_ = q;
    search_again();
}

void Viewer::search_again()
{
    size_t tl = pat_len_;
    if (!tl)
        return;
    bool fold = !last_.hex && !last_.match_case;
    /* From after the last hit, or from the top of the screen. */
    size_t from = hit_len_ ? hit_ + 1
                           : hex_ ? (size_t)hex_top_ * (size_t)bpr_ : line_start(top_line_);
    for (int pass = 0; pass < 2; pass++) {
        for (size_t i = from; i + tl <= len_; i++)
            if (match_at(data_, len_, i, pat_, tl, fold)) {
                hit_ = i;
                hit_len_ = tl;
                top_to(i);
                if (pass)
                    set_note("Search wrapped around to the top.");
                return;
            }
        from = 0;
    }
    hit_len_ = 0;
    char n[300];
    snprintf(n, sizeof(n), "Not found: %.200s%s", last_.text.c_str(), last_.hex ? " (hex)" : "");
    set_note(n);
}

const char* Viewer::goto_title() const
{
    return hex_ ? "Go to offset" : "Go to line";
}

const char* Viewer::goto_label() const
{
    return hex_ ? "File offset (0x1f40 or 1f40h hex, 8000 decimal):" : "Line number:";
}

void Viewer::go_to(const char* where)
{
    if (hex_) {
        size_t off;
        if (!parse_offset(where, &off)) {
            set_note("Not an offset: give 0x1f40, 1f40h or a decimal number.");
            return;
        }
        if (off >= len_) {
            char n[100];
            sprintf(n, "Past the end: the file has 0x%lx bytes.", (unsigned long)len_);
            set_note(n);
            if (!len_)
                return;
            off = len_ - 1;
        }
        /* Mark the byte, so the next search starts after it. */
        hit_ = off;
        hit_len_ = 1;
        top_to(off);
        return;
    }
    long line = atol(where);
    if (line < 1)
        return;
    if (line > lines_.size())
        line = lines_.size();
    hit_len_ = 0;
    top_line_ = line - 1;
    top_sub_ = 0;
}

} /* namespace ttrc */
