/*
 * ttrcode: a te::View seen as a tw::Source (ttrcode/DESIGN.md section 2).
 * Each frame lays out the visible rows (line number gutter, then the
 * view's cells), compares them with the last frame row by row, and turns
 * a change of the top line into a scroll record, so that typing redraws
 * a row and scrolling copies the window and draws only the new rows.
 */
#ifndef TTRCODE_EDITSOURCE_H
#define TTRCODE_EDITSOURCE_H

#include "../../te/syntax.h"
#include "../../te/view.h"
#include "../../tw/tw.h"

namespace ttrcode {

struct EditColors {
    uint32_t text, background;
    uint32_t selected_text, selected_background;
    uint32_t gutter_text, gutter_background;
    uint32_t control;               /* ^X for control characters */
    uint32_t found_background;      /* the last search hit */
    uint32_t cursor;
    uint32_t syntax[te::SYN_COUNT];     /* text colour per class; [0] unused */
    uint32_t current_line;              /* background of the caret's line */
    uint32_t ruler;                     /* the column marker */
    uint32_t whitespace;                /* visible spaces and tabs */
};

/* How the editor looks beyond its colours (Tools > Options). */
/* What the debugger marks in the margin (the gutter's first cell). */
enum {
    MARK_BREAKPOINT = 1,    /* a red dot */
    MARK_CURRENT = 2,       /* where the program stopped: a yellow arrow, the line yellow */
    MARK_FRAME = 4          /* a caller's line, chosen in Call Stack: a green arrow */
};

class LineMarks {
public:
    virtual ~LineMarks() {}
    /* MARK_* bits for a line (from 0) of the document v shows. */
    virtual int line_marks(const te::View* v, long line) const = 0;
};

struct EditLook {
    bool highlight_line;        /* the caret's line in current_line */
    int ruler;                  /* a line left of this column (from 0); <= 0: none */
    tw::CursorStyle cursor;     /* bar, block, underline */
    bool blink;

    EditLook() : highlight_line(true), ruler(80), cursor(tw::CURSOR_BAR), blink(true) {}
};

class EditSource : public tw::Source {
public:
    EditSource();
    ~EditSource();

    /* The view to draw (0: an empty window). */
    void set_view(te::View* v);
    te::View* view() const { return v_; }
    void set_colors(const EditColors& c);
    void set_line_numbers(bool on);
    void set_look(const EditLook& l);
    /* 0: no marks. With marks there is a margin even without line
     * numbers. */
    void set_marks(const LineMarks* m);
    /* The grid is cols x rows cells; the view gets what the gutter leaves. */
    void set_size(int cols, int rows);
    int gutter() const { return gutter_; }
    void redraw_all() { full_ = true; }

    /* tw::Source */
    void frame_begin();
    int cols() const { return cols_; }
    int rows() const { return rows_; }
    bool held() const { return false; }
    bool full_damage() const { return full_; }
    int scroll_count() const { return scroll_ ? 1 : 0; }
    void scroll(int i, int* top, int* bottom, int* count) const;
    bool row_dirty(int y, int* lo, int* hi) const;
    void cells(int y, int lo, int hi, tw::CellData* out) const;
    int cluster(int, int, uint32_t*, int) const { return 0; }
    tw::LineAttr line_attr(int) const { return tw::LINE_SINGLE; }
    tw::Cursor cursor() const;
    uint32_t background() const { return colors_.background; }
    uint32_t cursor_color() const { return colors_.cursor; }
    void frame_done();

private:
    void layout();
    int gutter_for(long lines) const;

    te::View* v_;
    const LineMarks* marks_;
    EditColors colors_;
    EditLook look_;
    bool numbers_;
    int cols_, rows_, gutter_;
    tw::CellData* cur_;         /* this frame, rows_ x cols_ */
    tw::CellData* prev_;        /* the last frame drawn */
    te::Cell* line_;
    int* lo_;
    int* hi_;
    bool full_;
    int scroll_;                /* rows moved up this frame */
    /* The last frame drawn. */
    bool prev_valid_;
    long prev_top_;
    int prev_left_, prev_gutter_;
    int prev_cx_, prev_cy_;     /* the caret's cell, -1: none */

    EditSource(const EditSource&);
    EditSource& operator=(const EditSource&);
};

} /* namespace ttrcode */

#endif
