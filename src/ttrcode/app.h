/*
 * ttrcode's main window (ttrcode/DESIGN.md): the menu bar, the workspace
 * tree, the tabs and the editor (one TwGrid for every document), the
 * terminal pane and the status bar.
 */
#ifndef TTRCODE_APP_H
#define TTRCODE_APP_H

#include "celllist.h"
#include "dock.h"
#include "editsource.h"
#include "valuetree.h"

#include "../../td/session.h"

#include "../../te/syntax.h"
#include "../../te/view.h"
#include "../../th/scheme.h"
#include "../../th/theme.h"
#include "../../tt/pane.h"
#include "core/str.h"
#include "core/vector.h"

#include <time.h>
#include <Xm/Xm.h>

namespace tx {
class FontChooser;
}
namespace ttr {
class SettingsFile;
}

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
    int tree_width, bottom_height;  /* older settings: the first layout's sizes */
    const char* layout;         /* the docks (dock.h), "" for the default */
    const char* debug_layout;   /* while debugging, "" for the default */
    const char* debug_program;  /* Debug > Settings */
    const char* debug_args;
    const char* debug_dir;
    const char* debug_watch;    /* Watch's expressions, separated by ';' */
    const char* debug_breakpoints;  /* file:line;file:line;function */
    const char* debug_data_breakpoints; /* expressions, separated by ';' */
    bool auto_indent;           /* Enter keeps the previous line's indentation */
    bool show_whitespace;
    bool highlight_line;
    int ruler;                  /* column marker; 0: none */
    const char* cursor_shape;   /* bar, block, underline */
    bool cursor_blink;
    const char* cursor_color;   /* "" : the scheme's */
    const char* renderer;       /* TwGrid's for every grid: auto, core, soft, gl */
    const char* terminal_background;    /* an animated background (GL) */
    const char* terminal_font;  /* "": the editor's */
    const char* text_rendering; /* the editor's: sharp, smooth, auto (tw) */
    const char* terminal_text_rendering;
};

/* An open file. */
struct Doc {
    te::Document text;
    te::View view;
    te::Syntax syntax;
    ttr::Str path;              /* empty: not saved yet */
    ttr::Str name;              /* the tab's label */
    Widget tab;                 /* the tab's toggle button */
    Widget close;               /* and its close button */
    bool shown_modified;        /* what the tab label says */
    time_t mtime;

    Doc() : view(text), syntax(text), tab(0), close(0), shown_modified(false), mtime(0)
    {
        view.set_syntax(&syntax);
    }
    /* The language from the name and the first line. */
    void detect_language()
    {
        syntax.set_language(te::language_for(path.empty() ? name.c_str() : path.c_str(),
                                             text.line(0), text.length(0)));
    }
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

/* A breakpoint set in the editor: a file and a line (from 0); or one on
 * a function, from the Breakpoints dialog. */
struct SourceBreakpoint {
    ttr::Str path;
    long line;
    ttr::Str func;      /* not empty: this function, path and line unused */
};

/* A data breakpoint (Breakpoints dialog): stop when the expression's
 * value changes. */
struct DataBreakpoint {
    ttr::Str expr;
    int id;             /* td's watchpoint while debugging; 0: not set */
    bool local;         /* ends with its frame */
    ttr::Str why;       /* why it is not set */
};

/* A match of Find in Files. */
struct Result {
    char* path;
    long line;
    size_t col, len;
};


enum Command {
    CMD_NEW, CMD_OPEN, CMD_OPEN_WORKSPACE, CMD_SAVE, CMD_SAVE_AS, CMD_SAVE_ALL, CMD_CLOSE, CMD_EXIT,
    CMD_UNDO, CMD_REDO, CMD_CUT, CMD_COPY, CMD_PASTE, CMD_DELETE, CMD_SELECT_ALL,
    CMD_FIND, CMD_FIND_NEXT, CMD_FIND_PREV, CMD_REPLACE, CMD_GOTO,
    CMD_FIND_IN_FILES, CMD_REPLACE_IN_FILES, CMD_NEXT_RESULT, CMD_PREV_RESULT, CMD_OPTIONS,
    CMD_VIEW_WORKSPACE, CMD_VIEW_BOTTOM, CMD_VIEW_FULLSCREEN, CMD_VIEW_LINE_NUMBERS,
    CMD_TERMINAL, CMD_NEXT_DOC, CMD_PREV_DOC, CMD_ABOUT, CMD_VIEW_RESULTS,
    /* Debug (debug.cpp). */
    CMD_DEBUG_GO, CMD_DEBUG_STOP, CMD_DEBUG_RESTART, CMD_DEBUG_BREAK, CMD_DEBUG_STEP_INTO,
    CMD_DEBUG_STEP_OVER, CMD_DEBUG_STEP_OUT, CMD_DEBUG_RUN_TO_CURSOR, CMD_DEBUG_TOGGLE_BP,
    CMD_DEBUG_SETTINGS, CMD_VIEW_PROGRAM, CMD_DEBUG_QUICKWATCH, CMD_DEBUG_BREAKPOINTS,
    CMD_DEBUG_CORE,
    CMD_DEBUG_ATTACH,
    /* View > Debug Windows: one per pane in k_debug_panes (app.cpp). */
    CMD_VIEW_DEBUG
};

/* The debugging layout until the user makes their own. */
const char* default_debug_layout();

class App : public tt::PaneHost, public PaneBuilder, public LineMarks, public CellListHost {
public:
    App(Widget top, const Options& o);
    ~App();

