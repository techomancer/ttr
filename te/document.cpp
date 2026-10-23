#include "document.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

namespace te {

namespace {

const long k_undo_max = 10000;

char* dup(const char* s, size_t n)
{
    char* d = (char*)malloc(n + 1);
    if (d) {
        memcpy(d, s, n);
        d[n] = 0;
    }
    return d;
}

bool word_char(char c)
{
    unsigned char u = (unsigned char)c;
    return u == '_' || u >= 0x80 || (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') ||
           (u >= 'A' && u <= 'Z');
}

bool same(char a, char b, bool match_case);

/* text at s[i] of a line of len bytes; whole: not inside a longer word. */
bool match_at(const char* s, size_t len, size_t i, const char* text, size_t tl, bool match_case,
              bool whole)
{
    if (i + tl > len)
        return false;
    for (size_t j = 0; j < tl; j++)
        if (!same(s[i + j], text[j], match_case))
            return false;
    if (whole && ((i > 0 && word_char(s[i - 1]) && word_char(text[0])) ||
                  (i + tl < len && word_char(s[i + tl]) && word_char(text[tl - 1]))))
        return false;
    return true;
}

bool same(char a, char b, bool match_case)
{
    return match_case ? a == b : tolower((unsigned char)a) == tolower((unsigned char)b);
}

} /* namespace */

Document::Document()
    : lines_(0), n_(0), cap_(0), crlf_(false), undo_(0), undo_n_(0), undo_cap_(0), redo_(0),
      redo_n_(0), redo_cap_(0), group_(0), group_depth_(0), merge_ok_(false), next_group_(1),
      saved_(0), version_(0), changed_from_(0)
{
    set_text("", 0);
}

Document::~Document()
{
    for (long i = 0; i < n_; i++)
        free(lines_[i].s);
    free(lines_);
    clear_ops(undo_, &undo_n_);
    clear_ops(redo_, &redo_n_);
    free(undo_);
    free(redo_);
}

void Document::clear_ops(Op* ops, long* count)
{
    for (long i = 0; i < *count; i++)
        free(ops[i].text);
    *count = 0;
}

void Document::reserve_lines(long n)
{
    if (n <= cap_)
        return;
    long nc = cap_ ? cap_ * 2 : 256;
    while (nc < n)
        nc *= 2;
    Line* nl = (Line*)realloc(lines_, (size_t)nc * sizeof(Line));
    if (!nl)
        return;
    lines_ = nl;
    cap_ = nc;
}

void Document::set_text(const char* data, size_t n)
{
    for (long i = 0; i < n_; i++)
        free(lines_[i].s);
    n_ = 0;
    /* CRLF only if every line end has the CR. */
    size_t lf = 0, crlf = 0;
    for (size_t i = 0; i < n; i++)
        if (data[i] == '\n') {
            lf++;
            if (i > 0 && data[i - 1] == '\r')
                crlf++;
        }
    crlf_ = lf > 0 && crlf == lf;
    size_t start = 0;
    for (size_t i = 0; i <= n; i++) {
        if (i == n || data[i] == '\n') {
            size_t len = i - start;
            if (crlf_ && i < n && len > 0)
                len--;
            reserve_lines(n_ + 1);
            Line& l = lines_[n_++];
            l.cap = len + 16;
            l.s = (char*)malloc(l.cap);
            memcpy(l.s, data + start, len);
            l.len = len;
            start = i + 1;
        }
    }
    clear_ops(undo_, &undo_n_);
    clear_ops(redo_, &redo_n_);
    saved_ = state();
    merge_ok_ = false;
    version_++;
    changed_from_ = 0;
}

char* Document::text(size_t* n) const
{
    size_t total = 0, eol = crlf_ ? 2 : 1;
    for (long i = 0; i < n_; i++)
        total += lines_[i].len + (i + 1 < n_ ? eol : 0);
    char* out = (char*)malloc(total + 1);
    if (!out) {
        *n = 0;
        return 0;
    }
    size_t o = 0;
    for (long i = 0; i < n_; i++) {
        memcpy(out + o, lines_[i].s, lines_[i].len);
        o += lines_[i].len;
        if (i + 1 < n_) {
            if (crlf_)
                out[o++] = '\r';
            out[o++] = '\n';
        }
    }
    out[o] = 0;
    *n = o;
    return out;
}

Pos Document::clamp(Pos p) const
{
    if (p.line < 0)
        return Pos(0, 0);
    if (p.line >= n_)
        return end();
    if (p.col > lines_[p.line].len)
        p.col = lines_[p.line].len;
    return p;
}

Pos Document::end() const
{
    return Pos(n_ - 1, lines_[n_ - 1].len);
}

void Document::line_insert(long i, size_t at, const char* s, size_t n)
{
    Line& l = lines_[i];
    if (l.len + n > l.cap) {
        size_t nc = (l.len + n) * 2 + 16;
        char* ns = (char*)realloc(l.s, nc);
        if (!ns)
            return;
        l.s = ns;
        l.cap = nc;
    }
    memmove(l.s + at + n, l.s + at, l.len - at);
    memcpy(l.s + at, s, n);
    l.len += n;
}

void Document::raw_insert(Pos at, const char* s, size_t n, Pos* end_pos)
{
    at = clamp(at);
    Line& first = lines_[at.line];
    size_t tail_n = first.len - at.col;
    char* tail = dup(first.s + at.col, tail_n);
    first.len = at.col;
    long line = at.line;
    size_t start = 0;
    for (size_t i = 0; i <= n; i++) {
        if (i == n || s[i] == '\n') {
            line_insert(line, lines_[line].len, s + start, i - start);
            if (i < n) {
                reserve_lines(n_ + 1);
                memmove(lines_ + line + 2, lines_ + line + 1, (size_t)(n_ - line - 1) * sizeof(Line));
                Line& nl = lines_[line + 1];
                nl.cap = 16;
                nl.s = (char*)malloc(nl.cap);
                nl.len = 0;
                n_++;
                line++;
            }
            start = i + 1;
        }
    }
    size_t end_col = lines_[line].len;
    line_insert(line, end_col, tail, tail_n);
    free(tail);
    if (end_pos)
        *end_pos = Pos(line, end_col);
    touch(at.line);
}

void Document::raw_erase(Pos a, Pos b)
{
    a = clamp(a);
    b = clamp(b);
    if (!(a < b))
        return;
    Line& la = lines_[a.line];
    Line& lb = lines_[b.line];
    size_t rest = lb.len - b.col;
    char* keep = dup(lb.s + b.col, rest);
    la.len = a.col;
    line_insert(a.line, la.len, keep, rest);
    free(keep);
    long gone = b.line - a.line;
    if (gone > 0) {
        for (long l = a.line + 1; l <= b.line; l++)
            free(lines_[l].s);
        memmove(lines_ + a.line + 1, lines_ + b.line + 1, (size_t)(n_ - b.line - 1) * sizeof(Line));
        n_ -= gone;
    }
    touch(a.line);
}

void Document::touch(long line)
{
    version_++;
    if (changed_from_ < 0 || line < changed_from_)
        changed_from_ = line;
}

long Document::take_changed()
{
    long c = changed_from_;
    changed_from_ = -1;
    return c;
}

char* Document::get(Pos a, Pos b, size_t* n) const
{
    a = clamp(a);
    b = clamp(b);
    size_t total = 0;
    for (long l = a.line; l <= b.line; l++) {
        size_t s = l == a.line ? a.col : 0;
        size_t e = l == b.line ? b.col : lines_[l].len;
        total += (e > s ? e - s : 0) + 1;
    }
    char* out = (char*)malloc(total + 1);
    size_t o = 0;
    for (long l = a.line; out && l <= b.line; l++) {
        size_t s = l == a.line ? a.col : 0;
        size_t e = l == b.line ? b.col : lines_[l].len;
        if (e > s) {
            memcpy(out + o, lines_[l].s + s, e - s);
            o += e - s;
        }
        if (l < b.line)
            out[o++] = '\n';
    }
    if (out)
        out[o] = 0;
    *n = o;
    return out;
}

void Document::push_op(Op** ops, long* count, long* cap, const Op& op)
{
    if (*count == *cap) {
        long nc = *cap ? *cap * 2 : 64;
        Op* n = (Op*)realloc(*ops, (size_t)nc * sizeof(Op));
        if (!n)
            return;
        *ops = n;
        *cap = nc;
    }
    (*ops)[(*count)++] = op;
}

void Document::record(bool insert, Pos at, const char* s, size_t n)
{
    clear_ops(redo_, &redo_n_);
    /* Typing: one more character right after the last insert. */
    if (insert && merge_ok_ && group_depth_ == 0 && n == 1 && s[0] != '\n' && undo_n_ > 0) {
        Op& last = undo_[undo_n_ - 1];
        if (last.insert && last.at.line == at.line && last.at.col + last.n == at.col &&
            !memchr(last.text, '\n', last.n) && last.n < 256) {
            char* t = (char*)realloc(last.text, last.n + 2);
            if (t) {
                t[last.n] = s[0];
                t[last.n + 1] = 0;
                last.text = t;
                last.n++;
                return;
            }
        }
    }
    Op op;
    op.insert = insert;
    op.at = at;
    op.text = dup(s, n);
    op.n = n;
    op.group = group_depth_ > 0 ? group_ : next_group_++;
    push_op(&undo_, &undo_n_, &undo_cap_, op);
    if (undo_n_ > k_undo_max) {
        /* Drop the oldest group. */
        unsigned long g = undo_[0].group;
        long k = 0;
        while (k < undo_n_ && undo_[k].group == g) {
            free(undo_[k].text);
            k++;
        }
        memmove(undo_, undo_ + k, (size_t)(undo_n_ - k) * sizeof(Op));
        undo_n_ -= k;
    }
    merge_ok_ = insert && n == 1 && s[0] != '\n';
}

Pos Document::insert(Pos at, const char* s, size_t n)
{
    at = clamp(at);
    Pos end_pos = at;
    if (n == 0)
        return at;
    record(true, at, s, n);
    raw_insert(at, s, n, &end_pos);
    return end_pos;
}

void Document::erase(Pos a, Pos b)
{
    a = clamp(a);
    b = clamp(b);
    if (!(a < b))
        return;
    size_t n;
    char* t = get(a, b, &n);
    if (t) {
        record(false, a, t, n);
        free(t);
    }
    merge_ok_ = false;
    raw_erase(a, b);
}

void Document::begin_group()
{
    if (group_depth_++ == 0)
        group_ = next_group_++;
    merge_ok_ = false;
}

void Document::end_group()
{
    if (group_depth_ > 0)
        group_depth_--;
    merge_ok_ = false;
}

static Pos end_of(Pos at, const char* s, size_t n)
{
    Pos e = at;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\n') {
            e.line++;
            e.col = 0;
        } else {
            e.col++;
        }
    }
    return e;
}

