#include "panel.h"

#include "../../tf/ops.h"
#include "../../tf/vfs.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace ttrc {

namespace {

/* qsort has no context argument. */
SortMode g_sort;
bool g_reverse;

int compare_nocase(const char* a, const char* b)
{
    for (;; a++, b++) {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb || !ca)
            return ca - cb;
    }
}

int compare_names(const char* a, const char* b)
{
    int c = compare_nocase(a, b);
    return c ? c : strcmp(a, b);
}

const char* extension(const char* name)
{
    const char* dot = strrchr(name, '.');
    return dot && dot != name ? dot + 1 : "";
}

bool is_dir(const Entry* e)
{
    return e->st.type == tf::FT_DIR || (e->st.type == tf::FT_LINK && e->target == tf::FT_DIR);
}

int compare_entries(const void* pa, const void* pb)
{
    const Entry* a = *(const Entry* const*)pa;
    const Entry* b = *(const Entry* const*)pb;
    if (a->up != b->up)
        return a->up ? -1 : 1;
    bool da = is_dir(a), db = is_dir(b);
    if (da != db)
        return da ? -1 : 1;
    int c = 0;
    switch (g_sort) {
    case SORT_EXT:
        c = compare_nocase(extension(a->name.c_str()), extension(b->name.c_str()));
        break;
    case SORT_SIZE:
        c = a->st.size > b->st.size ? -1 : a->st.size < b->st.size ? 1 : 0;
        break;
    case SORT_TIME:
        c = a->st.mtime > b->st.mtime ? -1 : a->st.mtime < b->st.mtime ? 1 : 0;
        break;
    default:
        break;
    }
    if (c == 0)
        c = compare_names(a->name.c_str(), b->name.c_str());
    return g_reverse ? -c : c;
}

/* A name that does not fit loses its middle: "long_fil~me.txt". */
void fit_name(const char* name, int width, char* out, size_t n)
{
    int w = tu::Screen::text_width(name);
    if (w <= width || width < 3) {
        strncpy(out, name, n - 1);
        out[n - 1] = 0;
        return;
    }
    /* Keep the head and the tail by characters (UTF-8 aware). */
    int keep_tail = (width - 1) / 2;
    int keep_head = width - 1 - keep_tail;
    size_t o = 0;
    const char* p = name;
    int col = 0;
    while (*p && o + 8 < n) {
        uint32_t cp;
        int len = tu::decode_char(p, &cp);
        char tmp[8];
        memcpy(tmp, p, (size_t)len);
        tmp[len] = 0;
        int chw = tu::Screen::text_width(tmp);
        if (col + chw > keep_head)
            break;
        memcpy(out + o, p, (size_t)len);
        o += (size_t)len;
        col += chw;
        p += len;
    }
    out[o++] = '~';
    /* The tail: walk back from the end. */
    const char* end = name + strlen(name);
    const char* t = end;
    int tcol = 0;
    while (t > name) {
        const char* q = t - 1;
        while (q > name && ((unsigned char)*q & 0xc0) == 0x80)
            q--;
        char tmp[8];
        size_t len = (size_t)(t - q);
        if (len >= sizeof(tmp))
            break;
        memcpy(tmp, q, len);
        tmp[len] = 0;
        int chw = tu::Screen::text_width(tmp);
        if (tcol + chw > keep_tail)
            break;
        tcol += chw;
        t = q;
    }
    size_t tl = (size_t)(end - t);
    if (o + tl + 1 > n)
        tl = n - o - 1;
    memcpy(out + o, t, tl);
    out[o + tl] = 0;
}

/* mc's marks before names: / directory, ~ link to a directory, @ link,
 * ! broken link, * executable, | fifo, = socket, - and + devices. */
