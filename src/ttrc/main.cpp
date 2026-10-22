/*
 * ttrc, To The Roots Commander (ttrc/DESIGN.md). Milestone 1: two panels
 * to browse with, on tu and tf. The shell, file operations, menus and
 * archives come in the next milestones.
 *
 *   ttrc [-P file] [left-dir [right-dir]]
 *
 * -P writes the active panel's directory to file at exit, for the `tc`
 * shell function (section 4.8).
 */
#include "panel.h"
#include "theme.h"

#include "../../tu/input.h"
#include "../../tu/screen.h"
#include "../../tu/term.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

using namespace ttrc;

long now_ms()
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* The line under the panels. Milestone 2 connects it to the shell; for
 * now it handles `cd`. */
struct CmdLine {
    char buf[2048];
    size_t len, pos;            /* bytes */

    CmdLine() : len(0), pos(0) { buf[0] = 0; }
    void clear()
    {
        len = pos = 0;
        buf[0] = 0;
    }
    void insert(uint32_t cp)
    {
        char b[4];
        size_t n;
        if (cp < 0x80) {
            b[0] = (char)cp;
            n = 1;
        } else if (cp < 0x800) {
            b[0] = (char)(0xc0 | (cp >> 6));
            b[1] = (char)(0x80 | (cp & 0x3f));
            n = 2;
        } else if (cp < 0x10000) {
            b[0] = (char)(0xe0 | (cp >> 12));
            b[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
            b[2] = (char)(0x80 | (cp & 0x3f));
            n = 3;
        } else {
            b[0] = (char)(0xf0 | (cp >> 18));
            b[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
            b[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
            b[3] = (char)(0x80 | (cp & 0x3f));
            n = 4;
        }
        if (len + n >= sizeof(buf))
            return;
        memmove(buf + pos + n, buf + pos, len - pos);
        memcpy(buf + pos, b, n);
        len += n;
        pos += n;
        buf[len] = 0;
    }
    size_t prev(size_t p) const
    {
        if (p == 0)
            return 0;
        p--;
        while (p > 0 && ((unsigned char)buf[p] & 0xc0) == 0x80)
            p--;
        return p;
    }
    size_t next(size_t p) const
    {
        if (p >= len)
            return len;
        p++;
        while (p < len && ((unsigned char)buf[p] & 0xc0) == 0x80)
            p++;
        return p;
    }
    void erase(size_t from, size_t to)
    {
        memmove(buf + from, buf + to, len - to);
        len -= to - from;
        buf[len] = 0;
        pos = from;
    }
};

class App {
public:
    App(tu::Term& term, const tf::Path& left, const tf::Path& right);
    ~App();
    int run();
    const tf::Path& active_path() const { return panels_[active_]->path(); }

private:
    void draw();
    void draw_menu_bar();
    void draw_key_bar();
    void draw_command_line();
    void handle(const tu::Event& ev);
    bool handle_panel_key(const tu::Event& ev);
    void handle_mouse(const tu::Event& ev);
    void run_command();
    void function_key(int n);
    void suspend();
    void message(const char* fmt, const char* arg = 0);
    Panel* active() { return panels_[active_]; }

    tu::Term& term_;
    tu::Screen screen_;
    tu::Input input_;
    Theme theme_;
    Panel* panels_[2];
    int active_;
    bool quit_;
    CmdLine cmd_;
    char message_[512];
    bool quick_;                /* quick search is on */
    char quick_text_[128];
    int panel_x_[2], panel_w_[2], panel_y_, panel_h_;
    long last_click_;
    int last_click_entry_;
};

App::App(tu::Term& term, const tf::Path& left, const tf::Path& right)
    : term_(term), screen_(term), active_(0), quit_(false), quick_(false), panel_y_(1),
      panel_h_(0), last_click_(0), last_click_entry_(-1)
{
    input_.init(term.info(), term.utf8());
    theme_ = theme_for(term.tier());
    panels_[0] = new Panel(tf::local_fs(), left);
    panels_[1] = new Panel(tf::local_fs(), right);
    message_[0] = 0;
    quick_text_[0] = 0;
    panel_x_[0] = panel_x_[1] = panel_w_[0] = panel_w_[1] = 0;
}

App::~App()
{
    delete panels_[0];
    delete panels_[1];
}

void App::message(const char* fmt, const char* arg)
{
    sprintf(message_, fmt, arg ? arg : "");
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
    /* Milestone 2 shows the shell's own prompt here. */
    char prompt[600];
    const char* p = active_path().c_str();
    const char* home = getenv("HOME");
    size_t hl = home ? strlen(home) : 0;
    if (hl > 1 && strncmp(p, home, hl) == 0 && (p[hl] == '/' || p[hl] == 0))
        sprintf(prompt, "~%.500s$ ", p + hl);
    else
        sprintf(prompt, "%.500s$ ", p);
    int pw = tu::Screen::text_width(prompt);
    if (pw > w / 2) {
        strcpy(prompt, "$ ");
        pw = 2;
    }
    screen_.text(0, y, prompt, theme_.command);
    /* Scroll the text so the cursor stays visible. */
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
    int w = screen_.cols(), h = screen_.rows();
    screen_.clear(theme_.command);
    if (w < 24 || h < 8) {
        screen_.text(0, 0, "ttrc: the window is too small", tu::Style());
        screen_.set_cursor(0, 0, false);
        return;
    }
    draw_menu_bar();
    panel_y_ = 1;
    panel_h_ = h - 3;
    panel_w_[0] = w / 2;
    panel_w_[1] = w - w / 2;
    panel_x_[0] = 0;
    panel_x_[1] = panel_w_[0];
    for (int i = 0; i < 2; i++)
        panels_[i]->draw(screen_, theme_, panel_x_[i], panel_y_, panel_w_[i], panel_h_,
                         i == active_);
    draw_command_line();
    draw_key_bar();
}

/* ------------------------------------------------------------------ */
/* Input                                                               */

void App::function_key(int n)
{
    switch (n) {
    case 10:
        quit_ = true;
        break;
    case 3: case 4: case 5: case 6: case 7: case 8:
        message("F%s: file operations and the viewer come in a later milestone.",
                n == 3 ? "3" : n == 4 ? "4" : n == 5 ? "5" : n == 6 ? "6" : n == 7 ? "7" : "8");
        break;
    case 1:
        message("%s", "Tab: other panel. Ins: mark. Alt-T: listing. Alt-.: hidden files. "
                      "Ctrl-F3..F6: sort. Ctrl-S: search. F10: quit.");
        break;
    default:
        message("%s", "Menus come in a later milestone.");
        break;
    }
}

/* `cd` is ours; anything else needs the shell (milestone 2). */
void App::run_command()
{
    const char* c = cmd_.buf;
    while (*c == ' ')
        c++;
    if (!*c)
        return;
    if (strncmp(c, "cd", 2) == 0 && (c[2] == 0 || c[2] == ' ')) {
        const char* arg = c + 2;
        while (*arg == ' ')
            arg++;
        char target[2048];
        strcpy(target, *arg ? arg : "~");
        size_t tl = strlen(target);
        while (tl > 1 && target[tl - 1] == ' ')
            target[--tl] = 0;
        tf::Path p = tf::Path::resolve(active_path(), target);
        if (!active()->load(p))
            message("%s", active()->error());
        cmd_.clear();
        return;
    }
    if (strcmp(c, "exit") == 0) {
        quit_ = true;
        return;
    }
    message("%s", "Running commands comes with the shell in milestone 2.");
}

bool App::handle_panel_key(const tu::Event& ev)
{
    Panel* p = active();
    bool text = cmd_.len > 0;
    switch (ev.key) {
    case tu::KEY_UP: p->move(-1); return true;
    case tu::KEY_DOWN: p->move(1); return true;
    case tu::KEY_PGUP:
        if (ev.mods & tu::MOD_CTRL) {
            if (!p->up())
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
        if (text) {
            run_command();
        } else if (!p->enter()) {
            if (p->error()[0])
                message("%s", p->error());
        }
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
    if (ev.y == screen_.rows() - 1) {           /* the F-key bar */
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
    /* Back from the shell's fg. */
    term_.start();
    term_.update_size();
    screen_.resize();
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

    /* Quick search: letters narrow, other keys end it. */
    if (quick_) {
        size_t ql = strlen(quick_text_);
        if (ev.key == tu::KEY_CHAR && !(ev.mods & (tu::MOD_CTRL | tu::MOD_ALT))) {
            if (ql + 5 < sizeof(quick_text_)) {
                char b[8];
                CmdLine one;
                one.insert(ev.ch);
                memcpy(b, one.buf, one.len + 1);
                strcat(quick_text_, b);
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

    /* Esc then a digit is a function key (mc), Alt-0 is F10. */
    if (ev.key == tu::KEY_CHAR && ev.mods == tu::MOD_ALT && ev.ch >= '0' && ev.ch <= '9') {
        function_key(ev.ch == '0' ? 10 : (int)(ev.ch - '0'));
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
        case 'r': active()->reload(); return;
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
        case 'o':
            message("%s", "The shell (Ctrl-O) comes in milestone 2.");
            return;
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
        default:
            /* Alt+letter jumps to a name starting with it (Norton). */
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

int App::run()
{
    if (!term_.start())
        return 1;
    screen_.resize();
    while (!quit_) {
        draw();
        screen_.flush();
        fd_set rd;
        FD_ZERO(&rd);
        int in = term_.in_fd(), sig = term_.signal_fd();
        FD_SET(in, &rd);
        if (sig >= 0)
            FD_SET(sig, &rd);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = input_.timeout_ms() * 1000;
        int n = select((in > sig ? in : sig) + 1, &rd, 0, 0, input_.pending() ? &tv : 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (sig >= 0 && n > 0 && FD_ISSET(sig, &rd)) {
            char b[16];
            ssize_t r = read(sig, b, sizeof(b));
            for (ssize_t i = 0; i < r; i++) {
                if (b[i] == 'W' && term_.update_size())
                    screen_.resize();
                if (b[i] == 'T' || b[i] == 'H')
                    quit_ = true;
            }
        }
        bool timed_out = n == 0;
        if (n > 0 && FD_ISSET(in, &rd)) {
            char b[4096];
            ssize_t r = read(in, b, sizeof(b));
            if (r == 0 || (r < 0 && errno != EINTR && errno != EAGAIN))
                break;          /* the terminal went away */
            if (r > 0)
                input_.feed(b, (size_t)r);
        }
        tu::Event ev;
        while (!quit_ && input_.next(&ev, timed_out))
            handle(ev);
    }
    term_.stop();
    return 0;
}

} /* namespace */

int main(int argc, char** argv)
{
    const char* pwd_file = 0;
    const char* dirs[2] = { 0, 0 };
    int nd = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-P") == 0 && i + 1 < argc) {
            pwd_file = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("usage: ttrc [-P file] [left-dir [right-dir]]\n");
            return 0;
        } else if (nd < 2) {
            dirs[nd++] = argv[i];
        } else {
            fprintf(stderr, "usage: ttrc [-P file] [left-dir [right-dir]]\n");
            return 2;
        }
    }
    if (!isatty(0) || !isatty(1)) {
        fprintf(stderr, "ttrc: needs a terminal\n");
        return 1;
    }
    tu::Term term;
    if (!term.open(0, 1)) {
        fprintf(stderr, "ttrc: no terminal description for TERM\n");
        return 1;
    }
    tf::Path cwd = tf::Path::cwd();
    tf::Path left = dirs[0] ? tf::Path::resolve(cwd, dirs[0]) : cwd;
    tf::Path right = dirs[1] ? tf::Path::resolve(cwd, dirs[1]) : left;
    App app(term, left, right);
    int rc = app.run();
    if (pwd_file) {
        FILE* f = fopen(pwd_file, "w");
        if (f) {
            fprintf(f, "%s\n", app.active_path().c_str());
            fclose(f);
        }
    }
    return rc;
}