bool Document::undo(Pos* cursor)
{
    if (undo_n_ == 0)
        return false;
    unsigned long g = undo_[undo_n_ - 1].group;
    merge_ok_ = false;
    while (undo_n_ > 0 && undo_[undo_n_ - 1].group == g) {
        Op op = undo_[--undo_n_];
        if (op.insert)
            raw_erase(op.at, end_of(op.at, op.text, op.n));
        else
            raw_insert(op.at, op.text, op.n, 0);
        if (cursor)
            *cursor = op.insert ? op.at : end_of(op.at, op.text, op.n);
        push_op(&redo_, &redo_n_, &redo_cap_, op);
    }
    return true;
}

bool Document::redo(Pos* cursor)
{
    if (redo_n_ == 0)
        return false;
    unsigned long g = redo_[redo_n_ - 1].group;
    merge_ok_ = false;
    while (redo_n_ > 0 && redo_[redo_n_ - 1].group == g) {
        Op op = redo_[--redo_n_];
        Pos e = op.at;
        if (op.insert)
            raw_insert(op.at, op.text, op.n, &e);
        else
            raw_erase(op.at, end_of(op.at, op.text, op.n));
        if (cursor)
            *cursor = op.insert ? e : op.at;
        push_op(&undo_, &undo_n_, &undo_cap_, op);
    }
    return true;
}

