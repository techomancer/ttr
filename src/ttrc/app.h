/*
 * ttrc's application: the two panels, the command line, the function key
 * bar, menus and dialogs, and the shell, and the event loop that runs
 * them (ttrc/DESIGN.md sections 4 and 7). Commands are in commands.cpp.
 */
#ifndef TTRC_APP_H
#define TTRC_APP_H

#include "cmdline.h"
#include "config.h"
#include "panel.h"
#include "shell.h"
#include "theme.h"
#include "viewer.h"

#include "../../tf/ops.h"
#include "../../tu/input.h"
#include "../../tu/screen.h"
#include "../../tu/term.h"
#include "../../tu/widgets.h"
#include "core/vector.h"

namespace ttrc {

class App {
public:
    App(tu::Term& term, const tf::Path& left, const tf::Path& right, bool dirs_given);
    ~App();
    int run();
    const tf::Path& active_path() const { return panels_[active_]->path(); }

    /* For work that runs while a dialog is open (find file): called
     * between events; return true while there is more to do. */
    typedef bool (*IdleFn)(void* ctx);

private:
    enum Mode {
        MODE_PANELS,        /* ttrc on screen, the shell waits */
        MODE_SHELL,         /* the shell on screen (Ctrl-O) */
        MODE_COMMAND        /* a command from the command line runs */
    };
    enum Pending {
        PEND_NONE,
        PEND_SHOW,          /* show the shell once the cd is done */
        PEND_RUN            /* run pending_cmd once the cd is done */
    };

    /* Drawing */
    void draw();
    void draw_menu_bar();
    void draw_key_bar();
    void draw_command_line();
    void apply_look();

    /* Input */
    bool wait_input(int timeout_ms, bool* timed_out);
    void handle(const tu::Event& ev);
    bool handle_panel_key(const tu::Event& ev);
    void handle_mouse(const tu::Event& ev);
    void shell_input(const char* data, size_t n);
    void function_key(int n);
    void suspend();
    void message(const char* fmt, const char* arg = 0);
    Panel* active() { return panels_[active_]; }
    Panel* other() { return panels_[1 - active_]; }

    /* The shell */
    void start_shell();
    void enter_pressed();
    void run(const char* command);
    bool needs_cd();
    void send_cd();
    void show_shell();
    void back_to_panels(bool finished);
    void follow_shell();
    void tick(long now);
    void shell_output();
    void resize();

    /* Dialogs and commands (commands.cpp) */
    int modal(tu::Dialog& d, const tu::DialogStyle& st, IdleFn idle = 0, void* ctx = 0);
    int ask(const char* title, const char* text, const char* const* buttons, bool alarm);
    bool ask_text(const char* title, const char* label, ttr::Str* value, tu::StrList* history);
    void show_error(const char* text);
    bool run_job(tf::Job& job, const char* title);
    void open_menu(int which);
    void menu_command(int id);
    void cmd_copy(bool move);
    void cmd_mkdir();
    void cmd_delete();
    void cmd_select(char how);
    void cmd_chmod();
    void cmd_chown();
    void cmd_symlink();
    void cmd_hotlist();
    void cmd_find();
    void cmd_compare();
    void cmd_user_menu();
    void cmd_view();
    void cmd_edit();
    void cmd_config();
    void cmd_appearance();
    void save_setup();
    bool open_file(const Entry& e);
    void run_tool(FullScreen* tool, Editor* editor);
    void cmd_new_file();
    ttr::Str local_file(const Entry& e);
    ttr::Str expand(const char* command);
    ttr::Str quoted(const char* s);

    tu::Term& term_;
    tu::Screen screen_;
    tu::Input input_;
    Config config_;
    Theme theme_;
    Panel* panels_[2];
    int active_;
    bool quit_;
    CmdLine cmd_;
    char message_[512];
    bool quick_;
    char quick_text_[128];
    int panel_x_[2], panel_w_[2], panel_y_, panel_h_;
    int shrink_;                /* panel rows given to the shell's output */
    long last_click_;
    int last_click_entry_;
    bool ctrl_x_;               /* Ctrl-X was pressed: a second key follows */
    tu::MenuBar menu_;

    Shell shell_;
    ShellView* view_;
    Mode mode_;
    Pending pending_;
    bool syncing_;
    long sync_started_, cmd_started_;
    ttr::Str pending_cmd_;
    ttr::Str prompt_;           /* the shell's prompt, when last idle */
    bool prompt_known_;
    ttr::Vector<ttr::Str> history_;
    int history_pos_;
    tu::StrList hist_target_, hist_mkdir_, hist_pattern_, hist_find_, hist_search_;
    FullScreen* tool_;          /* the viewer or editor on screen, or 0 */
};

} /* namespace ttrc */

#endif