    /* After XtRealizeWidget: start the terminal, focus the editor. */
    void realized();
    void open_file(const char* path);
    /* The tree shows 'dir', and the terminal goes there if its shell is
     * at the prompt. */
    void open_workspace(const char* dir);
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
    void restart_terminal();
    void options_button(int which);
    void fif_button(int which);
    void result_chosen(int i);
    void theme_chosen(int i);

    /* tt::PaneHost */
    void pane_exited(tt::TermPane* p, int code);
    bool pane_key(tt::TermPane* p, TwGridCallbackStruct* cb);
    void pane_focus(tt::TermPane* p, bool in);

    /* PaneBuilder: the docks' panes (dock.h). */
    Widget build_pane(Pane* p, Widget parent);
    void drop_pane(Pane* p);
    void layout_changed();

    /* LineMarks: breakpoints and the program's place in the margin. */
    int line_marks(const te::View* v, long line) const;
    /* Debugger panes' callbacks (debug.cpp, debugpanes.cpp). */
    void frame_chosen(int i);
    /* DataTips */
    void tip_motion(TwGridCallbackStruct* cb);
    bool tip_expression(int col, int row, ttr::Str* out);
    void tip_show();
    void tip_hide();
    static void tip_timeout(XtPointer data, XtIntervalId* id);
    void thread_chosen(int row);
    void debug_settings_button(int which);
    void vars_mode(bool autos);
    void memory_address_entered();
    void memory_format_chosen(int f);
    void quickwatch_button(int which);
    void breakpoints_button(int which);
    void attach_button(int which);
    void breakpoints_entered(bool data);

