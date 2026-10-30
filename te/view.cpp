#include "view.h"
#include "syntax.h"

#include "../tl/unicode.h"

#include <stdlib.h>
#include <string.h>

namespace te {

namespace {

/* One character: bytes and code point; bad UTF-8 is one byte (Latin-1). */
size_t decode(const char* s, size_t avail, uint32_t* cp)
{
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80 || avail < 2) {
        *cp = c;
        return 1;
    }
    size_t n = (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 0;
    if (n == 0 || n > avail) {
        *cp = c;
        return 1;
    }
    uint32_t v = n == 2 ? c & 0x1f : n == 3 ? c & 0x0f : c & 0x07;
    for (size_t i = 1; i < n; i++) {
        unsigned char d = (unsigned char)s[i];
        if ((d & 0xc0) != 0x80) {
            *cp = c;
            return 1;
        }
        v = (v << 6) | (d & 0x3f);
    }
    *cp = v;
    return n;
}

bool is_control(uint32_t cp)
{
    return cp < 0x20 || cp == 0x7f;
}

/* Columns a character takes at column col. */
int columns(uint32_t cp, int col, int tab)
{
    if (cp == '\t')
        return tab - col % tab;
    if (is_control(cp))
        return 2;
    if (cp < 0x7f)
        return 1;
    int w = tl::unicode_width(cp, false);
    return w < 0 ? 1 : w;
}

bool is_word(uint32_t cp)
{
    return cp >= 0x80 || cp == '_' || (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') ||
           (cp >= 'A' && cp <= 'Z');
}

} /* namespace */

View::View(Document& doc)
    : tab_width(8), indent_width(0), insert_spaces(false), auto_indent(true),
      show_whitespace(false), doc_(doc), want_col_(0), top_(0),
      left_(0), cols_(80), rows_(24), found_(false), block_(false), block_version_(0),
      syntax_(0), syn_buf_(0), syn_cap_(0)
{
}

View::~View()
{
    free(syn_buf_);
}

void View::set_size(int cols, int rows)
{
    cols_ = cols > 1 ? cols : 1;
    rows_ = rows > 1 ? rows : 1;
}

void View::selection(Pos* a, Pos* b) const
{
    if (anchor_ < cur_) {
        *a = anchor_;
        *b = cur_;
    } else {
        *a = cur_;
        *b = anchor_;
    }
}

Pos View::next_char(Pos p) const
{
    p = doc_.clamp(p);
    size_t len = doc_.length(p.line);
    if (p.col < len) {
        uint32_t cp;
        p.col += decode(doc_.line(p.line) + p.col, len - p.col, &cp);
    } else if (p.line + 1 < doc_.lines()) {
        p.line++;
        p.col = 0;
    }
    return p;
}

Pos View::prev_char(Pos p) const
{
    p = doc_.clamp(p);
    if (p.col > 0) {
        const char* s = doc_.line(p.line);
        size_t c = p.col - 1;
        while (c > 0 && ((unsigned char)s[c] & 0xc0) == 0x80 && p.col - c < 4)
            c--;
        uint32_t cp;
        /* Only a whole character counts; otherwise one byte back. */
        if (decode(s + c, p.col - c, &cp) != p.col - c)
            c = p.col - 1;
        p.col = c;
    } else if (p.line > 0) {
        p.line--;
        p.col = doc_.length(p.line);
    }
    return p;
}

void View::move_to(Pos p, bool extend, bool keep_column)
{
    cur_ = doc_.clamp(p);
    if (!extend)
        anchor_ = cur_;
    if (!keep_column)
        want_col_ = column_of(cur_);
    doc_.break_merge();
}

void View::set_cursor(Pos p, bool extend)
{
    move_to(p, extend, false);
}

void View::select_all()
{
    anchor_ = Pos(0, 0);
    cur_ = doc_.end();
    want_col_ = column_of(cur_);
}

void View::select_word(Pos p)
{
    p = doc_.clamp(p);
    const char* s = doc_.line(p.line);
    size_t len = doc_.length(p.line);
    uint32_t cp = ' ';
    if (p.col < len)
        decode(s + p.col, len - p.col, &cp);
    if (!is_word(cp)) {
        move_to(p, false, false);
        move_to(next_char(p), true, false);
        if (cur_.line != p.line)
            move_to(p, false, false);
        return;
    }
    Pos a = p, b = p;
    while (a.col > 0) {
        Pos q = prev_char(a);
        decode(s + q.col, len - q.col, &cp);
        if (!is_word(cp))
            break;
        a = q;
    }
    while (b.col < len) {
        decode(s + b.col, len - b.col, &cp);
        if (!is_word(cp))
            break;
        b = next_char(b);
    }
    move_to(a, false, false);
    move_to(b, true, false);
}

void View::move(Move m, bool extend)
{
    Pos p = cur_;
    Pos a, b;
    selection(&a, &b);
    switch (m) {
    case MOVE_LEFT:
        if (has_selection() && !extend) {
            move_to(a, false, false);
            return;
        }
        move_to(prev_char(p), extend, false);
        return;
    case MOVE_RIGHT:
        if (has_selection() && !extend) {
            move_to(b, false, false);
            return;
        }
        move_to(next_char(p), extend, false);
        return;
    case MOVE_UP:
    case MOVE_DOWN:
    case MOVE_PAGE_UP:
    case MOVE_PAGE_DOWN: {
        long d = m == MOVE_UP ? -1 : m == MOVE_DOWN ? 1 : m == MOVE_PAGE_UP ? -(rows_ - 1) : rows_ - 1;
        if (m == MOVE_PAGE_UP || m == MOVE_PAGE_DOWN)
            scroll(d);
        long line = p.line + d;
        if (line < 0)
            line = 0;
        if (line >= doc_.lines())
            line = doc_.lines() - 1;
        move_to(pos_at(line, want_col_), extend, true);
        return;
    }
    case MOVE_WORD_LEFT: {
        Pos q = prev_char(p);
        /* Skip what is not a word, then the word. */
        while (q != Pos(0, 0)) {
            uint32_t cp = ' ';
            if (q.col < doc_.length(q.line))
                decode(doc_.line(q.line) + q.col, doc_.length(q.line) - q.col, &cp);
            if (is_word(cp))
                break;
            q = prev_char(q);
        }
        while (q.col > 0) {
            Pos r = prev_char(q);
            uint32_t cp;
            decode(doc_.line(r.line) + r.col, doc_.length(r.line) - r.col, &cp);
            if (!is_word(cp))
                break;
            q = r;
        }
        move_to(q, extend, false);
        return;
    }
    case MOVE_WORD_RIGHT: {
        Pos q = p;
        size_t len = doc_.length(q.line);
        uint32_t cp = ' ';
        if (q.col >= len) {
            move_to(next_char(q), extend, false);
            return;
        }
        decode(doc_.line(q.line) + q.col, len - q.col, &cp);
        bool word = is_word(cp);
        while (q.col < len) {
            decode(doc_.line(q.line) + q.col, len - q.col, &cp);
            if (is_word(cp) != word)
                break;
            q = next_char(q);
        }
        while (q.col < len) {
            decode(doc_.line(q.line) + q.col, len - q.col, &cp);
            if (cp != ' ' && cp != '\t')
                break;
            q = next_char(q);
        }
        move_to(q, extend, false);
        return;
    }
    case MOVE_HOME: {
        const char* s = doc_.line(p.line);
        size_t len = doc_.length(p.line), first = 0;
        while (first < len && (s[first] == ' ' || s[first] == '\t'))
            first++;
        move_to(Pos(p.line, p.col == first ? 0 : first), extend, false);
        return;
    }
    case MOVE_END:
        move_to(Pos(p.line, doc_.length(p.line)), extend, false);
        return;
    case MOVE_DOC_START:
        move_to(Pos(0, 0), extend, false);
        return;
    case MOVE_DOC_END:
        move_to(doc_.end(), extend, false);
        return;
    }
}

void View::erase_selection()
{
    if (!has_selection())
        return;
    Pos a, b;
    selection(&a, &b);
    doc_.erase(a, b);
    cur_ = anchor_ = a;
}

void View::type(const char* s, size_t n)
{
    bool group = has_selection();
    if (group)
        doc_.begin_group();
    erase_selection();
    cur_ = anchor_ = doc_.insert(cur_, s, n);
    if (group)
        doc_.end_group();
    want_col_ = column_of(cur_);
    found_ = false;
}

void View::newline()
{
    char buf[512];
    size_t n = 0;
    buf[n++] = '\n';
    if (auto_indent) {
        const char* s = doc_.line(cur_.line);
        Pos a, b;
        selection(&a, &b);
        size_t lim = has_selection() ? a.col : cur_.col;
        for (size_t i = 0; i < lim && n < sizeof(buf) && (s[i] == ' ' || s[i] == '\t'); i++)
            buf[n++] = s[i];
    }
    doc_.begin_group();
    type(buf, n);
    doc_.end_group();
}

void View::backspace()
{
    if (has_selection()) {
        erase_selection();
    } else {
        Pos p = prev_char(cur_);
        if (p != cur_) {
            doc_.erase(p, cur_);
            cur_ = anchor_ = p;
        }
    }
    want_col_ = column_of(cur_);
}

void View::del()
{
    if (has_selection()) {
        erase_selection();
    } else {
        Pos p = next_char(cur_);
        if (p != cur_)
            doc_.erase(cur_, p);
    }
    want_col_ = column_of(cur_);
}

/* Lines first..last one indent in (or out), as one undo step; empty
 * lines stay empty. */
void View::indent_lines(long first, long last, bool out)
{
    doc_.begin_group();
    for (long l = first; l <= last && l < doc_.lines(); l++) {
        if (!out) {
            if (doc_.length(l) == 0)
                continue;
            if (insert_spaces) {
                char sp[64];
                int n = indent() < 64 ? indent() : 63;
                memset(sp, ' ', (size_t)n);
                doc_.insert(Pos(l, 0), sp, (size_t)n);
            } else {
                doc_.insert(Pos(l, 0), "\t", 1);
            }
        } else {
            const char* s = doc_.line(l);
            size_t n = 0;
            if (doc_.length(l) > 0 && s[0] == '\t')
                n = 1;
            else
                while (n < doc_.length(l) && (int)n < indent() && s[n] == ' ')
                    n++;
            if (n)
                doc_.erase(Pos(l, 0), Pos(l, n));
        }
    }
    doc_.end_group();
}

void View::tab(bool shift)
{
    Pos a, b;
    selection(&a, &b);
    if (!has_selection() && has_block()) {
        /* mc: Tab indents the marked block's lines; it stays marked. */
        Pos ba = block_a_, bb = block_b_;
        long last = bb.col == 0 && bb.line > ba.line ? bb.line - 1 : bb.line;
        indent_lines(ba.line, last, shift);
        block_a_ = Pos(ba.line, 0);
        block_b_ = last + 1 < doc_.lines() ? Pos(last + 1, 0) : Pos(last, doc_.length(last));
        block_ = true;
        block_version_ = doc_.version();
        cur_ = anchor_ = doc_.clamp(cur_);
        want_col_ = column_of(cur_);
        return;
    }
    if (has_selection() && a.line != b.line) {
        /* Indent or outdent the selected lines. */
        long last = b.col == 0 ? b.line - 1 : b.line;
        indent_lines(a.line, last, shift);
        anchor_ = Pos(a.line, 0);
        cur_ = last + 1 < doc_.lines() ? Pos(last + 1, 0) : Pos(last, doc_.length(last));
        return;
    }
    if (shift) {
        /* Outdent the current line. */
        Pos keep = cur_;
        set_anchor(Pos(cur_.line, 0));
        cur_ = Pos(cur_.line, doc_.length(cur_.line));
        if (cur_.line + 1 < doc_.lines())
            cur_ = Pos(cur_.line + 1, 0);
        else
            cur_ = Pos(cur_.line, doc_.length(cur_.line));
        anchor_ = Pos(keep.line, 0);
        if (anchor_.line == cur_.line) {
            const char* s = doc_.line(keep.line);
            size_t n = 0;
            if (doc_.length(keep.line) > 0 && s[0] == '\t')
                n = 1;
            else
                while (n < doc_.length(keep.line) && (int)n < indent() && s[n] == ' ')
                    n++;
            if (n)
                doc_.erase(Pos(keep.line, 0), Pos(keep.line, n));
            cur_ = anchor_ = doc_.clamp(Pos(keep.line, keep.col > n ? keep.col - n : 0));
        } else {
            tab(true);
            cur_ = anchor_ = doc_.clamp(Pos(keep.line, keep.col));
        }
        return;
    }
    if (insert_spaces) {
        int col = column_of(cur_);
        int n = indent() - col % indent();
        char sp[64];
        if (n > 63)
            n = 63;
        memset(sp, ' ', (size_t)n);
        type(sp, (size_t)n);
    } else {
        type("\t", 1);
    }
}

void View::delete_line()
{
    long l = cur_.line;
    if (l + 1 < doc_.lines())
        doc_.erase(Pos(l, 0), Pos(l + 1, 0));
    else if (l > 0)
        doc_.erase(Pos(l - 1, doc_.length(l - 1)), Pos(l, doc_.length(l)));
    else
        doc_.erase(Pos(0, 0), Pos(0, doc_.length(0)));
    cur_ = anchor_ = doc_.clamp(Pos(cur_.line, 0));
    move_to(pos_at(cur_.line, want_col_), false, true);
}

char* View::copy(size_t* n) const
{
    Pos a, b;
    selection(&a, &b);
    return doc_.get(a, b, n);
}

char* View::cut(size_t* n)
{
    char* t = copy(n);
    erase_selection();
    want_col_ = column_of(cur_);
    return t;
}

void View::paste(const char* s, size_t n)
{
    doc_.begin_group();
    type(s, n);
    doc_.end_group();
}

void View::mark_block()
{
    if (!has_selection())
        return;
    selection(&block_a_, &block_b_);
    block_ = true;
    block_version_ = doc_.version();
    anchor_ = cur_;
}

bool View::has_block() const
{
    return block_ && block_version_ == doc_.version() && block_a_ != block_b_;
}

void View::block(Pos* a, Pos* b) const
{
    *a = block_a_;
    *b = block_b_;
}

bool View::copy_block()
{
    if (!has_block())
        return false;
    Pos a = block_a_, b = block_b_;
    if (a < cur_ && cur_ < b)
        return false;
    size_t n;
    char* t = doc_.get(a, b, &n);
    if (!t)
        return false;
    Pos at = cur_;
    doc_.begin_group();
    Pos end = doc_.insert(at, t, n);
    doc_.end_group();
    free(t);
    block_a_ = at;
    block_b_ = end;
    block_version_ = doc_.version();
    cur_ = anchor_ = at;
    want_col_ = column_of(cur_);
    return true;
}

bool View::move_block()
{
    if (!has_block())
        return false;
    Pos a = block_a_, b = block_b_;
    Pos target = cur_;
    if (a < target && target < b)
        return false;
    if (target == a || target == b)
        return true;            /* already there */
    size_t n;
    char* t = doc_.get(a, b, &n);
    if (!t)
        return false;
    doc_.begin_group();
    doc_.erase(a, b);
    /* A target after the block moves with the deletion. */
    if (b <= target) {
        if (target.line == b.line)
            target.col = a.col + (target.col - b.col);
        target.line -= b.line - a.line;
    }
    Pos end = doc_.insert(target, t, n);
    doc_.end_group();
    free(t);
    block_a_ = target;
    block_b_ = end;
    block_version_ = doc_.version();
    cur_ = anchor_ = target;
    want_col_ = column_of(cur_);
    return true;
}

bool View::erase_block()
{
    if (!has_block())
        return false;
    doc_.begin_group();
    doc_.erase(block_a_, block_b_);
    doc_.end_group();
    cur_ = anchor_ = block_a_;
    block_ = false;
    want_col_ = column_of(cur_);
    return true;
}

bool View::undo()
{
    Pos p;
    if (!doc_.undo(&p))
        return false;
    cur_ = anchor_ = doc_.clamp(p);
    want_col_ = column_of(cur_);
    found_ = false;
    return true;
}

bool View::redo()
{
    Pos p;
    if (!doc_.redo(&p))
        return false;
    cur_ = anchor_ = doc_.clamp(p);
    want_col_ = column_of(cur_);
    found_ = false;
    return true;
}

bool View::find(const char* text, bool forward, bool match_case, bool* wrapped, bool whole_word)
{
    Pos a, b;
    selection(&a, &b);
    Pos from = forward ? (has_selection() ? b : cur_) : a;
    Pos ha, hb;
    if (!doc_.find(text, from, forward, match_case, &ha, &hb, wrapped, whole_word)) {
        found_ = false;
        return false;
    }
    anchor_ = ha;
    cur_ = hb;
    found_a_ = ha;
    found_b_ = hb;
    found_ = true;
    want_col_ = column_of(cur_);
    doc_.break_merge();
    return true;
}

int View::replace_all(const char* from, const char* to, bool match_case, bool whole_word)
{
    int n = doc_.replace_all(from, to, match_case, whole_word);
    cur_ = anchor_ = doc_.clamp(cur_);
    found_ = false;
    return n;
}

void View::goto_line(long line)
{
    move_to(Pos(line - 1, 0), false, false);
    /* Show it in the upper third. */
    top_ = cur_.line - rows_ / 3;
    if (top_ < 0)
        top_ = 0;
}

void View::scroll(long lines)
{
    top_ += lines;
    if (top_ > doc_.lines() - 1)
        top_ = doc_.lines() - 1;
    if (top_ < 0)
        top_ = 0;
}

void View::follow()
{
    cur_ = doc_.clamp(cur_);
    anchor_ = doc_.clamp(anchor_);
    if (cur_.line < top_)
        top_ = cur_.line;
    if (cur_.line >= top_ + rows_)
        top_ = cur_.line - rows_ + 1;
    int c = column_of(cur_);
    if (c < left_)
        left_ = c > 8 ? c - 8 : 0;
    if (c >= left_ + cols_)
        left_ = c - cols_ + 8;
    if (left_ < 0)
        left_ = 0;
}

void View::refit()
{
    int c = column_of(doc_.clamp(cur_));
    int least = c >= cols_ ? c - cols_ + 8 : 0;
    if (left_ > least)
        left_ = least < 0 ? 0 : least;
}

int View::column_of(Pos p) const
{
    p = doc_.clamp(p);
    const char* s = doc_.line(p.line);
    int col = 0;
    for (size_t i = 0; i < p.col;) {
        uint32_t cp;
        size_t n = decode(s + i, p.col - i, &cp);
        col += columns(cp, col, tab_width);
        i += n;
    }
    return col;
}

Pos View::pos_at(long line, int want) const
{
    if (line < 0)
        line = 0;
    if (line >= doc_.lines())
        line = doc_.lines() - 1;
    const char* s = doc_.line(line);
    size_t len = doc_.length(line);
    int col = 0;
    size_t i = 0;
    while (i < len) {
        uint32_t cp;
        size_t n = decode(s + i, len - i, &cp);
        int w = columns(cp, col, tab_width);
        if (col + w > want)
            break;
        col += w;
        i += n;
    }
    return Pos(line, i);
}

bool View::row(int y, Cell* out) const
{
    for (int x = 0; x < cols_; x++) {
        out[x].cp = ' ';
        out[x].width = 1;
        out[x].kind = CELL_TEXT;
        out[x].syn = 0;
        out[x].ws = 0;
    }
    long line = top_ + y;
    if (line < 0 || line >= doc_.lines())
        return false;
    Pos a, b;
    selection(&a, &b);
    bool sel = has_selection();
    bool blk = has_block();
    const char* s = doc_.line(line);
    size_t len = doc_.length(line);
    const uint8_t* syn = 0;
    if (syntax_ && len > 0) {
        if (len > syn_cap_) {
            uint8_t* p = (uint8_t*)realloc(syn_buf_, len);
            if (p) {
                syn_buf_ = p;
                syn_cap_ = len;
            }
        }
        if (len <= syn_cap_) {
            syntax_->classify(line, syn_buf_);
            syn = syn_buf_;
        }
    }
    int col = 0;
    for (size_t i = 0; i < len;) {
        uint32_t cp;
        size_t n = decode(s + i, len - i, &cp);
        int w = columns(cp, col, tab_width);
        uint8_t sc = syn ? syn[i] : 0;
        Pos here(line, i);
        uint8_t kind = CELL_TEXT;
        if (sel && a <= here && here < b)
            kind = CELL_SELECTED;
        else if (found_ && found_a_ <= here && here < found_b_)
            kind = CELL_FOUND;
        else if (blk && block_a_ <= here && here < block_b_)
            kind = CELL_BLOCK;
        int x = col - left_;
        if (x >= cols_)
            break;
        if (cp == '\t') {
            for (int k = 0; k < w; k++)
                if (x + k >= 0 && x + k < cols_) {
                    out[x + k].kind = kind;
                    out[x + k].syn = sc;
                }
            if (show_whitespace && x >= 0 && x < cols_) {
                out[x].cp = 0xbb;   /* >> */
                out[x].ws = 1;
            }
        } else if (is_control(cp)) {
            uint32_t shown[2] = { '^', cp == 0x7f ? (uint32_t)'?' : cp + '@' };
            for (int k = 0; k < 2; k++)
                if (x + k >= 0 && x + k < cols_) {
                    out[x + k].cp = shown[k];
                    out[x + k].kind = kind == CELL_TEXT ? (uint8_t)CELL_CONTROL : kind;
                }
        } else if (w == 2) {
            if (x >= 0 && x + 1 < cols_) {
                out[x].cp = cp;
                out[x].width = 2;
                out[x].kind = kind;
                out[x].syn = sc;
                out[x + 1].cp = ' ';
                out[x + 1].width = 0;
                out[x + 1].kind = kind;
                out[x + 1].syn = sc;
            } else if (x >= 0 && x < cols_) {
                out[x].kind = kind;     /* half a wide character: a blank */
            }
        } else if (w == 1 && x >= 0) {
            out[x].cp = cp;
            out[x].kind = kind;
            out[x].syn = sc;
            if (cp == ' ' && show_whitespace) {
                out[x].cp = 0xb7;   /* middle dot */
                out[x].ws = 1;
            }
        }
        col += w;
        i += n;
    }
    /* A selection that goes on past this line's end. */
    if (sel && a.line <= line && line < b.line) {
        int x = col - left_;
        if (x >= 0 && x < cols_)
            out[x].kind = CELL_SELECTED;
    } else if (blk && block_a_.line <= line && line < block_b_.line) {
        int x = col - left_;
        if (x >= 0 && x < cols_)
            out[x].kind = CELL_BLOCK;
    }
    return true;
}

bool View::screen_cursor(int* x, int* y) const
{
    long dy = cur_.line - top_;
    int dx = column_of(cur_) - left_;
    if (dy < 0 || dy >= rows_ || dx < 0 || dx >= cols_)
        return false;
    *x = dx;
    *y = (int)dy;
    return true;
}

} /* namespace te */
