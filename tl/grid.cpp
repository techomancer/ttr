#include "grid.h"

#include <stdlib.h>
#include <string.h>

namespace tl {

/* ---- GraphemePool ------------------------------------------------------ */

GraphemePool::GraphemePool() : data_(0), len_(0), cap_(0)
{
    clear();
}

GraphemePool::~GraphemePool()
{
    free(data_);
}

void GraphemePool::clear()
{
    len_ = 0;
    uint32_t zero = 0;
    add(&zero, 0);              /* index 0 is the empty dummy */
}

uint32_t GraphemePool::add(const uint32_t* cps, int n)
{
    if (n > TL_GRAPHEME_MAX)
        n = TL_GRAPHEME_MAX;
    if (len_ + (size_t)n + 1 > cap_) {
        size_t ncap = cap_ ? cap_ * 2 : 1024;
        while (ncap < len_ + (size_t)n + 1)
            ncap *= 2;
        uint32_t* p = (uint32_t*)realloc(data_, ncap * sizeof(uint32_t));
        if (!p)
            return 0;
        data_ = p;
        cap_ = ncap;
    }
    uint32_t index = (uint32_t)len_;
    data_[len_++] = (uint32_t)n;
    for (int i = 0; i < n; i++)
        data_[len_++] = cps[i];
    return index;
}

int GraphemePool::get(uint32_t index, const uint32_t** cps) const
{
    if (index >= len_)
        return 0;
    *cps = data_ + index + 1;
    return (int)data_[index];
}

void GraphemePool::swap(GraphemePool& o)
{
    uint32_t* d = data_; data_ = o.data_; o.data_ = d;
    size_t l = len_; len_ = o.len_; o.len_ = l;
    size_t c = cap_; cap_ = o.cap_; o.cap_ = c;
}

/* ---- Grid -------------------------------------------------------------- */

Grid::Grid()
    : full_damage(true), ring_(0), cap_(0), start_(0), len_(0), cols_(0), rows_(0),
      max_history_(0), base_(0)
{
}

Grid::~Grid()
{
    destroy();
}

void Grid::destroy()
{
    for (size_t i = 0; i < cap_; i++)
        free(ring_[i].cells);
    free(ring_);
    ring_ = 0;
    cap_ = start_ = len_ = 0;
}

bool Grid::alloc_row(Row& r, int cols)
{
    Cell* c = (Cell*)calloc((size_t)cols, sizeof(Cell));
    if (!c)
        return false;
    free(r.cells);
    r.cells = c;
    r.cols = (uint16_t)cols;
    r.flags = 0;
    r.line_attr = LINE_SINGLE;
    r.semantic = SEM_OUTPUT;
    r.damage_all();
    return true;
}

bool Grid::init(int cols, int rows, size_t max_history)
{
    destroy();
    cols_ = cols;
    rows_ = rows;
    max_history_ = max_history;
    base_ = 0;
    size_t initial = (size_t)rows + (max_history < 1000 ? max_history : 1000);
    ring_ = (Row*)calloc(initial, sizeof(Row));
    if (!ring_)
        return false;
    cap_ = initial;
    for (int y = 0; y < rows; y++)
        if (!alloc_row(ring_[y], cols))
            return false;
    len_ = (size_t)rows;
    start_ = 0;
    full_damage = true;
    graphemes_.clear();
    return true;
}

/* Make room for one more row in the ring, linearising it first. */
bool Grid::grow_ring(size_t need)
{
    if (need <= cap_)
        return true;
    size_t ncap = cap_ * 2;
    if (ncap < need)
        ncap = need;
    size_t limit = (size_t)rows_ + max_history_;
    if (ncap > limit)
        ncap = limit;
    if (ncap < need)
        return false;
    Row* nr = (Row*)calloc(ncap, sizeof(Row));
    if (!nr)
        return false;
    for (size_t i = 0; i < len_; i++)
        nr[i] = ring_[(start_ + i) % cap_];
    /* Unused slots of the old ring may hold allocated cells; keep them. */
    size_t k = len_;
    for (size_t i = len_; i < cap_; i++)
        nr[k++] = ring_[(start_ + i) % cap_];
    free(ring_);
    ring_ = nr;
    cap_ = ncap;
    start_ = 0;
    return true;
}

void Grid::fill_row(Row& r, const Cell& f)
{
    for (int x = 0; x < r.cols; x++)
        r.cells[x] = f;
    r.flags = (f.style ? ROW_HAS_STYLES : 0);
    r.line_attr = LINE_SINGLE;
    r.semantic = SEM_OUTPUT;
    r.damage_all();
}

void Grid::fill(Row& r, int lo, int hi, const Cell& f)
{
    if (lo < 0) lo = 0;
    if (hi >= r.cols) hi = r.cols - 1;
    if (lo > hi)
        return;
    for (int x = lo; x <= hi; x++)
        r.cells[x] = f;
    if (f.style)
        r.flags |= ROW_HAS_STYLES;
    r.damage(lo, hi);
}

/* The top active row becomes history; a fresh row appears at the bottom. */
void Grid::push_history_row(const Cell& f)
{
    if (len_ < (size_t)rows_ + max_history_ && grow_ring(len_ + 1)) {
        Row& r = ring_[(start_ + len_) % cap_];
        if (!r.cells && !alloc_row(r, cols_)) {
            /* Out of memory: fall back to dropping the oldest row. */
        } else {
            len_++;
            if (r.cols != cols_)
                alloc_row(r, cols_);
            fill_row(r, f);
            return;
        }
    }
    /* Ring full (or no history): the oldest row is dropped and reused. */
    Row& r = ring_[start_];
    start_ = (start_ + 1) % cap_;
    base_++;
    if (r.cols != cols_)
        alloc_row(r, cols_);
    fill_row(r, f);
}

void Grid::scroll_up(int top, int bot, int n, const Cell& f, bool to_history)
{
    if (n <= 0 || top > bot)
        return;
    if (n > bot - top + 1)
        n = bot - top + 1;
    if (to_history && top == 0 && bot == rows_ - 1) {
        /* Whole screen: rotate through the ring. The leaving rows become
         * history, or are recycled once the history is full. */
        for (int i = 0; i < n; i++)
            push_history_row(f);
        full_damage = true;
        return;
    }
    /* Rotate the row headers inside the region. */
    Row tmp[64];
    Row* saved = n <= 64 ? tmp : (Row*)malloc((size_t)n * sizeof(Row));
    if (!saved)
        return;
    for (int i = 0; i < n; i++)
        saved[i] = row(top + i);
    for (int y = top; y + n <= bot; y++)
        row(y) = row(y + n);
    for (int i = 0; i < n; i++) {
        row(bot - n + 1 + i) = saved[i];
        fill_row(row(bot - n + 1 + i), f);
    }
    if (saved != tmp)
        free(saved);
    for (int y = top; y <= bot; y++)
        row(y).damage_all();
}

void Grid::scroll_down(int top, int bot, int n, const Cell& f)
{
    if (n <= 0 || top > bot)
        return;
    if (n > bot - top + 1)
        n = bot - top + 1;
    Row tmp[64];
    Row* saved = n <= 64 ? tmp : (Row*)malloc((size_t)n * sizeof(Row));
    if (!saved)
        return;
    for (int i = 0; i < n; i++)
        saved[i] = row(bot - n + 1 + i);
    for (int y = bot; y - n >= top; y--)
        row(y) = row(y - n);
    for (int i = 0; i < n; i++) {
        row(top + i) = saved[i];
        fill_row(row(top + i), f);
    }
    if (saved != tmp)
        free(saved);
    for (int y = top; y <= bot; y++)
        row(y).damage_all();
}

void Grid::clear_history()
{
    size_t h = history();
    if (!h)
        return;
    /* The active rows move to the front; the freed slots keep their cell
     * buffers for reuse. */
    Row* nr = (Row*)calloc(cap_, sizeof(Row));
    if (!nr)
        return;
    size_t k = 0;
    for (size_t i = h; i < len_; i++)
        nr[k++] = ring_[(start_ + i) % cap_];
    for (size_t i = 0; i < h; i++)
        nr[k++] = ring_[(start_ + i) % cap_];
    for (size_t i = len_; i < cap_; i++)
        nr[k++] = ring_[(start_ + i) % cap_];
    free(ring_);
    ring_ = nr;
    start_ = 0;
    len_ = (size_t)rows_;
    base_ += h;
}

bool Grid::row_blank(const Row& r) const
{
    for (int x = 0; x < r.cols; x++)
        if (!r.cells[x].is_empty() || r.cells[x].style)
            return false;
    return true;
}

void Grid::trim_history()
{
    while (history() > max_history_) {
        start_ = (start_ + 1) % cap_;
        len_--;
        base_++;
    }
}

bool Grid::resize(int cols, int rows, int* cursor_y)
{
    if (cols == cols_ && rows == rows_)
        return true;
    /* Columns: reallocate every row, keeping the left part. */
    if (cols != cols_) {
        for (size_t i = 0; i < cap_; i++) {
            Row& r = ring_[i];
            if (!r.cells)
                continue;
            Cell* c = (Cell*)calloc((size_t)cols, sizeof(Cell));
            if (!c)
                return false;
            int keep = r.cols < cols ? r.cols : cols;
            memcpy(c, r.cells, (size_t)keep * sizeof(Cell));
            /* A wide character cut in half leaves no spacer behind. */
            if (keep > 0 && c[keep - 1].wide() == CELL_WIDE)
                c[keep - 1] = Cell();
            /* Without reflow a soft wrap no longer joins anything. */
            if (r.cols != cols)
                r.flags &= (uint8_t)~ROW_WRAPPED;
            free(r.cells);
            r.cells = c;
            r.cols = (uint16_t)cols;
        }
        cols_ = cols;
    }
    int cy = cursor_y ? *cursor_y : rows_ - 1;
    if (rows > rows_) {
        /* Pull rows back from the scrollback first, then add blank rows
         * at the bottom. */
        while (rows_ < rows && history() > 0) {
            rows_++;
            cy++;
        }
        while (rows_ < rows) {
            rows_++;
            if (!grow_ring(len_ + 1)) {
                rows_--;
                return false;
            }
            Row& r = ring_[(start_ + len_) % cap_];
            if (!r.cells || r.cols != cols_) {
                if (!alloc_row(r, cols_)) {
                    rows_--;
                    return false;
                }
            }
            Cell b = Cell();
            fill_row(r, b);
            len_++;
        }
    } else if (rows < rows_) {
        /* Blank rows below the cursor are dropped first; the rest of the
         * cut comes off the top into the scrollback. */
        int cut = rows_ - rows;
        /* Only blank rows below the cursor may be dropped. */
        int below = 0;
        while (below < cut && rows_ - 1 - below > cy && row_blank(row(rows_ - 1 - below)))
            below++;
        if (below > 0) {
            len_ -= (size_t)below;
            rows_ -= below;
            cut -= below;
        }
        if (cut > 0) {
            rows_ -= cut;
            cy -= cut;
            trim_history();
        }
        if (cy < 0)
            cy = 0;
    }
    if (cursor_y)
        *cursor_y = cy;
    full_damage = true;
    for (int y = 0; y < rows_; y++)
        row(y).damage_all();
    return true;
}

/* ---- reflow ------------------------------------------------------------ */

namespace {

/* A growable array of rows under construction. */
struct RowList {
    Row* rows;
    size_t n, cap;
    RowList() : rows(0), n(0), cap(0) {}
    ~RowList()
    {
        for (size_t i = 0; i < n; i++)
            free(rows[i].cells);
        free(rows);
    }
    Row* add(int cols)
    {
        if (n == cap) {
            size_t ncap = cap ? cap * 2 : 64;
            Row* p = (Row*)realloc(rows, ncap * sizeof(Row));
            if (!p)
                return 0;
            rows = p;
            cap = ncap;
        }
        Row& r = rows[n];
        memset(&r, 0, sizeof(Row));
        r.cells = (Cell*)calloc((size_t)cols, sizeof(Cell));
        if (!r.cells)
            return 0;
        r.cols = (uint16_t)cols;
        r.damage_all();
        n++;
        return &r;
    }
};

void note_cell(Row& r, const Cell& c)
{
    if (c.style)
        r.flags |= ROW_HAS_STYLES;
    if (c.is_grapheme())
        r.flags |= ROW_HAS_GRAPHEMES;
    if (c.link)
        r.flags |= ROW_HAS_LINKS;
}

} /* namespace */

bool Grid::reflow(int ncols, int nrows, int* cursor_x, int* cursor_y, bool* pending)
{
    uint64_t end = base_ + len_;
    uint64_t cabs = active_top() + (uint64_t)*cursor_y;
    int cur_off_in_row = *cursor_x + (*pending ? 1 : 0);

    /* Blank rows after the cursor row carry nothing worth keeping. */
    uint64_t last = end - 1;
    while (last > cabs && row_blank(abs_row(last)) && !(abs_row(last - 1).flags & ROW_WRAPPED))
        last--;

    RowList out;
    long cur_row = -1;
    int cur_col = 0;
    bool cur_pending = false;

    Cell* line = 0;
    size_t line_cap = 0;

    uint64_t a = base_;
    while (a <= last) {
        uint64_t b = a;
        while (b < last && (abs_row(b).flags & ROW_WRAPPED) && abs_row(b).line_attr == LINE_SINGLE)
            b++;

        /* Gather the logical line a..b. */
        size_t need = (size_t)(b - a + 1) * (size_t)cols_;
        if (need > line_cap) {
            Cell* p = (Cell*)realloc(line, need * sizeof(Cell));
            if (!p) {
                free(line);
                return false;
            }
            line = p;
            line_cap = need;
        }
        size_t len = 0;
        long cur_off = -1;
        for (uint64_t r = a; r <= b; r++) {
            const Row& row = abs_row(r);
            if (r == cabs)
                cur_off = (long)len + cur_off_in_row;
            int n = row.cols;
            if (r < b && n > 0 && row.cells[n - 1].wide() == CELL_SPACER_HEAD)
                n--;
            memcpy(line + len, row.cells, (size_t)n * sizeof(Cell));
            len += (size_t)n;
        }
        /* Trailing blanks are not content. */
        while (len > 0 && line[len - 1].is_empty() && !line[len - 1].style &&
               line[len - 1].wide() != CELL_SPACER_TAIL)
            len--;

        /* Cut it at the new width. */
        Row* row = out.add(ncols);
        if (!row) {
            free(line);
            return false;
        }
        row->semantic = abs_row(a).semantic;
        row->line_attr = abs_row(a).line_attr;
        int col = 0;
        bool cursor_done = false;
        for (size_t i = 0; i < len; i++) {
            Cell c = line[i];
            if (c.wide() == CELL_SPACER_TAIL)
                continue;
            int w = c.wide() == CELL_WIDE ? 2 : 1;
            if (w == 2 && ncols < 2) {
                c = Cell();
                w = 1;
            }
            if (col + w > ncols) {
                if (col < ncols) {
                    Cell& h = row->cells[col];
                    h = Cell();
                    h.ch = (uint32_t)CELL_SPACER_HEAD << Cell::WIDE_SHIFT;
                }
                row->flags |= ROW_WRAPPED;
                row = out.add(ncols);
                if (!row) {
                    free(line);
                    return false;
                }
                row->semantic = abs_row(a).semantic;
                col = 0;
            }
            if (!cursor_done && cur_off >= 0 && (long)i == cur_off) {
                cur_row = (long)out.n - 1;
                cur_col = col;
                cursor_done = true;
            }
            row->cells[col] = c;
            note_cell(*row, c);
            if (w == 2) {
                Cell t = c;
                t.ch = (uint32_t)CELL_SPACER_TAIL << Cell::WIDE_SHIFT;
                row->cells[col + 1] = t;
            }
            col += w;
        }
        if (cur_off >= 0 && !cursor_done) {
            /* The cursor is past the end of the content. */
            long extra = cur_off - (long)len;
            cur_row = (long)out.n - 1;
            if (col + extra >= ncols) {
                if (col + extra == ncols && extra == 0 && *pending) {
                    cur_col = ncols - 1;
                    cur_pending = true;
                } else {
                    cur_col = ncols - 1;
                }
            } else {
                cur_col = col + (int)extra;
            }
        }
        a = b + 1;
    }
    free(line);

    if (cur_row < 0) {
        cur_row = out.n ? (long)out.n - 1 : 0;
        cur_col = 0;
    }

    /* The active area is the last nrows rows, padded below if short, and
     * it must contain the cursor. */
    while (out.n < (size_t)nrows) {
        if (!out.add(ncols))
            return false;
    }
    size_t top = out.n - (size_t)nrows;
    if ((size_t)cur_row < top) {
        top = (size_t)cur_row;
        while (out.n > top + (size_t)nrows) {
            out.n--;
            free(out.rows[out.n].cells);
        }
    }
    size_t dropped = 0;
    if (top > max_history_) {
        dropped = top - max_history_;
        for (size_t i = 0; i < dropped; i++)
            free(out.rows[i].cells);
    }

    /* Install the new rows. */
    for (size_t i = 0; i < cap_; i++)
        free(ring_[i].cells);
    free(ring_);
    size_t n = out.n - dropped;
    ring_ = (Row*)malloc((n ? n : 1) * sizeof(Row));
    if (!ring_) {
        cap_ = start_ = len_ = 0;
        return false;
    }
    memcpy(ring_, out.rows + dropped, n * sizeof(Row));
    out.n = 0;                           /* ownership moved */
    cap_ = n;
    start_ = 0;
    len_ = n;
    base_ += dropped;
    cols_ = ncols;
    rows_ = nrows;
    full_damage = true;

    *cursor_x = cur_col;
    *cursor_y = (int)((size_t)cur_row - top);
    *pending = cur_pending;
    return true;
}

void Grid::compact_graphemes()
{
    GraphemePool np;
    for (size_t i = 0; i < len_; i++) {
        Row& r = ring_[(start_ + i) % cap_];
        if (!(r.flags & ROW_HAS_GRAPHEMES))
            continue;
        bool any = false;
        for (int x = 0; x < r.cols; x++) {
            Cell& c = r.cells[x];
            if (!c.is_grapheme())
                continue;
            const uint32_t* cps = 0;
            int n = graphemes_.get(c.cp(), &cps);
            uint32_t idx = np.add(cps, n);
            c.set_grapheme(idx);
            any = true;
        }
        if (!any)
            r.flags &= (uint8_t)~ROW_HAS_GRAPHEMES;
    }
    graphemes_.swap(np);
}

} /* namespace tl */