char type_mark(const Entry& e)
{
    switch (e.st.type) {
    case tf::FT_DIR: return '/';
    case tf::FT_LINK:
        return e.target == tf::FT_DIR ? '~' : e.target == tf::FT_OTHER ? '!' : '@';
    case tf::FT_FIFO: return '|';
    case tf::FT_SOCKET: return '=';
    case tf::FT_CHAR: return '-';
    case tf::FT_BLOCK: return '+';
    default:
        return (e.st.mode & 0111) ? '*' : ' ';
    }
}

void time_string(long t, char* out)
{
    time_t tt = (time_t)t;
    struct tm* tm = localtime(&tt);
    if (!tm) {
        strcpy(out, "?");
        return;
    }
    static const char* const months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    time_t now = time(0);
    /* Recent: the time of day; older or in the future: the year. */
    if (tt <= now + 3600 && now - tt < 182L * 24 * 3600)
        sprintf(out, "%s %2d %02d:%02d", months[tm->tm_mon], tm->tm_mday, tm->tm_hour,
                tm->tm_min);
    else
        sprintf(out, "%s %2d  %4d", months[tm->tm_mon], tm->tm_mday, tm->tm_year + 1900);
}

} /* namespace */

void mode_string(const tf::Stat& st, char out[11])
{
    char t = '-';
    switch (st.type) {
    case tf::FT_DIR: t = 'd'; break;
    case tf::FT_LINK: t = 'l'; break;
    case tf::FT_CHAR: t = 'c'; break;
    case tf::FT_BLOCK: t = 'b'; break;
    case tf::FT_FIFO: t = 'p'; break;
    case tf::FT_SOCKET: t = 's'; break;
    default: break;
    }
    unsigned m = st.mode;
    out[0] = t;
    out[1] = (m & 0400) ? 'r' : '-';
    out[2] = (m & 0200) ? 'w' : '-';
    out[3] = (m & 04000) ? ((m & 0100) ? 's' : 'S') : ((m & 0100) ? 'x' : '-');
    out[4] = (m & 040) ? 'r' : '-';
    out[5] = (m & 020) ? 'w' : '-';
    out[6] = (m & 02000) ? ((m & 010) ? 's' : 'S') : ((m & 010) ? 'x' : '-');
    out[7] = (m & 04) ? 'r' : '-';
    out[8] = (m & 02) ? 'w' : '-';
    out[9] = (m & 01000) ? ((m & 01) ? 't' : 'T') : ((m & 01) ? 'x' : '-');
    out[10] = 0;
}

void size_string(uint64_t n, int width, char* out)
{
    static const char units[] = "KMGTP";
    char b[32];
    sprintf(b, "%lu", (unsigned long)n);
    if (n > 0xffffffffu || (int)strlen(b) > width) {
        double v = (double)n;
        int u = -1;
        do {
            v /= 1024.0;
            u++;
            sprintf(b, "%lu%c", (unsigned long)(v + 0.5), units[u]);
        } while ((int)strlen(b) > width && u < 4);
    }
    strcpy(out, b);
}

Panel::Panel(tf::Fs* fs, const tf::Path& path)
    : fs_(fs), path_(path), cursor_(0), top_(0), sort_(SORT_NAME), reverse_(false),
      mode_(LIST_FULL), hidden_(true), lx_(0), ly_(0), lw_(0), lh_(0), cols_(1)
{
    error_[0] = 0;
    if (!load(path))
        load(tf::Path("/"));
}

Panel::~Panel()
{
    free_entries();
}

void Panel::free_entries()
{
    for (int i = 0; i < entries_.size(); i++)
        delete entries_[i];
    entries_.clear();
}

