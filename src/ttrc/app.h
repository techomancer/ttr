/*
 * ttrc's application: the two panels, the command line, the function key
 * bar and the shell, and the event loop that runs them (ttrc/DESIGN.md
 * sections 4 and 7).
 */
#ifndef TTRC_APP_H
#define TTRC_APP_H

#include "cmdline.h"
#include "panel.h"
#include "shell.h"
#include "theme.h"

#include "../../tu/input.h"
#include "../../tu/screen.h"
#include "../../tu/term.h"
#include "core/vector.h"

namespace ttrc {

class App {
public:
    App(tu::Term& term, const tf::Path& left, const tf::Path& right);
    ~App();
    int run();
    const tf::Path& active_path() const { return panels_[active_]->path(); }

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

    /* Input */
    void handle(const tu::Event& ev);
    bool handle_panel_key(const tu::Event& ev);
    void handle_mouse(const tu::Event& ev);
    void shell_input(const char* data, size_t n);
    void function_key(int n);
    void suspend();
    void message(const char* fmt, const char* arg = 0);
    Panel* active() { return panels_[active_]; }

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

    tu::Term& term_;
    tu::Screen screen_;
    tu::Input input_;
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
};

} /* namespace ttrc */

#endif
