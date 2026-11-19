/* ttrv's file list (filelist.h). */
#include "filelist.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <Xm/DrawingA.h>
#include <Xm/Form.h>
#include <Xm/ScrollBar.h>

namespace ttrv {

namespace {

const char* const k_titles[SORT_COUNT] = { "Name", "Size", "Image", "Type", "Modified" };

void expose_cb(Widget, XtPointer closure, XtPointer)
{
    ((FileList*)closure)->exposed();
}

void resize_cb(Widget, XtPointer closure, XtPointer)
{
    ((FileList*)closure)->resized();
}

void scroll_cb(Widget, XtPointer closure, XtPointer call)
{
    ((FileList*)closure)->scrolled(((XmScrollBarCallbackStruct*)call)->value);
}

void event_cb(Widget, XtPointer closure, XEvent* ev, Boolean*)
{
    FileList* l = (FileList*)closure;
    if (ev->type == KeyPress)
        l->key(&ev->xkey);
    else if (ev->type == ButtonPress)
        l->button(&ev->xbutton);
}

} /* namespace */

int natural_compare(const char* a, const char* b)
{
    while (*a && *b) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            while (*a == '0')
                a++;
            while (*b == '0')
                b++;
            const char* ea = a;
            const char* eb = b;
            while (isdigit((unsigned char)*ea))
                ea++;
            while (isdigit((unsigned char)*eb))
                eb++;
            if (ea - a != eb - b)
                return (int)(ea - a) - (int)(eb - b);
            int c = strncmp(a, b, (size_t)(ea - a));
            if (c)
                return c;
            a = ea;
            b = eb;
            continue;
        }
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb)
            return ca - cb;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

namespace {

int natural_cmp(const char* a, const char* b)
{
    return natural_compare(a, b);
}

const char* extension(const char* name)
{
    const char* dot = strrchr(name, '.');
    return dot && dot != name ? dot + 1 : "";
}

SortKey g_key;
bool g_reverse;

int compare_rows(const void* pa, const void* pb)
{
    const Row* a = *(const Row* const*)pa;
    const Row* b = *(const Row* const*)pb;
    /* ".." first, then folders, always. */
    if (a->up != b->up)
        return a->up ? -1 : 1;
    if (a->dir != b->dir)
        return a->dir ? -1 : 1;
    int c = 0;
    switch (g_key) {
    case SORT_SIZE:
        c = a->st.size < b->st.size ? -1 : a->st.size > b->st.size ? 1 : 0;
        break;
    case SORT_IMAGE: {
        double pa2 = (double)a->info.width * a->info.height;
        double pb2 = (double)b->info.width * b->info.height;
        c = pa2 < pb2 ? -1 : pa2 > pb2 ? 1 : 0;
        break;
    }
    case SORT_TYPE:
        c = natural_cmp(extension(a->name.c_str()), extension(b->name.c_str()));
        break;
    case SORT_TIME:
        c = a->st.mtime < b->st.mtime ? -1 : a->st.mtime > b->st.mtime ? 1 : 0;
        break;
    default:
        break;
    }
    if (c == 0)
        c = natural_cmp(a->name.c_str(), b->name.c_str());
    return g_reverse ? -c : c;
}

} /* namespace */

void format_size(uint64_t n, char* buf, size_t len)
{
    if (n < 1024)
        snprintf(buf, len, "%lu B", (unsigned long)n);
    else if (n < 1024UL * 1024UL)
        snprintf(buf, len, "%lu KB", (unsigned long)((n + 1023) / 1024));
    else if (n < (uint64_t)1024 * 1024 * 1024)
        snprintf(buf, len, "%.1f MB", (double)n / (1024.0 * 1024.0));
    else
        snprintf(buf, len, "%.1f GB", (double)n / (1024.0 * 1024.0 * 1024.0));
}

void format_image(const ti::Info& in, char* buf, size_t len)
{
    if (!in.width)
        buf[0] = 0;
    else
        snprintf(buf, len, "%dx%dx%d", in.width, in.height, in.colors ? in.colors : in.bits);
}

bool image_name(const char* name)
{
    static const char* const exts[] = {
        "png", "jpg", "jpeg", "jpe", "jfif", "gif", "tif", "tiff", "bmp", "dib", "ppm", "pgm",
        "pnm", "pbm", "rgb", "rgba", "bw", "sgi", "int", "inta", "xpm", 0
    };
    const char* e = extension(name);
    for (int i = 0; exts[i]; i++) {
        const char* a = e;
        const char* b = exts[i];
        while (*a && *b && tolower((unsigned char)*a) == *b) {
            a++;
            b++;
        }
        if (!*a && !*b)
            return true;
    }
    return false;
}

