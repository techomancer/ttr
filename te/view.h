/*
 * te/view.h - editing a te::Document: the cursor and the selection,
 * moving and editing commands, scrolling, and the layout of screen rows
 * as cells, which tu (ttrc) and tw (ttrcode) draw in their own way. A
 * document can have several views (split windows).
 */
#ifndef TE_VIEW_H
#define TE_VIEW_H

#include "document.h"

namespace te {

/* What a cell shows; the renderer picks the colours. */
enum CellKind {
    CELL_TEXT = 0,
    CELL_SELECTED,
    CELL_CONTROL,           /* a control character, shown as ^X */
    CELL_FOUND,             /* the last search hit */
    CELL_BLOCK              /* the marked block (mc's F3) */
};

struct Cell {
    uint32_t cp;            /* code point; ' ' for tabs and padding */
    uint8_t width;          /* columns: 1, 2, or 0 for the right half of a wide character */
    uint8_t kind;           /* CellKind */
    uint8_t syn;            /* SynClass (syntax.h), SYN_NONE without colouring */
    uint8_t ws;             /* 1: visible whitespace (show_whitespace) */
};

class Syntax;

enum Move {
    MOVE_LEFT,
    MOVE_RIGHT,
    MOVE_UP,
    MOVE_DOWN,
    MOVE_WORD_LEFT,
    MOVE_WORD_RIGHT,
    MOVE_HOME,              /* first non-blank, then column 0 (Visual Studio) */
    MOVE_END,
    MOVE_PAGE_UP,
    MOVE_PAGE_DOWN,
    MOVE_DOC_START,
    MOVE_DOC_END
};

class View {
public:
    explicit View(Document& doc);
    ~View();

    Document& document() { return doc_; }
    const Document& document() const { return doc_; }

    /* Settings */
    int tab_width;          /* columns per tab stop (8) */
    int indent_width;       /* columns per indent with spaces (0: tab_width) */
    bool insert_spaces;     /* Tab inserts spaces instead of a tab */
    bool auto_indent;       /* Enter keeps the line's indentation */
    bool show_whitespace;   /* row() shows spaces as middle dots, tabs as >> */

    /* Syntax colouring for row() (owned by the host; 0: none). */
    void set_syntax(Syntax* s) { syntax_ = s; }

    /* The area the view is drawn in, in cells. */
    void set_size(int cols, int rows);
    int cols() const { return cols_; }
    int rows() const { return rows_; }

    /* Cursor and selection. The selection runs from the anchor to the
     * cursor; it is empty when they meet. */
    Pos cursor() const { return cur_; }
    Pos anchor() const { return anchor_; }
    bool has_selection() const { return anchor_ != cur_; }
    void selection(Pos* a, Pos* b) const;
    /* extend: keep the anchor (Shift+arrows); otherwise it follows. */
    void set_cursor(Pos p, bool extend);
    void set_anchor(Pos p) { anchor_ = doc_.clamp(p); }
    void select_all();
    /* The word at p (a double click); a single character if p is not in
     * a word. */
    void select_word(Pos p);
    void clear_selection() { anchor_ = cur_; }

    /* Commands */
    void move(Move m, bool extend);
    void type(const char* utf8, size_t n);      /* replaces the selection */
    void newline();
    void backspace();
    void del();
    /* Tab: a tab (or spaces); with a selection of lines, or else with a
     * marked block, indent them (outdent with shift). */
    void tab(bool shift);
    void delete_line();
    /* The selection's text (malloc'd), and cut and paste. */
    char* copy(size_t* n) const;
    char* cut(size_t* n);
    void paste(const char* s, size_t n);
    /* mc's blocks: a marked block stays where it is while the cursor
     * moves on; F5 and F6 copy or move it to the cursor, which must be
     * outside it, and the copy becomes the block. Any other edit drops
     * the block. */
    void mark_block();                  /* the selection becomes the block */
    bool has_block() const;
    void block(Pos* a, Pos* b) const;
    void clear_block() { block_ = false; }
    bool copy_block();
    bool move_block();
    bool erase_block();
    bool undo();
    bool redo();
    /* Search from the cursor; the hit becomes the selection. */
    bool find(const char* text, bool forward, bool match_case, bool* wrapped,
              bool whole_word = false);
    int replace_all(const char* from, const char* to, bool match_case, bool whole_word = false);
    void goto_line(long line);

    /* Scrolling: the first line and column shown. */
    long top() const { return top_; }
    int left() const { return left_; }
    void scroll(long lines);
    /* Bring the cursor into view (call after commands). */
    void follow();

    /* Layout: columns of a position and the position at a column. */
    int column_of(Pos p) const;
    Pos pos_at(long line, int col) const;
    /* Screen row y of the view as cols() cells; false past the end. */
    bool row(int y, Cell* out) const;
    /* Where the cursor is on screen; false if scrolled out of sight. */
    bool screen_cursor(int* x, int* y) const;

private:
    int indent() const { return indent_width > 0 ? indent_width : tab_width > 0 ? tab_width : 8; }
    Pos next_char(Pos p) const;
    Pos prev_char(Pos p) const;
    void erase_selection();
    void indent_lines(long first, long last, bool out);
    void move_to(Pos p, bool extend, bool keep_column);

    Document& doc_;
    Pos cur_, anchor_;
    int want_col_;          /* the column Up and Down keep */
    long top_;
    int left_;
    int cols_, rows_;
    Pos found_a_, found_b_; /* the last search hit */
    bool found_;
    Pos block_a_, block_b_;
    bool block_;
    unsigned long block_version_;   /* the document's version when marked */
    Syntax* syntax_;
    mutable uint8_t* syn_buf_;      /* a line's classes, for row() */
    mutable size_t syn_cap_;

    View(const View&);
    View& operator=(const View&);
};

} /* namespace te */

#endif
