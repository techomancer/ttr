/*
 * tu/widgets.h - dialogs and menus drawn into a tu::Screen (ttrc/DESIGN.md
 * section 3.6): labels, input lines, check boxes, radio groups, buttons,
 * lists and a progress bar in a framed dialog with a shadow, and a menu
 * bar with pull-downs. The caller runs the loop: it draws, feeds events
 * to handle() and stops when a result is set, so the program can keep
 * doing its own work (ttrc reads its shell) while a dialog is open.
 */
#ifndef TU_WIDGETS_H
#define TU_WIDGETS_H

#include "input.h"
#include "screen.h"

namespace tu {

struct DialogStyle {
    Style body;             /* background and text */
    Style frame;
    Style title;
    Style input;
    Style input_focus;
    Style button;
    Style button_focus;
    Style hotkey;           /* the underlined letter of buttons and items */
    Style list;
    Style list_cursor;
    Style list_cursor_unfocused;
    Style progress;         /* the filled part */
    FrameStyle frame_style;
};

/* A growing array of owned C strings. */
class StrList {
public:
    StrList() : items_(0), n_(0), cap_(0) {}
    ~StrList() { clear(); }
    void add(const char* s);
    void insert(int at, const char* s);
    void remove(int i);
    void clear();
    int size() const { return n_; }
    const char* operator[](int i) const { return items_[i]; }
    void set(int i, const char* s);

private:
    char** items_;
    int n_, cap_;
    StrList(const StrList&);
    StrList& operator=(const StrList&);
};

class Dialog;

class Widget {
public:
    Widget() : x(0), y(0), w(1), h(1), dialog(0) {}
    virtual ~Widget() {}
    virtual bool focusable() const { return false; }
    /* Draw at screen position (sx, sy). */
    virtual void draw(Screen& s, const DialogStyle& st, int sx, int sy, bool focused) = 0;
    /* True if the event was used. */
    virtual bool key(const Event& ev) { (void)ev; return false; }
    virtual void click(int rx, int ry, bool dbl) { (void)rx; (void)ry; (void)dbl; }
    /* The hotkey letter that focuses (or presses) this widget, or 0. */
    virtual uint32_t hotkey() const { return 0; }
    /* Where the text cursor goes when focused; false: hidden. */
    virtual bool cursor(int* cx, int* cy) const { (void)cx; (void)cy; return false; }
    /* Its hotkey was pressed (after it got the focus). */
    virtual void activate() {}
    /* Enter anywhere ends the dialog with this, if non-zero. */
    virtual int default_result() const { return 0; }