FileList::FileList(Widget parent, const char* name, FileListHost* host, const ListColors& c,
                   const char* font)
    : host_(host), dpy_(XtDisplay(parent)), c_(c), font_(0), gc_(0), buf_(0), buf_w_(0),
      buf_h_(0), cursor_(0), top_(0), row_h_(16), header_h_(20), key_(SORT_NAME),
      reverse_(false), typed_at_(0), click_at_(0), click_row_(-1), vis_first_(-1),
      vis_last_(-1), mode_(VIEW_DETAILS), thumb_(128), cols_(1), cell_w_(144), cell_h_(170),
      gap_(0), fmt_ready_(false), pix_clock_(0)
{
    typed_[0] = 0;
    form_ = XmCreateForm(parent, (char*)name, 0, 0);
    Arg a[8];
    int n = 0;
    XtSetArg(a[n], XmNorientation, XmVERTICAL); n++;
    XtSetArg(a[n], XmNtopAttachment, XmATTACH_FORM); n++;
    XtSetArg(a[n], XmNbottomAttachment, XmATTACH_FORM); n++;
    XtSetArg(a[n], XmNrightAttachment, XmATTACH_FORM); n++;
    XtSetArg(a[n], XmNtraversalOn, False); n++;
    sb_ = XmCreateScrollBar(form_, (char*)"scroll", a, n);
    XtAddCallback(sb_, XmNvalueChangedCallback, scroll_cb, this);
    XtAddCallback(sb_, XmNdragCallback, scroll_cb, this);
    XtManageChild(sb_);
    n = 0;
    XtSetArg(a[n], XmNtopAttachment, XmATTACH_FORM); n++;
    XtSetArg(a[n], XmNbottomAttachment, XmATTACH_FORM); n++;
    XtSetArg(a[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(a[n], XmNrightAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(a[n], XmNrightWidget, sb_); n++;
    XtSetArg(a[n], XmNresizePolicy, XmRESIZE_NONE); n++;
    XtSetArg(a[n], XmNtraversalOn, True); n++;
    XtSetArg(a[n], XmNbackground, c.bg); n++;
    da_ = XmCreateDrawingArea(form_, (char*)"files", a, n);
    XtAddCallback(da_, XmNexposeCallback, expose_cb, this);
    XtAddCallback(da_, XmNresizeCallback, resize_cb, this);
    XtAddEventHandler(da_, KeyPressMask | ButtonPressMask, False, event_cb, this);
    XtManageChild(da_);
    XtManageChild(form_);

    font_ = XLoadQueryFont(dpy_, font && *font ? font
                                               : "-*-helvetica-medium-r-normal--12-*-*-*-*-*-iso8859-1");
    if (!font_)
        font_ = XLoadQueryFont(dpy_, "fixed");
    int fh = font_ ? font_->ascent + font_->descent : 13;
    row_h_ = fh + 4 < 18 ? 18 : fh + 4;
    header_h_ = fh + 7;
}

FileList::~FileList()
{
    forget_thumbs();
    conv_.destroy();
    fmt_.release();
    clear();
    if (buf_)
        XFreePixmap(dpy_, buf_);
    if (gc_)
        XFreeGC(dpy_, gc_);
    if (font_)
        XFreeFont(dpy_, font_);
}

void FileList::clear()
{
    for (int i = 0; i < rows_.size(); i++)
        delete rows_[i];
    rows_.clear();
    cursor_ = top_ = 0;
    vis_first_ = vis_last_ = -1;
}

void FileList::set_colors(const ListColors& c)
{
    c_ = c;
    XtVaSetValues(da_, XmNbackground, c.bg, (char*)0);
    paint();
}

bool FileList::ensure_gc()
{
    if (gc_)
        return true;
    if (!XtIsRealized(da_))
        return false;
    XGCValues v;
    v.graphics_exposures = False;
    gc_ = XCreateGC(dpy_, XtWindow(da_), GCGraphicsExposures, &v);
    if (font_)
        XSetFont(dpy_, gc_, font_->fid);
    return true;
}

void FileList::sort_rows()
{
    int n = rows_.size();
    if (n < 2)
        return;
    Row** a = (Row**)malloc(sizeof(Row*) * (size_t)n);
    if (!a)
        return;
    for (int i = 0; i < n; i++)
        a[i] = rows_[i];
    g_key = key_;
    g_reverse = reverse_;
    qsort(a, (size_t)n, sizeof(Row*), compare_rows);
    for (int i = 0; i < n; i++)
        rows_[i] = a[i];
    free(a);
}

int FileList::find(const char* name) const
{
    for (int i = 0; i < rows_.size(); i++)
        if (strcmp(rows_[i]->name.c_str(), name) == 0)
            return i;
    return -1;
}

void FileList::rows_changed(const char* keep)
{
    ttr::Str was(keep ? keep : (row(cursor_) ? row(cursor_)->name.c_str() : ""));
    sort_rows();
    int at = was.empty() ? -1 : find(was.c_str());
    cursor_ = at >= 0 ? at : (cursor_ < rows_.size() ? cursor_ : rows_.size() - 1);
    if (cursor_ < 0)
        cursor_ = 0;
    if (at < 0 && keep)
        top_ = 0;
    layout();
    scroll_to_cursor();
    paint();
    tell_visible();
    if (host_)
        host_->list_cursor(this, cursor_);
}

void FileList::row_changed(int i)
{
    int f, l;
    visible(&f, &l);
    if (i >= f && i <= l)
        paint();
}

void FileList::set_mode(ViewMode m, int size)
{
    if (size < 48)
        size = 48;
    if (size > 256)
        size = 256;
    if (m == mode_ && size == thumb_)
        return;
    mode_ = m;
    if (size != thumb_) {
        thumb_ = size;
        forget_thumbs();
        conv_.destroy();
    }
    top_ = 0;
    vis_first_ = vis_last_ = -1;
    layout();
    scroll_to_cursor();
    paint();
    tell_visible();
}

void FileList::forget_thumbs()
{
    for (int i = 0; i < pix_.size(); i++) {
        XFreePixmap(dpy_, pix_[i]->pm);
        delete pix_[i];
    }
    pix_.clear();
}

/* Row i's thumbnail as a pixmap of the window's depth, converted once
 * (dithered on an 8-bit screen) and kept while it is used. */
Pixmap FileList::thumb_pixmap(int i, int* w, int* h)
{
    const ti::Image* im = host_ ? host_->list_thumb(this, i) : 0;
    if (!im || im->empty())
        return 0;
    for (int k = 0; k < pix_.size(); k++) {
        ThumbPix* t = pix_[k];
        if (t->im == im && t->rgba == im->rgba && t->w == im->w && t->h == im->h) {
            t->used = ++pix_clock_;
            *w = t->w;
            *h = t->h;
            return t->pm;
        }
    }
    int depth = 0;
    Colormap cmap = 0;
    XtVaGetValues(da_, XmNdepth, &depth, XmNcolormap, &cmap, (char*)0);
    if (!fmt_ready_) {
        Widget sh = da_;
        while (XtParent(sh) && !XtIsShell(sh))
            sh = XtParent(sh);
        Visual* vis = 0;
        XtVaGetValues(sh, XmNvisual, &vis, (char*)0);
        if (!vis)
            vis = DefaultVisualOfScreen(XtScreen(da_));
        if (!fmt_.init(dpy_, vis, cmap, depth))
            return 0;
        fmt_ready_ = true;
    }
    if (conv_.width() < im->w || conv_.height() < im->h) {
        Widget sh = da_;
        while (XtParent(sh) && !XtIsShell(sh))
            sh = XtParent(sh);
        Visual* vis = 0;
        XtVaGetValues(sh, XmNvisual, &vis, (char*)0);
        if (!vis)
            vis = DefaultVisualOfScreen(XtScreen(da_));
        int cw = thumb_ > im->w ? thumb_ : im->w, ch = thumb_ > im->h ? thumb_ : im->h;
        if (!conv_.create(dpy_, vis, depth, cw, ch))
            return 0;
    }
    fmt_.convert(*im, c_.cell_rgb, &conv_, 0, 0);
    Pixmap pm = XCreatePixmap(dpy_, XtWindow(da_), (unsigned)im->w, (unsigned)im->h,
                              (unsigned)depth);
    conv_.put(pm, gc_, 0, 0, 0, 0, im->w, im->h);
    conv_.wait();
    ThumbPix* t = new ThumbPix;
    t->im = im;
    t->rgba = im->rgba;
    t->pm = pm;
    t->w = im->w;
    t->h = im->h;
    t->used = ++pix_clock_;
    pix_.push_back(t);
    /* At most a few screens of them on the server. */
    while (pix_.size() > 400) {
        int lru = 0;
        for (int k = 1; k < pix_.size(); k++)
            if (pix_[k]->used < pix_[lru]->used)
                lru = k;
        XFreePixmap(dpy_, pix_[lru]->pm);
        delete pix_[lru];
        pix_.remove_at(lru);
    }
    *w = im->w;
    *h = im->h;
    return pm;
}

void FileList::sort_by(SortKey k, bool reverse)
{
    key_ = k;
    reverse_ = reverse;
    rows_changed(0);
}

/* Lines: a row in the details, a row of cells in the thumbnails. top_
 * is the first line in sight. */
int FileList::per_line() const
{
    return mode_ == VIEW_THUMBS ? cols_ : 1;
}

int FileList::line_h() const
{
    return mode_ == VIEW_THUMBS ? cell_h_ : row_h_;
}

int FileList::body_top() const
{
    return mode_ == VIEW_THUMBS ? 0 : header_h_;
}

int FileList::page_lines() const
{
    Dimension dh = 0;
    XtVaGetValues(da_, XmNheight, &dh, (char*)0);
    int n = ((int)dh - body_top()) / line_h();
    return n < 1 ? 1 : n;
}

int FileList::lines() const
{
    int per = per_line();
    return (rows_.size() + per - 1) / per;
}

void FileList::visible(int* first, int* last) const
{
    int per = per_line();
    *first = top_ * per;
    *last = (top_ + page_lines() + 1) * per - 1;
    if (*last >= rows_.size())
        *last = rows_.size() - 1;
}

int FileList::marked_count() const
{
    int n = 0;
    for (int i = 0; i < rows_.size(); i++)
        if (rows_[i]->marked)
            n++;
    return n;
}

void FileList::tell_visible()
{
    int f, l;
    visible(&f, &l);
    if (f == vis_first_ && l == vis_last_)
        return;
    vis_first_ = f;
    vis_last_ = l;
    if (host_)
        host_->list_visible(this, f, l);
}

/* The columns: Name takes what the others leave. */
void FileList::layout()
{
    Dimension dw = 0;
    XtVaGetValues(da_, XmNwidth, &dw, (char*)0);
    int w = dw;
    int cw = font_ ? XTextWidth(font_, "0", 1) : 7;
    int size_w = cw * 9, image_w = cw * 15, type_w = cw * 8, time_w = cw * 17;
    int name_w = w - size_w - image_w - type_w - time_w;
    if (name_w < cw * 16)
        name_w = cw * 16;
    col_x_[SORT_NAME] = 0;
    col_x_[SORT_SIZE] = name_w;
    col_x_[SORT_IMAGE] = col_x_[SORT_SIZE] + size_w;
    col_x_[SORT_TYPE] = col_x_[SORT_IMAGE] + image_w;
    col_x_[SORT_TIME] = col_x_[SORT_TYPE] + type_w;
    col_x_[SORT_COUNT] = col_x_[SORT_TIME] + time_w;
    /* The cells: as many across as fit, the room left shared out. */
    int fh = font_ ? font_->ascent + font_->descent : 13;
    cell_w_ = thumb_ + 16;
    cell_h_ = thumb_ + 14 + 2 * (fh + 3);
    cols_ = w / cell_w_;
    if (cols_ < 1)
        cols_ = 1;
    gap_ = (w - cols_ * cell_w_) / cols_;
    if (gap_ < 0)
        gap_ = 0;
    update_scrollbar();
}

void FileList::update_scrollbar()
{
    int page = page_lines();
    int n = lines();
    int max = n > page ? n : page;
    if (top_ > max - page)
        top_ = max - page;
    if (top_ < 0)
        top_ = 0;
    XtVaSetValues(sb_, XmNminimum, 0, XmNmaximum, max, XmNsliderSize, page, XmNvalue, top_,
                  XmNpageIncrement, page > 1 ? page - 1 : 1, XmNincrement, 1, (char*)0);
}

void FileList::scroll_to_cursor()
{
    int page = page_lines();
    int line = cursor_ / per_line();
    if (line < top_)
        top_ = line;
    else if (line >= top_ + page)
        top_ = line - page + 1;
    update_scrollbar();
}

void FileList::set_cursor(int i)
{
    move(i);
}

void FileList::move(int to)
{
    if (rows_.empty())
        return;
    if (to < 0)
        to = 0;
    if (to >= rows_.size())
        to = rows_.size() - 1;
    if (to == cursor_)
        return;
    cursor_ = to;
    scroll_to_cursor();
    paint();
    tell_visible();
    if (host_)
        host_->list_cursor(this, cursor_);
}

void FileList::scrolled(int value)
{
    top_ = value;
    paint();
    tell_visible();
}

void FileList::exposed()
{
    paint();
}

void FileList::resized()
{
    layout();
    scroll_to_cursor();
    paint();
    tell_visible();
}

/* A small drawn icon: a folder, a picture, an archive, a page. */
void FileList::icon(Drawable d, const Row* r, int x, int y, int sz, bool sel)
{
    (void)sel;
    int s = sz;
    XSetForeground(dpy_, gc_, c_.fg);
    if (r->dir && !r->archive) {
        XSetForeground(dpy_, gc_, c_.folder);
        XFillRectangle(dpy_, d, gc_, x, y + s / 4, (unsigned)s, (unsigned)(s * 3 / 4 - 1));
        XFillRectangle(dpy_, d, gc_, x, y + s / 8, (unsigned)(s / 2), (unsigned)(s / 4));
        XSetForeground(dpy_, gc_, c_.header_bottom);
        XDrawRectangle(dpy_, d, gc_, x, y + s / 4, (unsigned)(s - 1), (unsigned)(s * 3 / 4 - 2));
    } else if (r->archive) {
        XSetForeground(dpy_, gc_, c_.archive);
        XFillRectangle(dpy_, d, gc_, x + 1, y + 1, (unsigned)(s - 2), (unsigned)(s - 2));
        XSetForeground(dpy_, gc_, c_.header_bottom);
        XDrawRectangle(dpy_, d, gc_, x + 1, y + 1, (unsigned)(s - 3), (unsigned)(s - 3));
        for (int k = 2; k < s - 3; k += 3)
            XDrawLine(dpy_, d, gc_, x + s / 2 - 1, y + k, x + s / 2 + 1, y + k);
    } else if (r->image) {
        XSetForeground(dpy_, gc_, c_.image);
        XFillRectangle(dpy_, d, gc_, x + 1, y + 2, (unsigned)(s - 2), (unsigned)(s - 4));
        XSetForeground(dpy_, gc_, c_.folder);
        XFillRectangle(dpy_, d, gc_, x + 3, y + s / 2, (unsigned)(s / 3), (unsigned)(s / 3 - 1));
        XSetForeground(dpy_, gc_, c_.header_bottom);
        XDrawRectangle(dpy_, d, gc_, x + 1, y + 2, (unsigned)(s - 3), (unsigned)(s - 5));
    } else {
        XSetForeground(dpy_, gc_, c_.bg);
        XFillRectangle(dpy_, d, gc_, x + 2, y + 1, (unsigned)(s - 5), (unsigned)(s - 2));
        XSetForeground(dpy_, gc_, c_.dim);
        XDrawRectangle(dpy_, d, gc_, x + 2, y + 1, (unsigned)(s - 6), (unsigned)(s - 3));
        for (int k = 4; k < s - 3; k += 3)
            XDrawLine(dpy_, d, gc_, x + 4, y + k, x + s - 6, y + k);
    }
}

namespace {

/* Text cut to fit 'room' pixels, with "..." when cut. */
void fit_text(XFontStruct* f, const char* s, int room, char* out, size_t cap)
{
    size_t n = strlen(s);
    if (n >= cap)
        n = cap - 1;
    memcpy(out, s, n);
    out[n] = 0;
    if (!f || XTextWidth(f, out, (int)n) <= room)
        return;
    int dots = XTextWidth(f, "...", 3);
    while (n > 0 && XTextWidth(f, out, (int)n) + dots > room)
        n--;
    if (n + 4 > cap)
        n = cap - 4;
    strcpy(out + n, "...");
}

} /* namespace */

void FileList::paint_header(Drawable d)
{
    int w = col_x_[SORT_COUNT];
    XSetForeground(dpy_, gc_, c_.header_bg);
    XFillRectangle(dpy_, d, gc_, 0, 0, (unsigned)(buf_w_ > w ? buf_w_ : w), (unsigned)header_h_);
    int base = header_h_ - 4 - (font_ ? font_->descent : 2);
    for (int k = 0; k < SORT_COUNT; k++) {
        int x0 = col_x_[k], x1 = col_x_[k + 1] - 1;
        if (k == SORT_COUNT - 1 && buf_w_ > x1)
            x1 = buf_w_ - 1;
        XSetForeground(dpy_, gc_, c_.header_top);
        XDrawLine(dpy_, d, gc_, x0, 0, x1, 0);
        XDrawLine(dpy_, d, gc_, x0, 0, x0, header_h_ - 1);
        XSetForeground(dpy_, gc_, c_.header_bottom);
        XDrawLine(dpy_, d, gc_, x0, header_h_ - 1, x1, header_h_ - 1);
        XDrawLine(dpy_, d, gc_, x1, 0, x1, header_h_ - 1);
        char t[40];
        snprintf(t, sizeof(t), "%s%s", k_titles[k], k == key_ ? (reverse_ ? "-" : "+") : "");
        XSetForeground(dpy_, gc_, c_.header_fg);
        int tx = x0 + 5;
        if (k == SORT_SIZE && font_)
            tx = x1 - 5 - XTextWidth(font_, t, (int)strlen(t));
        XDrawString(dpy_, d, gc_, tx, base, t, (int)strlen(t));
    }
}

void FileList::paint_row(Drawable d, int i, int y)
{
    const Row* r = rows_[i];
    bool sel = i == cursor_;
    int w = buf_w_;
    XSetForeground(dpy_, gc_, sel ? c_.sel_bg : c_.bg);
    XFillRectangle(dpy_, d, gc_, 0, y, (unsigned)w, (unsigned)row_h_);
    int isz = row_h_ - 4;
    if (isz > 16)
        isz = 16;
    icon(d, r, 3, y + (row_h_ - isz) / 2, isz, sel);
    unsigned long fg = sel ? c_.sel_fg : c_.fg;
    int base = y + row_h_ - 3 - (font_ ? font_->descent : 2);
    char text[300], cut[300];
    /* Name */
    snprintf(text, sizeof(text), "%s", r->up ? ".." : r->name.c_str());
    fit_text(font_, text, col_x_[SORT_SIZE] - isz - 14, cut, sizeof(cut));
    XSetForeground(dpy_, gc_, r->marked && !sel ? c_.archive : fg);
    XDrawString(dpy_, d, gc_, isz + 8, base, cut, (int)strlen(cut));
    if (r->marked) {
        /* Marked: underlined too, readable on any colours. */
        int tw = font_ ? XTextWidth(font_, cut, (int)strlen(cut)) : 0;
        XDrawLine(dpy_, d, gc_, isz + 8, base + 1, isz + 8 + tw, base + 1);
    }
    XSetForeground(dpy_, gc_, fg);
    if (r->up)
        return;
    /* Size, right aligned */
    if (!r->dir || r->archive) {
        format_size(r->st.size, text, sizeof(text));
        int tw = font_ ? XTextWidth(font_, text, (int)strlen(text)) : 0;
        XDrawString(dpy_, d, gc_, col_x_[SORT_IMAGE] - 6 - tw, base, text, (int)strlen(text));
    }
    /* Image properties */
    if (r->probed && r->info.width) {
        format_image(r->info, text, sizeof(text));
        fit_text(font_, text, col_x_[SORT_TYPE] - col_x_[SORT_IMAGE] - 8, cut, sizeof(cut));
        XDrawString(dpy_, d, gc_, col_x_[SORT_IMAGE] + 5, base, cut, (int)strlen(cut));
    }
    /* Type */
    if (r->archive)
        snprintf(text, sizeof(text), "Archive");
    else if (r->dir)
        snprintf(text, sizeof(text), "Folder");
    else if (r->probed && r->info.format)
        snprintf(text, sizeof(text), "%s", ti::format_name(r->info.format));
    else {
        const char* e = extension(r->name.c_str());
        size_t k = 0;
        for (; e[k] && k < 10; k++)
            text[k] = (char)toupper((unsigned char)e[k]);
        text[k] = 0;
    }
    fit_text(font_, text, col_x_[SORT_TIME] - col_x_[SORT_TYPE] - 8, cut, sizeof(cut));
    XDrawString(dpy_, d, gc_, col_x_[SORT_TYPE] + 5, base, cut, (int)strlen(cut));
    /* Modified */
    time_t t = (time_t)r->st.mtime;
    struct tm* tm = localtime(&t);
    if (tm) {
        strftime(text, sizeof(text), "%Y-%m-%d %H:%M", tm);
        XDrawString(dpy_, d, gc_, col_x_[SORT_TIME] + 5, base, text, (int)strlen(text));
    }
}

/* One cell, ACDSee's: the thumbnail in a sunken square, its size and
 * depth under it, the name below. */
void FileList::paint_cell(Drawable d, int i, int x, int y)
{
    const Row* r = rows_[i];
    bool sel = i == cursor_;
    int fh = font_ ? font_->ascent + font_->descent : 13;
    int bx = x + 8, by = y + 6, bs = thumb_;
    /* The square. */
    XSetForeground(dpy_, gc_, c_.header_bg);
    XFillRectangle(dpy_, d, gc_, bx, by, (unsigned)bs, (unsigned)(bs + fh + 3));
    XSetForeground(dpy_, gc_, c_.header_bottom);
    XDrawLine(dpy_, d, gc_, bx - 1, by - 1, bx + bs, by - 1);
    XDrawLine(dpy_, d, gc_, bx - 1, by - 1, bx - 1, by + bs + fh + 3);
    XSetForeground(dpy_, gc_, c_.header_top);
    XDrawLine(dpy_, d, gc_, bx - 1, by + bs + fh + 3, bx + bs, by + bs + fh + 3);
    XDrawLine(dpy_, d, gc_, bx + bs, by - 1, bx + bs, by + bs + fh + 3);
    int tw = 0, th = 0;
    Pixmap pm = r->image ? thumb_pixmap(i, &tw, &th) : 0;
    if (pm) {
        XCopyArea(dpy_, pm, d, gc_, 0, 0, (unsigned)tw, (unsigned)th, bx + (bs - tw) / 2,
                  by + (bs - th) / 2);
    } else if (r->dir || !r->image) {
        int is = bs / 2;
        icon(d, r, bx + (bs - is) / 2, by + (bs - is) / 2, is, false);
    } else {
        /* Not here yet: a faint frame where it will be. */
        XSetForeground(dpy_, gc_, c_.dim);
        XDrawRectangle(dpy_, d, gc_, bx + bs / 4, by + bs / 4, (unsigned)(bs / 2),
                       (unsigned)(bs / 2));
    }
    char text[300], cut[300];
    /* Size and depth, in the square's bottom strip. */
    if (r->probed && r->info.width) {
        format_image(r->info, text, sizeof(text));
        fit_text(font_, text, bs - 4, cut, sizeof(cut));
        int w = font_ ? XTextWidth(font_, cut, (int)strlen(cut)) : 0;
        XSetForeground(dpy_, gc_, c_.header_fg);
        XDrawString(dpy_, d, gc_, bx + (bs - w) / 2, by + bs + fh - (font_ ? font_->descent : 2),
                    cut, (int)strlen(cut));
    }
    /* The name, highlighted when it is the cursor's. */
    int ny = by + bs + fh + 6;
    snprintf(text, sizeof(text), "%s", r->up ? ".." : r->name.c_str());
    fit_text(font_, text, cell_w_ - 6, cut, sizeof(cut));
    int nw = font_ ? XTextWidth(font_, cut, (int)strlen(cut)) : 0;
    int nx = x + (cell_w_ - nw) / 2;
    if (sel) {
        XSetForeground(dpy_, gc_, c_.sel_bg);
        XFillRectangle(dpy_, d, gc_, nx - 3, ny, (unsigned)(nw + 6), (unsigned)(fh + 2));
    }
    XSetForeground(dpy_, gc_, sel ? c_.sel_fg : r->marked ? c_.archive : c_.fg);
    XDrawString(dpy_, d, gc_, nx, ny + 1 + (font_ ? font_->ascent : 11), cut, (int)strlen(cut));
    if (r->marked)
        XDrawLine(dpy_, d, gc_, nx, ny + fh, nx + nw, ny + fh);
    if (sel) {
        /* The cell's outline too, so the cursor shows on any thumbnail. */
        XSetForeground(dpy_, gc_, c_.sel_bg);
        XDrawRectangle(dpy_, d, gc_, bx - 3, by - 3, (unsigned)(bs + 5), (unsigned)(bs + fh + 8));
        XDrawRectangle(dpy_, d, gc_, bx - 4, by - 4, (unsigned)(bs + 7), (unsigned)(bs + fh + 10));
    }
}

void FileList::paint()
{
    if (!ensure_gc())
        return;
    Dimension dw = 0, dh = 0;
    XtVaGetValues(da_, XmNwidth, &dw, XmNheight, &dh, (char*)0);
    int w = dw, h = dh;
    if (w < 1 || h < 1)
        return;
    if (!buf_ || buf_w_ != w || buf_h_ != h) {
        if (buf_)
            XFreePixmap(dpy_, buf_);
        int depth = 0;
        XtVaGetValues(da_, XmNdepth, &depth, (char*)0);
        buf_ = XCreatePixmap(dpy_, XtWindow(da_), (unsigned)w, (unsigned)h, (unsigned)depth);
        buf_w_ = w;
        buf_h_ = h;
        layout();
    }
    XSetForeground(dpy_, gc_, c_.bg);
    XFillRectangle(dpy_, buf_, gc_, 0, 0, (unsigned)w, (unsigned)h);
    if (mode_ == VIEW_THUMBS) {
        for (int line = top_, y = 0; y < h && line * cols_ < rows_.size(); line++, y += cell_h_)
            for (int c = 0; c < cols_; c++) {
                int i = line * cols_ + c;
                if (i >= rows_.size())
                    break;
                paint_cell(buf_, i, c * (cell_w_ + gap_) + gap_ / 2, y);
            }
    } else {
        int y = header_h_;
        for (int i = top_; i < rows_.size() && y < h; i++, y += row_h_)
            paint_row(buf_, i, y);
        paint_header(buf_);
    }
    XCopyArea(dpy_, buf_, XtWindow(da_), gc_, 0, 0, (unsigned)w, (unsigned)h, 0, 0);
}

void FileList::key(XKeyEvent* ev)
{
    char buf[16];
    KeySym ks = 0;
    int n = XLookupString(ev, buf, sizeof(buf) - 1, &ks, 0);
    buf[n > 0 ? n : 0] = 0;
    bool ctrl = (ev->state & ControlMask) != 0;
    bool alt = (ev->state & Mod1Mask) != 0;
    int per = per_line();
    int page = (page_lines() - (mode_ == VIEW_THUMBS ? 0 : 1)) * per;
    if (page < per)
        page = per;
    bool grid = mode_ == VIEW_THUMBS;
    if (!alt && !ctrl) {
        switch (ks) {
        case XK_Left: case XK_KP_Left:
            if (grid) {
                move(cursor_ - 1);
                return;
            }
            break;
        case XK_Right: case XK_KP_Right:
            if (grid) {
                move(cursor_ + 1);
                return;
            }
            break;
        default:
            break;
        }
    }
    if (!alt) {
        switch (ks) {
        case XK_Up: case XK_KP_Up:
            if (cursor_ - per >= 0 || !grid)
                move(cursor_ - per);
            return;
        case XK_Down: case XK_KP_Down:
            if (cursor_ + per < rows_.size() || !grid)
                move(cursor_ + per);
            else if (cursor_ / per < (rows_.size() - 1) / per)
                move(rows_.size() - 1);     /* the short last line */
            return;
        case XK_Prior: case XK_KP_Prior: move(cursor_ - page); return;
        case XK_Next: case XK_KP_Next: move(cursor_ + page); return;
        case XK_Home: case XK_KP_Home: move(0); return;
        case XK_End: case XK_KP_End: move(rows_.size() - 1); return;
        case XK_Return: case XK_KP_Enter:
            if (host_ && row(cursor_))
                host_->list_activate(this, cursor_);
            return;
        case XK_BackSpace:
            if (host_)
                host_->list_back(this);
            return;
        case XK_space:
            if (!ctrl && row(cursor_) && !row(cursor_)->up) {
                row(cursor_)->marked = !row(cursor_)->marked;
                paint();
                move(cursor_ + 1);
                return;
            }
            break;
        default:
            break;
        }
    }
    if (host_ && host_->list_key(this, ev))
        return;
    /* Typing: the next name that starts with what was typed. */
    if (!ctrl && !alt && n == 1 && (unsigned char)buf[0] > ' ' && buf[0] != 0x7f) {
        size_t tl = strlen(typed_);
        if (ev->time - typed_at_ > 1000 || tl + 2 > sizeof(typed_))
            tl = 0;
        typed_[tl] = buf[0];
        typed_[tl + 1] = 0;
        typed_at_ = ev->time;
        tl++;
        int start = tl == 1 ? cursor_ + 1 : cursor_;
        for (int k = 0; k < rows_.size(); k++) {
            int i = (start + k) % rows_.size();
            const char* nm = rows_[i]->name.c_str();
            if (strncasecmp(nm, typed_, tl) == 0) {
                move(i);
                return;
            }
        }
    }
}

void FileList::button(XButtonEvent* ev)
{
    XmProcessTraversal(da_, XmTRAVERSE_CURRENT);
    if (ev->button == 4 || ev->button == 5) {
        int d = ev->button == 4 ? -3 : 3;
        top_ += d;
        update_scrollbar();
        paint();
        tell_visible();
        return;
    }
    if (ev->button != 1)
        return;
    if (mode_ == VIEW_THUMBS) {
        int line = top_ + ev->y / cell_h_;
        int col = ev->x / (cell_w_ + gap_);
        if (col >= cols_)
            col = cols_ - 1;
        int i = line * cols_ + col;
        if (i >= rows_.size())
            return;
        if (ev->state & ControlMask) {
            if (!rows_[i]->up)
                rows_[i]->marked = !rows_[i]->marked;
            paint();
        }
        bool dbl = i == click_row_ && ev->time - click_at_ < 400;
        click_row_ = i;
        click_at_ = ev->time;
        move(i);
        if (dbl && host_) {
            click_row_ = -1;
            host_->list_activate(this, i);
        }
        return;
    }
    if (ev->y < header_h_) {
        for (int k = 0; k < SORT_COUNT; k++)
            if (ev->x >= col_x_[k] && (ev->x < col_x_[k + 1] || k == SORT_COUNT - 1)) {
                sort_by((SortKey)k, k == key_ ? !reverse_ : false);
                return;
            }
        return;
    }
    int i = top_ + (ev->y - header_h_) / row_h_;
    if (i >= rows_.size())
        return;
    if (ev->state & ControlMask) {
        if (!rows_[i]->up)
            rows_[i]->marked = !rows_[i]->marked;
        paint();
    }
    bool dbl = i == click_row_ && ev->time - click_at_ < 400;
    click_row_ = i;
    click_at_ = ev->time;
    move(i);
    if (dbl && host_) {
        click_row_ = -1;
        host_->list_activate(this, i);
    }
}

} /* namespace ttrv */
