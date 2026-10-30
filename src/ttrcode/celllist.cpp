#include "celllist.h"

#include <stdlib.h>
#include <string.h>
#include <X11/keysym.h>
#include <Xm/Form.h>
#include <Xm/ScrollBar.h>

namespace ttrcode {

namespace {

/* Two clicks this close (ms) on one row are a double click. */
const unsigned long k_double_click = 400;
/* The tree: two cells for each level, and the + or - with a space. */
const int k_indent = 2;

void input_cb(Widget, XtPointer data, XtPointer call)
{
    ((CellList*)data)->key((TwGridCallbackStruct*)call);
}

void pointer_cb(Widget, XtPointer data, XtPointer call)
{
    ((CellList*)data)->pointer((TwGridCallbackStruct*)call);
}

void resize_cb(Widget, XtPointer data, XtPointer call)
{
    TwGridCallbackStruct* cb = (TwGridCallbackStruct*)call;
    ((CellList*)data)->resized(cb->cols, cb->rows);
}

void focus_cb(Widget, XtPointer data, XtPointer call)
{
    ((CellList*)data)->focused(((TwGridCallbackStruct*)call)->reason == TW_CR_FOCUS_IN);
}

void destroyed_cb(Widget, XtPointer data, XtPointer)
{
    ((CellList*)data)->forget_widgets();
}

void scroll_cb(Widget, XtPointer data, XtPointer call)
{
    ((CellList*)data)->scrolled(((XmScrollBarCallbackStruct*)call)->value);
}

tw::CellStyle style(uint32_t fg, uint32_t bg)
{
    tw::CellStyle s;
    s.fg = fg;
    s.bg = bg;
    s.deco = fg;
    s.flags = 0;
    return s;
}

/* The next code point of UTF-8 text (bad bytes as themselves). */
uint32_t next_cp(const char** p)
{
    const unsigned char* s = (const unsigned char*)*p;
    uint32_t c = *s;
    int n = c < 0x80 ? 0 : (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : 0;
    if (n) {
        uint32_t v = c & (0x3f >> n);
        int i;
        for (i = 1; i <= n; i++) {
            if ((s[i] & 0xc0) != 0x80) {
                n = 0;
                v = c;
                break;
            }
            v = (v << 6) | (s[i] & 0x3f);
        }
        c = v;
    }
    *p = (const char*)(s + 1 + n);
    return c;
}

} /* namespace */

CellList::CellList(Widget parent, const char* name, CellListHost* host, ArgList grid_args,
                   Cardinal n)
    : host_(host), ncols_(0), marks_(false), tree_(false), tag_(0), sel_(-1), top_(0), cols_(1), nrows_(1),
      dirty_(true), focused_(false), edit_row_(-1), edit_col_(0), edit_len_(0), edit_pos_(0),
      drag_col_(-1), drag_x_(0), drag_w_(0), user_sized_(false), last_click_(0),
      last_click_row_(-1)
{
    colors_.text = 0x000000;
    colors_.background = 0xffffff;
    colors_.selected_text = 0xffffff;
    colors_.selected_background = 0x000080;
    colors_.header_text = 0x000000;
    colors_.header_background = 0xd4d0c8;
    colors_.lines = 0xa0a0a0;
    colors_.changed = 0xd00000;
    colors_.dim = 0x808080;
    colors_.cursor = 0x000000;
    int i;
    for (i = 0; i < LIST_COLUMNS; i++)
        width_[i] = 20;
    edit_[0] = 0;
    form_ = XtVaCreateWidget(name, xmFormWidgetClass, parent, (char*)0);
    scroll_ = XtVaCreateManagedWidget("scrollBar", xmScrollBarWidgetClass, form_,
                                      XmNorientation, XmVERTICAL, XmNtraversalOn, False,
                                      XmNtopAttachment, XmATTACH_FORM,
                                      XmNrightAttachment, XmATTACH_FORM,
                                      XmNbottomAttachment, XmATTACH_FORM,
                                      XmNminimum, 0, XmNmaximum, 1, XmNsliderSize, 1, (char*)0);
    XtAddCallback(scroll_, XmNvalueChangedCallback, scroll_cb, (XtPointer)this);
    XtAddCallback(scroll_, XmNdragCallback, scroll_cb, (XtPointer)this);
    Arg args[24];
    Cardinal k = 0;
    for (i = 0; i < (int)n && k < 16; i++)
        args[k++] = grid_args[i];
    XtSetArg(args[k], TwNsource, (tw::Source*)this); k++;
    XtSetArg(args[k], TwNcolumns, 40); k++;
    XtSetArg(args[k], TwNrows, 6); k++;
    XtSetArg(args[k], XmNtopAttachment, XmATTACH_FORM); k++;
    XtSetArg(args[k], XmNleftAttachment, XmATTACH_FORM); k++;
    XtSetArg(args[k], XmNbottomAttachment, XmATTACH_FORM); k++;
    XtSetArg(args[k], XmNrightAttachment, XmATTACH_WIDGET); k++;
    XtSetArg(args[k], XmNrightWidget, scroll_); k++;
    grid_ = TwCreateGrid(form_, "list", args, k);
    XtAddCallback(grid_, TwNinputCallback, input_cb, (XtPointer)this);
    XtAddCallback(grid_, TwNpointerCallback, pointer_cb, (XtPointer)this);
    XtAddCallback(grid_, TwNresizeCallback, resize_cb, (XtPointer)this);
    XtAddCallback(grid_, TwNfocusCallback, focus_cb, (XtPointer)this);
    XtManageChild(grid_);
    XtManageChild(form_);
    XtAddCallback(form_, XmNdestroyCallback, destroyed_cb, (XtPointer)this);
}

CellList::~CellList()
{
    if (form_) {
        XtRemoveCallback(form_, XmNdestroyCallback, destroyed_cb, (XtPointer)this);
        XtDestroyWidget(form_);
    }
}

void CellList::forget_widgets()
{
    form_ = grid_ = scroll_ = 0;
}

void CellList::damage()
{
    dirty_ = true;
    if (grid_)
        TwGridDamage(grid_);
}

void CellList::set_colors(const ListColors& c)
{
    colors_ = c;
    damage();
}

void CellList::set_columns(int n, const char* const* titles, const int* widths)
{
    if (n > LIST_COLUMNS)
        n = LIST_COLUMNS;
    ncols_ = n;
    int i;
    for (i = 0; i < n; i++) {
        titles_[i] = titles ? titles[i] : "";
        width_[i] = widths ? widths[i] : 20;
    }
    damage();
}

void CellList::set_marks(bool on)
{
    marks_ = on;
    damage();
}

void CellList::clear()
{
    rows_.clear();
}

int CellList::add_row(int depth, int expand)
{
    Row r;
    r.depth = depth;
    r.expand = expand;
    int i;
    for (i = 0; i < LIST_COLUMNS; i++)
        r.color[i] = 0;
    r.mark = 0;
    r.mark_color = 0;
    rows_.push_back(r);
    return rows_.size() - 1;
}

void CellList::set_cell(int row, int col, const char* text, uint32_t color)
{
    if (row < 0 || row >= rows_.size() || col < 0 || col >= LIST_COLUMNS)
        return;
    rows_[row].text[col] = text ? text : "";
    rows_[row].color[col] = color;
}

void CellList::set_mark(int row, uint32_t cp, uint32_t color)
{
    if (row < 0 || row >= rows_.size())
        return;
    rows_[row].mark = cp;
    rows_[row].mark_color = color;
}

void CellList::done()
{
    tree_ = false;
    int i;
    for (i = 0; i < rows_.size() && !tree_; i++)
        tree_ = rows_[i].depth > 0 || rows_[i].expand != EXPAND_NONE;
    if (sel_ >= rows_.size())
        sel_ = rows_.size() - 1;
    if (sel_ < 0 && !rows_.empty())
        sel_ = 0;
    if (edit_row_ >= rows_.size())
        edit_row_ = -1;
    int vis = visible_rows();
    if (top_ > rows_.size() - vis)
        top_ = rows_.size() - vis;
    if (top_ < 0)
        top_ = 0;
    update_scrollbar();
    damage();
}

void CellList::select(int row)
{
    if (row < 0 || row >= rows_.size())
        return;
    sel_ = row;
    make_visible(row);
    damage();
}

int CellList::visible_rows() const
{
    int v = nrows_ - header_rows();
    return v > 0 ? v : 1;
}

void CellList::make_visible(int row)
{
    int vis = visible_rows();
    if (row < top_)
        top_ = row;
    else if (row >= top_ + vis)
        top_ = row - vis + 1;
    if (top_ < 0)
        top_ = 0;
    update_scrollbar();
}

void CellList::update_scrollbar()
{
    if (!scroll_)
        return;
    int vis = visible_rows();
    int max = rows_.size() > vis ? rows_.size() : vis;
    if (max < 1)
        max = 1;
    int v = top_ > max - vis ? max - vis : top_;
    if (v < 0)
        v = 0;
    XtVaSetValues(scroll_, XmNmaximum, max, XmNsliderSize, vis, XmNvalue, v,
                  XmNpageIncrement, vis > 1 ? vis - 1 : 1, (char*)0);
}

void CellList::scrolled(int value)
{
    top_ = value;
    damage();
}

void CellList::resized(int cols, int rows)
{
    bool changed = cols != cols_ || rows != nrows_;
    cols_ = cols > 0 ? cols : 1;
    nrows_ = rows > 0 ? rows : 1;
    if (sel_ >= 0)
        make_visible(sel_);
    else
        update_scrollbar();
    damage();
    if (changed && host_)
        host_->list_resized(this);
}

void CellList::focused(bool in)
{
    focused_ = in;
    if (!in && edit_row_ >= 0) {
        /* Leaving an edit keeps it, as clicking elsewhere does in VC6. */
        int row = edit_row_;
        edit_row_ = -1;
        edit_[edit_len_] = 0;
        char copy[512];
        strcpy(copy, edit_);
        if (host_)
            host_->list_edited(this, row, copy);
    }
    if (host_)
        host_->list_focus(this, in);
    damage();
}

void CellList::focus()
{
    if (grid_)
        XmProcessTraversal(grid_, XmTRAVERSE_CURRENT);
}

int CellList::column_x(int c) const
{
    int x = marks_ ? 2 : 0;
    int i;
    for (i = 0; i < c && i < ncols_; i++)
        x += width_[i];
    return x;
}

int CellList::row_at(int y) const
{
    int r = y - header_rows();
    if (r < 0)
        return -1;
    r += top_;
    return r < rows_.size() ? r : -1;
}

void CellList::begin_edit(int row, int col, const char* text)
{
    if (row < 0 || row >= rows_.size())
        return;
    edit_row_ = row;
    edit_col_ = col;
    size_t n = strlen(text);
    if (n >= sizeof(edit_))
        n = sizeof(edit_) - 1;
    memcpy(edit_, text, n);
    edit_[n] = 0;
    edit_len_ = (int)n;
    edit_pos_ = (int)n;
    select(row);
}

void CellList::cancel_edit()
{
    edit_row_ = -1;
    damage();
}

/* ---- input ---- */

void CellList::key(TwGridCallbackStruct* cb)
{
    KeySym ks = cb->keysym;
    if (ks == XK_Shift_L || ks == XK_Shift_R || ks == XK_Control_L || ks == XK_Control_R ||
        ks == XK_Alt_L || ks == XK_Alt_R || ks == XK_Meta_L || ks == XK_Meta_R)
        return;
    if (edit_row_ >= 0) {
        bool ctrl = (cb->state & ControlMask) != 0;
        switch (ks) {
        case XK_Return:
        case XK_KP_Enter: {
            int row = edit_row_;
            edit_row_ = -1;
            edit_[edit_len_] = 0;
            char copy[512];
            strcpy(copy, edit_);
            damage();
            if (host_)
                host_->list_edited(this, row, copy);
            return;
        }
        case XK_Escape:
            cancel_edit();
            return;
        case XK_BackSpace:
            if (edit_pos_ > 0) {
                memmove(edit_ + edit_pos_ - 1, edit_ + edit_pos_, (size_t)(edit_len_ - edit_pos_));
                edit_pos_--;
                edit_len_--;
            }
            break;
        case XK_Delete:
        case XK_KP_Delete:
            if (edit_pos_ < edit_len_) {
                memmove(edit_ + edit_pos_, edit_ + edit_pos_ + 1,
                        (size_t)(edit_len_ - edit_pos_ - 1));
                edit_len_--;
            }
            break;
        case XK_Left:
        case XK_KP_Left:
            if (edit_pos_ > 0)
                edit_pos_--;
            break;
        case XK_Right:
        case XK_KP_Right:
            if (edit_pos_ < edit_len_)
                edit_pos_++;
            break;
        case XK_Home:
        case XK_KP_Home:
            edit_pos_ = 0;
            break;
        case XK_End:
        case XK_KP_End:
            edit_pos_ = edit_len_;
            break;
        default:
            if (!ctrl && cb->text_len > 0 && (unsigned char)cb->text[0] >= 32 &&
                edit_len_ + cb->text_len < (int)sizeof(edit_) - 1) {
                memmove(edit_ + edit_pos_ + cb->text_len, edit_ + edit_pos_,
                        (size_t)(edit_len_ - edit_pos_));
                memcpy(edit_ + edit_pos_, cb->text, (size_t)cb->text_len);
                edit_pos_ += cb->text_len;
                edit_len_ += cb->text_len;
            } else if (host_) {
                host_->list_key(this, cb);
                return;
            }
            break;
        }
        edit_[edit_len_] = 0;
        damage();
        return;
    }
    if (host_ && host_->list_key(this, cb))
        return;
    int n = rows_.size();
    if (!n)
        return;
    int vis = visible_rows();
    int s = sel_ < 0 ? 0 : sel_;
    switch (ks) {
    case XK_Up:
    case XK_KP_Up:
        select(s > 0 ? s - 1 : 0);
        return;
    case XK_Down:
    case XK_KP_Down:
        select(s + 1 < n ? s + 1 : n - 1);
        return;
    case XK_Prior:
    case XK_KP_Prior:
        select(s - vis > 0 ? s - vis : 0);
        return;
    case XK_Next:
    case XK_KP_Next:
        select(s + vis < n ? s + vis : n - 1);
        return;
    case XK_Home:
    case XK_KP_Home:
        select(0);
        return;
    case XK_End:
    case XK_KP_End:
        select(n - 1);
        return;
    case XK_Left:
    case XK_KP_Left:
    case XK_minus:
    case XK_KP_Subtract:
        if (rows_[s].expand == EXPAND_OPEN) {
            if (host_)
                host_->list_toggle(this, s);
        } else if (ks == XK_Left || ks == XK_KP_Left) {
            /* To the parent. */
            int d = rows_[s].depth, i;
            for (i = s - 1; i >= 0; i--)
                if (rows_[i].depth < d) {
                    select(i);
                    break;
                }
        }
        return;
    case XK_Right:
    case XK_KP_Right:
    case XK_plus:
    case XK_KP_Add:
        if (rows_[s].expand == EXPAND_CLOSED && host_)
            host_->list_toggle(this, s);
        return;
    case XK_Return:
    case XK_KP_Enter:
        if (host_)
            host_->list_activate(this, s);
        return;
    default:
        return;
    }
}

void CellList::pointer(TwGridCallbackStruct* cb)
{
    if (cb->reason == TW_CR_BUTTON_PRESS && (cb->button == 4 || cb->button == 5)) {
        int vis = visible_rows();
        int max = rows_.size() - vis;
        top_ += cb->button == 4 ? -3 : 3;
        if (top_ > max)
            top_ = max;
        if (top_ < 0)
            top_ = 0;
        update_scrollbar();
        damage();
        return;
    }
    if (cb->reason == TW_CR_BUTTON_RELEASE) {
        drag_col_ = -1;
        return;
    }
    if (cb->reason == TW_CR_MOTION) {
        if (drag_col_ >= 0) {
            int w = drag_w_ + cb->col - drag_x_;
            if (w < 4)
                w = 4;
            if (w != width_[drag_col_]) {
                width_[drag_col_] = w;
                user_sized_ = true;
                damage();
            }
        }
        return;
    }
    if (cb->reason != TW_CR_BUTTON_PRESS || cb->button != 1)
        return;
    focus();
    if (header_rows() && cb->row == 0) {
        /* On a separator: drag it. */
        int c;
        for (c = 1; c < ncols_; c++) {
            int x = column_x(c);
            if (cb->col >= x - 1 && cb->col <= x) {
                drag_col_ = c - 1;
                drag_x_ = cb->col;
                drag_w_ = width_[c - 1];
                return;
            }
        }
        return;
    }
    int r = row_at(cb->row);
    if (r < 0)
        return;
    Time t = cb->event ? cb->event->xbutton.time : 0;
    bool dbl = r == last_click_row_ && t - last_click_ < k_double_click;
    last_click_ = t;
    last_click_row_ = dbl ? -1 : r;
    select(r);
    const Row& row = rows_[r];
    int ex = column_x(0) + row.depth * k_indent;
    if (row.expand != EXPAND_NONE && cb->col >= ex && cb->col <= ex + 1) {
        if (host_)
            host_->list_toggle(this, r);
        return;
    }
    if (dbl && host_) {
        if (row.expand != EXPAND_NONE && cb->col < column_x(1))
            host_->list_toggle(this, r);
        else
            host_->list_activate(this, r);
    }
}

/* ---- drawing ---- */

bool CellList::row_dirty(int y, int* lo, int* hi) const
{
    (void)y;
    if (!dirty_)
        return false;
    *lo = 0;
    *hi = cols_ - 1;
    return true;
}

tw::Cursor CellList::cursor() const
{
    tw::Cursor c;
    c.x = 0;
    c.y = 0;
    c.visible = false;
    c.blink = true;
    c.wide = false;
    c.shape = tw::CURSOR_BAR;
    if (edit_row_ >= top_ && edit_row_ < top_ + visible_rows() && focused_) {
        const Row& r = rows_[edit_row_];
        int x = column_x(edit_col_) + (edit_col_ > 0 || !tree_ ? 1 : 0);
        if (edit_col_ == 0 && tree_)
            x += r.depth * k_indent + k_indent;
        /* The caret's cell: count code points before it. */
        const char* p = edit_;
        const char* end = edit_ + edit_pos_;
        int n = 0;
        while (p < end && *p) {
            next_cp(&p);
            n++;
        }
        c.x = x + 1 + n;
        c.y = edit_row_ - top_ + header_rows();
        if (c.x < cols_)
            c.visible = true;
    }
    return c;
}

void CellList::cells(int y, int lo, int hi, tw::CellData* out) const
{
    int n = hi - lo + 1;
    if (n <= 0)
        return;
    /* The whole row, then the part asked for. */
    tw::CellData* line = (tw::CellData*)calloc((size_t)cols_, sizeof(tw::CellData));
    if (!line)
        return;
    tw::CellStyle plain = style(colors_.text, colors_.background);
    tw::CellStyle head = style(colors_.header_text, colors_.header_background);
    bool header = header_rows() && y == 0;
    int r = header ? -1 : row_at(y);
    bool sel = r >= 0 && r == sel_;
    tw::CellStyle base = header ? head : plain;
    if (sel) {
        base = focused_ ? style(colors_.selected_text, colors_.selected_background)
                        : style(colors_.text, colors_.header_background);
    }
    int x;
    for (x = 0; x < cols_; x++) {
        line[x].cp = ' ';
        line[x].width = 1;
        line[x].style = base;
    }
    int nc = ncols_ > 0 ? ncols_ : 1;
    int c;
    for (c = 0; c < nc; c++) {
        int start = column_x(c);
        int end = c + 1 < nc ? column_x(c + 1) : cols_;
        if (start >= cols_)
            break;
        if (end > cols_)
            end = cols_;
        if (c > 0) {
            line[start].style.flags |= tw::S_LEFT_LINE;
            line[start].style.deco = colors_.lines;
        }
        const char* text = 0;
        uint32_t color = 0;
        /* A space after each separator; a tree's first column starts
         * with its indentation and + or - instead. */
        int at = start + (c > 0 || header || !tree_ ? 1 : 0);
        if (header) {
            text = titles_[c].c_str();
        } else if (r >= 0) {
            const Row& row = rows_[r];
            if (c == 0 && tree_) {
                at = start + row.depth * k_indent;
                if (row.expand != EXPAND_NONE && at < end) {
                    line[at].cp = row.expand == EXPAND_OPEN ? '-' : '+';
                    if (!sel)
                        line[at].style.fg = colors_.dim;
                }
                at += k_indent;
            }
            if (r == edit_row_ && c == edit_col_) {
                text = edit_;
                /* Inside a box of the selection's colours. */
                at++;
            } else {
                text = row.text[c].c_str();
                color = row.color[c];
            }
        }
        if (!text)
            continue;
        const char* p = text;
        uint32_t cur = color;
        while (*p && at < end - (c + 1 < nc ? 1 : 0)) {
            uint32_t cp = next_cp(&p);
            if (cp == (uint32_t)LIST_CHANGED[0] || cp == (uint32_t)LIST_PLAIN[0]) {
                cur = cp == (uint32_t)LIST_CHANGED[0] ? colors_.changed : color;
                continue;
            }
            if (cp < 32)
                cp = ' ';
            line[at].cp = cp;
            if (cur && !sel)
                line[at].style.fg = cur;
            at++;
        }
    }
    if (!header && r >= 0 && marks_ && rows_[r].mark) {
        line[0].cp = rows_[r].mark;
        line[0].style.fg = rows_[r].mark_color;
    }
    for (x = 0; x < n; x++)
        out[x] = lo + x < cols_ ? line[lo + x] : line[cols_ - 1];
    free(line);
}

} /* namespace ttrcode */
