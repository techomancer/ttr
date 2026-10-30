/*
 * ttrcode's debugger windows (td/DESIGN.md section 8.2): rows in columns
 * on a TwGrid, the editor's character grid, as Visual C++ 6 drew Watch,
 * Variables and the rest. A header row names the columns (drag its
 * separators to resize them); rows can be a tree (+ and - open and close
 * them) with a mark left of them (Call Stack's arrows); a cell's text can
 * have its own colour (values that changed, in red); and a cell can be
 * edited in place (Watch's expressions).
 */
#ifndef TTRCODE_CELLLIST_H
#define TTRCODE_CELLLIST_H

#include "core/str.h"
#include "core/vector.h"

#include "../../tw/grid_widget.h"
#include "../../tw/tw.h"

#include <Xm/Xm.h>

namespace ttrcode {

struct ListColors {
    uint32_t text, background;
    uint32_t selected_text, selected_background;
    uint32_t header_text, header_background;
    uint32_t lines;             /* between the columns */
    uint32_t changed;           /* values that changed since the last stop */
    uint32_t dim;               /* types, addresses */
    uint32_t cursor;
};

class CellList;

class CellListHost {
public:
    virtual ~CellListHost() {}
    /* Enter or a double click. */
    virtual void list_activate(CellList* l, int row) { (void)l; (void)row; }
    /* Its + or - was clicked, or Left, Right, + or - typed. */
    virtual void list_toggle(CellList* l, int row) { (void)l; (void)row; }
    /* Every key first (the debugger's keys); true: taken. */
    virtual bool list_key(CellList* l, TwGridCallbackStruct* cb)
    {
        (void)l;
        (void)cb;
        return false;
    }
    /* An edit ended with Enter (not with Escape). */
    virtual void list_edited(CellList* l, int row, const char* text)
    {
        (void)l;
        (void)row;
        (void)text;
    }
    virtual void list_focus(CellList* l, bool in) { (void)l; (void)in; }
    /* Its size in cells changed (registers and memory lay out again). */
    virtual void list_resized(CellList* l) { (void)l; }
};

enum { LIST_COLUMNS = 4 };
/* In a cell's text: what follows is in the changed colour, or the cell's
 * own again (registers and memory mark single values). */
#define LIST_CHANGED "\001"
#define LIST_PLAIN "\002"
enum { EXPAND_NONE, EXPAND_CLOSED, EXPAND_OPEN };

class CellList : public tw::Source {
public:
    /* grid_args: the TwGrid's font, renderer and the like. */
    CellList(Widget parent, const char* name, CellListHost* host, ArgList grid_args,
             Cardinal n);
    ~CellList();

    /* An XmForm with the grid and its scroll bar. */
    Widget widget() const { return form_; }
    Widget grid() const { return grid_; }
    void set_colors(const ListColors& c);
    /* Titles, and widths in cells; the last column gets what is left.
     * No titles: no header row. */
    void set_columns(int n, const char* const* titles, const int* widths);
    int column_width(int i) const { return width_[i]; }
    /* The user dragged a separator: widths are theirs from then on. */
    bool user_sized() const { return user_sized_; }
    int width() const { return cols_; }
    /* Room for a mark (an arrow) left of the first column. */
    void set_marks(bool on);
    /* An id for the host (which pane this is). */
    void set_tag(int t) { tag_ = t; }
    int tag() const { return tag_; }

    /* The rows, made again after every stop. */
    void clear();
    int add_row(int depth = 0, int expand = EXPAND_NONE);
    void set_cell(int row, int col, const char* text, uint32_t color = 0);
    void set_mark(int row, uint32_t cp, uint32_t color);
    /* After the rows: keeps the selection and the scroll position. */
    void done();
    int row_count() const { return rows_.size(); }
    const char* cell(int row, int col) const { return rows_[row].text[col].c_str(); }
    int selected() const { return sel_; }
    void select(int row);
    /* The visible rows, and how many fit. */
    int visible_rows() const;
    int top() const { return top_; }

    /* Edit column col of a row in place, starting with text. */
    void begin_edit(int row, int col, const char* text);
    bool editing() const { return edit_row_ >= 0; }
    void cancel_edit();
    void focus();

    /* The grid's callbacks (public for the C trampolines). */
    void key(TwGridCallbackStruct* cb);
    void pointer(TwGridCallbackStruct* cb);
    void resized(int cols, int rows);
    void scrolled(int value);
    void focused(bool in);
    /* Its widgets were destroyed (with their parent). */
    void forget_widgets();

    /* tw::Source */
    int cols() const { return cols_; }
    int rows() const { return nrows_; }
    bool held() const { return false; }
    bool full_damage() const { return dirty_; }
    int scroll_count() const { return 0; }
    void scroll(int, int*, int*, int*) const {}
    bool row_dirty(int y, int* lo, int* hi) const;
    void cells(int y, int lo, int hi, tw::CellData* out) const;
    int cluster(int, int, uint32_t*, int) const { return 0; }
    tw::LineAttr line_attr(int) const { return tw::LINE_SINGLE; }
    tw::Cursor cursor() const;
    uint32_t background() const { return colors_.background; }
    uint32_t cursor_color() const { return colors_.cursor; }
    void frame_done() { dirty_ = false; }

private:
    struct Row {
        int depth, expand;
        ttr::Str text[LIST_COLUMNS];
        uint32_t color[LIST_COLUMNS];
        uint32_t mark, mark_color;
    };

    void damage();
    void update_scrollbar();
    void make_visible(int row);
    int header_rows() const { return ncols_ > 0 && titles_[0].length() ? 1 : 0; }
    /* Where column c starts, in cells. */
    int column_x(int c) const;
    /* The data row at grid row y, -1 if none. */
    int row_at(int y) const;

    Widget form_, grid_, scroll_;
    CellListHost* host_;
    ListColors colors_;
    int ncols_;
    ttr::Str titles_[LIST_COLUMNS];
    int width_[LIST_COLUMNS];
    bool marks_;
    bool tree_;                 /* some row has depth or a + or -: room for them */
    int tag_;
    ttr::Vector<Row> rows_;
    int sel_, top_;
    int cols_, nrows_;
    bool dirty_;
    bool focused_;
    /* Editing. */
    int edit_row_, edit_col_;
    char edit_[512];
    int edit_len_, edit_pos_;
    /* A header separator being dragged: which, and from where. */
    int drag_col_, drag_x_, drag_w_;
    bool user_sized_;
    Time last_click_;
    int last_click_row_;

    CellList(const CellList&);
    CellList& operator=(const CellList&);
};

} /* namespace ttrcode */

#endif
