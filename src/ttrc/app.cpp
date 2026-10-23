#include "app.h"

#include "../../tf/archive.h"
#include "../../tf/vfs.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

namespace ttrc {

namespace {

long now_ms()
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

const char k_ctrl_o = 0x0f;
const char k_ctrl_u = 0x15;

} /* namespace */

namespace {
char g_program_dir[1024];
}

void set_program_path(const char* argv0)
{
    const char* slash = argv0 ? strrchr(argv0, '/') : 0;
    if (!slash || slash - argv0 >= (long)sizeof(g_program_dir))
        return;
    memcpy(g_program_dir, argv0, (size_t)(slash - argv0));
    g_program_dir[slash - argv0] = 0;
}

ttr::Str ttre_path()
{
    if (g_program_dir[0]) {
        char p[1100];
        snprintf(p, sizeof(p), "%s/ttre", g_program_dir);
        if (access(p, X_OK) == 0) {
            /* An absolute path: the shell may be somewhere else. */
            if (p[0] != '/') {
                char cwd[900];
                if (getcwd(cwd, sizeof(cwd))) {
                    char abs[2000];
                    snprintf(abs, sizeof(abs), "%s/%s", cwd, p);
                    return ttr::Str(abs);
                }
            }
            return ttr::Str(p);
        }
    }
    return ttr::Str("ttre");
}

App::App(tu::Term& term, const tf::Path& left, const tf::Path& right, bool dirs_given)
    : term_(term), screen_(term), active_(0), quit_(false), quick_(false), panel_y_(1),
      panel_h_(0), shrink_(0), last_click_(0), last_click_entry_(-1), ctrl_x_(false), view_(0),
      mode_(MODE_PANELS), pending_(PEND_NONE), syncing_(false), sync_started_(0),
      cmd_started_(0), prompt_known_(false), history_pos_(0), tool_(0), progress_(0),
      progress_text_(0), progress_bar_(0), in_progress_(false)
{
    config_.load();
    input_.init(term.info(), term.utf8());
    apply_look();
    tf::Path l = left, r = right;
    /* Where the panels were last time, unless directories were given. */
    if (!dirs_given && config_.remember_dirs) {
        if (!config_.left_dir.empty())
            l = tf::Path(config_.left_dir.c_str());
        if (!config_.right_dir.empty())
            r = tf::Path(config_.right_dir.c_str());
    }
    panels_[0] = new Panel(tf::vfs(), l);
    panels_[1] = new Panel(tf::vfs(), r);
    panels_[0]->set_list_mode(config_.left_mode == LIST_BRIEF ? LIST_BRIEF : LIST_FULL);
    panels_[1]->set_list_mode(config_.right_mode == LIST_BRIEF ? LIST_BRIEF : LIST_FULL);
    panels_[0]->set_sort_mode((SortMode)(config_.left_sort & 15), (config_.left_sort & 16) != 0);
    panels_[1]->set_sort_mode((SortMode)(config_.right_sort & 15), (config_.right_sort & 16) != 0);
    panels_[0]->set_hidden(config_.show_hidden);
    panels_[1]->set_hidden(config_.show_hidden);
    message_[0] = 0;
    quick_text_[0] = 0;
    panel_x_[0] = panel_x_[1] = panel_w_[0] = panel_w_[1] = 0;
}

/* The theme and terminal tier from the settings. */
void App::apply_look()
{
    const char* t = config_.tier.c_str();
    if (strcmp(t, "basic") == 0)
        term_.set_tier(tu::TIER_BASIC);
    else if (strcmp(t, "ansi") == 0)
        term_.set_tier(tu::TIER_ANSI);
    else if (strcmp(t, "256") == 0)
        term_.set_tier(tu::TIER_256);
    else if (strcmp(t, "modern") == 0)
        term_.set_tier(tu::TIER_MODERN);
    theme_ = theme_for(term_.tier(), config_.theme.c_str());
    screen_.invalidate();
}

App::~App()
{
    tf::set_progress(0, 0);
    delete progress_;
    remove_temp_dir();
    delete view_;
    delete panels_[0];
    delete panels_[1];
}

void App::message(const char* fmt, const char* arg)
{
    snprintf(message_, sizeof(message_), fmt, arg ? arg : "");
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */

void App::draw_menu_bar()
{
    int w = screen_.cols();
    screen_.fill(0, 0, w, 1, ' ', theme_.menu_bar);
    static const char* const items[] = { "Left", "File", "Command", "Options", "Right", 0 };
    int x = 2;
    for (int i = 0; items[i]; i++) {
        char first[2] = { items[i][0], 0 };
        screen_.text(x, 0, first, theme_.menu_hotkey);
        screen_.text(x + 1, 0, items[i] + 1, theme_.menu_bar);
        x += (int)strlen(items[i]) + 5;
    }
}

void App::draw_key_bar()
{
    static const char* const labels[10] = {
        "Help", "Menu", "View", "Edit", "Copy", "RenMov", "Mkdir", "Delete", "PullDn", "Quit"
    };
    int w = screen_.cols(), y = screen_.rows() - 1;
    int slot = w / 10;
    if (slot < 3)
        return;
    screen_.fill(0, y, w, 1, ' ', theme_.key_num);
    for (int i = 0; i < 10; i++) {
        int x = i * slot;
        char num[4];
        sprintf(num, "%d", i + 1);
        int nw = (int)strlen(num);
        screen_.text(x, y, num, theme_.key_num);
        int lw = (i == 9 ? w - x : slot) - nw;
        screen_.fill(x + nw, y, lw, 1, ' ', theme_.key_label);
        screen_.text(x + nw, y, labels[i], theme_.key_label, lw);
    }
}

void App::draw_command_line()
{
    int w = screen_.cols(), y = screen_.rows() - 2;
    screen_.fill(0, y, w, 1, ' ', theme_.command);
    if (message_[0]) {
        screen_.text(0, y, message_, theme_.error, w);
        screen_.set_cursor(0, y, false);
        return;
    }
    if (quick_) {
        char b[200];
        sprintf(b, "Search: %.120s", quick_text_);
        int used = screen_.text(0, y, b, theme_.command, w);
        screen_.set_cursor(used, y, true);
        return;
    }
    /* The shell's own prompt, as it last showed it. */
    char prompt[600];
    if (prompt_known_ && !prompt_.empty()) {
        snprintf(prompt, sizeof(prompt), "%s", prompt_.c_str());
    } else {
        const char* p = active_path().c_str();
        const char* home = getenv("HOME");
        size_t hl = home ? strlen(home) : 0;
        if (hl > 1 && strncmp(p, home, hl) == 0 && (p[hl] == '/' || p[hl] == 0))
            snprintf(prompt, sizeof(prompt), "~%.500s$ ", p + hl);
        else
            snprintf(prompt, sizeof(prompt), "%.500s$ ", p);
    }
    int pw = tu::Screen::text_width(prompt);
    if (pw > w / 2) {
        strcpy(prompt, "> ");
        pw = 2;
    }
    screen_.text(0, y, prompt, theme_.command);
    int room = w - pw - 1;
    char before[2048];
    memcpy(before, cmd_.buf, cmd_.pos);
    before[cmd_.pos] = 0;
    int cur = tu::Screen::text_width(before);
    const char* shown = cmd_.buf;
    while (cur > room && *shown) {
        uint32_t cp;
        int n = tu::decode_char(shown, &cp);
        char one[8];
        memcpy(one, shown, (size_t)n);
        one[n] = 0;
        cur -= tu::Screen::text_width(one);
        shown += n;
    }
    screen_.text(pw, y, shown, theme_.command, room + 1);
    screen_.set_cursor(pw + cur, y, true);
}

void App::draw()
{
    if (tool_) {
        tool_->draw(screen_, theme_);
        return;
    }
    int w = screen_.cols(), h = screen_.rows();
    screen_.clear(theme_.command);
    if (w < 24 || h < 8) {
        screen_.text(0, 0, "ttrc: the window is too small", tu::Style());
        screen_.set_cursor(0, 0, false);
        return;
    }
    draw_menu_bar();
    int full = h - 3;
    if (shrink_ > full - 6)
        shrink_ = full - 6;
    if (shrink_ < 0)
        shrink_ = 0;
    panel_y_ = 1;
    panel_h_ = full - shrink_;
    panel_w_[0] = w / 2;
    panel_w_[1] = w - w / 2;
    panel_x_[0] = 0;
    panel_x_[1] = panel_w_[0];
    for (int i = 0; i < 2; i++)
        panels_[i]->draw(screen_, theme_, panel_x_[i], panel_y_, panel_w_[i], panel_h_,
                         i == active_);
    /* Under shortened panels: the shell's last lines above its prompt. */
    if (shrink_ > 0 && shell_.alive()) {
        int cy = shell_.copy().cursor().y;
        int from = cy - shrink_;
        if (from < 0)
            from = 0;
        shell_.paint(screen_, from, panel_y_ + panel_h_, shrink_);
    }
    draw_command_line();
    draw_key_bar();
}

/* ------------------------------------------------------------------ */
/* The shell                                                           */

void App::start_shell()
{
    const char* dir = tf::is_local_path(active_path().c_str()) ? active_path().c_str() : "/";
    if (!shell_.start(term_.cols(), term_.rows(), dir, false, term_.utf8())) {
        message("No shell: %s", shell_.error());
        return;
    }
    view_ = new PassThroughView(shell_, term_, screen_);
}

/* The shell is somewhere else than the active panel. */
bool App::needs_cd()
{
    char where[4096];
    /* The shell cannot go into an archive; it stays where it is. */
    if (!tf::is_local_path(active_path().c_str()))
        return false;
    if (!shell_.cwd(where, sizeof(where)))
        return false;
    return strcmp(where, active_path().c_str()) != 0;
}

void App::send_cd()
{
    char line[1200];
    if (!shell_.cd_command(active_path().c_str(), line, sizeof(line))) {
        message("%s", "The shell cannot be sent to this directory (its name cannot be quoted for it).");
        return;
    }
    shell_.write(line);
    syncing_ = true;
    sync_started_ = now_ms();
}

void App::show_shell()
{
    if (!view_ || !shell_.alive())
        return;
    if (!view_->shown())
        view_->show();
}

/* Ctrl-O from the panels: the shell, in the active panel's directory,
 * with what was typed on ttrc's command line. */
void App::enter_pressed()
{
    const char* c = cmd_.buf;
    while (*c == ' ')
        c++;
    if (!*c) {
        if (!active()->enter() && active()->error()[0])
            message("%s", active()->error());
        return;
    }
    /* cd without anything else is ours: it also goes into archives. */
    if (strncmp(c, "cd", 2) == 0 && (c[2] == 0 || c[2] == ' ') && !strpbrk(c, ";|&`$<>(")) {
        const char* arg = c + 2;
        while (*arg == ' ')
            arg++;
        char target[2048];
        snprintf(target, sizeof(target), "%s", *arg ? arg : "~");
        size_t tl = strlen(target);
        while (tl > 1 && target[tl - 1] == ' ')
            target[--tl] = 0;
        /* Quotes around the whole argument. */
        if (tl >= 2 && (target[0] == '\'' || target[0] == '"') && target[tl - 1] == target[0]) {
            target[tl - 1] = 0;
            memmove(target, target + 1, tl - 1);
        }
        tf::Path p = tf::Path::resolve(active_path(), target);
        if (!active()->load(p))
            message("%s", active()->error());
        history_.push_back(ttr::Str(c));
        history_pos_ = history_.size();
        cmd_.clear();
        return;
    }
    if (strcmp(c, "exit") == 0) {
        cmd_.clear();
        request_quit();
        return;
    }
    run(c);
}

/* Run a command in the shell, in the active panel's directory. */
void App::run(const char* command)
{
    if (!shell_.alive()) {
        message("%s", "There is no shell to run commands in.");
        return;
    }
    if (!shell_.idle(now_ms())) {
        /* Still starting (F4 right after ttrc came up): run it as soon
         * as the shell has its prompt. */
        if (!prompt_known_ && mode_ == MODE_PANELS) {
            queued_cmd_ = command;
            return;
        }
        message("%s", "The shell is still busy. Ctrl-O shows it.");
        return;
    }
    history_.push_back(ttr::Str(command));
    history_pos_ = history_.size();
    pending_cmd_ = command;
    cmd_.clear();
    show_shell();
    mode_ = MODE_COMMAND;
    if (needs_cd()) {
        pending_ = PEND_RUN;
        send_cd();
        if (syncing_)
            return;
    }
    shell_.write(pending_cmd_.c_str());
    shell_.write("\r");
    cmd_started_ = now_ms();
    pending_ = PEND_NONE;
}

/* Back from the shell: the panels, following the shell's directory, and
 * whatever was typed at its prompt on ttrc's command line. */
void App::back_to_panels(bool finished)
{
    long now = now_ms();
    if (shell_.alive() && shell_.idle(now)) {
        ttr::Str typed = shell_.typed();
        if (!typed.empty() && !finished) {
            cmd_.set(typed.c_str());
            char kill[1] = { k_ctrl_u };
            shell_.write(kill, 1);
        }
    }
    if (view_)
        view_->hide();
    mode_ = MODE_PANELS;
    pending_ = PEND_NONE;
    syncing_ = false;
    prompt_known_ = false;      /* read the prompt again once idle */
    follow_shell();
    /* The command may have changed files. */
    panels_[0]->reload();
    panels_[1]->reload();
}

/* The active panel goes where the shell went. */
void App::follow_shell()
{
    char where[4096];
    if (shell_.cwd(where, sizeof(where)) && strcmp(where, active_path().c_str()) != 0)
        active()->load(tf::Path(where));
}

void App::tick(long now)
{
    if (!shell_.alive())
        return;
    if (syncing_) {
        if (now - sync_started_ >= 150 && shell_.idle(now)) {
            syncing_ = false;
            if (pending_ == PEND_RUN) {
                shell_.write(pending_cmd_.c_str());
                shell_.write("\r");
                cmd_started_ = now;
            }
            pending_ = PEND_NONE;
        }
        return;
    }
    if (mode_ == MODE_COMMAND && now - cmd_started_ >= 200 && shell_.idle(now)) {
        if (config_.auto_return)
            back_to_panels(true);
        else
            mode_ = MODE_SHELL;     /* stay in the shell; Ctrl-O comes back */
        return;
    }
    if (mode_ == MODE_PANELS && !prompt_known_ && shell_.idle(now)) {
        shell_.mark_prompt();
        prompt_ = shell_.prompt();
        prompt_known_ = true;
    }
    if (mode_ == MODE_PANELS && prompt_known_ && !queued_cmd_.empty()) {
        ttr::Str c = queued_cmd_;
        queued_cmd_ = "";
        run(c.c_str());
    }
}

void App::shell_output()
{
    char buf[16384];
    size_t n = 0;
    if (!shell_.read(buf, sizeof(buf), &n)) {
        quit_ = true;           /* the shell has gone: so does ttrc */
        return;
    }
    if (n > 0) {
        if (view_)
            view_->output(buf, n);
        if (mode_ == MODE_PANELS)
            prompt_known_ = false;
    }
}

/* Keys while the shell is on screen go to it, except Ctrl-O. */
void App::shell_input(const char* data, size_t n)
{
    const char* o = (const char*)memchr(data, k_ctrl_o, n);
    if (!o) {
        shell_.write(data, n);
        return;
    }
    if (o > data)
        shell_.write(data, (size_t)(o - data));
    back_to_panels(false);
    size_t rest = n - (size_t)(o - data) - 1;
    if (rest)
        input_.feed(o + 1, rest);
}

void App::resize()
{
    term_.update_size();
    screen_.resize();
    if (shell_.alive())
        shell_.resize(term_.cols(), term_.rows());
}

/* ------------------------------------------------------------------ */
/* Keys                                                                */

void App::function_key(int n)
{
    switch (n) {
    case 1:
        message("%s", "Tab: other panel. Ins: mark. Ctrl-O: shell. F9: menu. "
                      "Alt-T: listing. Ctrl-S: search. F10: quit.");
        break;
    case 2: cmd_user_menu(); break;
    case 3: cmd_view(); break;
    case 4: cmd_edit(); break;
    case 5: cmd_copy(false); break;
    case 6: cmd_copy(true); break;
    case 7: cmd_mkdir(); break;
    case 8: cmd_delete(); break;
    case 9: open_menu(active_ == 0 ? 0 : 4); break;
    case 10: request_quit(); break;
    default: break;
    }
}

void App::request_quit()
{
    if (config_.confirm_exit) {
        static const char* const b[] = { "&Yes", "&No", 0 };
        if (ask("Quit", "Quit ttrc?", b, false) != 1)
            return;
    }
    quit_ = true;
}

bool App::handle_panel_key(const tu::Event& ev)
{
    Panel* p = active();
    bool text = cmd_.len > 0;
    switch (ev.key) {
    case tu::KEY_UP:
        if (ev.mods & tu::MOD_CTRL) {
            shrink_++;
            return true;
        }
        p->move(-1);
        return true;
    case tu::KEY_DOWN:
        if (ev.mods & tu::MOD_CTRL) {
            shrink_--;
            return true;
        }
        p->move(1);
        return true;
    case tu::KEY_PGUP:
        if (ev.mods & tu::MOD_CTRL) {
            if (!p->up() && p->error()[0])
                message("%s", p->error());
        } else {
            p->page(-1);
        }
        return true;
    case tu::KEY_PGDN:
        if (ev.mods & tu::MOD_CTRL)
            p->enter();
        else
            p->page(1);
        return true;
    case tu::KEY_HOME:
        if (text)
            return false;
        p->home();
        return true;
    case tu::KEY_END:
        if (text)
            return false;
        p->end();
        return true;
    case tu::KEY_LEFT:
        if (text)
            return false;
        p->column(-1);
        return true;
    case tu::KEY_RIGHT:
        if (text)
            return false;
        p->column(1);
        return true;
    case tu::KEY_INSERT:
        p->toggle_mark();
        return true;
    case tu::KEY_TAB:
        active_ = 1 - active_;
        return true;
    case tu::KEY_ENTER:
        if (ev.mods & (tu::MOD_ALT | tu::MOD_CTRL)) {
            /* The file name onto the command line. */
            const Entry* e = p->current();
            char q[1100];
            if (e && !e->up && shell_.quote(e->name.c_str(), q, sizeof(q) - 1)) {
                strcat(q, " ");
                cmd_.insert_text(q);
            }
            return true;
        }
        if (!text) {
            const Entry* e = p->current();
            /* An executable file runs, as in mc; other files go to their
             * association, if one matches. */
            if (e && Panel::is_archive(*e)) {
                if (!p->enter())
                    message("%s", p->error());
                return true;
            }
            if (e && !e->up && e->st.type == tf::FT_FILE && (e->st.mode & 0111) &&
                tf::is_local_path(active_path().c_str())) {
                char q[1100], c[1200];
                if (shell_.quote(e->name.c_str(), q, sizeof(q))) {
                    snprintf(c, sizeof(c), "./%s", q);
                    run(c);
                }
                return true;
            }
            if (e && !e->up && e->st.type != tf::FT_DIR &&
                !(e->st.type == tf::FT_LINK && e->target == tf::FT_DIR) && open_file(*e))
                return true;
        }
        enter_pressed();
        return true;
    default:
        break;
    }
    if (ev.key >= tu::KEY_F1 && ev.key <= tu::KEY_F1 + 11 && (ev.mods & tu::MOD_CTRL)) {
        int n = ev.key - tu::KEY_F1 + 1;
        if (n >= 3 && n <= 6) {
            static const SortMode m[4] = { SORT_NAME, SORT_EXT, SORT_TIME, SORT_SIZE };
            p->set_sort(m[n - 3]);
            return true;
        }
    }
    return false;
}

void App::handle_mouse(const tu::Event& ev)
{
    if (ev.action == tu::MOUSE_WHEEL_UP || ev.action == tu::MOUSE_WHEEL_DOWN) {
        int side = ev.x >= panel_x_[1] ? 1 : 0;
        panels_[side]->move(ev.action == tu::MOUSE_WHEEL_UP ? -3 : 3);
        return;
    }
    if (ev.action != tu::MOUSE_PRESS || ev.button != 1)
        return;
    if (ev.y == 0) {
        /* The menu bar: the title under the click, laid out as
         * draw_menu_bar() does. */
        static const char* const items[] = { "Left", "File", "Command", "Options", "Right", 0 };
        int x = 2, which = 0;
        for (int i = 0; items[i]; i++) {
            if (ev.x >= x - 1)
                which = i;
            x += (int)strlen(items[i]) + 5;
        }
        open_menu(which);
        return;
    }
    if (ev.y == screen_.rows() - 1) {
        int slot = screen_.cols() / 10;
        if (slot > 0)
            function_key(ev.x / slot + 1 > 10 ? 10 : ev.x / slot + 1);
        return;
    }
    if (ev.y < panel_y_ || ev.y >= panel_y_ + panel_h_)
        return;
    int side = ev.x >= panel_x_[1] ? 1 : 0;
    active_ = side;
    int i = panels_[side]->entry_at(ev.x, ev.y);
    if (i < 0)
        return;
    long t = now_ms();
    bool dbl = i == last_click_entry_ && t - last_click_ < 400;
    panels_[side]->set_cursor(i);
    last_click_ = t;
    last_click_entry_ = i;
    if (dbl) {
        last_click_entry_ = -1;
        panels_[side]->enter();
    }
}

void App::suspend()
{
    term_.stop();
    signal(SIGTSTP, SIG_DFL);
    kill(getpid(), SIGTSTP);
    term_.start();
    resize();
}

void App::handle(const tu::Event& ev)
{
    bool had_message = message_[0] != 0;
    message_[0] = 0;
    if (ev.key == tu::KEY_MOUSE) {
        handle_mouse(ev);
        return;
    }
    if (ev.key == tu::KEY_PASTE_BEGIN || ev.key == tu::KEY_PASTE_END ||
        ev.key == tu::KEY_FOCUS_IN || ev.key == tu::KEY_FOCUS_OUT)
        return;

    if (quick_) {
        size_t ql = strlen(quick_text_);
        if (ev.key == tu::KEY_CHAR && !(ev.mods & (tu::MOD_CTRL | tu::MOD_ALT))) {
            CmdLine one;
            one.insert(ev.ch);
            if (ql + one.len + 1 < sizeof(quick_text_)) {
                strcat(quick_text_, one.buf);
                if (!active()->search(quick_text_, false))
                    quick_text_[ql] = 0;
            }
            return;
        }
        if (ev.key == tu::KEY_BACKSPACE) {
            if (ql > 0)
                quick_text_[ql - 1] = 0;
            return;
        }
        if (ev.key == tu::KEY_CHAR && (ev.mods & tu::MOD_CTRL) && ev.ch == 's') {
            active()->search(quick_text_, true);
            return;
        }
        quick_ = false;
        if (ev.key == tu::KEY_ESC || ev.key == tu::KEY_ENTER)
            return;
    }

    /* Ctrl-X then a letter (mc's file commands). */
    if (ctrl_x_) {
        ctrl_x_ = false;
        if (ev.key == tu::KEY_CHAR) {
            switch (ev.ch) {
            case 'c': cmd_chmod(); return;
            case 'o': cmd_chown(); return;
            case 's': case 'l': cmd_symlink(); return;
            case 'd': cmd_compare(); return;
            default: break;
            }
        }
        message("%s", "Ctrl-X then: c chmod, o chown, s symlink, d compare directories.");
        return;
    }
    if (ev.key == tu::KEY_CHAR && (ev.mods & tu::MOD_CTRL) && ev.ch == 'x') {
        ctrl_x_ = true;
        return;
    }
    if (ev.key == tu::KEY_CHAR && (ev.mods & tu::MOD_CTRL) && ev.ch == '\\') {
        cmd_hotlist();
        return;
    }
    if (ev.key == tu::KEY_CHAR && (ev.mods & tu::MOD_ALT) && ev.ch == '?') {
        cmd_find();
        return;
    }
    /* + - * select when the command line is empty (mc). */
    if (ev.key == tu::KEY_CHAR && !ev.mods && cmd_.len == 0 &&
        (ev.ch == '+' || ev.ch == '-' || ev.ch == '*')) {
        cmd_select((char)ev.ch);
        return;
    }
    if (ev.key == tu::KEY_CHAR && ev.mods == tu::MOD_ALT && ev.ch >= '0' && ev.ch <= '9') {
        function_key(ev.ch == '0' ? 10 : (int)(ev.ch - '0'));
        return;
    }
    if (ev.key == tu::KEY_F1 + 3 && (ev.mods & tu::MOD_SHIFT)) {
        cmd_new_file();
        return;
    }
    if (ev.key >= tu::KEY_F1 && ev.key <= tu::KEY_F1 + 9 && !(ev.mods & tu::MOD_CTRL)) {
        function_key(ev.key - tu::KEY_F1 + 1);
        return;
    }
    if (handle_panel_key(ev))
        return;

    if (ev.key == tu::KEY_CHAR && (ev.mods & tu::MOD_CTRL)) {
        switch (ev.ch) {
        case 'o':
            if (!shell_.alive()) {
                message("%s", "There is no shell.");
                return;
            }
            /* Back to a command that is still running: when it ends,
             * so does this visit to the shell. */
            mode_ = shell_.idle(now_ms()) ? MODE_SHELL : MODE_COMMAND;
            cmd_started_ = now_ms();
            show_shell();
            if (mode_ == MODE_SHELL) {
                if (needs_cd()) {
                    pending_ = PEND_SHOW;
                    send_cd();
                }
                /* What was typed here continues there. */
                if (cmd_.len) {
                    shell_.write(cmd_.buf, cmd_.len);
                    cmd_.clear();
                }
            }
            return;
        case 'r':
            active()->reload();
            return;
        case 'u': {
            Panel* t = panels_[0];
            panels_[0] = panels_[1];
            panels_[1] = t;
            return;
        }
        case 'l': screen_.invalidate(); return;
        case 't': active()->toggle_mark(); return;
        case 's':
            quick_ = true;
            quick_text_[0] = 0;
            return;
        case 'z': suspend(); return;
        case 'a': cmd_.pos = 0; return;
        case 'e': cmd_.pos = cmd_.len; return;
        case 'k': cmd_.erase(cmd_.pos, cmd_.len); return;
        case 'w': {
            size_t p = cmd_.pos;
            while (p > 0 && cmd_.buf[p - 1] == ' ')
                p--;
            while (p > 0 && cmd_.buf[p - 1] != ' ')
                p--;
            cmd_.erase(p, cmd_.pos);
            return;
        }
        default:
            return;
        }
    }
    if (ev.key == tu::KEY_CHAR && (ev.mods & tu::MOD_ALT)) {
        switch (ev.ch) {
        case 't': case 'T':
            active()->set_list_mode(active()->list_mode() == LIST_FULL ? LIST_BRIEF : LIST_FULL);
            return;
        case '.':
            active()->toggle_hidden();
            return;
        case 's': case 'S':
            quick_ = true;
            quick_text_[0] = 0;
            return;
        case 'p': case 'P':
            if (history_pos_ > 0) {
                history_pos_--;
                cmd_.set(history_[history_pos_].c_str());
            }
            return;
        case 'n': case 'N':
            if (history_pos_ < history_.size() - 1) {
                history_pos_++;
                cmd_.set(history_[history_pos_].c_str());
            } else {
                history_pos_ = history_.size();
                cmd_.clear();
            }
            return;
        default:
            if (ev.ch > ' ' && ev.ch < 0x7f) {
                char b[2] = { (char)ev.ch, 0 };
                active()->search(b, true);
            }
            return;
        }
    }
    switch (ev.key) {
    case tu::KEY_CHAR:
        cmd_.insert(ev.ch);
        return;
    case tu::KEY_BACKSPACE:
        if (cmd_.pos > 0)
            cmd_.erase(cmd_.prev(cmd_.pos), cmd_.pos);
        return;
    case tu::KEY_DELETE:
        if (cmd_.pos < cmd_.len)
            cmd_.erase(cmd_.pos, cmd_.next(cmd_.pos));
        return;
    case tu::KEY_LEFT: cmd_.pos = cmd_.prev(cmd_.pos); return;
    case tu::KEY_RIGHT: cmd_.pos = cmd_.next(cmd_.pos); return;
    case tu::KEY_HOME: cmd_.pos = 0; return;
    case tu::KEY_END: cmd_.pos = cmd_.len; return;
    case tu::KEY_ESC:
        if (!had_message)
            cmd_.clear();
        return;
    default:
        return;
    }
}

/* ------------------------------------------------------------------ */
/* The loop                                                            */

/* Wait for input, the shell or a signal, up to timeout_ms (-1: until
 * something happens). Shell output and signals are handled here; key
 * bytes go to the shell when it is on screen and to input_ otherwise.
 * False when the terminal has gone. */
bool App::wait_input(int timeout_ms, bool* timed_out)
{
    *timed_out = false;
    fd_set rd;
    FD_ZERO(&rd);
    int in = term_.in_fd(), sig = term_.signal_fd(), sh = shell_.alive() ? shell_.fd() : -1;
    int top = in;
    FD_SET(in, &rd);
    if (sig >= 0) {
        FD_SET(sig, &rd);
        if (sig > top)
            top = sig;
    }
    if (sh >= 0) {
        FD_SET(sh, &rd);
        if (sh > top)
            top = sh;
    }
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int n = select(top + 1, &rd, 0, 0, timeout_ms < 0 ? 0 : &tv);
    if (n < 0)
        return errno == EINTR;
    if (n == 0) {
        *timed_out = true;
        return true;
    }
    if (sig >= 0 && FD_ISSET(sig, &rd)) {
        char b[16];
        ssize_t r = read(sig, b, sizeof(b));
        for (ssize_t i = 0; i < r; i++) {
            if (b[i] == 'W')
                resize();
            if (b[i] == 'T' || b[i] == 'H')
                quit_ = true;
        }
    }
    if (sh >= 0 && FD_ISSET(sh, &rd))
        shell_output();
    if (FD_ISSET(in, &rd)) {
        char b[4096];
        ssize_t r = read(in, b, sizeof(b));
        if (r == 0 || (r < 0 && errno != EINTR && errno != EAGAIN))
            return false;
        if (r > 0) {
            if (mode_ == MODE_PANELS)
                input_.feed(b, (size_t)r);
            else
                shell_input(b, (size_t)r);
        }
    }
    return true;
}

int App::run()
{
    if (!term_.start())
        return 1;
    screen_.resize();
    tf::set_progress(archive_progress_cb, this);
    start_shell();
    while (!quit_) {
        /* An archive finished opening: its progress dialog goes. */
        if (progress_) {
            delete progress_;
            progress_ = 0;
        }
        if (mode_ == MODE_PANELS) {
            draw();
            screen_.flush();
        }
        bool polling = syncing_ || mode_ == MODE_COMMAND || (mode_ == MODE_PANELS && !prompt_known_);
        int timeout = input_.pending() ? input_.timeout_ms() : polling ? 50 : -1;
        bool timed_out = false;
        if (!wait_input(timeout, &timed_out))
            break;
        tu::Event ev;
        while (!quit_ && mode_ == MODE_PANELS && input_.next(&ev, timed_out))
            handle(ev);
        tick(now_ms());
    }
    if (view_ && view_->shown())
        view_->hide();
    save_setup();
    term_.stop();
    return 0;
}

} /* namespace ttrc */

namespace ttrc {

bool App::archive_progress_cb(void* ctx, const char* what, uint64_t done, uint64_t total)
{
    return ((App*)ctx)->archive_progress(what, done, total);
}

/* Called by tf while it reads or unpacks an archive. */
bool App::archive_progress(const char* what, uint64_t done, uint64_t total)
{
    if (in_progress_ || !term_.started())
        return true;
    in_progress_ = true;
    if (!progress_) {
        int w = screen_.cols() - 10;
        if (w > 56)
            w = 56;
        progress_ = new tu::Dialog("Opening archive", w, 8);
        progress_text_ = new tu::Label("");
        progress_bar_ = new tu::ProgressBar();
        progress_->add(progress_text_, 3, 2, w - 6);
        progress_->add(progress_bar_, 3, 3, w - 6);
        tu::Button* b[1] = { new tu::Button("&Stop", 1, true) };
        progress_->add_buttons(5, b, 1);
    }
    char d[32], t[32], line[200];
    size_string(done, 12, d);
    size_string(total, 12, t);
    if (total)
        snprintf(line, sizeof(line), "%s: %s of %s bytes", what, d, t);
    else
        snprintf(line, sizeof(line), "%s: %s bytes", what, d);
    progress_text_->set(line);
    progress_bar_->fraction = total ? (double)done / (double)total : 0;
    draw();
    progress_->draw(screen_, theme_.dialog);
    screen_.flush();
    bool timed_out = false;
    bool go_on = true;
    if (!wait_input(0, &timed_out)) {
        quit_ = true;
        go_on = false;
    }
    tu::Event ev;
    while (go_on && input_.next(&ev, timed_out)) {
        progress_->handle(ev);
        if (progress_->result() != tu::Dialog::RESULT_NONE)
            go_on = false;
    }
    in_progress_ = false;
    return go_on;
}

ttr::Str App::temp_dir() const
{
    char dir[300];
    const char* tmp = getenv("TMPDIR");
    snprintf(dir, sizeof(dir), "%.200s/ttrc.%ld", tmp && *tmp ? tmp : "/tmp", (long)getpid());
    return ttr::Str(dir);
}

void App::remove_temp_dir()
{
    ttr::Str dir = temp_dir();
    tf::Stat st;
    if (tf::local_fs()->stat(dir.c_str(), &st, false) != 0 || st.type != tf::FT_DIR)
        return;
    tf::Job job(tf::OP_DELETE, tf::local_fs(), 0);
    job.add(dir.c_str());
    while (job.step(200) == tf::Job::WORKING) {
    }
    if (job.state() != tf::Job::DONE)
        job.answer(tf::ANS_ABORT);
}

} /* namespace ttrc */
