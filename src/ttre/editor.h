/*
 * ttre, the To The Roots editor: one file in a terminal, the text-mode
 * sibling of ttrcode's editor (ttre/DESIGN.md). te does the editing, tu
 * the drawing, th's schemes the colours; the keys are mc's.
 */
#ifndef TTRE_EDITOR_H
#define TTRE_EDITOR_H

#include "settings.h"

#include "../../te/syntax.h"
#include "../../te/view.h"
#include "../../tu/input.h"
#include "../../tu/screen.h"
#include "../../tu/term.h"
#include "../../tu/theme.h"
#include "../../tu/widgets.h"
#include "core/str.h"

namespace ttre {

class Editor {
public:
    Editor(tu::Term& term, const char* path, long line);
    ~Editor();
    /* Read the file (a missing one is a new, empty file). 0 or errno. */
    int load();
    int run();

    /* Menu ids, public for the menu tables. */
    enum {
        M_SAVE = 1, M_SAVE_AS, M_QUIT, M_UNDO, M_REDO, M_MARK, M_COPY, M_MOVE, M_DELETE_BLOCK,
        M_DELETE_LINE, M_SELECT_ALL, M_FIND, M_FIND_NEXT, M_FIND_PREV, M_REPLACE, M_GOTO,
        M_OPTIONS, M_HELP
    };

private:
    struct Colors {
        tu::Style text, select, ctrl, whitespace, gutter, status, current_line;
        tu::Style syntax[te::SYN_COUNT];
        tu::Color ruler_bg;     /* COLOR_DEFAULT: no column marker colour */
        bool tints;             /* the terminal can show subtle backgrounds */
    };

    /* The loop and its dialogs */
    bool wait_input(int timeout_ms, bool* timed_out);
    void resize();
    int modal(tu::Dialog& d, const tu::DialogStyle& st);
    int ask(const char* title, const char* text, const char* const* buttons, bool alarm);
    bool ask_text(const char* title, const char* label, ttr::Str* value, tu::StrList* hist);
    void show_error(const char* text);
    void note(const char* text);

    /* Look and drawing */
    void apply_settings();
    void draw();
    void draw_key_bar();
    int gutter() const;

    /* Commands */
    void handle(const tu::Event& ev);
    void mouse(const tu::Event& ev);
    void function_key(int n, bool shift);
    void command(int id);
    void open_menu(int which);
    bool save(bool ask_name);
    void request_quit();
    void dialog_find(bool replace);
    void find(bool forward);
    void dialog_goto();
    void dialog_options();
    void dialog_help();
    void copy_selection();
    void paste();

    tu::Term& term_;
    tu::Screen screen_;
    tu::Input input_;
    Settings set_;
    tu::Theme theme_;
    Colors col_;
    ttr::Str path_;
    long start_line_;
    te::Document doc_;
    te::View view_;
    te::Syntax syntax_;
    bool marking_;              /* F3 started a block; the next F3 ends it */
    bool quit_;
    char* clip_;
    size_t clip_len_;
    char note_[300];
    ttr::Str find_text_;
    bool match_case_, whole_word_, backwards_;
    tu::StrList hist_find_, hist_replace_;
    long last_click_;
    int click_count_;
    bool dragging_;
};

} /* namespace ttre */

#endif
