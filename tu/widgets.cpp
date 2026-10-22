#include "widgets.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

namespace tu {

namespace {

char* dup(const char* s)
{
    size_t n = strlen(s);
    char* d = (char*)malloc(n + 1);
    if (d)
        memcpy(d, s, n + 1);
    return d;
}

long now_ms()
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

uint32_t lower(uint32_t c)
{
    return c < 128 ? (uint32_t)tolower((int)c) : c;
}

} /* namespace */

/* ------------------------------------------------------------------ */

void StrList::add(const char* s)
{
    insert(n_, s);
}

void StrList::insert(int at, const char* s)
{
    if (n_ == cap_) {
        int nc = cap_ ? cap_ * 2 : 16;
        char** ni = (char**)realloc(items_, (size_t)nc * sizeof(char*));
        if (!ni)
            return;
        items_ = ni;
        cap_ = nc;
    }
    if (at < 0)
        at = 0;
    if (at > n_)
        at = n_;
    memmove(items_ + at + 1, items_ + at, (size_t)(n_ - at) * sizeof(char*));
    items_[at] = dup(s);
    n_++;
}

void StrList::remove(int i)
{
    if (i < 0 || i >= n_)
        return;
    free(items_[i]);
    memmove(items_ + i, items_ + i + 1, (size_t)(n_ - i - 1) * sizeof(char*));
    n_--;
}

void StrList::set(int i, const char* s)
{
    if (i < 0 || i >= n_)
        return;
    free(items_[i]);
    items_[i] = dup(s);
}

void StrList::clear()
{
    for (int i = 0; i < n_; i++)
        free(items_[i]);
    free(items_);
    items_ = 0;
    n_ = cap_ = 0;
}

uint32_t hot_letter(const char* label)
{
    const char* a = strchr(label, '&');
    if (!a || !a[1])
        return 0;
    uint32_t cp;
    decode_char(a + 1, &cp);
    return lower(cp);
}

int draw_hot(Screen& s, int x, int y, const char* label, const Style& st, const Style& hot,
             int max_cols)
{
    int used = 0;
    const char* p = label;
    bool next_hot = false;
    while (*p && used < max_cols) {
        if (*p == '&' && !next_hot) {
            next_hot = true;
            p++;
            continue;
        }
        uint32_t cp;
        int n = decode_char(p, &cp);
        char b[8];
        memcpy(b, p, (size_t)n);
        b[n] = 0;
        used += s.text(x + used, y, b, next_hot ? hot : st, max_cols - used);
        next_hot = false;
        p += n;
    }
    return used;
}

/* ------------------------------------------------------------------ */

Label::Label(const char* text) : text_(dup(text)) {}
Label::~Label() { free(text_); }

void Label::set(const char* text)
{
    free(text_);
    text_ = dup(text);
}

void Label::draw(Screen& s, const DialogStyle& st, int sx, int sy, bool)
{
    /* Several lines, split at '\n'. */
    const char* p = text_;
    for (int line = 0; line < h && p; line++) {
        const char* e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        char b[1024];
        if (n >= sizeof(b))
            n = sizeof(b) - 1;
        memcpy(b, p, n);
        b[n] = 0;
        s.fill(sx, sy + line, w, 1, ' ', st.body);
        s.text(sx, sy + line, b, st.body, w);
        p = e ? e + 1 : 0;
    }
}

/* ------------------------------------------------------------------ */

InputLine::InputLine(const char* text)
    : masked(false), len_(0), pos_(0), scroll_(0), fresh_(false), history_(0), hpos_(0),
      last_sx_(0)
{
    set(text);
}

void InputLine::set(const char* text)
{
    size_t n = strlen(text);
    if (n >= sizeof(buf_))
        n = sizeof(buf_) - 1;
    memcpy(buf_, text, n);
    buf_[n] = 0;
    len_ = pos_ = n;
    scroll_ = 0;
}

size_t InputLine::prev(size_t p) const
{
    if (p == 0)
        return 0;
    p--;
    while (p > 0 && ((unsigned char)buf_[p] & 0xc0) == 0x80)
        p--;
    return p;
}

size_t InputLine::next(size_t p) const
{
    if (p >= len_)
        return len_;
    p++;
    while (p < len_ && ((unsigned char)buf_[p] & 0xc0) == 0x80)
        p++;
    return p;
}

void InputLine::draw(Screen& s, const DialogStyle& st, int sx, int sy, bool focused)
{
    const Style& style = focused ? st.input_focus : st.input;
    s.fill(sx, sy, w, 1, ' ', style);
    last_sx_ = sx;
    char before[2048];
    memcpy(before, buf_, pos_);
    before[pos_] = 0;
    int cur = Screen::text_width(before);
    if (cur - scroll_ >= w)
        scroll_ = cur - w + 1;
    if (cur < scroll_)
        scroll_ = cur;
    /* Skip scroll_ columns. */
    const char* p = buf_;
    int col = 0;
    while (*p && col < scroll_) {
        uint32_t cp;
        int n = decode_char(p, &cp);
        char one[8];
        memcpy(one, p, (size_t)n);
        one[n] = 0;
        col += Screen::text_width(one);
        p += n;
    }
    if (masked) {
        int n = Screen::text_width(p);
        for (int i = 0; i < n && i < w; i++)
            s.put(sx + i, sy, '*', style);
    } else {
        s.text(sx, sy, p, style, w);
    }
}

bool InputLine::cursor(int* cx, int* cy) const
{
    char before[2048];
    memcpy(before, buf_, pos_);
    before[pos_] = 0;
    *cx = x + Screen::text_width(before) - scroll_;
    *cy = y;
    return true;
}

void InputLine::click(int rx, int, bool)
{
    /* Put the cursor at the clicked column. */
    int col = rx + scroll_;
    size_t p = 0;
    int c = 0;
    while (p < len_ && c < col) {
        size_t n = next(p);
        char one[8];
        memcpy(one, buf_ + p, n - p);
        one[n - p] = 0;
        c += Screen::text_width(one);
        p = n;
    }
    pos_ = p;
}

bool InputLine::key(const Event& ev)
{
    switch (ev.key) {
    case KEY_LEFT: pos_ = prev(pos_); return true;
    case KEY_RIGHT: pos_ = next(pos_); return true;
    case KEY_HOME: pos_ = 0; return true;
    case KEY_END: pos_ = len_; return true;
    case KEY_BACKSPACE:
        if (pos_ > 0) {
            size_t p = prev(pos_);
            memmove(buf_ + p, buf_ + pos_, len_ - pos_ + 1);
            len_ -= pos_ - p;
            pos_ = p;
        }
        return true;
    case KEY_DELETE:
        if (pos_ < len_) {
            size_t n = next(pos_);
            memmove(buf_ + pos_, buf_ + n, len_ - n + 1);
            len_ -= n - pos_;
        }
        return true;
    case KEY_UP:
    case KEY_DOWN:
        if (!history_ || history_->size() == 0)
            return false;
        if (ev.key == KEY_UP && hpos_ > 0)
            hpos_--;
        else if (ev.key == KEY_DOWN && hpos_ < history_->size())
            hpos_++;
        set(hpos_ < history_->size() ? (*history_)[hpos_] : "");
        return true;
    case KEY_CHAR:
        if (ev.mods & MOD_CTRL) {
            switch (ev.ch) {
            case 'a': pos_ = 0; return true;
            case 'e': pos_ = len_; return true;
            case 'u': set(""); return true;
            case 'k': len_ = pos_; buf_[len_] = 0; return true;
            case 'w': {
                size_t p = pos_;
                while (p > 0 && buf_[p - 1] == ' ')
                    p--;
                while (p > 0 && buf_[p - 1] != ' ' && buf_[p - 1] != '/')
                    p--;
                memmove(buf_ + p, buf_ + pos_, len_ - pos_ + 1);
                len_ -= pos_ - p;
                pos_ = p;
                return true;
            }
            default: return false;
            }
        }
        if (ev.mods & MOD_ALT)
            return false;
        {
            char b[5];
            uint32_t cp = ev.ch;
            size_t n;
            if (cp < 0x80) { b[0] = (char)cp; n = 1; }
            else if (cp < 0x800) { b[0] = (char)(0xc0 | (cp >> 6)); b[1] = (char)(0x80 | (cp & 0x3f)); n = 2; }
            else if (cp < 0x10000) { b[0] = (char)(0xe0 | (cp >> 12)); b[1] = (char)(0x80 | ((cp >> 6) & 0x3f)); b[2] = (char)(0x80 | (cp & 0x3f)); n = 3; }
            else { b[0] = (char)(0xf0 | (cp >> 18)); b[1] = (char)(0x80 | ((cp >> 12) & 0x3f)); b[2] = (char)(0x80 | ((cp >> 6) & 0x3f)); b[3] = (char)(0x80 | (cp & 0x3f)); n = 4; }
            if (len_ + n >= sizeof(buf_))
                return true;
            memmove(buf_ + pos_ + n, buf_ + pos_, len_ - pos_ + 1);
            memcpy(buf_ + pos_, b, n);
            len_ += n;
            pos_ += n;
        }
        return true;
    default:
        return false;
    }
}

/* ------------------------------------------------------------------ */

CheckBox::CheckBox(const char* label, bool o) : on(o), label_(dup(label)) {}
CheckBox::~CheckBox() { free(label_); }

void CheckBox::draw(Screen& s, const DialogStyle& st, int sx, int sy, bool focused)
{
    const Style& style = focused ? st.button_focus : st.body;
    s.fill(sx, sy, w, 1, ' ', st.body);
    s.text(sx, sy, on ? "[x] " : "[ ] ", style);
    draw_hot(s, sx + 4, sy, label_, st.body, st.hotkey, w - 4);
}

bool CheckBox::key(const Event& ev)
{
    if (ev.key == KEY_CHAR && ev.ch == ' ' && !ev.mods) {
        on = !on;
        return true;
    }
    return false;
}

void CheckBox::click(int, int, bool)
{
    on = !on;
}

uint32_t CheckBox::hotkey() const
{
    return hot_letter(label_);
}

bool CheckBox::cursor(int* cx, int* cy) const
{
    *cx = x + 1;
    *cy = y;
    return true;
}

/* ------------------------------------------------------------------ */

RadioGroup::RadioGroup() : selected(0), focus_(0) {}

void RadioGroup::add(const char* label)
{
    labels_.add(label);
    h = labels_.size();
}

void RadioGroup::draw(Screen& s, const DialogStyle& st, int sx, int sy, bool focused)
{
    for (int i = 0; i < labels_.size(); i++) {
        s.fill(sx, sy + i, w, 1, ' ', st.body);
        const Style& style = focused && i == focus_ ? st.button_focus : st.body;
        s.text(sx, sy + i, i == selected ? "(*) " : "( ) ", style);
        draw_hot(s, sx + 4, sy + i, labels_[i], st.body, st.hotkey, w - 4);
    }
}

bool RadioGroup::key(const Event& ev)
{
    if (ev.key == KEY_UP && focus_ > 0) {
        focus_--;
        selected = focus_;
        return true;
    }
    if (ev.key == KEY_DOWN && focus_ < labels_.size() - 1) {
        focus_++;
        selected = focus_;
        return true;
    }
    if (ev.key == KEY_CHAR && ev.ch == ' ' && !ev.mods) {
        selected = focus_;
        return true;
    }
    if (ev.key == KEY_CHAR && !(ev.mods & MOD_CTRL)) {
        for (int i = 0; i < labels_.size(); i++)
            if (hot_letter(labels_[i]) && hot_letter(labels_[i]) == lower(ev.ch)) {
                selected = focus_ = i;
                return true;
            }
    }
    return false;
}

void RadioGroup::click(int, int ry, bool)
{
    if (ry >= 0 && ry < labels_.size())
        selected = focus_ = ry;
}

bool RadioGroup::cursor(int* cx, int* cy) const
{
    *cx = x + 1;
    *cy = y + focus_;
    return true;
}

/* ------------------------------------------------------------------ */

Button::Button(const char* label, int r, bool def)
    : result(r), is_default(def), label_(dup(label)), hot_(hot_letter(label))
{
    w = width_of(label) + (def ? 2 : 0);
}

Button::~Button() { free(label_); }

int Button::width_of(const char* label)
{
    char b[256];
    size_t o = 0;
    for (const char* p = label; *p && o + 1 < sizeof(b); p++)
        if (*p != '&')
            b[o++] = *p;
    b[o] = 0;
    return Screen::text_width(b) + 4;
}

void Button::draw(Screen& s, const DialogStyle& st, int sx, int sy, bool focused)
{
    const Style& style = focused ? st.button_focus : st.button;
    s.fill(sx, sy, w, 1, ' ', style);
    int x0 = sx;
    s.text(x0, sy, is_default ? "[< " : "[ ", style);
    x0 += is_default ? 3 : 2;
    Style hot = st.hotkey;
    hot.bg = style.bg;
    x0 += draw_hot(s, x0, sy, label_, style, hot, w);
    s.text(x0, sy, is_default ? " >]" : " ]", style);
}

bool Button::key(const Event& ev)
{
    if (ev.key == KEY_ENTER || (ev.key == KEY_CHAR && ev.ch == ' ' && !ev.mods)) {
        dialog->set_result(result);
        return true;
    }
    return false;
}

void Button::click(int, int, bool)
{
    dialog->set_result(result);
}

/* ------------------------------------------------------------------ */

ListBox::ListBox() : current(0), top(0), activate_result(0) {}

void ListBox::clamp()
{
    int n = items.size();
    if (current >= n)
        current = n - 1;
    if (current < 0)
        current = 0;
    if (current < top)
        top = current;
    if (current >= top + h)
        top = current - h + 1;
    if (top > n - h)
        top = n - h;
    if (top < 0)
        top = 0;
}

void ListBox::draw(Screen& s, const DialogStyle& st, int sx, int sy, bool focused)
{
    clamp();
    for (int r = 0; r < h; r++) {
        int i = top + r;
        const Style& style = i == current && i < items.size()
                                 ? (focused ? st.list_cursor : st.list_cursor_unfocused)
                                 : st.list;
        s.fill(sx, sy + r, w, 1, ' ', style);
        if (i < items.size())
            s.text(sx + 1, sy + r, items[i], style, w - 2);
    }
    /* A scroll mark on the right edge when there is more. */
    if (items.size() > h && h > 1) {
        int pos = (int)((long)current * (h - 1) / (items.size() - 1));
        s.put(sx + w - 1, sy + pos, 0x2592, st.list);
    }
}

bool ListBox::key(const Event& ev)
{
    switch (ev.key) {
    case KEY_UP: current--; break;
    case KEY_DOWN: current++; break;
    case KEY_PGUP: current -= h - 1; break;
    case KEY_PGDN: current += h - 1; break;
    case KEY_HOME: current = 0; break;
    case KEY_END: current = items.size() - 1; break;
    case KEY_ENTER:
        if (activate_result && items.size() > 0) {
            dialog->set_result(activate_result);
            return true;
        }
        return false;
    default:
        return false;
    }
    clamp();
    return true;
}

void ListBox::click(int, int ry, bool dbl)
{
    if (top + ry < items.size())
        current = top + ry;
    if (dbl && activate_result)
        dialog->set_result(activate_result);
}

void ProgressBar::draw(Screen& s, const DialogStyle& st, int sx, int sy, bool)
{
    double f = fraction < 0 ? 0 : fraction > 1 ? 1 : fraction;
    int full = (int)(f * w + 0.5);
    s.fill(sx, sy, w, 1, ' ', st.input);
    s.fill(sx, sy, full, 1, ' ', st.progress);
}

/* ------------------------------------------------------------------ */

Dialog::Dialog(const char* title, int w, int h)
    : title_(dup(title)), w_(w), h_(h), ox_(0), oy_(0), widgets_(0), n_(0), cap_(0), focus_(-1),
      result_(RESULT_NONE), last_click_(0), last_click_widget_(0)
{
}

Dialog::~Dialog()
{
    for (int i = 0; i < n_; i++)
        delete widgets_[i];
    free(widgets_);
    free(title_);
}

void Dialog::set_title(const char* t)
{
    free(title_);
    title_ = dup(t);
}

Widget* Dialog::add(Widget* wd, int x, int y, int w, int h)
{
    if (n_ == cap_) {
        int nc = cap_ ? cap_ * 2 : 16;
        Widget** nw = (Widget**)realloc(widgets_, (size_t)nc * sizeof(Widget*));
        if (!nw)
            return wd;
        widgets_ = nw;
        cap_ = nc;
    }
    wd->x = x;
    wd->y = y;
    if (w > 0)
        wd->w = w;
    if (h > 1 || wd->h < 1)
        wd->h = h;
    wd->dialog = this;
    widgets_[n_++] = wd;
    if (focus_ < 0 && wd->focusable())
        focus_ = n_ - 1;
    return wd;
}

void Dialog::add_buttons(int y, Button** b, int n)
{
    int total = 0;
    for (int i = 0; i < n; i++)
        total += b[i]->w + (i ? 1 : 0);
    int x = (w_ - total) / 2;
    for (int i = 0; i < n; i++) {
        add(b[i], x, y);
        x += b[i]->w + 1;
    }
}

void Dialog::focus(Widget* wd)
{
    for (int i = 0; i < n_; i++)
        if (widgets_[i] == wd)
            focus_ = i;
}

Widget* Dialog::focused() const
{
    return focus_ >= 0 && focus_ < n_ ? widgets_[focus_] : 0;
}

void Dialog::move_focus(int dir)
{
    if (n_ == 0)
        return;
    int i = focus_ < 0 ? 0 : focus_;
    for (int k = 0; k < n_; k++) {
        i = (i + dir + n_) % n_;
        if (widgets_[i]->focusable()) {
            focus_ = i;
            return;
        }
    }
}

void Dialog::draw(Screen& s, const DialogStyle& st)
{
    ox_ = (s.cols() - w_) / 2;
    oy_ = (s.rows() - h_) / 2 - 1;
    if (oy_ < 1)
        oy_ = s.rows() > h_ ? 1 : 0;
    if (ox_ < 0)
        ox_ = 0;
    s.shadow(ox_ + 2, oy_ + h_, w_, 1);
    s.shadow(ox_ + w_, oy_ + 1, 2, h_ - 1);
    s.fill(ox_, oy_, w_, h_, ' ', st.body);
    s.frame(ox_ + 1, oy_, w_ - 2, h_, st.frame_style, st.frame);
    char t[300];
    t[0] = ' ';
    size_t tl = strlen(title_);
    if (tl > sizeof(t) - 3)
        tl = sizeof(t) - 3;
    memcpy(t + 1, title_, tl);
    t[tl + 1] = ' ';
    t[tl + 2] = 0;
    int tw = Screen::text_width(t);
    s.text(ox_ + (w_ - tw) / 2, oy_, t, st.title, w_ - 4);
    int cx = -1, cy = -1;
    for (int i = 0; i < n_; i++) {
        Widget* wd = widgets_[i];
        bool f = i == focus_;
        wd->draw(s, st, ox_ + wd->x, oy_ + wd->y, f);
        if (f && wd->cursor(&cx, &cy)) {
            cx += ox_;
            cy += oy_;
        }
    }
    s.set_cursor(cx, cy, cx >= 0);
}

void Dialog::handle(const Event& ev)
{
    if (ev.key == KEY_ESC) {
        result_ = RESULT_CANCEL;
        return;
    }
    if (ev.key == KEY_MOUSE) {
        if (ev.action != MOUSE_PRESS || ev.button != 1)
            return;
        int rx = ev.x - ox_, ry = ev.y - oy_;
        for (int i = 0; i < n_; i++) {
            Widget* wd = widgets_[i];
            if (rx >= wd->x && rx < wd->x + wd->w && ry >= wd->y && ry < wd->y + wd->h) {
                if (wd->focusable())
                    focus_ = i;
                long t = now_ms();
                bool dbl = wd == last_click_widget_ && t - last_click_ < 400;
                last_click_ = t;
                last_click_widget_ = wd;
                wd->click(rx - wd->x, ry - wd->y, dbl);
                return;
            }
        }
        return;
    }
    Widget* f = focused();
    if (f && f->key(ev))
        return;
    switch (ev.key) {
    case KEY_TAB:
        move_focus(ev.mods & MOD_SHIFT ? -1 : 1);
        return;
    case KEY_DOWN:
    case KEY_RIGHT:
        move_focus(1);
        return;
    case KEY_UP:
    case KEY_LEFT:
        move_focus(-1);
        return;
    case KEY_ENTER:
        for (int i = 0; i < n_; i++)
            if (widgets_[i]->default_result()) {
                result_ = widgets_[i]->default_result();
                return;
            }
        return;
    case KEY_CHAR:
        /* Hotkeys: Alt+letter, or a plain letter that the focused widget
         * did not take. */
        if (ev.mods & MOD_CTRL)
            return;
        for (int i = 0; i < n_; i++) {
            uint32_t h = widgets_[i]->hotkey();
            if (h && h == lower(ev.ch)) {
                if (widgets_[i]->focusable())
                    focus_ = i;
                widgets_[i]->activate();
                return;
            }
        }
        return;
    default:
        return;
    }
}

void Button::activate()
{
    dialog->set_result(result);
}

/* ------------------------------------------------------------------ */
/* Menus                                                               */

MenuBar::MenuBar()
    : n_(0), current_(0), item_(0), open_(false), done_(false), selected_(-1), drop_x_(0),
      drop_y_(0), drop_w_(0), drop_h_(0)
{
}

void MenuBar::add(const char* title, const MenuItem* items)
{
    if (n_ >= 8)
        return;
    menus_[n_].title = title;
    menus_[n_].items = items;
    menus_[n_].x = 0;
    n_++;
}

int MenuBar::count_items(int m) const
{
    int n = 0;
    while (menus_[m].items[n].label || menus_[m].items[n].id)
        n++;
    return n;
}

void MenuBar::open(int index)
{
    current_ = index < 0 || index >= n_ ? 0 : index;
    item_ = 0;
    open_ = true;
    done_ = false;
    selected_ = -1;
}

void MenuBar::move(int dir)
{
    int n = count_items(current_);
    if (n == 0)
        return;
    for (int k = 0; k < n; k++) {
        item_ = (item_ + dir + n) % n;
        if (menus_[current_].items[item_].label)
            return;
    }
}

void MenuBar::draw(Screen& s, const DialogStyle& st, const Style& bar, const Style& bar_hot)
{
    s.fill(0, 0, s.cols(), 1, ' ', bar);
    int x = 2;
    for (int i = 0; i < n_; i++) {
        menus_[i].x = x;
        bool cur = open_ && i == current_;
        const Style& ts = cur ? st.list_cursor : bar;
        int tl = (int)strlen(menus_[i].title);
        s.fill(x - 1, 0, tl + 2, 1, ' ', ts);
        char first[2] = { menus_[i].title[0], 0 };
        Style hot = bar_hot;
        hot.bg = ts.bg;
        s.text(x, 0, first, hot);
        s.text(x + 1, 0, menus_[i].title + 1, ts);
        x += tl + 5;
    }
    if (!open_)
        return;
    /* The pull-down. */
    const MenuItem* items = menus_[current_].items;
    int n = count_items(current_);
    int lw = 0, hw = 0;
    for (int i = 0; i < n; i++) {
        if (!items[i].label)
            continue;
        int l = Button::width_of(items[i].label) - 4;
        if (l > lw)
            lw = l;
        if (items[i].hint && (int)strlen(items[i].hint) > hw)
            hw = (int)strlen(items[i].hint);
    }
    drop_w_ = lw + hw + 6;
    drop_h_ = n + 2;
    drop_x_ = menus_[current_].x - 2;
    if (drop_x_ + drop_w_ > s.cols())
        drop_x_ = s.cols() - drop_w_;
    if (drop_x_ < 0)
        drop_x_ = 0;
    drop_y_ = 1;
    s.shadow(drop_x_ + 2, drop_y_ + drop_h_, drop_w_, 1);
    s.shadow(drop_x_ + drop_w_, drop_y_ + 1, 2, drop_h_ - 1);
    s.fill(drop_x_, drop_y_, drop_w_, drop_h_, ' ', st.body);
    s.frame(drop_x_, drop_y_, drop_w_, drop_h_, st.frame_style, st.frame);
    for (int i = 0; i < n; i++) {
        int y = drop_y_ + 1 + i;
        if (!items[i].label) {
            s.hline(drop_x_, y, drop_w_, st.frame_style, st.frame);
            continue;
        }
        bool cur = i == item_;
        const Style& is = cur ? st.list_cursor : st.body;
        s.fill(drop_x_ + 1, y, drop_w_ - 2, 1, ' ', is);
        Style hot = st.hotkey;
        hot.bg = is.bg;
        draw_hot(s, drop_x_ + 2, y, items[i].label, is, hot, lw);
        if (items[i].hint)
            s.text(drop_x_ + drop_w_ - 2 - (int)strlen(items[i].hint), y, items[i].hint, is);
    }
    s.set_cursor(0, 0, false);
}

void MenuBar::handle(const Event& ev)
{
    const MenuItem* items = menus_[current_].items;
    int n = count_items(current_);
    switch (ev.key) {
    case KEY_ESC:
    case KEY_F1 + 9:
        open_ = false;
        done_ = true;
        selected_ = -1;
        return;
    case KEY_LEFT:
        current_ = (current_ + n_ - 1) % n_;
        item_ = 0;
        return;
    case KEY_RIGHT:
        current_ = (current_ + 1) % n_;
        item_ = 0;
        return;
    case KEY_UP:
        move(-1);
        return;
    case KEY_DOWN:
        move(1);
        return;
    case KEY_HOME:
        item_ = 0;
        return;
    case KEY_END:
        item_ = n - 1;
        return;
    case KEY_ENTER:
        if (item_ < n && items[item_].label) {
            selected_ = items[item_].id;
            done_ = true;
            open_ = false;
        }
        return;
    case KEY_CHAR:
        for (int i = 0; i < n; i++)
            if (items[i].label && hot_letter(items[i].label) == lower(ev.ch)) {
                selected_ = items[i].id;
                done_ = true;
                open_ = false;
                return;
            }
        return;
    case KEY_MOUSE:
        if (ev.action != MOUSE_PRESS || ev.button != 1)
            return;
        if (ev.y == 0) {
            for (int i = n_ - 1; i >= 0; i--)
                if (ev.x >= menus_[i].x - 1) {
                    current_ = i;
                    item_ = 0;
                    return;
                }
            return;
        }
        if (ev.x > drop_x_ && ev.x < drop_x_ + drop_w_ - 1 && ev.y > drop_y_ &&
            ev.y < drop_y_ + drop_h_ - 1) {
            int i = ev.y - drop_y_ - 1;
            if (i < n && items[i].label) {
                selected_ = items[i].id;
                done_ = true;
                open_ = false;
            }
            return;
        }
        open_ = false;
        done_ = true;
        selected_ = -1;
        return;
    default:
        return;
    }
}

} /* namespace tu */