    /* CellListHost: the debugger's panes (debugpanes.cpp). */
    void list_activate(CellList* l, int row);
    void list_toggle(CellList* l, int row);
    bool list_key(CellList* l, TwGridCallbackStruct* cb);
    void list_edited(CellList* l, int row, const char* text);
    void list_resized(CellList* l);

private:
    void build_menus();
    void build_layout();
    void make_terminal(Widget parent);
    /* The debugger (debug.cpp). */
    void debug_command(int cmd);
    bool debug_start();
    void debug_end();
    void debug_stopped(const td::Stop& s);
    void debug_show_frame(int i, bool focus_editor);
    void debug_fill();
    void debug_status(const char* msg);
    void show_debug_settings();
    void toggle_breakpoint();
    /* Debug > Open Core File. */
    void debug_open_core();
    /* The Breakpoints dialog (breakpoints.cpp). */
    void show_breakpoints();
    void show_attach();
    void fill_attach();
    bool debug_attach(int pid, const char* name);
    int source_frame() const;
    void fill_breakpoints();
    bool add_location_breakpoint(const char* spec);
    bool add_data_breakpoint(const char* expr);
    void set_data_breakpoints();
    void remove_breakpoint_at(int item);
    ttr::Str breakpoint_spec(const SourceBreakpoint& b) const;
    void goto_source(const char* path, long line);
    Widget build_debug_pane(Pane* p, Widget parent);
    void drop_debug_pane(Pane* p);
    static void debug_idle(void* ctx);
    /* The debugger's keys (VC6), from the editor and the debug panes. */
    bool debug_key(KeySym ks, unsigned state);
    /* The panes (debugpanes.cpp). */
    CellList* make_list(Widget parent, const char* name, int tag);
    ListColors list_colors() const;
    void recolor_lists();
    void fill_callstack();
    void fill_registers();
    void fill_threads();
    void load_frames();
    void load_threads();
    void fill_variables();
    void fill_watch();
    void fill_memory();
    void fill_quickwatch();
    void auto_values(ttr::Vector<td::Value>* out);
    void show_quickwatch();
    void add_watch(const char* expr);
    void save_watches(ttr::SettingsFile* f);
    int watch_index(int row) const;
    const td::Frame* frame() const
    {
        return dbg_ && cur_frame_ < frames_.size() ? &frames_[cur_frame_] : 0;
    }
    void make_panes();
    ttr::Str default_layout() const;
    /* Open the pane (where it was last) and give it the focus. */
    void show_pane(Pane* p);
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
    bool ask_dir(const char* title, const char* start, ttr::Str* out);
    void find(bool forward);
    void show_find(bool replace);
    void set_fullscreen(bool on);
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
    /* Options (options.cpp). */
    void apply_look();
    void show_options();
    void set_font(const char* xlfd);
    /* "": the editor's font. */
    void set_terminal_font(const char* xlfd);
    void set_text_rendering(const char* editor, const char* terminal);
    /* Find in Files and the results pane (findfiles.cpp). */
    Widget build_results(Widget parent);
    void show_results();
    void show_find_in_files(bool replace);
    int find_in_files(const char* what, const char* dir, const char* types, bool match_case,
                      bool whole, int* files);
    void replace_in_files(const char* what, const char* with, const char* dir,
                          const char* types, bool match_case, bool whole);
    void search_file(const char* path, const char* what, bool match_case, bool whole);
    void walk(const char* dir, const char* what, const char* types, bool match_case, bool whole,
              int depth);
    void clear_results();
    void fill_results();
    void goto_result(int i);
    bool confirm(const char* message);