bool Document::find(const char* text, Pos from, bool forward, bool match_case, Pos* a, Pos* b,
                    bool* wrapped, bool whole_word) const
{
    size_t tl = strlen(text);
    if (wrapped)
        *wrapped = false;
    if (!tl)
        return false;
    from = clamp(from);
    for (long k = 0; k <= n_; k++) {
        long l = forward ? (from.line + k) % n_ : ((from.line - k) % n_ + n_) % n_;
        const Line& ln = lines_[l];
        if (forward) {
            size_t start = k == 0 ? from.col : 0;
            for (size_t i = start; i + tl <= ln.len; i++) {
                if (match_at(ln.s, ln.len, i, text, tl, match_case, whole_word)) {
                    *a = Pos(l, i);
                    *b = Pos(l, i + tl);
                    if (wrapped)
                        *wrapped = l < from.line || (l == from.line && i < from.col);
                    return true;
                }
            }
        } else {
            long last = (long)ln.len - (long)tl;
            if (k == 0)
                last = (long)from.col - 1 < last ? (long)from.col - 1 : last;
            for (long i = last; i >= 0; i--) {
                if (match_at(ln.s, ln.len, (size_t)i, text, tl, match_case, whole_word)) {
                    *a = Pos(l, (size_t)i);
                    *b = Pos(l, (size_t)i + tl);
                    if (wrapped)
                        *wrapped = l > from.line;
                    return true;
                }
            }
        }
    }
    return false;
}

int Document::replace_all(const char* from, const char* to, bool match_case, bool whole_word)
{
    size_t fl = strlen(from), tl = strlen(to);
    if (!fl || memchr(from, '\n', fl))
        return 0;
    int count = 0;
    begin_group();
    for (long l = 0; l < n_; l++) {
        size_t i = 0;
        while (i + fl <= lines_[l].len) {
            if (match_at(lines_[l].s, lines_[l].len, i, from, fl, match_case, whole_word)) {
                erase(Pos(l, i), Pos(l, i + fl));
                insert(Pos(l, i), to, tl);
                i += tl;
                count++;
            } else {
                i++;
            }
        }
    }
    end_group();
    return count;
}

} /* namespace te */
