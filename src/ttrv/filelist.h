/*
 * ttrv's file list (ttrv/DESIGN.md section 2.2): the files of a folder
 * in rows, ACDSee's details view, drawn by our own code on a drawing
 * area with a scroll bar: a header (click: sort by that column, again:
 * reverse), icons, the cursor, marked files, keyboard and mouse. The
 * image column fills in as headers arrive.
 *
 * The rows belong to the list; the host fills them, then calls
 * rows_changed() (resorts, keeps the cursor on the same file).
 */
#ifndef TTRV_FILELIST_H
#define TTRV_FILELIST_H

#include "../../tf/tf.h"
#include "../../ti/ti.h"
#include "core/str.h"
#include "core/vector.h"

#include <Xm/Xm.h>

namespace ttrv {

struct Row {
    ttr::Str name;
    tf::Stat st;
    bool up;                /* ".." */
    bool dir;               /* a folder, or an archive entered as one */
    bool archive;
    bool image;             /* by its name: worth a header */
    bool probed;            /* info is the header's (or failed: info.format 0) */
    ti::Info info;
    bool marked;
    Row() : up(false), dir(false), archive(false), image(false), probed(false), marked(false) {}
};

enum SortKey { SORT_NAME, SORT_SIZE, SORT_IMAGE, SORT_TYPE, SORT_TIME, SORT_COUNT };

struct ListColors {
    unsigned long bg, fg, sel_bg, sel_fg, header_bg, header_fg, header_top, header_bottom,
        dim, folder, image, archive;
};

class FileList;

class FileListHost {
public:
    virtual ~FileListHost() {}
    virtual void list_cursor(FileList* l, int row) = 0;
    /* Enter or a double click. */
    virtual void list_activate(FileList* l, int row) = 0;
    /* Backspace (the parent folder). */
    virtual void list_back(FileList* l) = 0;
    /* The rows in sight changed (scrolling, resizing, new rows). */
    virtual void list_visible(FileList* l, int first, int last) = 0;
    /* A key the list does not use (for the window's shortcuts). */
    virtual bool list_key(FileList* l, XKeyEvent* ev) { (void)l; (void)ev; return false; }
};

class FileList {
public:
    FileList(Widget parent, const char* name, FileListHost* host, const ListColors& c,
             const char* font);
    ~FileList();

    Widget widget() const { return form_; }
    Widget canvas() const { return da_; }

    ttr::Vector<Row*>& rows() { return rows_; }
    int count() const { return rows_.size(); }
    Row* row(int i) const { return i >= 0 && i < rows_.size() ? rows_[i] : 0; }
    void clear();
    /* After the host changed the rows: sorted, the cursor put on the
     * row named 'keep' if there is one (else kept in range). */
    void rows_changed(const char* keep = 0);
    /* One row's data changed (its header came): redraw it. */
    void row_changed(int i);

    int cursor() const { return cursor_; }
    void set_cursor(int i);
    /* The row of this name, or -1. */
    int find(const char* name) const;
    void sort_by(SortKey k, bool reverse);
    SortKey sort_key() const { return key_; }
    bool sort_reverse() const { return reverse_; }
    void visible(int* first, int* last) const;
    int marked_count() const;

    void set_colors(const ListColors& c);

    /* Callbacks (public for the C trampolines). */
    void exposed();
    void resized();
    void key(XKeyEvent* ev);
    void button(XButtonEvent* ev);
    void scrolled(int value);

private:
    FileList(const FileList&);
    FileList& operator=(const FileList&);

    void layout();
    void paint();
    void paint_row(Drawable d, int i, int y);
    void paint_header(Drawable d);
    void icon(Drawable d, const Row* r, int x, int y, int sz, bool sel);
    void update_scrollbar();
    void scroll_to_cursor();
    void move(int to);
    void sort_rows();
    bool ensure_gc();
    void tell_visible();

    Widget form_, da_, sb_;
    FileListHost* host_;
    Display* dpy_;
    ListColors c_;
    XFontStruct* font_;
    GC gc_;
    Pixmap buf_;
    int buf_w_, buf_h_;
    ttr::Vector<Row*> rows_;
    int cursor_, top_;
    int row_h_, header_h_;
    int col_x_[SORT_COUNT + 1];     /* each column's left edge, and the end */
    SortKey key_;
    bool reverse_;
    char typed_[64];
    Time typed_at_;
    Time click_at_;
    int click_row_;
    int vis_first_, vis_last_;
};

/* "1.2 MB", "88 KB", "512 B". */
void format_size(uint64_t n, char* buf, size_t len);
/* "2048x1536x24" (indexed images: their colours, as ACDSee showed). */
void format_image(const ti::Info& in, char* buf, size_t len);
/* By the name: one of ti's formats. */
bool image_name(const char* name);

} /* namespace ttrv */

#endif
