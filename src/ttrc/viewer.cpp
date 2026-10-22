#include "viewer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ttrc {

namespace {

const size_t k_max_view = 64u << 20;
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

bool match_at(const char* d, size_t len, size_t i, const char* t, size_t tl)
{
    if (i + tl > len)
        return false;
    for (size_t k = 0; k < tl; k++)
        if (tolower((unsigned char)d[i + k]) != tolower((unsigned char)t[k]))
            return false;
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
      left_(0), hex_top_(0), rows_(20), width_(80), bpr_(16), hit_(0), hit_len_(0)
{
    name_[0] = 0;
    last_search_[0] = 0;
}

Viewer::~Viewer()
{
    free(data_);
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

void Viewer::top_to(size_t off)
{
    if (hex_) {
        hex_top_ = (long)(off / (size_t)bpr_) - 2;
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
    scroll(-2);
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
    char st[700];
    long line = hex_ ? line_of((size_t)hex_top_ * (size_t)bpr_) : top_line_;
    int pct = len_ ? (int)((hex_ ? (double)hex_top_ * bpr_ : (double)line_start(top_line_)) * 100 /
                            (double)len_) : 100;
    snprintf(st, sizeof(st), " %s   line %ld of %d   %d%%   %lu bytes%s%s%s", name_, line + 1,
             lines_.size(), pct, (unsigned long)len_, hex_ ? "   [hex]" : "",
             !hex_ && !wrap_ ? "   [unwrapped]" : "", truncated_ ? "   [first 64 MB]" : "");
    s.fill(0, 0, w, 1, ' ', t.text_status);
    s.text(0, 0, note_[0] ? note_ : st, t.text_status, w);

    if (hex_) {
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
    static const char* const keys[10] = {
        "Help", wrap_ ? "UnWrap" : "Wrap", "Quit", hex_ ? "Ascii" : "Hex", "Goto", "",
        "Search", "", "", "Quit"
    };
    key_bar(s, t, keys);
    s.set_cursor(0, 0, false);
}

ToolRequest Viewer::handle(const tu::Event& ev)
{
    note_[0] = 0;
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
        case 1: set_note("Arrows, PgUp/PgDn, Home/End move. F2 wrap. F4 hex. F7 or / search, n next. q quit."); return REQ_NONE;
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
            top_to(at);
            scroll(2);
            return REQ_NONE;
        }
        case 5: return REQ_GOTO;
        case 7:
            if (ev.mods & tu::MOD_SHIFT) {
                search(last_search_);
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
        case 'n': search(last_search_); return REQ_NONE;
        case 'h': case 'H': {
            size_t at = hex_ ? (size_t)hex_top_ * (size_t)bpr_ : line_start(top_line_);
            hex_ = !hex_;
            top_to(at);
            scroll(2);
            return REQ_NONE;
        }
        case 'w': wrap_ = !wrap_; top_sub_ = 0; left_ = 0; return REQ_NONE;
        case 'g': return REQ_GOTO;
        default: return REQ_NONE;
        }
    }
    return REQ_NONE;
}

void Viewer::search(const char* text)
{
    size_t tl = strlen(text);
    if (!tl)
        return;
    snprintf(last_search_, sizeof(last_search_), "%s", text);
    /* From after the last hit, or from the top of the screen. */
    size_t from = hit_len_ ? hit_ + 1
                           : hex_ ? (size_t)hex_top_ * (size_t)bpr_ : line_start(top_line_);
    for (int pass = 0; pass < 2; pass++) {
        for (size_t i = from; i + tl <= len_; i++)
            if (match_at(data_, len_, i, text, tl)) {
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
    snprintf(n, sizeof(n), "Not found: %.200s", text);
    set_note(n);
}

void Viewer::goto_line(long line)
{
    if (line < 1)
        line = 1;
    if (line > lines_.size())
        line = lines_.size();
    if (hex_)
        hex_ = false;
    top_line_ = line - 1;
    top_sub_ = 0;
}

} /* namespace ttrc */
