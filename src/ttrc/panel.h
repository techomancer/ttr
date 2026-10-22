/*
 * One of ttrc's two panels: a directory listing read through tf, with a
 * cursor, marks, sorting and listing modes, drawn the way Norton
 * Commander drew it (ttrc/DESIGN.md section 7).
 */
#ifndef TTRC_PANEL_H
#define TTRC_PANEL_H

#include "theme.h"

#include "../../tf/path.h"
#include "../../tf/tf.h"
#include "core/str.h"
#include "core/vector.h"

namespace ttrc {

struct Entry {
    ttr::Str name;
    tf::Stat st;
    tf::FileType target;    /* for links: what they point to; FT_OTHER if broken */
    bool up;                /* ".." */
    bool marked;
};

enum SortMode {
    SORT_NAME,
    SORT_EXT,
    SORT_SIZE,
    SORT_TIME,
    SORT_UNSORTED
};

enum ListMode {
    LIST_FULL,              /* name, size, time */
    LIST_BRIEF              /* names in two columns */
};

class Panel {
public:
    Panel(tf::Fs* fs, const tf::Path& path);
    ~Panel();

    /* Read 'path'; the cursor goes to 'select' if it is there. False,
     * with the error in error(), if it cannot be read; the panel then
     * keeps what it showed. */
    bool load(const tf::Path& path, const char* select = 0);
    /* Read the same directory again, keeping cursor and marks. */
    void reload();
    const tf::Path& path() const { return path_; }
    const char* error() const { return error_; }

    void draw(tu::Screen& s, const Theme& t, int x, int y, int w, int h, bool active);

    /* Moves; page sizes come from the last draw. */
    void move(int delta);
    void page(int dir);
    void home();
    void end();
    void column(int dir);       /* brief mode: a column left or right */
    /* Enter the directory under the cursor (or go up on ".."). True if
     * the panel changed. */
    bool enter();
    bool up();
    void toggle_mark();

    SortMode sort() const { return sort_; }
    void set_sort(SortMode m);
    ListMode list_mode() const { return mode_; }
    void set_list_mode(ListMode m) { mode_ = m; }
    void toggle_hidden();

    /* Quick search: the first name from the cursor on that starts with
     * prefix (case-insensitive); false if none. */
    bool search(const char* prefix, bool next);

    /* Mouse: the entry at a screen position of the last draw, or -1. */
    int entry_at(int sx, int sy) const;
    void set_cursor(int i);

    const Entry* current() const;
    int count() const { return entries_.size(); }
    int marked_count() const;
    uint64_t marked_bytes() const;

private:
    void sort_entries();
    void clamp();
    void draw_entry(tu::Screen& s, const Theme& t, const Entry& e, int x, int y, int w,
                    bool cursor);
    void free_entries();

    tf::Fs* fs_;
    tf::Path path_;
    ttr::Vector<Entry*> entries_;
    int cursor_, top_;
    SortMode sort_;
    bool reverse_;
    ListMode mode_;
    bool hidden_;
    char error_[300];
    /* Geometry of the last draw. */
    int lx_, ly_, lw_, lh_;     /* the listing area */
    int cols_;                  /* columns of names */
};

/* "rwxr-xr-x" style, with the type letter first. */
void mode_string(const tf::Stat& st, char out[11]);
/* A size in at most 'width' characters: 1234567, 12345K, 1234M. */
void size_string(uint64_t n, int width, char* out);

} /* namespace ttrc */

#endif
