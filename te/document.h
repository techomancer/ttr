/*
 * te: the text editing core shared by ttrc's editor (drawn with tu) and
 * ttrcode (drawn with tw). See te/DESIGN.md.
 *
 * te::Document is the text: lines of bytes (UTF-8 or anything else; te
 * never rejects bytes), edits with undo and redo in groups, search and
 * replace. It knows nothing of screens, keys or files: hosts load and
 * save through text() and set_text(). Builds with MIPSpro 7.4 and
 * g++ -std=c++98 -pedantic; no STL, no exceptions.
 */
#ifndef TE_DOCUMENT_H
#define TE_DOCUMENT_H

#include "../tl/port.h"

#include <stddef.h>

namespace te {

/* A place in the text: a line and a byte offset in it. */
struct Pos {
    long line;
    size_t col;

    Pos() : line(0), col(0) {}
    Pos(long l, size_t c) : line(l), col(c) {}
    bool operator==(const Pos& o) const { return line == o.line && col == o.col; }
    bool operator!=(const Pos& o) const { return !(*this == o); }
    bool operator<(const Pos& o) const
    {
        return line < o.line || (line == o.line && col < o.col);
    }
    bool operator<=(const Pos& o) const { return !(o < *this); }
};

class Document {
public:
    Document();
    ~Document();

    /* Replace everything (loading a file). Line ends: "\n", or "\r\n"
     * when every line has it (the file is then saved that way again).
     * Clears undo and the modified flag. */
    void set_text(const char* data, size_t n);
    /* The whole text with its line ends; malloc'd, *n bytes. */
    char* text(size_t* n) const;
    bool crlf() const { return crlf_; }
    void set_crlf(bool on) { crlf_ = on; }

    long lines() const { return n_; }
    const char* line(long i) const { return lines_[i].s; }
    size_t length(long i) const { return lines_[i].len; }
    /* A position moved into the text. */
    Pos clamp(Pos p) const;
    Pos end() const;

    /* Edits. insert() returns the position after the inserted text. */
    Pos insert(Pos at, const char* s, size_t n);
    void erase(Pos a, Pos b);
    /* The text between two positions; malloc'd. */
    char* get(Pos a, Pos b, size_t* n) const;

    /* Undo groups: everything between begin_group() and end_group() is
     * undone at once. Typing merges into one step until the cursor jumps
     * or a line ends. Undo and redo set *cursor to where the change was. */
    void begin_group();
    void end_group();
    bool undo(Pos* cursor);
    bool redo(Pos* cursor);
    bool can_undo() const { return undo_n_ > 0; }
    bool can_redo() const { return redo_n_ > 0; }
    /* Stop merging typing into the last undo step. */
    void break_merge() { merge_ok_ = false; }

    /* Modified since set_text() or the last mark_saved(); undoing back to
     * the saved state counts as unmodified. */
    bool modified() const { return saved_ != state(); }
    void mark_saved() { saved_ = state(); }
    /* Changes with every edit, undo and redo (for renderers). */
    unsigned long version() const { return version_; }

    /* Search, within lines (patterns do not cross line ends). Forward
     * searches start at 'from', backward ones end just before it. False
     * if not found anywhere (the search wraps around). */
    bool find(const char* text, Pos from, bool forward, bool match_case, Pos* a, Pos* b,
              bool* wrapped) const;
    /* Replace every match, as one undo step; returns how many. */
    int replace_all(const char* from, const char* to, bool match_case);

private:
    struct Line {
        char* s;
        size_t len, cap;
    };
    struct Op {
        bool insert;        /* the change inserted text (undo erases it) */
        Pos at;
        char* text;
        size_t n;
        unsigned long group;
    };

    void reserve_lines(long n);
    void line_insert(long i, size_t at, const char* s, size_t n);
    void raw_insert(Pos at, const char* s, size_t n, Pos* end);
    void raw_erase(Pos a, Pos b);
    void record(bool insert, Pos at, const char* s, size_t n);
    void clear_ops(Op* ops, long* count);
    void push_op(Op** ops, long* count, long* cap, const Op& op);
    /* The group id of the last undo step: identifies the text's state. */
    unsigned long state() const { return undo_n_ ? undo_[undo_n_ - 1].group : 0; }

    Line* lines_;
    long n_, cap_;
    bool crlf_;
    Op* undo_;
    long undo_n_, undo_cap_;
    Op* redo_;
    long redo_n_, redo_cap_;
    unsigned long group_;       /* current group id */
    int group_depth_;
    bool merge_ok_;
    unsigned long next_group_;  /* ids for new undo groups */
    unsigned long saved_;       /* state() when saved */
    unsigned long version_;

    Document(const Document&);
    Document& operator=(const Document&);
};

} /* namespace te */

#endif