bool Panel::load(const tf::Path& path, const char* select)
{
    int err = 0;
    tf::DirReader* d = fs_->open_dir(path.c_str(), &err);
    if (!d) {
        if (err == EINVAL && !tf::is_local_path(path.c_str()))
            sprintf(error_, "%.200s: not an archive ttrc can read, or a damaged one",
                    path.c_str());
        else
            sprintf(error_, "%.200s: %.80s", path.c_str(), strerror(err));
        return false;
    }
    error_[0] = 0;
    free_entries();
    path_ = path;
    if (!path_.is_root()) {
        Entry* up = new Entry;
        up->name = "..";
        up->up = true;
        up->marked = false;
        up->target = tf::FT_DIR;
        fs_->stat(path_.parent().c_str(), &up->st, true);
        up->st.type = tf::FT_DIR;
        entries_.push_back(up);
    }
    const char* n;
    while ((n = d->next()) != 0) {
        if (!hidden_ && n[0] == '.')
            continue;
        Entry* e = new Entry;
        e->name = n;
        e->up = false;
        e->marked = false;
        e->target = tf::FT_OTHER;
        tf::Path full = path_.child(n);
        if (fs_->stat(full.c_str(), &e->st, false) != 0) {
            e->st = tf::Stat();
        } else if (e->st.type == tf::FT_LINK) {
            tf::Stat t;
            if (fs_->stat(full.c_str(), &t, true) == 0)
                e->target = t.type;
        }
        entries_.push_back(e);
    }
    delete d;
    sort_entries();
    cursor_ = 0;
    top_ = 0;
    if (select)
        for (int i = 0; i < entries_.size(); i++)
            if (strcmp(entries_[i]->name.c_str(), select) == 0) {
                cursor_ = i;
                break;
            }
    clamp();
    return true;
}

void Panel::reload()
{
    /* Keep the cursor's name and the marks. */
    ttr::Str at = current() ? current()->name : ttr::Str("");
    ttr::Vector<ttr::Str> marks;
    for (int i = 0; i < entries_.size(); i++)
        if (entries_[i]->marked)
            marks.push_back(entries_[i]->name);
    int old_top = top_;
    if (!load(path_, at.c_str()))
        return;
    for (int i = 0; i < entries_.size(); i++)
        for (int k = 0; k < marks.size(); k++)
            if (strcmp(entries_[i]->name.c_str(), marks[k].c_str()) == 0)
                entries_[i]->marked = true;
    top_ = old_top;
    clamp();
}

void Panel::sort_entries()
{
    if (sort_ == SORT_UNSORTED || entries_.size() < 2)
        return;
    g_sort = sort_;
    g_reverse = reverse_;
    qsort(&entries_[0], (size_t)entries_.size(), sizeof(Entry*), compare_entries);
}

void Panel::set_sort(SortMode m)
{
    if (m == sort_)
        reverse_ = !reverse_;
    else
        reverse_ = false;
    sort_ = m;
    ttr::Str at = current() ? current()->name : ttr::Str("");
    sort_entries();
    for (int i = 0; i < entries_.size(); i++)
        if (strcmp(entries_[i]->name.c_str(), at.c_str()) == 0)
            cursor_ = i;
    clamp();
}

void Panel::set_hidden(bool on)
{
    if (on != hidden_) {
        hidden_ = on;
        reload();
    }
}

void Panel::set_sort_mode(SortMode m, bool reverse)
{
    sort_ = m;
    reverse_ = reverse;
    ttr::Str at = current() ? current()->name : ttr::Str("");
    sort_entries();
    if (!at.empty())
        select_name(at.c_str());
}

void Panel::toggle_hidden()
{
    hidden_ = !hidden_;
    reload();
}

const Entry* Panel::current() const
{
    return cursor_ >= 0 && cursor_ < entries_.size() ? entries_[cursor_] : 0;
}

int Panel::selection(ttr::Vector<ttr::Str>* names) const
{
    for (int i = 0; i < entries_.size(); i++)
        if (entries_[i]->marked)
            names->push_back(entries_[i]->name);
    if (names->size() == 0) {
        const Entry* e = current();
        if (e && !e->up)
            names->push_back(e->name);
    }
    return names->size();
}

int Panel::mark_matching(const char* pattern, bool on, bool dirs_too)
{
    int n = 0;
    for (int i = 0; i < entries_.size(); i++) {
        Entry* e = entries_[i];
        if (e->up || (!dirs_too && is_dir(e)))
            continue;
        if (tf::match_pattern(pattern, e->name.c_str(), false)) {
            e->marked = on;
            n++;
        }
    }
    return n;
}

