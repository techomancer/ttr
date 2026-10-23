/*
 * ttrcode's main window (ttrcode/DESIGN.md): the menu bar, the workspace
 * tree, the tabs and the editor (one TwGrid for every document), the
 * terminal pane and the status bar.
 */
#ifndef TTRCODE_APP_H
#define TTRCODE_APP_H

#include "editsource.h"

#include "../../te/view.h"
#include "../../th/scheme.h"
#include "../../th/theme.h"
#include "../../tt/pane.h"
#include "core/str.h"
#include "core/vector.h"

#include <time.h>
#include <Xm/Xm.h>

namespace ttrcode {

struct Options {
    const char* font;
    const char* fallback_fonts;
    const char* workspace;      /* directory */
    int tab_width;
    int indent_width;
    bool insert_spaces;
    bool line_numbers;
    const char* scheme;         /* text colour scheme (th/scheme.h) */
    const char* ui_theme;       /* UI theme; "" follows ttr's */
    int tree_width, bottom_height;
};

/* An open file. */
struct Doc {
    te::Document text;
    te::View view;
    ttr::Str path;              /* empty: not saved yet */
    ttr::Str name;              /* the tab's label */
    Widget tab;                 /* the tab's toggle button */
    Widget close;               /* and its close button */
    bool shown_modified;        /* what the tab label says */
    time_t mtime;

    Doc() : view(text), tab(0), close(0), shown_modified(false), mtime(0) {}
};

/* The workspace tree's rows. */
struct TreeRow {
    char* path;
    char* name;
    int depth;
    bool dir;
    bool open;
};

/* ~/.config/ttrcode/settings */
ttr::Str settings_path();
/* The UI theme ttr uses (its settings file), or "". */
const char* ttr_theme();

enum Command {
    CMD_NEW, CMD_OPEN, CMD_SAVE, CMD_SAVE_AS, CMD_SAVE_ALL, CMD_CLOSE, CMD_EXIT,
    CMD_UNDO, CMD_REDO, CMD_CUT, CMD_COPY, CMD_PASTE, CMD_DELETE, CMD_SELECT_ALL,
    CMD_FIND, CMD_FIND_NEXT, CMD_FIND_PREV, CMD_REPLACE, CMD_GOTO,
    CMD_VIEW_WORKSPACE, CMD_VIEW_BOTTOM, CMD_VIEW_FULLSCREEN, CMD_VIEW_LINE_NUMBERS,
    CMD_TERMINAL, CMD_NEXT_DOC, CMD_PREV_DOC, CMD_ABOUT
};

class App : public tt::PaneHost {
public:
    App(Widget top, const Options& o);
    ~App();

    /* After XtRealizeWidget: start the terminal, focus the editor. */
    void realized();
    void open_file(const char* path);
    void command(int cmd);
    void quit();

    /* Callbacks from the C trampolines in app.cpp. */
    void editor_key(TwGridCallbackStruct* cb);
    void editor_pointer(TwGridCallbackStruct* cb);
    void editor_resize(TwGridCallbackStruct* cb);
    void editor_focus(bool in);
    void scrolled(int value);
    void tab_chosen(Widget tab, bool set);
    void tab_close(Widget w);
    void sash_event(Widget w, XEvent* ev);
    void tree_activate(int row);
    bool global_key(KeySym ks, unsigned state);
    void autoscroll_step();
    void find_dialog_button(int which);
    Boolean convert(Atom sel, Atom target, Atom* type, XtPointer* value, unsigned long* length,
                    int* format);
    void lost(Atom sel);
    void got_paste(Widget w, Atom sel, Atom type, XtPointer value, unsigned long length,
                   int format, bool second_try);
    void focus_changed_window();
    void scheme_chosen(int i);
    void theme_chosen(int i);

    /* tt::PaneHost */
    void pane_exited(tt::TermPane* p, int code);
    bool pane_key(tt::TermPane* p, TwGridCallbackStruct* cb);
    void pane_focus(tt::TermPane* p, bool in);

private:
    void build_menus();
    void build_layout();
    void make_terminal();
    Doc* doc() { return cur_ >= 0 && cur_ < docs_.size() ? docs_[cur_] : 0; }
    void show_doc(int i);
    Doc* new_doc();
    void close_doc(int i, bool* cancelled);
    bool save_doc(Doc* d, bool ask_name);
    int load(Doc* d, const char* path);
    int write_file(Doc* d, const char* path);
    void after_edit(bool follow);
    void update_ui();
    void update_tab(Doc* d);
    void update_scrollbar();
    void update_title();
    void error(const char* fmt, const char* arg, int err);
    int ask_save(Doc* d);           /* 0 save, 1 discard, 2 cancel */
    bool ask_file(const char* title, const char* start, ttr::Str* out);
    bool ask_text(const char* title, const char* label, ttr::Str* out);
    void find(bool forward);
    void show_find(bool replace);
    void set_fullscreen(bool on);
    void show_workspace(bool on);
    void show_bottom(bool on);
    void layout();
    void set_mnemonics(bool on);
    void own(Atom sel, Time t);
    void request_paste(Atom sel, Time t);
    Time now() const;
    te::Pos pos_at(int col, int row);
    /* The tree. */
    void tree_fill();
    void tree_read(int at, const char* dir, int depth);
    void tree_toggle(int row);
    /* Colours and settings. */
    void set_scheme(const char* name);
    void set_theme(const char* name);
    void build_color_menus(Widget view_menu);
    void save_settings();
    Pixel tab_fill();

    Widget top_, form_, bar_, body_, right_, tree_box_, tree_sash_, bottom_sash_, tree_,
        edit_box_, tabs_, grid_,
        scroll_, status_, bottom_slot_;
    Widget cascades_[8];
    int ncascades_;
    char mnemonics_[8];
    tt::TermPane* term_;
    Options opt_;
    tt::PaneOptions term_opt_;
    EditSource src_;
    ttr::Vector<Doc*> docs_;
    int cur_;
    int untitled_;
    ttr::Vector<TreeRow*> rows_;
    ttr::Str root_;
    /* View state. */
    bool fullscreen_, workspace_on_, bottom_on_;
    bool was_workspace_, was_bottom_;
    int tree_w_, bottom_h_;     /* the side panes' sizes in pixels */
    int drag_start_, drag_size_;    /* a sash being dragged */
    Pixmap close_pixmap_;
    Dimension save_w_, save_h_;
    Position save_x_, save_y_;
    bool in_scroll_update_;
    /* The mouse. */
    bool selecting_;
    Time last_click_;
    int click_count_, last_col_, last_row_;
    int autoscroll_;
    XtIntervalId autoscroll_id_;
    /* Search. */
    ttr::Str find_text_, replace_text_;
    bool match_case_;
    Widget find_dlg_, find_what_, replace_with_, case_toggle_, replace_row_;
    /* Selections. */
    char* primary_;
    size_t primary_len_;
    char* clipboard_;
    size_t clipboard_len_;
    Atom a_clipboard_, a_utf8_, a_targets_, a_text_;
    /* Modal dialogs: the answer, -1 while waiting. */
    int answer_;
    /* Colours: the lists the menus show, and what is chosen. */
    th::List schemes_, themes_;
    Widget* scheme_items_;
    Widget* theme_items_;
    th::Scheme scheme_;
    uint32_t palette_[16];      /* the terminal's, from the scheme */
    ttr::Str theme_name_;       /* "" follows ttr's */
    th::Theme theme_;           /* what that is */
};

} /* namespace ttrcode */

#endif