    int x, y, w, h;         /* inside the dialog's frame */
    Dialog* dialog;
};

class Label : public Widget {
public:
    Label(const char* text);
    ~Label();
    void set(const char* text);
    void draw(Screen& s, const DialogStyle& st, int sx, int sy, bool focused);

private:
    char* text_;
};

class InputLine : public Widget {
public:
    InputLine(const char* text = "");
    void set(const char* text);
    /* The first character typed replaces the text (a suggestion, such
     * as the last search); moving first keeps it. */
    void replace_on_type() { fresh_ = len_ > 0; }
    const char* text() const { return buf_; }
    bool focusable() const { return true; }
    void draw(Screen& s, const DialogStyle& st, int sx, int sy, bool focused);
    bool key(const Event& ev);
    bool cursor(int* cx, int* cy) const;
    void click(int rx, int ry, bool dbl);
    /* Shared history for Up and Down (not owned). */
    void set_history(StrList* h) { history_ = h; hpos_ = h ? h->size() : 0; }
    /* Hide the text (passwords). */
    bool masked;

private:
    size_t prev(size_t p) const;
    size_t next(size_t p) const;
    char buf_[2048];
    size_t len_, pos_;
    int scroll_;            /* first shown column */
    bool fresh_;            /* the first key replaces the text */
    StrList* history_;
    int hpos_;
    int last_sx_;
};

class CheckBox : public Widget {
public:
    CheckBox(const char* label, bool on);
    ~CheckBox();
    bool on;
    bool focusable() const { return true; }
    void draw(Screen& s, const DialogStyle& st, int sx, int sy, bool focused);
    bool key(const Event& ev);
    void click(int rx, int ry, bool dbl);
    uint32_t hotkey() const;
    bool cursor(int* cx, int* cy) const;
    void activate() { on = !on; }

private:
    char* label_;
};

class RadioGroup : public Widget {
public:
    RadioGroup();
    void add(const char* label);
    int selected;
    bool focusable() const { return true; }
    void draw(Screen& s, const DialogStyle& st, int sx, int sy, bool focused);
    bool key(const Event& ev);
    void click(int rx, int ry, bool dbl);
    bool cursor(int* cx, int* cy) const;

private:
    StrList labels_;
    int focus_;
};

class Button : public Widget {
public:
    /* "&OK": the letter after & is the hotkey. */
    Button(const char* label, int result, bool is_default = false);
    ~Button();
    int result;
    bool is_default;
    bool focusable() const { return true; }
    void draw(Screen& s, const DialogStyle& st, int sx, int sy, bool focused);
    bool key(const Event& ev);
    void click(int rx, int ry, bool dbl);
    uint32_t hotkey() const { return hot_; }
    void activate();
    int default_result() const { return is_default ? result : 0; }
    static int width_of(const char* label);

private:
    char* label_;
    uint32_t hot_;
};

class ListBox : public Widget {
public:
    ListBox();
    StrList items;
    int current;
    int top;
    /* Enter or a double click: the dialog ends with this result (0: no). */
    int activate_result;
    bool focusable() const { return true; }
    void draw(Screen& s, const DialogStyle& st, int sx, int sy, bool focused);
    bool key(const Event& ev);
    void click(int rx, int ry, bool dbl);
    void clamp();
};

class ProgressBar : public Widget {
public:
    ProgressBar() : fraction(0) {}
    double fraction;        /* 0..1 */
    void draw(Screen& s, const DialogStyle& st, int sx, int sy, bool focused);
};

class Dialog {
public:
    /* Results: >0 from buttons, RESULT_CANCEL for Esc, RESULT_NONE while
     * running. */
    enum { RESULT_NONE = 0, RESULT_CANCEL = -1 };

    Dialog(const char* title, int w, int h);
    ~Dialog();
    /* The dialog owns added widgets. Positions are inside the frame. */
    Widget* add(Widget* wd, int x, int y, int w = -1, int h = 1);
    /* A row of buttons, centred at row y. */
    void add_buttons(int y, Button** buttons, int n);
    void focus(Widget* wd);
    Widget* focused() const;

    void draw(Screen& s, const DialogStyle& st);
    /* Feed an event; result() becomes non-zero when the dialog ends. */
    void handle(const Event& ev);
    int result() const { return result_; }
    void set_result(int r) { result_ = r; }
    int width() const { return w_; }
    int height() const { return h_; }
    void set_title(const char* t);

private:
    void move_focus(int dir);
    char* title_;
    int w_, h_;
    int ox_, oy_;           /* screen position of the last draw */
    Widget** widgets_;
    int n_, cap_;
    int focus_;
    int result_;
    long last_click_;
    Widget* last_click_widget_;
};

/* ---- menus ---- */

struct MenuItem {
    const char* label;      /* "&Copy"; 0 for a separator */
    const char* hint;       /* "F5", or 0 */
    int id;
};

/* A menu bar across the top row and its pull-downs. */
class MenuBar {
public:
    MenuBar();
    /* Titles with their items (arrays ending with a null label and id 0). */
    void add(const char* title, const MenuItem* items);
    void open(int index);
    bool is_open() const { return open_; }
    void draw(Screen& s, const DialogStyle& st, const Style& bar, const Style& bar_hot);
    /* Feed an event; selected() is the chosen id, or -1 when closed
     * without a choice. */
    void handle(const Event& ev);
    int selected() const { return selected_; }
    bool done() const { return done_; }

private:
    int count_items(int menu) const;
    void move(int dir);
    struct Menu {
        const char* title;
        const MenuItem* items;
        int x;
    };
    Menu menus_[8];
    int n_;
    int current_, item_;
    bool open_, done_;
    int selected_;
    int drop_x_, drop_y_, drop_w_, drop_h_;
};

/* Draw a label with "&" marking the hotkey. Returns columns used. */
int draw_hot(Screen& s, int x, int y, const char* label, const Style& st, const Style& hot,
             int max_cols);
/* The hotkey letter of a label ("&Copy" -> 'c'), lower case; 0 if none. */
uint32_t hot_letter(const char* label);

} /* namespace tu */

#endif
