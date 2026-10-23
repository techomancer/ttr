#include "viewer.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ttrc {

namespace {

size_t utf8(uint32_t cp, char* b)
{
    if (cp < 0x80) {
        b[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        b[0] = (char)(0xc0 | (cp >> 6));
        b[1] = (char)(0x80 | (cp & 0x3f));
        return 2;
    }
    if (cp < 0x10000) {
        b[0] = (char)(0xe0 | (cp >> 12));
        b[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
        b[2] = (char)(0x80 | (cp & 0x3f));
        return 3;
    }
    b[0] = (char)(0xf0 | (cp >> 18));
    b[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
    b[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
    b[3] = (char)(0x80 | (cp & 0x3f));
    return 4;
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

Editor::Editor() : fs_(0), view_(doc_), marking_(false), clip_(0), clip_len_(0)
{
    last_search_[0] = 0;
}

Editor::~Editor()
{
    free(clip_);
}

int Editor::load(tf::Fs* fs, const char* path)
{
    fs_ = fs;
    path_ = path;
    int err = 0;
    tf::Stream* s = fs->open(path, tf::OPEN_READ, 0, &err);
    if (!s) {
        doc_.set_text("", 0);
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
    doc_.set_text(buf ? buf : "", buf ? len : 0);
    free(buf);
    view_.set_cursor(te::Pos(0, 0), false);
    return err;
}

int Editor::save(const char* path)
{
    const char* p = path ? path : path_.c_str();
    int err = 0;
    tf::Stream* s = fs_->open(p, tf::OPEN_WRITE | tf::OPEN_CREATE | tf::OPEN_TRUNCATE, 0644, &err);
    if (!s)
        return err;
    size_t n;
    char* t = doc_.text(&n);
    if (!t)
        err = ENOMEM;
    for (size_t o = 0; !err && o < n;) {
        long w = s->write(t + o, n - o);
        if (w <= 0)
            err = w < 0 ? (int)-w : EIO;
        else
            o += (size_t)w;
    }
    free(t);
    int c = s->close();
    delete s;
    if (!err)
        err = c;
    if (!err) {
        doc_.mark_saved();
        if (path)
            path_ = path;
    }
    return err;
}

void Editor::copy_selection()
{
    if (!view_.has_selection())
        return;
    free(clip_);
    clip_ = view_.copy(&clip_len_);
}

void Editor::paste()
{
    if (clip_ && clip_len_)
        view_.paste(clip_, clip_len_);
}

void Editor::draw(tu::Screen& s, const Theme& t)
{
    int w = s.cols(), h = s.rows();
    view_.set_size(w, h > 3 ? h - 2 : 1);
    view_.follow();
    s.fill(0, 0, w, h, ' ', t.text);
    char st[800];
    const char* name = strrchr(path_.c_str(), '/');
    name = name ? name + 1 : path_.c_str();
    te::Pos cur = view_.cursor();
    snprintf(st, sizeof(st), " %s%s   line %ld of %ld, column %d%s", name,
             doc_.modified() ? " *" : "", cur.line + 1, doc_.lines(), view_.column_of(cur) + 1,
             marking_ ? "   [marking: F3 ends]" : "");
    s.fill(0, 0, w, 1, ' ', t.text_status);
    s.text(0, 0, note_[0] ? note_ : st, t.text_status, w);
    te::Cell* row = (te::Cell*)malloc(sizeof(te::Cell) * (size_t)view_.cols());
    for (int r = 0; row && r < view_.rows(); r++) {
        if (!view_.row(r, row))
            break;
        for (int x = 0; x < view_.cols(); x++) {
            const te::Cell& c = row[x];
            if (c.width == 0)
                continue;
            const tu::Style& cs = c.kind == te::CELL_TEXT      ? t.text
                                  : c.kind == te::CELL_CONTROL ? t.text_ctrl
                                                               : t.text_select;
            if (c.width == 2) {
                char b[5];
                b[utf8(c.cp, b)] = 0;
                s.text(x, r + 1, b, cs, 2);
            } else {
                s.put(x, r + 1, c.cp, cs);
            }
        }
    }
    free(row);
    edit_key_bar(s, t);
    int cx, cy;
    bool shown = view_.screen_cursor(&cx, &cy);
    s.set_cursor(shown ? cx : 0, shown ? cy + 1 : 0, shown);
}

ToolRequest Editor::handle(const tu::Event& ev)
{
    note_[0] = 0;
    bool shift = (ev.mods & tu::MOD_SHIFT) != 0;
    bool ctrl = (ev.mods & tu::MOD_CTRL) != 0;
    bool extend = shift || marking_;
    ToolRequest req = REQ_NONE;
    switch (ev.key) {
    case tu::KEY_MOUSE:
        if (ev.action == tu::MOUSE_WHEEL_UP || ev.action == tu::MOUSE_WHEEL_DOWN) {
            view_.scroll(ev.action == tu::MOUSE_WHEEL_UP ? -3 : 3);
            /* The cursor stays on screen, so drawing does not jump back. */
            te::Pos p = view_.cursor();
            long top = view_.top(), last = top + view_.rows() - 1;
            if (p.line < top || p.line > last)
                view_.set_cursor(view_.pos_at(p.line < top ? top : last, view_.column_of(p)),
                                 extend);
        } else if ((ev.action == tu::MOUSE_PRESS || ev.action == tu::MOUSE_DRAG) &&
                   ev.button == 1 && ev.y >= 1 && ev.y <= view_.rows()) {
            view_.set_cursor(view_.pos_at(view_.top() + ev.y - 1, ev.x + view_.left()),
                             extend || ev.action == tu::MOUSE_DRAG);
        }
        return REQ_NONE;
    case tu::KEY_PASTE_BEGIN:
        view_.auto_indent = false;
        return REQ_NONE;
    case tu::KEY_PASTE_END:
        view_.auto_indent = true;
        return REQ_NONE;
    case tu::KEY_LEFT:
        view_.move(ctrl ? te::MOVE_WORD_LEFT : te::MOVE_LEFT, extend);
        break;
    case tu::KEY_RIGHT:
        view_.move(ctrl ? te::MOVE_WORD_RIGHT : te::MOVE_RIGHT, extend);
        break;
    case tu::KEY_UP:
        view_.move(te::MOVE_UP, extend);
        break;
    case tu::KEY_DOWN:
        view_.move(te::MOVE_DOWN, extend);
        break;
    case tu::KEY_PGUP:
        view_.move(ctrl ? te::MOVE_DOC_START : te::MOVE_PAGE_UP, extend);
        break;
    case tu::KEY_PGDN:
        view_.move(ctrl ? te::MOVE_DOC_END : te::MOVE_PAGE_DOWN, extend);
        break;
    case tu::KEY_HOME:
        view_.move(ctrl ? te::MOVE_DOC_START : te::MOVE_HOME, extend);
        break;
    case tu::KEY_END:
        view_.move(ctrl ? te::MOVE_DOC_END : te::MOVE_END, extend);
        break;
    case tu::KEY_ENTER:
        view_.newline();
        break;
    case tu::KEY_TAB:
        view_.tab(shift);
        break;
    case tu::KEY_BACKSPACE:
        view_.backspace();
        break;
    case tu::KEY_DELETE:
        if (shift && view_.has_selection()) {
            free(clip_);
            clip_ = view_.cut(&clip_len_);
        } else {
            view_.del();
        }
        break;
    case tu::KEY_INSERT:
        if (shift)
            paste();
        else if (ctrl)
            copy_selection();
        break;
    case tu::KEY_ESC:
        if (marking_ || view_.has_selection() || view_.has_block()) {
            marking_ = false;
            view_.clear_selection();
            view_.clear_block();
            return REQ_NONE;
        }
        return doc_.modified() ? REQ_ASK_QUIT : REQ_CLOSE;
    case tu::KEY_CHAR:
        if (ctrl) {
            switch (ev.ch) {
            case 'u': case 'z':
                if (!view_.undo())
                    set_note("Nothing to undo.");
                break;
            case 'y': view_.delete_line(); break;
            case 's': return REQ_SAVE;
            case 'l': return REQ_GOTO;
            default: break;
            }
        } else if (ev.mods & tu::MOD_ALT) {
            if (ev.ch == 'l' || ev.ch == 'L')
                return REQ_GOTO;
            if ((ev.ch == 'r' || ev.ch == 'R') && !view_.redo())
                set_note("Nothing to redo.");
        } else {
            char b[4];
            view_.type(b, utf8(ev.ch, b));
        }
        break;
    default:
        if (ev.key >= tu::KEY_F1 && ev.key <= tu::KEY_F1 + 11)
            req = function_key(ev.key - tu::KEY_F1 + 1, shift);
        break;
    }
    view_.follow();
    return req;
}

ToolRequest Editor::function_key(int n, bool shift)
{
    switch (n) {
    case 1:
        set_note("F2 save, F3 mark, F5 copy, F6 move, F8 delete block, F7 search, F4 replace, "
                 "Ctrl-U undo, Alt-R redo, Ctrl-Y delete line, F10 quit.");
        return REQ_NONE;
    case 2:
        return shift || path_.empty() ? REQ_SAVE_AS : REQ_SAVE;
    case 3:
        /* Start a block here; the next F3 (or a Shift selection) marks it. */
        if (marking_ || view_.has_selection()) {
            marking_ = false;
            view_.mark_block();
        } else {
            view_.clear_block();
            view_.set_anchor(view_.cursor());
            marking_ = true;
        }
        return REQ_NONE;
    case 4:
        return REQ_REPLACE;
    case 5: case 6:
        if (marking_ || view_.has_selection()) {
            marking_ = false;
            view_.mark_block();
        }
        if (!view_.has_block())
            set_note("No block: F3 marks one.");
        else if (!(n == 5 ? view_.copy_block() : view_.move_block()))
            set_note("The cursor is inside the block.");
        return REQ_NONE;
    case 7:
        if (shift) {
            search(last_search_);
            return REQ_NONE;
        }
        return REQ_SEARCH;
    case 8:
        marking_ = false;
        if (!view_.erase_block() && view_.has_selection())
            view_.del();
        return REQ_NONE;
    case 10:
        return doc_.modified() ? REQ_ASK_QUIT : REQ_CLOSE;
    case 12:
        return REQ_SAVE_AS;
    default:
        return REQ_NONE;
    }
}

void Editor::search(const char* text)
{
    if (!text[0])
        return;
    snprintf(last_search_, sizeof(last_search_), "%s", text);
    marking_ = false;
    bool wrapped = false;
    if (!view_.find(text, true, false, &wrapped)) {
        char m[300];
        snprintf(m, sizeof(m), "Not found: %.200s", text);
        set_note(m);
    } else if (wrapped) {
        set_note("Search wrapped around.");
    }
    view_.follow();
}

void Editor::goto_line(long line)
{
    marking_ = false;
    view_.goto_line(line);
    view_.follow();
}

int Editor::replace_all(const char* from, const char* to)
{
    int count = view_.replace_all(from, to, true);
    char m[100];
    sprintf(m, "%d replaced.", count);
    set_note(m);
    view_.follow();
    return count;
}

} /* namespace ttrc */
