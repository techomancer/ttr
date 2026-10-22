/*
 * ttrc's viewer (F3) and editor (F4), full-screen on tu (ttrc/DESIGN.md
 * section 3.5). Both read through tf, so files inside archives open
 * without being extracted first. The host (the App) runs the loop: it
 * draws them, feeds them events, and answers their requests for a
 * dialog (search, go to line, save as, quit without saving?).
 */
#ifndef TTRC_VIEWER_H
#define TTRC_VIEWER_H

#include "theme.h"

#include "../../tf/tf.h"
#include "../../tu/input.h"
#include "../../tu/screen.h"
#include "core/str.h"
#include "core/vector.h"

namespace ttrc {

/* What a full-screen tool wants from its host. */
enum ToolRequest {
    REQ_NONE,
    REQ_CLOSE,
    REQ_SEARCH,         /* ask for text, call search() */
    REQ_GOTO,           /* ask for a line number, call goto_line() */
    REQ_REPLACE,        /* editor: ask for two texts, call replace_all() */
    REQ_SAVE,           /* editor: save where it came from */
    REQ_SAVE_AS,        /* editor: ask for a path, call save() */
    REQ_ASK_QUIT        /* editor: modified; ask save / discard / cancel */
};

class FullScreen {
public:
    virtual ~FullScreen() {}
    virtual void draw(tu::Screen& s, const Theme& t) = 0;
    /* Returns what the host should do next. */
    virtual ToolRequest handle(const tu::Event& ev) = 0;
    virtual void search(const char* text) = 0;
    virtual void goto_line(long line) = 0;
    /* A note for the status line until the next key. */
    const char* note() const { return note_; }
    void set_note(const char* n);

protected:
    FullScreen() { note_[0] = 0; }
    char note_[256];
};

class Viewer : public FullScreen {
public:
    Viewer();
    ~Viewer();
    /* Read the file (up to 64 MB). 0 or an errno value. */
    int load(tf::Fs* fs, const char* path);
    void draw(tu::Screen& s, const Theme& t);
    ToolRequest handle(const tu::Event& ev);
    void search(const char* text);
    void goto_line(long line);

private:
    size_t line_start(long i) const;
    size_t line_end(long i) const;      /* before the newline */
    /* The byte offsets where each screen row of line i starts. */
    int rows_of(long i, int width, size_t* starts, int max) const;
    void scroll(int rows);
    void top_to(size_t off);
    int width() const { return width_; }
    long line_of(size_t off) const;

    char* data_;
    size_t len_;
    bool truncated_;
    ttr::Vector<size_t> lines_;     /* start offsets */
    char name_[512];
    bool hex_, wrap_;
    long top_line_;
    int top_sub_;               /* screen row within the top line */
    int left_;                  /* first column when not wrapping */
    long hex_top_;              /* first hex row */
    int rows_, width_, bpr_;    /* from the last draw */
    size_t hit_, hit_len_;
    char last_search_[256];
};

class Editor : public FullScreen {
public:
    Editor();
    ~Editor();
    /* A new, empty file if it does not exist. 0 or an errno value. */
    int load(tf::Fs* fs, const char* path);
    /* Save to path (null: where it came from). 0 or an errno value. */
    int save(const char* path);
    bool modified() const { return modified_; }
    const char* path() const { return path_.c_str(); }
    void draw(tu::Screen& s, const Theme& t);
    ToolRequest handle(const tu::Event& ev);
    void search(const char* text);
    void goto_line(long line);
    int replace_all(const char* from, const char* to);

private:
    struct Line {
        char* s;
        size_t len, cap;
    };
    struct Pos {
        long line;
        size_t col;             /* byte offset */
    };
    struct Undo {
        bool insert;            /* undo removes text that was inserted */
        Pos at;
        char* text;
    };

    void clear();
    Line* new_line(const char* s, size_t n);
    void line_insert(Line* l, size_t at, const char* s, size_t n);
    void insert_text(Pos at, const char* s, size_t n, bool record);
    void erase(Pos a, Pos b, bool record);
    char* text_between(Pos a, Pos b, size_t* n) const;
    Pos end_of(Pos a, const char* s, size_t n) const;
    void push_undo(bool insert, Pos at, const char* s, size_t n);
    void undo();
    bool has_selection() const;
    void selection(Pos* a, Pos* b) const;
    void delete_selection();
    void copy_selection();
    void paste();
    void move_cursor(Pos p, bool select);
    int col_of(long line, size_t byte) const;     /* screen column */
    size_t byte_at(long line, int col) const;
    void clamp_view();
    bool less(Pos a, Pos b) const { return a.line < b.line || (a.line == b.line && a.col < b.col); }

    tf::Fs* fs_;
    ttr::Str path_;
    ttr::Vector<Line*> lines_;
    Pos cur_;
    int want_col_;              /* the column Up and Down keep */
    long top_;
    int left_;
    int rows_, width_;
    bool modified_;
    bool marking_;              /* F3 started a selection */
    Pos anchor_;
    bool selected_;
    char* clip_;
    size_t clip_len_;
    ttr::Vector<Undo> undo_;
    char last_search_[256];
};

} /* namespace ttrc */

#endif