void Panel::invert_marks()
{
    for (int i = 0; i < entries_.size(); i++)
        if (!entries_[i]->up && !is_dir(entries_[i]))
            entries_[i]->marked = !entries_[i]->marked;
}

void Panel::unmark_all()
{
    for (int i = 0; i < entries_.size(); i++)
        entries_[i]->marked = false;
}

void Panel::set_marked(const char* name, bool on)
{
    for (int i = 0; i < entries_.size(); i++)
        if (strcmp(entries_[i]->name.c_str(), name) == 0)
            entries_[i]->marked = on;
}

bool Panel::select_name(const char* name)
{
    for (int i = 0; i < entries_.size(); i++)
        if (strcmp(entries_[i]->name.c_str(), name) == 0) {
            cursor_ = i;
            clamp();
            return true;
        }
    return false;
}

int Panel::marked_count() const
{
    int n = 0;
    for (int i = 0; i < entries_.size(); i++)
        if (entries_[i]->marked)
            n++;
    return n;
}

uint64_t Panel::marked_bytes() const
{
    uint64_t n = 0;
    for (int i = 0; i < entries_.size(); i++)
        if (entries_[i]->marked && entries_[i]->st.type == tf::FT_FILE)
            n += entries_[i]->st.size;
    return n;
}

/* Keep the cursor inside the list and on screen. */
void Panel::clamp()
{
    int n = entries_.size();
    if (cursor_ >= n)
        cursor_ = n - 1;
    if (cursor_ < 0)
        cursor_ = 0;
    int visible = lh_ > 0 ? lh_ * cols_ : 1;
    if (cursor_ < top_)
        top_ = mode_ == LIST_BRIEF ? cursor_ - cursor_ % lh_ : cursor_;
    if (cursor_ >= top_ + visible)
        top_ = mode_ == LIST_BRIEF ? cursor_ - cursor_ % lh_ - lh_ * (cols_ - 1)
                                   : cursor_ - visible + 1;
    if (top_ > n - visible)
        top_ = n - visible;
    if (top_ < 0)
        top_ = 0;
}

void Panel::move(int delta)
{
    cursor_ += delta;
    clamp();
}

void Panel::page(int dir)
{
    int visible = lh_ > 0 ? lh_ * cols_ : 1;
    cursor_ += dir * (visible > 1 ? visible - 1 : 1);
    top_ += dir * (visible > 1 ? visible - 1 : 1);
    clamp();
}

void Panel::home()
{
    cursor_ = 0;
    clamp();
}

void Panel::end()
{
    cursor_ = entries_.size() - 1;
    clamp();
}

void Panel::column(int dir)
{
    if (mode_ == LIST_BRIEF && lh_ > 0)
        move(dir * lh_);
}

void Panel::set_cursor(int i)
{
    cursor_ = i;
    clamp();
}

bool Panel::enter()
{
    const Entry* e = current();
    if (!e)
        return false;
    if (e->up)
        return up();
    if (!is_dir(e)) {
        /* Archives open like directories (mc's notation). */
        const char* scheme = tf::archive_scheme(e->name.c_str());
        if (!scheme || e->st.type == tf::FT_DIR)
            return false;
        char p[4200];
        snprintf(p, sizeof(p), "%s/%s://", path_.child(e->name.c_str()).c_str(), scheme);
        return load(tf::Path(p));
    }
    return load(path_.child(e->name.c_str()));
}

bool Panel::is_archive(const Entry& e)
{
    return !e.up && e.st.type == tf::FT_FILE && tf::archive_scheme(e.name.c_str()) != 0;
}

bool Panel::up()
{
    if (path_.is_root())
        return false;
    ttr::Str from(path_.name());
    return load(path_.parent(), from.c_str());
}

void Panel::toggle_mark()
{
    Entry* e = cursor_ < entries_.size() ? entries_[cursor_] : 0;
    if (e && !e->up)
        e->marked = !e->marked;
    move(1);
}