    Widget top_, form_, bar_, body_, tree_, edit_box_, tabs_, grid_, scroll_, status_;
    /* The dock areas and their panes. */
    Docks* docks_;
    Pane* pane_workspace_;
    Pane* pane_terminal_;
    Pane* pane_results_;
    Pane* pane_program_;        /* the debugged program's terminal */
    ttr::Vector<Pane*> debug_panes_;
    Widget cascades_[8];
    int ncascades_;
    char mnemonics_[8];
    tt::TermPane* term_;
    tt::TermPane* prog_term_;   /* no program of its own (PaneOptions::no_program) */
    Options opt_;
    tt::PaneOptions term_opt_;
    EditSource src_;
    ttr::Vector<Doc*> docs_;
    int cur_;
    int untitled_;
    ttr::Vector<TreeRow*> rows_;
    ttr::Str root_;
    /* View state. */
    bool fullscreen_;
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
    /* DataTips (datatip.cpp) */
    XtIntervalId tip_timer_;
    Widget tip_shell_, tip_label_;
    bool tip_up_, tip_watch_;
    ttr::Str tip_expr_;
    int tip_x_, tip_y_;         /* where the mouse rests, on the screen */
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
    /* Strings opt_ points into, and the font TwGrid holds. */
    ttr::Str cursor_shape_, cursor_color_;
    char* font_;
    char* term_font_;           /* "" : the editor's */
    ttr::Str text_, term_text_;
    /* The Options dialog. */
    tx::FontChooser* opt_font_;
    tx::FontChooser* opt_term_font_;
    Widget opt_text_, opt_term_text_;
    Widget opt_texts_[3], opt_term_texts_[3];
    Widget opt_renderer_, opt_renderers_[4];
    bool renderer_chosen_;      /* in Options: saved (else -renderer stays out of the file) */
    Widget opt_dlg_, opt_tab_, opt_indent_, opt_spaces_, opt_auto_, opt_ws_,
        opt_line_, opt_numbers_, opt_ruler_, opt_shape_, opt_blink_, opt_ccolor_;
    Widget opt_shapes_[3];
    /* Find in Files. */
    Widget fif_dlg_, fif_what_, fif_with_, fif_with_label_, fif_folder_, fif_types_, fif_case_,
        fif_word_, fif_find_btn_, fif_replace_btn_;
    bool fif_replace_;
    /* The debugger. */
    td::Session* dbg_;
    ttr::Vector<SourceBreakpoint> bps_;
    ttr::Vector<DataBreakpoint> data_bps_;
    ttr::Str dbg_program_, dbg_args_, dbg_dir_;
    ttr::Str stop_path_;        /* where the program stopped, "": nowhere */
    long stop_line_;
    ttr::Str frame_path_;       /* the caller chosen in Call Stack */
    long frame_line_;
    int cur_frame_;
    ttr::Vector<td::Frame> frames_;
    /* The threads at the last stop, and where each was (the Threads
     * pane). */
    ttr::Vector<int> thread_ids_;
    ttr::Vector<ttr::Str> thread_where_;
    td::Regs last_regs_;        /* the last stop's, to mark the registers that changed */
    bool have_last_regs_;
    td::Regs cur_regs_;         /* this stop's (frame 0) */
    int regs_stop_;             /* the stop cur_regs_ is from */
    int regs_thread_;           /* and the thread */
    bool dbg_kill_req_;         /* Stop Debugging while it runs */
    bool dbg_attached_;         /* attached to it: Stop Debugging detaches */
    ttr::Str edit_layout_, debug_layout_;
    /* The debugger's panes (debugpanes.cpp): 0 while closed. */
    CellList* stack_list_;
    CellList* regs_list_;
    CellList* threads_list_;
    CellList* watch_list_;
    CellList* vars_list_;
    CellList* mem_list_;
    Widget vars_box_, vars_context_, vars_menu_, vars_auto_w_, vars_locals_w_;
    Widget mem_box_, mem_addr_w_, mem_format_w_;
    ValueTree watch_tree_, vars_tree_, quick_tree_;
    ttr::Vector<ttr::Str> watches_;
    bool vars_auto_;            /* Variables shows Auto, else Locals */
    int vars_menu_stop_;        /* the stop Context's frames are from */
    int mem_format_;            /* MEM_BYTE... (debugpanes.cpp) */
    ttr::Str mem_expr_;         /* the address, as typed: evaluated at every stop */
    td::Addr mem_addr_;
    int mem_per_row_;           /* units on a row, from the pane's width */
    /* What Memory showed at the stop before, to mark what changed. */
    ttr::Vector<unsigned char> mem_prev_, mem_cur_;
    td::Addr mem_prev_addr_, mem_cur_addr_;
    int mem_prev_stop_;
    int frames_serial_;         /* counts frames_ reloads (stops, thread switches) */
    int stop_serial_;           /* counts stops: what "changed" compares with */
    Widget quick_dlg_, quick_expr_;
    CellList* quick_list_;
    ttr::Str quick_text_;
    Widget dbg_dlg_, dbg_prog_, dbg_args_w_, dbg_dir_w_;
    Widget bp_dlg_, bp_at_w_, bp_expr_w_, bp_list_w_, bp_note_w_;
    Widget attach_dlg_;
    ttr::Vector<td::ProcessInfo> attach_procs_;   /* what the dialog lists */
    ttr::Str dbg_msg_;          /* the status bar's first words */
    /* Find Results (0 while its pane is closed). */
    Widget results_;
    bool searched_;             /* a search has run: the list has something to say */
    ttr::Vector<Result*> results_list_;
    int cur_result_;
    int scanned_, result_files_;
    ttr::Str fif_what_text_;
    bool fif_case_on_, fif_word_on_;
};

} /* namespace ttrcode */

#endif
