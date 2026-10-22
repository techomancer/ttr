#include "viewer.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ttrc {

namespace {

const int k_tab = 8;

int char_cols(const char* p, size_t n, int col)
{
    if (*p == '\t')
        return k_tab - col % k_tab;
    unsigned char c = (unsigned char)*p;
    if (c < 0x20 || c == 0x7f)
        return 1;
    char one[8];
    if (n > 4)
        n = 4;
    memcpy(one, p, n);
    one[n] = 0;
    return tu::Screen::text_width(one);
}

size_t char_len(const char* p, size_t avail)
{
    unsigned char c = (unsigned char)*p;
    size_t n = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 1;
    if (n > avail)
        n = avail;
    for (size_t i = 1; i < n; i++)
        if (((unsigned char)p[i] & 0xc0) != 0x80)
            return 1;
    return n ? n : 1;
}

void edit_key_bar(tu::Screen& s, const Theme& t)
{
    static const char* const labels[10] = {
        "Help", "Save", "Mark", "Replac", "Copy", "Move", "Search", "Delete", "", "Quit"
    };
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

Editor::Editor()
    : fs_(0), want_col_(0), top_(0), left_(0), rows_(20), width_(80), modified_(false),
      marking_(false), selected_(false), clip_(0), clip_len_(0)
{
    cur_.line = 0;
    cur_.col = 0;
    anchor_ = cur_;
    last_search_[0] = 0;
}

Editor::~Editor()
{
    clear();
    free(clip_);
}

void Editor::clear()
{
    for (int i = 0; i < lines_.size(); i++) {
        free(lines_[i]->s);
        delete lines_[i];
    }
    lines_.clear();
    for (int i = 0; i < undo_.size(); i++)
        free(undo_[i].text);
    undo_.clear();
}

Editor::Line* Editor::new_line(const char* s, size_t n)
{
    Line* l = new Line;
    l->cap = n + 16;
    l->s = (char*)malloc(l->cap);
    memcpy(l->s, s, n);
    l->len = n;
    return l;
}

void Editor::line_insert(Line* l, size_t at, const char* s, size_t n)
{
    if (l->len + n > l->cap) {
        size_t nc = (l->len + n) * 2 + 16;
        char* ns = (char*)realloc(l->s, nc);
        if (!ns)
            return;
        l->s = ns;
        l->cap = nc;
    }
    memmove(l->s + at + n, l->s + at, l->len - at);
    memcpy(l->s + at, s, n);
    l->len += n;
}

int Editor::load(tf::Fs* fs, const char* path)
{
    fs_ = fs;
    path_ = path;
    clear();
    int err = 0;
    tf::Stream* s = fs->open(path, tf::OPEN_READ, 0, &err);
    if (!s) {
        lines_.push_back(new_line("", 0));
        return err == ENOENT ? 0 : err;
    }
    size_t cap = 65536, len = 0;
    char* buf = (char*)malloc(cap);
    while (buf) {
        if (len == cap) {
            char* n = (char*)realloc(buf, cap * 2);
            if (!n)
                break;
            buf = n;
            cap *= 2;
        }
        long r = s->read(buf + len, cap - len);
        if (r < 0) {
            err = (int)-r;
            break;
        }
        if (r == 0)
            break;
        len += (size_t)r;
    }
    delete s;
    size_t start = 0;
    for (size_t i = 0; buf && i <= len; i++)
        if (i == len || buf[i] == '\n') {
            lines_.push_back(new_line(buf + start, i - start));
            start = i + 1;
        }
    free(buf);
    if (lines_.size() == 0)
        lines_.push_back(new_line("", 0));
    modified_ = false;
    return err;
}

int Editor::save(const char* path)
{
    const char* p = path ? path : path_.c_str();
    int err = 0;
    tf::Stream* s = fs_->open(p, tf::OPEN_WRITE | tf::OPEN_CREATE | tf::OPEN_TRUNCATE, 0644, &err);
    if (!s)
        return err;
    for (int i = 0; i < lines_.size() && !err; i++) {
        long w = s->write(lines_[i]->s, lines_[i]->len);
        if (w < 0)
            err = (int)-w;
        else if (i + 1 < lines_.size() && s->write("\n", 1) < 0)
            err = EIO;
    }
    int c = s->close();
    delete s;
    if (!err)
        err = c;
    if (!err) {
        modified_ = false;
        if (path)
            path_ = path;
    }
    return err;
}

/* ---- text operations, with undo ---- */

Editor::Pos Editor::end_of(Pos a, const char* s, size_t n) const
{
    Pos e = a;
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

void Editor::push_undo(bool insert, Pos at, const char* s, size_t n)
{
    /* Typing joins the previous insert when it continues it. */
    if (insert && undo_.size() > 0 && n == 1 && s[0] != '\n') {
        Undo& last = undo_[undo_.size() - 1];
        size_t ll = strlen(last.text);
        Pos end = end_of(last.at, last.text, ll);
        if (last.insert && end.line == at.line && end.col == at.col && !strchr(last.text, '\n') &&
            ll < 200) {
            char* t = (char*)realloc(last.text, ll + 2);
            if (t) {
                t[ll] = s[0];
                t[ll + 1] = 0;
                last.text = t;
                return;
            }
        }
    }
    Undo u;
    u.insert = insert;
    u.at = at;
    u.text = (char*)malloc(n + 1);
    if (!u.text)
        return;
    memcpy(u.text, s, n);
    u.text[n] = 0;
    undo_.push_back(u);
    if (undo_.size() > 2000) {
        free(undo_[0].text);
        undo_.remove_at(0);
    }
}

void Editor::insert_text(Pos at, const char* s, size_t n, bool record)
{
    if (record)
        push_undo(true, at, s, n);
    Line* l = lines_[(int)at.line];
    /* The rest of the line moves to after the last inserted line. */
    size_t tail_n = l->len - at.col;
    char* tail = (char*)malloc(tail_n + 1);
    memcpy(tail, l->s + at.col, tail_n);
    l->len = at.col;
    long line = at.line;
    size_t start = 0;
    for (size_t i = 0; i <= n; i++) {
        if (i == n || s[i] == '\n') {
            line_insert(lines_[(int)line], lines_[(int)line]->len, s + start, i - start);
            if (i < n) {
                Line* nl = new_line("", 0);
                lines_.push_back(nl);
                for (int k = lines_.size() - 1; k > line + 1; k--)
                    lines_[k] = lines_[k - 1];
                lines_[(int)line + 1] = nl;
                line++;
            }
            start = i + 1;
        }
    }
    Line* last = lines_[(int)line];
    size_t end_col = last->len;
    line_insert(last, last->len, tail, tail_n);
    free(tail);
    cur_.line = line;
    cur_.col = end_col;
    modified_ = true;
}

char* Editor::text_between(Pos a, Pos b, size_t* n) const
{
    size_t cap = 64, o = 0;
    char* out = (char*)malloc(cap);
    for (long l = a.line; l <= b.line && out; l++) {
        Line* ln = lines_[(int)l];
        size_t s = l == a.line ? a.col : 0;
        size_t e = l == b.line ? b.col : ln->len;
        size_t need = o + (e - s) + 2;
        if (need > cap) {
            cap = need * 2;
            char* no = (char*)realloc(out, cap);
            if (!no)
                break;
            out = no;
        }
        memcpy(out + o, ln->s + s, e - s);
        o += e - s;
        if (l < b.line)
            out[o++] = '\n';
    }
    if (out)
        out[o] = 0;
    *n = o;
    return out;
}

void Editor::erase(Pos a, Pos b, bool record)
{
    if (!less(a, b))
        return;
    if (record) {
        size_t n;
        char* t = text_between(a, b, &n);
        if (t) {
            push_undo(false, a, t, n);
            free(t);
        }
    }
    Line* la = lines_[(int)a.line];
    Line* lb = lines_[(int)b.line];
    /* The part of b's line after b joins a's line at a. */
    size_t rest = lb->len - b.col;
    char* keep = (char*)malloc(rest + 1);
    memcpy(keep, lb->s + b.col, rest);
    la->len = a.col;
    line_insert(la, la->len, keep, rest);
    free(keep);
    for (long l = a.line + 1; l <= b.line; l++) {
        free(lines_[(int)a.line + 1]->s);
        delete lines_[(int)a.line + 1];
        lines_.remove_at((int)a.line + 1);
    }
    cur_ = a;
    modified_ = true;
}

void Editor::undo()
{
    if (undo_.size() == 0) {
        set_note("Nothing to undo.");
        return;
    }
    Undo u = undo_[undo_.size() - 1];
    undo_.remove_at(undo_.size() - 1);
    size_t n = strlen(u.text);
    if (u.insert)
        erase(u.at, end_of(u.at, u.text, n), false);
    else
        insert_text(u.at, u.text, n, false);
    free(u.text);
    selected_ = marking_ = false;
}

/* ---- selection ---- */

bool Editor::has_selection() const
{
    return (selected_ || marking_) && (anchor_.line != cur_.line || anchor_.col != cur_.col);
}

void Editor::selection(Pos* a, Pos* b) const
{
    if (less(anchor_, cur_)) {
        *a = anchor_;
        *b = cur_;
    } else {
        *a = cur_;
        *b = anchor_;
    }
}

void Editor::copy_selection()
{
    if (!has_selection())
        return;
    Pos a, b;
    selection(&a, &b);
    free(clip_);
    clip_ = text_between(a, b, &clip_len_);
}

void Editor::delete_selection()
{
    if (!has_selection())
        return;
    Pos a, b;
    selection(&a, &b);
    erase(a, b, true);
    selected_ = marking_ = false;
}

void Editor::paste()
{
    if (clip_ && clip_len_)
        insert_text(cur_, clip_, clip_len_, true);
}

/* ---- cursor and view ---- */

int Editor::col_of(long line, size_t byte) const
{
    Line* l = lines_[(int)line];
    int col = 0;
    for (size_t i = 0; i < byte && i < l->len;) {
        size_t n = char_len(l->s + i, l->len - i);
        col += char_cols(l->s + i, n, col);
        i += n;
    }
    return col;
}

size_t Editor::byte_at(long line, int want) const
{
    Line* l = lines_[(int)line];
    int col = 0;
    size_t i = 0;
    while (i < l->len) {
        size_t n = char_len(l->s + i, l->len - i);
        int c = char_cols(l->s + i, n, col);
        if (col + c > want)
            break;
        col += c;
        i += n;
    }
    return i;
}

void Editor::move_cursor(Pos p, bool select)
{
    if (select && !marking_ && !selected_) {
        anchor_ = cur_;
        selected_ = true;
    } else if (!select && selected_ && !marking_) {
        selected_ = false;
    }
    if (p.line < 0)
        p.line = 0;
    if (p.line >= lines_.size())
        p.line = lines_.size() - 1;
    if (p.col > lines_[(int)p.line]->len)
        p.col = lines_[(int)p.line]->len;
    cur_ = p;
}

void Editor::clamp_view()
{
    if (cur_.line < top_)
        top_ = cur_.line;
    if (cur_.line >= top_ + rows_)
        top_ = cur_.line - rows_ + 1;
    int c = col_of(cur_.line, cur_.col);
    if (c < left_)
        left_ = c - c % 8;
    if (c >= left_ + width_ - 1)
        left_ = c - width_ + 9;
    if (left_ < 0)
        left_ = 0;
}

void Editor::draw(tu::Screen& s, const Theme& t)
{
    int w = s.cols(), h = s.rows();
    width_ = w;
    rows_ = h - 2;
    if (rows_ < 1)
        rows_ = 1;
    clamp_view();
    s.fill(0, 0, w, h, ' ', t.text);
    char st[800];
    const char* name = strrchr(path_.c_str(), '/');
    name = name ? name + 1 : path_.c_str();
    snprintf(st, sizeof(st), " %s%s   line %ld of %d, column %d%s", name, modified_ ? " *" : "",
             cur_.line + 1, lines_.size(), col_of(cur_.line, cur_.col) + 1,
             marking_ ? "   [marking: F3 ends]" : "");
    s.fill(0, 0, w, 1, ' ', t.text_status);
    s.text(0, 0, note_[0] ? note_ : st, t.text_status, w);
    Pos sa, sb;
    bool sel = has_selection();
    if (sel)
        selection(&sa, &sb);
    for (int r = 0; r < rows_; r++) {
        long ln = top_ + r;
        if (ln >= lines_.size())
            break;
        Line* l = lines_[(int)ln];
        int col = 0;
        for (size_t i = 0; i < l->len;) {
            size_t n = char_len(l->s + i, l->len - i);
            int c = char_cols(l->s + i, n, col);
            Pos here;
            here.line = ln;
            here.col = i;
            bool in = sel && !less(here, sa) && less(here, sb);
            const tu::Style& cs = in ? t.text_select : t.text;
            int x = col - left_;
            if (x >= w)
                break;
            if (x >= 0) {
                unsigned char ch = (unsigned char)l->s[i];
                if (ch == '\t') {
                    s.fill(x, r + 1, c, 1, ' ', cs);
                } else if (ch < 0x20 || ch == 0x7f) {
                    char cc[2] = { (char)(ch == 0x7f ? '?' : ch + '@'), 0 };
                    s.text(x, r + 1, cc, in ? t.text_select : t.text_ctrl);
                } else {
                    char one[8];
                    memcpy(one, l->s + i, n);
                    one[n] = 0;
                    s.text(x, r + 1, one, cs, w - x);
                }
            }
            col += c;
            i += n;
        }
        /* A selected line end. */
        if (sel && ln >= sa.line && ln < sb.line) {
            int x = col - left_;
            if (x >= 0 && x < w)
                s.put(x, r + 1, ' ', t.text_select);
        }
    }
    edit_key_bar(s, t);
    s.set_cursor(col_of(cur_.line, cur_.col) - left_, (int)(cur_.line - top_) + 1, true);
}

ToolRequest Editor::handle(const tu::Event& ev)
{
    note_[0] = 0;
    bool shift = (ev.mods & tu::MOD_SHIFT) != 0;
    Pos p = cur_;
    if (ev.key == tu::KEY_MOUSE) {
        if (ev.action == tu::MOUSE_WHEEL_UP) {
            top_ = top_ > 3 ? top_ - 3 : 0;
            if (cur_.line >= top_ + rows_)
                move_cursor(p, false);
        } else if (ev.action == tu::MOUSE_WHEEL_DOWN) {
            if (top_ + 3 < lines_.size())
                top_ += 3;
        } else if (ev.action == tu::MOUSE_PRESS && ev.button == 1 && ev.y >= 1 && ev.y <= rows_) {
            p.line = top_ + ev.y - 1;
            if (p.line >= lines_.size())
                p.line = lines_.size() - 1;
            p.col = byte_at(p.line, ev.x + left_);
            move_cursor(p, false);
        }
        return REQ_NONE;
    }
    switch (ev.key) {
    case tu::KEY_LEFT:
        if (p.col > 0) {
            size_t c = p.col - 1;
            while (c > 0 && ((unsigned char)lines_[(int)p.line]->s[c] & 0xc0) == 0x80)
                c--;
            p.col = c;
        } else if (p.line > 0) {
            p.line--;
            p.col = lines_[(int)p.line]->len;
        }
        move_cursor(p, shift);
        want_col_ = col_of(cur_.line, cur_.col);
        return REQ_NONE;
    case tu::KEY_RIGHT: {
        Line* l = lines_[(int)p.line];
        if (p.col < l->len)
            p.col += char_len(l->s + p.col, l->len - p.col);
        else if (p.line + 1 < lines_.size()) {
            p.line++;
            p.col = 0;
        }
        move_cursor(p, shift);
        want_col_ = col_of(cur_.line, cur_.col);
        return REQ_NONE;
    }
    case tu::KEY_UP: case tu::KEY_DOWN: case tu::KEY_PGUP: case tu::KEY_PGDN: {
        long d = ev.key == tu::KEY_UP ? -1 : ev.key == tu::KEY_DOWN ? 1
                 : ev.key == tu::KEY_PGUP ? -(rows_ - 1) : rows_ - 1;
        if (ev.mods & tu::MOD_CTRL) {
            p.line = d < 0 ? 0 : lines_.size() - 1;
        } else {
            p.line += d;
            if (ev.key == tu::KEY_PGUP || ev.key == tu::KEY_PGDN)
                top_ += d;
        }
        if (p.line < 0)
            p.line = 0;
        if (p.line >= lines_.size())
            p.line = lines_.size() - 1;
        if (top_ < 0)
            top_ = 0;
        p.col = byte_at(p.line, want_col_);
        move_cursor(p, shift);
        return REQ_NONE;
    }
    case tu::KEY_HOME:
        if (ev.mods & tu::MOD_CTRL)
            p.line = 0;
        p.col = 0;
        move_cursor(p, shift);
        want_col_ = 0;
        return REQ_NONE;
    case tu::KEY_END:
        if (ev.mods & tu::MOD_CTRL)
            p.line = lines_.size() - 1;
        p.col = lines_[(int)p.line]->len;
        move_cursor(p, shift);
        want_col_ = col_of(cur_.line, cur_.col);
        return REQ_NONE;
    case tu::KEY_ENTER: {
        /* Keep the indentation of this line. */
        delete_selection();
        Line* l = lines_[(int)cur_.line];
        char ins[256];
        ins[0] = '\n';
        size_t n = 1;
        for (size_t i = 0; i < l->len && n < sizeof(ins) && (l->s[i] == ' ' || l->s[i] == '\t'); i++)
            ins[n++] = l->s[i];
        insert_text(cur_, ins, n, true);
        want_col_ = col_of(cur_.line, cur_.col);
        return REQ_NONE;
    }
    case tu::KEY_TAB:
        delete_selection();
        insert_text(cur_, "\t", 1, true);
        return REQ_NONE;
    case tu::KEY_BACKSPACE:
        if (has_selection()) {
            delete_selection();
        } else if (cur_.col > 0 || cur_.line > 0) {
            Pos a = cur_;
            if (a.col > 0) {
                size_t c = a.col - 1;
                while (c > 0 && ((unsigned char)lines_[(int)a.line]->s[c] & 0xc0) == 0x80)
                    c--;
                a.col = c;
            } else {
                a.line--;
                a.col = lines_[(int)a.line]->len;
            }
            erase(a, cur_, true);
        }
        want_col_ = col_of(cur_.line, cur_.col);
        return REQ_NONE;
    case tu::KEY_DELETE:
        if (has_selection()) {
            delete_selection();
        } else {
            Pos b = cur_;
            Line* l = lines_[(int)b.line];
            if (b.col < l->len)
                b.col += char_len(l->s + b.col, l->len - b.col);
            else if (b.line + 1 < lines_.size()) {
                b.line++;
                b.col = 0;
            }
            erase(cur_, b, true);
        }
        return REQ_NONE;
    case tu::KEY_INSERT:
        if (ev.mods & tu::MOD_SHIFT)
            paste();
        else if (ev.mods & tu::MOD_CTRL)
            copy_selection();
        return REQ_NONE;
    case tu::KEY_ESC:
        if (marking_ || selected_) {
            marking_ = selected_ = false;
            return REQ_NONE;
        }
        return modified_ ? REQ_ASK_QUIT : REQ_CLOSE;
    default:
        break;
    }
    if (ev.key >= tu::KEY_F1 && ev.key <= tu::KEY_F1 + 11) {
        int n = ev.key - tu::KEY_F1 + 1;
        switch (n) {
        case 1:
            set_note("F2 save, F3 mark, F5 copy, F6 move, F8 delete block, F7 search, "
                     "F4 replace, Ctrl-U undo, Ctrl-Y delete line, F10 quit.");
            return REQ_NONE;
        case 2:
            return shift || path_.empty() ? REQ_SAVE_AS : REQ_SAVE;
        case 3:
            if (marking_) {
                marking_ = false;
                selected_ = true;
            } else {
                anchor_ = cur_;
                marking_ = true;
                selected_ = false;
            }
            return REQ_NONE;
        case 4: return REQ_REPLACE;
        case 5: case 6:
            if (has_selection()) {
                Pos a, b;
                selection(&a, &b);
                size_t len;
                char* t = text_between(a, b, &len);
                bool inside = !less(cur_, a) && less(cur_, b);
                if (t && !inside) {
                    Pos at = cur_;
                    if (n == 6) {
                        /* Move: delete first, adjusting the target if
                         * it comes after the block. */
                        Pos target = at;
                        erase(a, b, true);
                        if (less(b, target) || (b.line == target.line && b.col <= target.col)) {
                            if (target.line == b.line)
                                target.col = a.col + (target.col - b.col);
                            target.line -= b.line - a.line;
                        }
                        at = target;
                    }
                    insert_text(at, t, len, true);
                    selected_ = marking_ = false;
                }
                free(t);
            }
            return REQ_NONE;
        case 7:
            if (shift) {
                search(last_search_);
                return REQ_NONE;
            }
            return REQ_SEARCH;
        case 8:
            delete_selection();
            return REQ_NONE;
        case 10:
            return modified_ ? REQ_ASK_QUIT : REQ_CLOSE;
        case 12:
            return REQ_SAVE_AS;
        default:
            return REQ_NONE;
        }
    }
    if (ev.key == tu::KEY_CHAR && (ev.mods & tu::MOD_CTRL)) {
        switch (ev.ch) {
        case 'u': case 'z': undo(); return REQ_NONE;
        case 'y': {
            Pos a = cur_, b = cur_;
            a.col = 0;
            if (b.line + 1 < lines_.size()) {
                b.line++;
                b.col = 0;
            } else {
                b.col = lines_[(int)b.line]->len;
            }
            erase(a, b, true);
            return REQ_NONE;
        }
        case 's': return REQ_SAVE;
        case 'l': return REQ_GOTO;
        default: return REQ_NONE;
        }
    }
    if (ev.key == tu::KEY_CHAR && (ev.mods & tu::MOD_ALT)) {
        if (ev.ch == 'l' || ev.ch == 'L')
            return REQ_GOTO;
        return REQ_NONE;
    }
    if (ev.key == tu::KEY_CHAR) {
        delete_selection();
        char b[5];
        uint32_t cp = ev.ch;
        size_t n;
        if (cp < 0x80) { b[0] = (char)cp; n = 1; }
        else if (cp < 0x800) { b[0] = (char)(0xc0 | (cp >> 6)); b[1] = (char)(0x80 | (cp & 0x3f)); n = 2; }
        else if (cp < 0x10000) { b[0] = (char)(0xe0 | (cp >> 12)); b[1] = (char)(0x80 | ((cp >> 6) & 0x3f)); b[2] = (char)(0x80 | (cp & 0x3f)); n = 3; }
        else { b[0] = (char)(0xf0 | (cp >> 18)); b[1] = (char)(0x80 | ((cp >> 12) & 0x3f)); b[2] = (char)(0x80 | ((cp >> 6) & 0x3f)); b[3] = (char)(0x80 | (cp & 0x3f)); n = 4; }
        insert_text(cur_, b, n, true);
        want_col_ = col_of(cur_.line, cur_.col);
        return REQ_NONE;
    }
    return REQ_NONE;
}

void Editor::search(const char* text)
{
    size_t tl = strlen(text);
    if (!tl)
        return;
    snprintf(last_search_, sizeof(last_search_), "%s", text);
    long n = lines_.size();
    for (long k = 0; k <= n; k++) {
        long l = (cur_.line + k) % n;
        Line* ln = lines_[(int)l];
        size_t from = k == 0 ? cur_.col + 1 : 0;
        for (size_t i = from; i + tl <= ln->len; i++) {
            size_t j = 0;
            while (j < tl && tolower((unsigned char)ln->s[i + j]) == tolower((unsigned char)text[j]))
                j++;
            if (j == tl) {
                anchor_.line = l;
                anchor_.col = i + tl;
                cur_.line = l;
                cur_.col = i;
                selected_ = true;
                marking_ = false;
                /* The cursor at the start, the hit selected. */
                Pos t = anchor_;
                anchor_ = cur_;
                cur_ = t;
                if (k == n || l < anchor_.line)
                    set_note("Search wrapped around.");
                return;
            }
        }
    }
    char m[300];
    snprintf(m, sizeof(m), "Not found: %.200s", text);
    set_note(m);
}

void Editor::goto_line(long line)
{
    Pos p;
    p.line = line - 1;
    p.col = 0;
    move_cursor(p, false);
}

int Editor::replace_all(const char* from, const char* to)
{
    size_t fl = strlen(from), tl = strlen(to);
    if (!fl)
        return 0;
    int count = 0;
    for (long l = 0; l < lines_.size(); l++) {
        size_t i = 0;
        while (i + fl <= lines_[(int)l]->len) {
            if (memcmp(lines_[(int)l]->s + i, from, fl) == 0 && !memchr(from, '\n', fl)) {
                Pos a, b;
                a.line = b.line = l;
                a.col = i;
                b.col = i + fl;
                erase(a, b, true);
                insert_text(a, to, tl, true);
                i += tl;
                count++;
            } else {
                i++;
            }
        }
    }
    char m[100];
    sprintf(m, "%d replaced.", count);
    set_note(m);
    return count;
}

} /* namespace ttrc */