bool Panel::search(const char* prefix, bool next)
{
    int n = entries_.size();
    size_t pl = strlen(prefix);
    for (int k = next ? 1 : 0; k < n + 1; k++) {
        int i = (cursor_ + k) % (n ? n : 1);
        const char* nm = n ? entries_[i]->name.c_str() : "";
        size_t j = 0;
        while (j < pl && nm[j] && tolower((unsigned char)nm[j]) == tolower((unsigned char)prefix[j]))
            j++;
        if (n && j == pl) {
            cursor_ = i;
            clamp();
            return true;
        }
    }
    return false;
}

int Panel::entry_at(int sx, int sy) const
{
    if (sx < lx_ || sx >= lx_ + lw_ || sy < ly_ || sy >= ly_ + lh_)
        return -1;
    int col = cols_ > 1 ? (sx - lx_) * cols_ / lw_ : 0;
    int i = top_ + col * lh_ + (sy - ly_);
    return i < entries_.size() ? i : -1;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */

void Panel::draw_entry(tu::Screen& s, const Theme& t, const Entry& e, int x, int y, int w,
                       bool cursor)
{
    tu::Style st;
    switch (e.st.type) {
    case tf::FT_DIR: st = t.dir; break;
    case tf::FT_LINK: st = e.target == tf::FT_DIR ? t.dir : t.link; break;
    case tf::FT_CHAR: case tf::FT_BLOCK: case tf::FT_FIFO: case tf::FT_SOCKET:
        st = t.special;
        break;
    default:
        st = (e.st.mode & 0111) ? t.exec : t.panel;
        break;
    }
    if (e.marked)
        st = t.marked;
    if (cursor)
        st = e.marked ? t.cursor_marked : t.cursor;
    s.fill(x, y, w, 1, ' ', st);
    char mark[2] = { e.up ? '/' : type_mark(e), 0 };
    s.text(x, y, mark, st, 1);
    char name[1024];
    fit_name(e.name.c_str(), w - 1, name, sizeof(name));
    s.text(x + 1, y, name, st, w - 1);
}

void Panel::draw(tu::Screen& s, const Theme& t, int x, int y, int w, int h, bool active)
{
    if (w < 12 || h < 6)
        return;
    s.fill(x, y, w, h, ' ', t.panel);
    s.frame(x, y, w, h, t.frame_style, t.frame);

    int sep = y + h - 3;                    /* the line above the status */
    lx_ = x + 1;
    ly_ = y + 2;
    lw_ = w - 2;
    lh_ = sep - ly_;
    if (lh_ < 1)
        lh_ = 1;

    int name_w, size_x = 0, time_x = 0;
    if (mode_ == LIST_FULL) {
        cols_ = 1;
        name_w = w - 2 - 1 - 7 - 1 - 12;
        if (name_w < 6)
            name_w = 6;
        size_x = x + 1 + name_w + 1;
        time_x = size_x + 7 + 1;
        s.vline(size_x - 1, y, sep - y + 1, t.frame_style, t.frame);
        s.vline(time_x - 1, y, sep - y + 1, t.frame_style, t.frame);
        s.put(size_x - 1, sep, 0x2534, t.frame);
        s.put(time_x - 1, sep, 0x2534, t.frame);
        s.text(x + 1 + (name_w - 4) / 2, y + 1, "Name", t.header);
        s.text(size_x + 2, y + 1, "Size", t.header);
        s.text(time_x + 1, y + 1, "Modified", t.header);
    } else {
        cols_ = 2;
        name_w = (w - 3) / 2;
        int mid = x + 1 + name_w;
        s.vline(mid, y, sep - y + 1, t.frame_style, t.frame);
        s.put(mid, sep, 0x2534, t.frame);
        s.text(x + 1 + (name_w - 4) / 2, y + 1, "Name", t.header);
        s.text(mid + 1 + (w - 2 - name_w - 1 - 4) / 2, y + 1, "Name", t.header);
    }
    clamp();

    for (int c = 0; c < cols_; c++) {
        for (int r = 0; r < lh_; r++) {
            int i = top_ + c * lh_ + r;
            if (i >= entries_.size())
                break;
            const Entry& e = *entries_[i];
            bool cur = active && i == cursor_;
            int ex, ew;
            if (mode_ == LIST_FULL) {
                ex = x + 1;
                ew = name_w;
            } else {
                ex = c == 0 ? x + 1 : x + 1 + name_w + 1;
                ew = c == 0 ? name_w : w - 2 - name_w - 1;
            }
            draw_entry(s, t, e, ex, ly_ + r, ew, cur);
            if (mode_ == LIST_FULL) {
                tu::Style st = cur ? (e.marked ? t.cursor_marked : t.cursor)
                                   : e.marked ? t.marked : t.panel;
                char b[32];
                if (e.up)
                    strcpy(b, "UP--DIR");
                else if (e.st.type == tf::FT_DIR ||
                         (e.st.type == tf::FT_LINK && e.target == tf::FT_DIR))
                    strcpy(b, "<DIR>");
                else
                    size_string(e.st.size, 7, b);
                s.fill(size_x, ly_ + r, 7, 1, ' ', st);
                s.text(size_x + 7 - (int)strlen(b), ly_ + r, b, st);
                time_string(e.st.mtime, b);
                s.fill(time_x, ly_ + r, 12, 1, ' ', st);
                s.text(time_x, ly_ + r, b, st, 12);
            }
        }
    }

    /* The path in the top border, over the column lines. */
    char title[1024];
    fit_name(path_.c_str(), w - 6, title + 1, sizeof(title) - 2);
    title[0] = ' ';
    size_t tl = strlen(title);
    title[tl] = ' ';
    title[tl + 1] = 0;
    int tw = tu::Screen::text_width(title);
    s.text(x + (w - tw) / 2, y, title, active ? t.frame_active_title : t.frame_title);

    /* The status line: what is selected, or the file under the cursor. */
    s.hline(x, sep, w, t.frame_style, t.frame);
    if (mode_ == LIST_FULL) {
        s.put(size_x - 1, sep, 0x2534, t.frame);
        s.put(time_x - 1, sep, 0x2534, t.frame);
    } else {
        s.put(x + 1 + name_w, sep, 0x2534, t.frame);
    }
    char line[1200];
    int marked = marked_count();
    const Entry* e = current();
    if (marked > 0) {
        char b[32];
        size_string(marked_bytes(), 12, b);
        sprintf(line, " %s bytes in %d file%s ", b, marked, marked == 1 ? "" : "s");
        int lw = tu::Screen::text_width(line);
        s.text(x + (w - lw) / 2, sep, line, t.marked);
    }
    if (e) {
        char right[64];
        if (e->up)
            strcpy(right, "UP--DIR");
        else {
            char m[11];
            mode_string(e->st, m);
            char sz[32];
            if (e->st.type == tf::FT_DIR)
                strcpy(sz, "<DIR>");
            else
                size_string(e->st.size, 10, sz);
            sprintf(right, "%s %10s", m, sz);
        }
        int rw = (int)strlen(right);
        char nm[1100];
        char lt[1024];
        lt[0] = 0;
        if (e->st.type == tf::FT_LINK) {
            char tgt[1024];
            if (fs_->readlink(path_.child(e->name.c_str()).c_str(), tgt, sizeof(tgt)) == 0)
                sprintf(lt, " -> %.900s", tgt);
        }
        sprintf(nm, "%.1000s%s", e->name.c_str(), lt);
        char fitted[1100];
        fit_name(nm, w - 2 - rw - 1, fitted, sizeof(fitted));
        s.fill(x + 1, sep + 1, w - 2, 1, ' ', t.status);
        s.text(x + 1, sep + 1, fitted, t.status, w - 2 - rw - 1);
        s.text(x + w - 1 - rw, sep + 1, right, t.status);
    }
}

} /* namespace ttrc */
