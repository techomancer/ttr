/*
 * ttrc's commands: dialogs, file operations, menus, the hotlist, find
 * file, comparing directories, the user menu and file associations
 * (ttrc/DESIGN.md section 7).
 */
#include "app.h"

#include "../../tf/ops.h"
#include "../../tf/vfs.h"

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

namespace ttrc {

namespace {

long now_ms()
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

enum MenuId {
    M_NONE = 0,
    M_LEFT_FULL = 1, M_LEFT_BRIEF, M_LEFT_NAME, M_LEFT_EXT, M_LEFT_TIME, M_LEFT_SIZE,
    M_LEFT_UNSORTED, M_LEFT_REVERSE, M_LEFT_HIDDEN, M_LEFT_REREAD,
    M_RIGHT_FULL = 21, M_RIGHT_BRIEF, M_RIGHT_NAME, M_RIGHT_EXT, M_RIGHT_TIME, M_RIGHT_SIZE,
    M_RIGHT_UNSORTED, M_RIGHT_REVERSE, M_RIGHT_HIDDEN, M_RIGHT_REREAD,
    M_VIEW = 41, M_EDIT, M_COPY, M_MOVE, M_MKDIR, M_DELETE, M_CHMOD, M_CHOWN, M_SYMLINK,
    M_SELECT, M_UNSELECT, M_INVERT, M_QUIT,
    M_USERMENU = 61, M_HOTLIST, M_FIND, M_COMPARE, M_SWAP, M_SHELL,
    M_CONFIG = 81, M_APPEARANCE, M_SAVE
};

const tu::MenuItem k_left[] = {
    { "&Full listing", 0, M_LEFT_FULL },
    { "&Brief listing", 0, M_LEFT_BRIEF },
    { 0, 0, -1 },
    { "Sort by &name", "C-F3", M_LEFT_NAME },
    { "Sort by e&xtension", "C-F4", M_LEFT_EXT },
    { "Sort by &time", "C-F5", M_LEFT_TIME },
    { "Sort by si&ze", "C-F6", M_LEFT_SIZE },
    { "&Unsorted", 0, M_LEFT_UNSORTED },
    { "Re&verse order", 0, M_LEFT_REVERSE },
    { 0, 0, -1 },
    { "&Hidden files", "M-.", M_LEFT_HIDDEN },
    { "&Re-read", "C-r", M_LEFT_REREAD },
    { 0, 0, 0 }
};

const tu::MenuItem k_right[] = {
    { "&Full listing", 0, M_RIGHT_FULL },
    { "&Brief listing", 0, M_RIGHT_BRIEF },
    { 0, 0, -1 },
    { "Sort by &name", 0, M_RIGHT_NAME },
    { "Sort by e&xtension", 0, M_RIGHT_EXT },
    { "Sort by &time", 0, M_RIGHT_TIME },
    { "Sort by si&ze", 0, M_RIGHT_SIZE },
    { "&Unsorted", 0, M_RIGHT_UNSORTED },
    { "Re&verse order", 0, M_RIGHT_REVERSE },
    { 0, 0, -1 },
    { "&Hidden files", 0, M_RIGHT_HIDDEN },
    { "&Re-read", 0, M_RIGHT_REREAD },
    { 0, 0, 0 }
};

const tu::MenuItem k_file[] = {
    { "&View", "F3", M_VIEW },
    { "&Edit", "F4", M_EDIT },
    { "&Copy", "F5", M_COPY },
    { "&Rename or move", "F6", M_MOVE },
    { "&Make directory", "F7", M_MKDIR },
    { "&Delete", "F8", M_DELETE },
    { 0, 0, -1 },
    { "C&hmod", "C-x c", M_CHMOD },
    { "Ch&own", "C-x o", M_CHOWN },
    { "&Symbolic link", "C-x s", M_SYMLINK },
    { 0, 0, -1 },
    { "Select &group", "+", M_SELECT },
    { "U&nselect group", "-", M_UNSELECT },
    { "&Invert selection", "*", M_INVERT },
    { 0, 0, -1 },
    { "E&xit", "F10", M_QUIT },
    { 0, 0, 0 }
};

const tu::MenuItem k_command[] = {
    { "&User menu", "F2", M_USERMENU },
    { "Directory &hotlist", "C-\\", M_HOTLIST },
    { "&Find file", "M-?", M_FIND },
    { "&Compare directories", "C-x d", M_COMPARE },
    { 0, 0, -1 },
    { "S&wap panels", "C-u", M_SWAP },
    { "&Shell", "C-o", M_SHELL },
    { 0, 0, 0 }
};

const tu::MenuItem k_options[] = {
    { "&Configuration...", 0, M_CONFIG },
    { "&Appearance...", 0, M_APPEARANCE },
    { 0, 0, -1 },
    { "&Save setup", 0, M_SAVE },
    { 0, 0, 0 }
};

void size_text(uint64_t n, char* out)
{
    size_string(n, 12, out);
}

void time_text(long t, char* out)
{
    time_t tt = (time_t)t;
    struct tm* tm = localtime(&tt);
    if (!tm) {
        strcpy(out, "?");
        return;
    }
    strftime(out, 32, "%Y-%m-%d %H:%M:%S", tm);
}

/* Read a whole small file (the hotlist, the user menu). */
char* slurp(const char* path)
{
    FILE* f = fopen(path, "r");
    if (!f)
        return 0;
    size_t cap = 4096, len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) {
        fclose(f);
        return 0;
    }
    size_t n;
    while ((n = fread(buf + len, 1, cap - len - 1, f)) > 0) {
        len += n;
        if (len + 1 >= cap) {
            if (cap > (1 << 20))
                break;
            char* nb = (char*)realloc(buf, cap * 2);
            if (!nb)
                break;
            buf = nb;
            cap *= 2;
        }
    }
    fclose(f);
    buf[len] = 0;
    return buf;
}

void make_config_dir()
{
    ttr::Str p = config_file("x");
    char dir[1200];
    snprintf(dir, sizeof(dir), "%s", p.c_str());
    char* slash = strrchr(dir, '/');
    if (!slash)
        return;
    *slash = 0;
    /* mkdir -p */
    for (char* c = dir + 1; *c; c++) {
        if (*c == '/') {
            *c = 0;
            tf::local_fs()->mkdir(dir, 0755);
            *c = '/';
        }
    }
    tf::local_fs()->mkdir(dir, 0755);
}

} /* namespace */

/* ------------------------------------------------------------------ */
/* Dialogs                                                             */

/* Run a dialog over the panels until it ends; the shell keeps being read.
 * idle, if given, gets time between events while it has work. */
int App::modal(tu::Dialog& d, const tu::DialogStyle& st, IdleFn idle, void* ctx)
{
    bool busy = idle != 0;
    while (!quit_ && d.result() == tu::Dialog::RESULT_NONE) {
        draw();
        d.draw(screen_, st);
        screen_.flush();
        bool timed_out = false;
        int timeout = input_.pending() ? input_.timeout_ms() : busy ? 0 : 200;
        if (!wait_input(timeout, &timed_out)) {
            quit_ = true;
            break;
        }
        tu::Event ev;
        while (d.result() == tu::Dialog::RESULT_NONE && input_.next(&ev, timed_out))
            d.handle(ev);
        if (busy && d.result() == tu::Dialog::RESULT_NONE)
            busy = idle(ctx);
    }
    return quit_ ? tu::Dialog::RESULT_CANCEL : d.result();
}

/* A question with buttons; returns the button's index from 1, or -1. */
int App::ask(const char* title, const char* text, const char* const* buttons, bool alarm)
{
    int lines = 1, longest = 0, cur = 0;
    for (const char* p = text; *p; p++) {
        if (*p == '\n') {
            lines++;
            cur = 0;
        } else if (++cur > longest) {
            longest = cur;
        }
    }
    int nb = 0, bw = 0;
    while (buttons[nb]) {
        bw += tu::Button::width_of(buttons[nb]) + 1;
        nb++;
    }
    int w = longest + 8;
    if (w < bw + 6)
        w = bw + 6;
    if (w < 30)
        w = 30;
    if (w > screen_.cols() - 4)
        w = screen_.cols() - 4;
    tu::Dialog d(title, w, lines + 5);
    d.add(new tu::Label(text), 3, 2, w - 6, lines);
    tu::Button* b[8];
    for (int i = 0; i < nb && i < 8; i++)
        b[i] = new tu::Button(buttons[i], i + 1, i == 0);
    d.add_buttons(lines + 3, b, nb < 8 ? nb : 8);
    int r = modal(d, alarm ? theme_.alarm : theme_.dialog);
    return r > 0 ? r : -1;
}

void App::show_error(const char* text)
{
    static const char* const ok[] = { "&OK", 0 };
    ask("Error", text, ok, true);
}

bool App::ask_text(const char* title, const char* label, ttr::Str* value, tu::StrList* history)
{
    int w = screen_.cols() - 10;
    if (w > 70)
        w = 70;
    if (w < 30)
        w = 30;
    tu::Dialog d(title, w, 8);
    d.add(new tu::Label(label), 3, 2, w - 6);
    tu::InputLine* in = new tu::InputLine(value->c_str());
    in->set_history(history);
    d.add(in, 3, 3, w - 6);
    tu::Button* b[2] = { new tu::Button("&OK", 1, true), new tu::Button("&Cancel", 2) };
    d.add_buttons(5, b, 2);
    d.focus(in);
    if (modal(d, theme_.dialog) != 1)
        return false;
    *value = in->text();
    if (history && *in->text())
        history->add(in->text());
    return true;
}

/* The viewer's search: text or hex bytes. False on Cancel or nothing. */
bool App::ask_search(SearchQuery* q)
{
    int w = screen_.cols() - 10;
    if (w > 70)
        w = 70;
    if (w < 40)
        w = 40;
    tu::Dialog d("Search", w, 10);
    d.add(new tu::Label("Find:"), 3, 2, w - 6);
    tu::InputLine* in = new tu::InputLine(q->text.c_str());
    in->set_history(&hist_search_);
    in->replace_on_type();
    d.add(in, 3, 3, w - 6);
    tu::RadioGroup* mode = new tu::RadioGroup();
    mode->add("&Text");
    mode->add("He&x bytes");
    mode->selected = q->hex ? 1 : 0;
    d.add(mode, 3, 5, 18);
    tu::CheckBox* mc = new tu::CheckBox("&Match case", q->match_case);
    d.add(mc, 24, 5, 20);
    tu::Button* b[2] = { new tu::Button("&Find", 1, true), new tu::Button("&Cancel", 2) };
    d.add_buttons(7, b, 2);
    d.focus(in);
    if (modal(d, theme_.dialog) != 1 || !*in->text())
        return false;
    q->text = in->text();
    q->hex = mode->selected == 1;
    q->match_case = mc->on;
    hist_search_.add(in->text());
    return true;
}

/* ------------------------------------------------------------------ */
/* Jobs                                                                */

/* Run a copy, move or delete with a progress dialog, asking what it
 * asks. True if it finished (with or without skipped files). */
bool App::run_job(tf::Job& job, const char* title)
{
    int w = screen_.cols() - 8;
    if (w > 72)
        w = 72;
    if (w < 40)
        w = 40;
    tu::Dialog d(title, w, 11);
    tu::Label* what = new tu::Label("Counting...");
    tu::Label* from = new tu::Label("");
    tu::Label* to = new tu::Label("");
    tu::ProgressBar* bar = new tu::ProgressBar();
    tu::Label* count = new tu::Label("");
    d.add(what, 3, 2, w - 6);
    d.add(from, 3, 3, w - 6);
    d.add(to, 3, 4, w - 6);
    d.add(bar, 3, 6, w - 6);
    d.add(count, 3, 7, w - 6);
    tu::Button* abort_b[1] = { new tu::Button("&Abort", 1, true) };
    d.add_buttons(9, abort_b, 1);
    long last_draw = 0;
    for (;;) {
        tf::Job::State st = job.step(60);
        long now = now_ms();
        if (st == tf::Job::WORKING && now - last_draw < 100 && !input_.pending()) {
            /* keep working; look at the keyboard now and then */
        } else {
            last_draw = now;
            char b[1200];
            if (job.counting()) {
                snprintf(b, sizeof(b), "Counting: %d files", job.files_total());
                what->set(b);
            } else {
                what->set(title);
                snprintf(b, sizeof(b), "%s", job.source());
                from->set(b);
                snprintf(b, sizeof(b), "%s", *job.target() ? job.target() : "");
                to->set(b);
                char done[32], total[32];
                size_text(job.bytes_done(), done);
                size_text(job.bytes_total(), total);
                snprintf(b, sizeof(b), "%d of %d files, %s of %s bytes", job.files_done(),
                         job.files_total(), done, total);
                count->set(b);
                bar->fraction = job.bytes_total()
                                    ? (double)job.bytes_done() / (double)job.bytes_total()
                                    : job.files_total() ? (double)job.files_done() / job.files_total()
                                                        : 0;
            }
            draw();
            d.draw(screen_, theme_.dialog);
            screen_.flush();
        }
        if (st == tf::Job::DONE || st == tf::Job::ABORTED)
            break;
        if (st == tf::Job::ASK_OVERWRITE) {
            char q[2600], ss[32], ts[32], st1[32], tt[32];
            size_text(job.source_stat().size, ss);
            size_text(job.target_stat().size, ts);
            time_text(job.source_stat().mtime, st1);
            time_text(job.target_stat().mtime, tt);
            snprintf(q, sizeof(q), "%.900s exists.\n\nNew:      %12s bytes  %s\nExisting: %12s bytes  %s\n\nOverwrite it?",
                     job.target(), ss, st1, ts, tt);
            static const char* const b[] = { "&Yes", "&No", "&All", "No&ne", "If &older", "A&bort", 0 };
            static const tf::Answer a[] = { tf::ANS_YES, tf::ANS_NO, tf::ANS_ALL, tf::ANS_NONE,
                                            tf::ANS_UPDATE, tf::ANS_ABORT };
            int r = ask("File exists", q, b, true);
            job.answer(r >= 1 ? a[r - 1] : tf::ANS_ABORT);
            continue;
        }
        if (st == tf::Job::ASK_ERROR) {
            char q[2600];
            const char* p = *job.target() && strcmp(job.what(), "read") != 0 &&
                                    strcmp(job.what(), "open") != 0 &&
                                    strncmp(job.what(), "delete", 6) != 0
                                ? job.target()
                                : job.source();
            snprintf(q, sizeof(q), "Cannot %s\n%.900s\n\n%s", job.what(), p, strerror(job.error()));
            static const char* const b[] = { "&Retry", "&Skip", "Skip a&ll", "A&bort", 0 };
            static const tf::Answer a[] = { tf::ANS_RETRY, tf::ANS_SKIP, tf::ANS_SKIP_ALL,
                                            tf::ANS_ABORT };
            int r = ask("Error", q, b, true);
            job.answer(r >= 1 ? a[r - 1] : tf::ANS_ABORT);
            continue;
        }
        /* Esc or Abort */
        bool timed_out = false;
        if (!wait_input(0, &timed_out)) {
            job.answer(tf::ANS_ABORT);
            quit_ = true;
            break;
        }
        tu::Event ev;
        while (input_.next(&ev, timed_out)) {
            d.handle(ev);
            if (d.result() != tu::Dialog::RESULT_NONE) {
                static const char* const b[] = { "&Yes", "&No", 0 };
                if (ask("Abort", "Stop now?", b, true) == 1)
                    job.answer(tf::ANS_ABORT);
                d.set_result(tu::Dialog::RESULT_NONE);
            }
        }
    }
    panels_[0]->reload();
    panels_[1]->reload();
    if (job.failures() > 0) {
        char b[100];
        sprintf(b, "%d file%s skipped.", job.failures(), job.failures() == 1 ? "" : "s");
        message("%s", b);
    }
    return job.state() == tf::Job::DONE;
}

/* F5 and F6 */
void App::cmd_copy(bool move)
{
    ttr::Vector<ttr::Str> names;
    if (active()->selection(&names) == 0)
        return;
    char label[1200];
    if (names.size() == 1)
        snprintf(label, sizeof(label), "%s \"%.200s\" to:", move ? "Move" : "Copy",
                 names[0].c_str());
    else
        snprintf(label, sizeof(label), "%s %d files to:", move ? "Move" : "Copy", names.size());
    int w = screen_.cols() - 10;
    if (w > 70)
        w = 70;
    tu::Dialog d(move ? "Rename or move" : "Copy", w, 10);
    d.add(new tu::Label(label), 3, 2, w - 6);
    tu::InputLine* in = new tu::InputLine(other()->path().c_str());
    if (names.size() == 1 && move && strcmp(other()->path().c_str(), active_path().c_str()) == 0)
        in->set(names[0].c_str());      /* both panels here: rename */
    in->set_history(&hist_target_);
    d.add(in, 3, 3, w - 6);
    tu::CheckBox* keep = new tu::CheckBox("&Preserve modes and times", config_.preserve_attrs);
    d.add(keep, 3, 5, w - 6);
    tu::Button* b[2] = { new tu::Button("&OK", 1, true), new tu::Button("&Cancel", 2) };
    d.add_buttons(7, b, 2);
    d.focus(in);
    if (modal(d, theme_.dialog) != 1)
        return;
    if (*in->text())
        hist_target_.add(in->text());
    tf::Path target = tf::Path::resolve(active_path(), in->text());
    tf::Stat st;
    bool is_dir = tf::vfs()->stat(target.c_str(), &st, true) == 0 && st.type == tf::FT_DIR;
    if (names.size() > 1 && !is_dir) {
        show_error("Copying or moving several files needs a directory as the target.");
        return;
    }
    tf::Job job(move ? tf::OP_MOVE : tf::OP_COPY, active()->fs(), other()->fs());
    job.preserve_attrs = keep->on;
    job.set_overwrite_all(!config_.confirm_overwrite);
    for (int i = 0; i < names.size(); i++)
        job.add(active_path().child(names[i].c_str()).c_str());
    job.set_target(target.c_str(), is_dir);
    run_job(job, move ? "Moving" : "Copying");
    if (names.size() == 1 && !is_dir && target.parent().c_str() &&
        strcmp(target.parent().c_str(), active_path().c_str()) == 0)
        active()->select_name(target.name());
    active()->unmark_all();
}

/* F7, with intermediate directories ("a/b/c"). */
void App::cmd_mkdir()
{
    ttr::Str name("");
    if (!ask_text("Make directory", "Name of the new directory:", &name, &hist_mkdir_) ||
        name.empty())
        return;
    tf::Path p = tf::Path::resolve(active_path(), name.c_str());
    char buf[4096];
    snprintf(buf, sizeof(buf), "%s", p.c_str());
    int err = 0;
    for (char* c = buf + 1; ; c++) {
        if (*c == '/' || *c == 0) {
            char save = *c;
            *c = 0;
            int r = active()->fs()->mkdir(buf, 0777);
            if (save == 0 && r != 0)
                err = r;
            *c = save;
            if (!save)
                break;
        }
    }
    if (err) {
        char m[4300];
        snprintf(m, sizeof(m), "Cannot create %.4000s:\n%s", p.c_str(), strerror(err));
        show_error(m);
        return;
    }
    active()->reload();
    /* The cursor on the new directory, if it is here. */
    if (strcmp(p.parent().c_str(), active_path().c_str()) == 0)
        active()->select_name(p.name());
}

/* F8 */
void App::cmd_delete()
{
    ttr::Vector<ttr::Str> names;
    if (active()->selection(&names) == 0)
        return;
    if (config_.confirm_delete) {
        char q[600];
        if (names.size() == 1)
            snprintf(q, sizeof(q), "Delete \"%.400s\"?", names[0].c_str());
        else
            snprintf(q, sizeof(q), "Delete %d files and directories?", names.size());
        static const char* const b[] = { "&Yes", "&No", 0 };
        if (ask("Delete", q, b, true) != 1)
            return;
    }
    tf::Job job(tf::OP_DELETE, active()->fs(), 0);
    for (int i = 0; i < names.size(); i++)
        job.add(active_path().child(names[i].c_str()).c_str());
    run_job(job, "Deleting");
    active()->unmark_all();
}

/* + - * */
void App::cmd_select(char how)
{
    if (how == '*') {
        active()->invert_marks();
        return;
    }
    ttr::Str pat("*");
    if (!ask_text(how == '+' ? "Select" : "Unselect", "Files matching (* ? [abc]):", &pat,
                  &hist_pattern_))
        return;
    bool dirs = pat.c_str()[0] && pat.c_str()[strlen(pat.c_str()) - 1] == '/';
    char p[1024];
    snprintf(p, sizeof(p), "%s", pat.c_str());
    if (dirs)
        p[strlen(p) - 1] = 0;
    active()->mark_matching(p, how == '+', dirs);
}

void App::cmd_chmod()
{
    ttr::Vector<ttr::Str> names;
    if (active()->selection(&names) == 0)
        return;
    tf::Stat st;
    tf::Path first = active_path().child(names[0].c_str());
    if (active()->fs()->stat(first.c_str(), &st, false) != 0)
        return;
    static const char* const bits[12] = {
        "&set user ID", "set &group ID", "s&ticky",
        "&read by owner", "&write by owner", "e&xecute/search by owner",
        "r&ead by group", "wr&ite by group", "exe&cute/search by group",
        "re&ad by others", "write by &others", "execute/searc&h by others"
    };
    static const unsigned masks[12] = { 04000, 02000, 01000, 0400, 0200, 0100,
                                        040, 020, 010, 04, 02, 01 };
    char title[300];
    snprintf(title, sizeof(title), "Chmod %.200s%s", names[0].c_str(), names.size() > 1 ? " ..." : "");
    tu::Dialog d(title, 50, 18);
    tu::CheckBox* box[12];
    for (int i = 0; i < 12; i++) {
        box[i] = new tu::CheckBox(bits[i], (st.mode & masks[i]) != 0);
        d.add(box[i], 4, 2 + i, 42);
    }
    tu::Button* b[2] = { new tu::Button("&Set", 1, true), new tu::Button("&Cancel", 2) };
    d.add_buttons(15, b, 2);
    if (modal(d, theme_.dialog) != 1)
        return;
    unsigned mode = 0;
    for (int i = 0; i < 12; i++)
        if (box[i]->on)
            mode |= masks[i];
    for (int i = 0; i < names.size(); i++) {
        tf::Attrs a;
        a.mode = mode;
        int r = active()->fs()->set_attrs(active_path().child(names[i].c_str()).c_str(), a,
                                          tf::ATTR_MODE);
        if (r != 0) {
            char m[600];
            snprintf(m, sizeof(m), "Cannot change %.400s:\n%s", names[i].c_str(), strerror(r));
            show_error(m);
            break;
        }
    }
    active()->unmark_all();
    active()->reload();
}

void App::cmd_chown()
{
    ttr::Vector<ttr::Str> names;
    if (active()->selection(&names) == 0)
        return;
    tf::Stat st;
    active()->fs()->stat(active_path().child(names[0].c_str()).c_str(), &st, false);
    char cur[200];
    struct passwd* pw = getpwuid((uid_t)st.uid);
    struct group* gr = getgrgid((gid_t)st.gid);
    snprintf(cur, sizeof(cur), "%s:%s", pw ? pw->pw_name : "?", gr ? gr->gr_name : "?");
    ttr::Str v(cur);
    if (!ask_text("Chown", "Owner and group (user:group):", &v, 0))
        return;
    char buf[300];
    snprintf(buf, sizeof(buf), "%s", v.c_str());
    char* colon = strchr(buf, ':');
    if (colon)
        *colon = 0;
    tf::Attrs a;
    a.uid = st.uid;
    a.gid = st.gid;
    if (buf[0]) {
        struct passwd* p = getpwnam(buf);
        a.uid = p ? (long)p->pw_uid : atol(buf);
    }
    if (colon && colon[1]) {
        struct group* g = getgrnam(colon + 1);
        a.gid = g ? (long)g->gr_gid : atol(colon + 1);
    }
    for (int i = 0; i < names.size(); i++) {
        int r = active()->fs()->set_attrs(active_path().child(names[i].c_str()).c_str(), a,
                                          tf::ATTR_OWNER);
        if (r != 0) {
            char m[600];
            snprintf(m, sizeof(m), "Cannot change %.400s:\n%s", names[i].c_str(), strerror(r));
            show_error(m);
            break;
        }
    }
    active()->unmark_all();
    active()->reload();
}

void App::cmd_symlink()
{
    const Entry* e = active()->current();
    int w = screen_.cols() - 10;
    if (w > 70)
        w = 70;
    tu::Dialog d("Symbolic link", w, 11);
    d.add(new tu::Label("Existing file (what the link points to):"), 3, 2, w - 6);
    tu::InputLine* target = new tu::InputLine(
        e && !e->up ? active_path().child(e->name.c_str()).c_str() : active_path().c_str());
    d.add(target, 3, 3, w - 6);
    d.add(new tu::Label("Name of the link:"), 3, 5, w - 6);
    tu::InputLine* link = new tu::InputLine(
        e && !e->up ? other()->path().child(e->name.c_str()).c_str() : other()->path().c_str());
    d.add(link, 3, 6, w - 6);
    tu::Button* b[2] = { new tu::Button("&OK", 1, true), new tu::Button("&Cancel", 2) };
    d.add_buttons(8, b, 2);
    d.focus(link);
    if (modal(d, theme_.dialog) != 1)
        return;
    tf::Path lp = tf::Path::resolve(active_path(), link->text());
    int r = active()->fs()->symlink(target->text(), lp.c_str());
    if (r != 0) {
        char m[4300];
        snprintf(m, sizeof(m), "Cannot create %.4000s:\n%s", lp.c_str(), strerror(r));
        show_error(m);
    }
    panels_[0]->reload();
    panels_[1]->reload();
}

/* ------------------------------------------------------------------ */
/* Hotlist, find, compare                                              */

void App::cmd_hotlist()
{
    ttr::Str path = config_file("hotlist");
    for (;;) {
        tu::Dialog d("Directory hotlist", 64, screen_.rows() - 6 > 22 ? 22 : screen_.rows() - 6);
        int lh = d.height() - 6;
        tu::ListBox* list = new tu::ListBox();
        list->activate_result = 1;
        char* text = slurp(path.c_str());
        for (char* line = text ? strtok(text, "\n") : 0; line; line = strtok(0, "\n"))
            if (line[0] && line[0] != '#')
                list->items.add(line);
        free(text);
        d.add(list, 3, 2, 58, lh);
        tu::Button* b[4] = { new tu::Button("&Go", 1, true), new tu::Button("&Add current", 2),
                             new tu::Button("&Remove", 3), new tu::Button("&Cancel", 4) };
        d.add_buttons(d.height() - 3, b, 4);
        int r = modal(d, theme_.dialog);
        if (r == 1 && list->items.size()) {
            if (!active()->load(tf::Path(list->items[list->current])))
                show_error(active()->error());
            return;
        }
        if (r == 2 || (r == 3 && list->items.size())) {
            if (r == 2)
                list->items.add(active_path().c_str());
            else
                list->items.remove(list->current);
            make_config_dir();
            FILE* f = fopen(path.c_str(), "w");
            if (f) {
                for (int i = 0; i < list->items.size(); i++)
                    fprintf(f, "%s\n", list->items[i]);
                fclose(f);
            }
            continue;
        }
        return;
    }
}

namespace {

/* Find file: walks directories between events. */
struct FindState {
    tf::Fs* fs;
    char base[4096];        /* results are shown relative to it */
    char pattern[512];
    char content[512];
    ttr::Vector<ttr::Str> dirs;
    tu::ListBox* list;
    tu::Label* status;
    int found;
    bool done;
};

bool contains(tf::Fs* fs, const char* path, const char* text)
{
    int err = 0;
    tf::Stream* s = fs->open(path, tf::OPEN_READ, 0, &err);
    if (!s)
        return false;
    size_t tl = strlen(text);
    char buf[65536 + 512];
    size_t keep = 0;
    bool hit = false;
    for (;;) {
        long n = s->read(buf + keep, 65536);
        if (n <= 0)
            break;
        size_t len = keep + (size_t)n;
        for (size_t i = 0; i + tl <= len; i++)
            if (buf[i] == text[0] && memcmp(buf + i, text, tl) == 0) {
                hit = true;
                break;
            }
        if (hit)
            break;
        keep = tl > 1 && len >= tl - 1 ? tl - 1 : 0;
        memmove(buf, buf + len - keep, keep);
    }
    delete s;
    return hit;
}

bool find_step(void* ctx)
{
    FindState* f = (FindState*)ctx;
    long end = now_ms() + 40;
    while (f->dirs.size() > 0 && now_ms() < end) {
        ttr::Str dir = f->dirs[f->dirs.size() - 1];
        f->dirs.remove_at(f->dirs.size() - 1);
        char st[600];
        snprintf(st, sizeof(st), "Searching %.500s", dir.c_str());
        f->status->set(st);
        int err = 0;
        tf::DirReader* d = f->fs->open_dir(dir.c_str(), &err);
        if (!d)
            continue;
        const char* n;
        tf::Path base(dir.c_str());
        while ((n = d->next()) != 0) {
            tf::Path p = base.child(n);
            tf::Stat s;
            if (f->fs->stat(p.c_str(), &s, false) != 0)
                continue;
            if (s.type == tf::FT_DIR)
                f->dirs.push_back(ttr::Str(p.c_str()));
            if (!tf::match_pattern(f->pattern, n, false))
                continue;
            if (f->content[0] && (s.type != tf::FT_FILE || !contains(f->fs, p.c_str(), f->content)))
                continue;
            size_t bl = strlen(f->base);
            const char* shown = p.c_str();
            if (strncmp(shown, f->base, bl) == 0 && shown[bl] == '/')
                shown += bl + 1;
            else if (strcmp(f->base, "/") == 0)
                shown += 1;
            f->list->items.add(shown);
            f->found++;
        }
        delete d;
    }
    if (f->dirs.size() == 0) {
        char st[100];
        sprintf(st, "Done: %d found.", f->found);
        f->status->set(st);
        f->done = true;
        return false;
    }
    return true;
}

} /* namespace */

void App::cmd_find()
{
    int w = screen_.cols() - 10;
    if (w > 70)
        w = 70;
    tu::Dialog q("Find file", w, 12);
    q.add(new tu::Label("Start at:"), 3, 2, w - 6);
    tu::InputLine* start = new tu::InputLine(active_path().c_str());
    q.add(start, 3, 3, w - 6);
    q.add(new tu::Label("File name (* ? [abc]):"), 3, 4, w - 6);
    tu::InputLine* name = new tu::InputLine("*");
    name->set_history(&hist_find_);
    q.add(name, 3, 5, w - 6);
    q.add(new tu::Label("Containing (empty: any):"), 3, 6, w - 6);
    tu::InputLine* text = new tu::InputLine("");
    q.add(text, 3, 7, w - 6);
    tu::Button* b[2] = { new tu::Button("&Find", 1, true), new tu::Button("&Cancel", 2) };
    q.add_buttons(9, b, 2);
    q.focus(name);
    if (modal(q, theme_.dialog) != 1)
        return;
    if (*name->text())
        hist_find_.add(name->text());

    FindState f;
    f.fs = active()->fs();
    snprintf(f.pattern, sizeof(f.pattern), "%.500s", *name->text() ? name->text() : "*");
    snprintf(f.content, sizeof(f.content), "%.500s", text->text());
    tf::Path base = tf::Path::resolve(active_path(), start->text());
    snprintf(f.base, sizeof(f.base), "%s", base.c_str());
    f.dirs.push_back(ttr::Str(base.c_str()));
    f.found = 0;
    f.done = false;
    int h = screen_.rows() - 4;
    if (h > 24)
        h = 24;
    tu::Dialog d("Find file", w, h);
    f.list = new tu::ListBox();
    f.list->activate_result = 1;
    d.add(f.list, 3, 2, w - 6, h - 7);
    f.status = new tu::Label("");
    d.add(f.status, 3, h - 4, w - 6);
    tu::Button* rb[2] = { new tu::Button("&Go to", 1, true), new tu::Button("&Close", 2) };
    d.add_buttons(h - 2, rb, 2);
    int r = modal(d, theme_.dialog, find_step, &f);
    if (r == 1 && f.list->items.size() > 0) {
        tf::Path p = base.child(f.list->items[f.list->current]);
        if (active()->load(p.parent()))
            active()->select_name(p.name());
        else
            show_error(active()->error());
    }
}

/* Mark the files that are only in one panel, or newer there. */
void App::cmd_compare()
{
    Panel* a = panels_[0];
    Panel* b = panels_[1];
    a->unmark_all();
    b->unmark_all();
    int marked = 0;
    for (int side = 0; side < 2; side++) {
        Panel* p = side ? b : a;
        Panel* q = side ? a : b;
        for (int i = 0; i < p->count(); i++) {
            const Entry* e = p->entry(i);
            if (e->up || e->st.type == tf::FT_DIR)
                continue;
            const Entry* match = 0;
            for (int k = 0; k < q->count(); k++)
                if (strcmp(q->entry(k)->name.c_str(), e->name.c_str()) == 0)
                    match = q->entry(k);
            if (!match || match->st.type == tf::FT_DIR || e->st.mtime > match->st.mtime ||
                (e->st.mtime == match->st.mtime && e->st.size != match->st.size)) {
                p->set_marked(e->name.c_str(), true);
                marked++;
            }
        }
    }
    if (!marked)
        message("%s", "The directories hold the same files.");
}

/* ------------------------------------------------------------------ */
/* Running files: user menu, view, edit, associations                  */

ttr::Str App::quoted(const char* s)
{
    char q[4200];
    if (!shell_.quote(s, q, sizeof(q)))
        return ttr::Str("''");
    return ttr::Str(q);
}

/* mc's macros: %f the current file, %F the other panel's, %d and %D the
 * directories, %s the marked files (or the current one), %t the marked
 * ones, %% a percent sign. Names are quoted for the shell. */
ttr::Str App::expand(const char* command)
{
    size_t cap = strlen(command) * 2 + 4096, o = 0;
    char* out = (char*)malloc(cap);
    if (!out)
        return ttr::Str("");
    for (const char* p = command; *p; p++) {
        ttr::Str add("");
        bool macro = true;
        if (*p == '%' && p[1]) {
            char c = p[1];
            const Entry* e;
            switch (c) {
            case 'f':
                e = active()->current();
                add = quoted(e && !e->up ? e->name.c_str() : ".");
                break;
            case 'F':
                e = other()->current();
                add = quoted(e && !e->up ? other()->path().child(e->name.c_str()).c_str()
                                         : other()->path().c_str());
                break;
            case 'd': add = quoted(active_path().c_str()); break;
            case 'D': add = quoted(other()->path().c_str()); break;
            case 's': case 't': {
                ttr::Vector<ttr::Str> names;
                if (c == 's')
                    active()->selection(&names);
                else
                    for (int i = 0; i < active()->count(); i++)
                        if (active()->entry(i)->marked)
                            names.push_back(active()->entry(i)->name);
                char buf[8192];
                size_t bo = 0;
                buf[0] = 0;
                for (int i = 0; i < names.size(); i++) {
                    ttr::Str q = quoted(names[i].c_str());
                    size_t ql = strlen(q.c_str());
                    if (bo + ql + 2 >= sizeof(buf))
                        break;
                    if (bo)
                        buf[bo++] = ' ';
                    memcpy(buf + bo, q.c_str(), ql + 1);
                    bo += ql;
                }
                add = buf;
                break;
            }
            case '%': add = "%"; break;
            default: macro = false; break;
            }
        } else {
            macro = false;
        }
        const char* a = macro ? add.c_str() : 0;
        size_t al = macro ? strlen(a) : 1;
        if (o + al + 1 >= cap) {
            cap = (o + al + 1) * 2;
            char* n = (char*)realloc(out, cap);
            if (!n)
                break;
            out = n;
        }
        if (macro) {
            memcpy(out + o, a, al);
            o += al;
            p++;
        } else {
            out[o++] = *p;
        }
    }
    out[o] = 0;
    ttr::Str r(out);
    free(out);
    return r;
}

/* The user menu (F2): ~/.config/ttrc/menu, entries of a line "key title"
 * and the command lines under it, indented:
 *
 *   t Make a tar of the marked files
 *       tar cf archive.tar %s
 */
void App::cmd_user_menu()
{
    static const char k_default_menu[] =
        "# ttrc user menu: a key and a title, then the command indented.\n"
        "# %f current file, %F the other panel's, %d %D the directories,\n"
        "# %s marked files (or the current one), %t marked files.\n"
        "t Pack the marked files into archive.tar\n"
        "    tar cf archive.tar %s\n"
        "z Compress the current file with gzip\n"
        "    gzip -9 %f\n"
        "d Show disk usage of the current directory\n"
        "    du -sk * | sort -n\n"
        "c Compare the current file with the other panel's\n"
        "    diff %f %F | more\n";
    ttr::Str path = config_file("menu");
    char* text = slurp(path.c_str());
    if (!text)
        text = strdup(k_default_menu);
    if (!text)
        return;
    tu::Dialog d("User menu", 64, screen_.rows() - 6 > 20 ? 20 : screen_.rows() - 6);
    tu::ListBox* list = new tu::ListBox();
    list->activate_result = 1;
    tu::StrList commands;
    char* save = 0;
    char* line = strtok_r(text, "\n", &save);
    while (line) {
        if (line[0] == '#' || !line[0]) {
            line = strtok_r(0, "\n", &save);
            continue;
        }
        if (line[0] != ' ' && line[0] != '\t') {
            list->items.add(line);
            commands.add("");
        } else if (commands.size() > 0) {
            const char* c = line;
            while (*c == ' ' || *c == '\t')
                c++;
            char joined[8192];
            const char* prev = commands[commands.size() - 1];
            snprintf(joined, sizeof(joined), "%s%s%s", prev, *prev ? "\n" : "", c);
            commands.set(commands.size() - 1, joined);
        }
        line = strtok_r(0, "\n", &save);
    }
    free(text);
    if (list->items.size() == 0) {
        message("%s", "The user menu is empty.");
        delete list;
        return;
    }
    d.add(list, 3, 2, 58, d.height() - 6);
    tu::Button* b[2] = { new tu::Button("&Run", 1, true), new tu::Button("&Cancel", 2) };
    d.add_buttons(d.height() - 3, b, 2);
    if (modal(d, theme_.dialog) != 1)
        return;
    ttr::Str run_cmd = expand(commands[list->current]);
    /* Several lines: one command line for the shell. */
    char buf[8192];
    snprintf(buf, sizeof(buf), "%s", run_cmd.c_str());
    for (char* c = buf; *c; c++)
        if (*c == '\n')
            *c = ';';
    run(buf);
}

/* What Enter does with a file: ~/.config/ttrc/assoc, lines of
 * "patterns: command", for example "*.tar.gz *.tgz: tar tzvf %f | more". */
bool App::open_file(const Entry& e)
{
    char* text = slurp(config_file("assoc").c_str());
    if (!text)
        return false;
    bool done = false;
    char* save = 0;
    for (char* line = strtok_r(text, "\n", &save); line && !done; line = strtok_r(0, "\n", &save)) {
        if (line[0] == '#')
            continue;
        char* colon = strchr(line, ':');
        if (!colon)
            continue;
        *colon = 0;
        const char* cmd = colon + 1;
        while (*cmd == ' ')
            cmd++;
        char* psave = 0;
        for (char* pat = strtok_r(line, " \t", &psave); pat; pat = strtok_r(0, " \t", &psave))
            if (tf::match_pattern(pat, e.name.c_str(), true)) {
                ttr::Str c = expand(cmd);
                run(c.c_str());
                done = true;
                break;
            }
    }
    free(text);
    return done;
}

/* The file under the cursor as a local path: the file itself, or a copy
 * of it in a temporary directory when it is inside an archive. */
ttr::Str App::local_file(const Entry& e)
{
    tf::Path p = active_path().child(e.name.c_str());
    if (tf::is_local_path(p.c_str()))
        return ttr::Str(p.c_str());
    ttr::Str td = temp_dir();
    const char* dir = td.c_str();
    tf::local_fs()->mkdir(dir, 0700);
    tf::Job job(tf::OP_COPY, tf::vfs(), tf::vfs());
    job.add(p.c_str());
    job.set_target(dir, true);
    job.set_overwrite_all(true);
    if (!run_job(job, "Extracting"))
        return ttr::Str("");
    return ttr::Str(tf::Path(dir).child(e.name.c_str()).c_str());
}

/* F3: ttrc's viewer, which reads through tf (archives too), or $PAGER on
 * a local copy. */
void App::cmd_view()
{
    const Entry* e = active()->current();
    if (!e || e->up || e->st.type == tf::FT_DIR)
        return;
    if (config_.internal_viewer) {
        Viewer v;
        v.set_images(ti::protocol_from_setting(config_.images.c_str()), &term_);
        int err = v.load(tf::vfs(), active_path().child(e->name.c_str()).c_str());
        if (err) {
            char m[600];
            snprintf(m, sizeof(m), "Cannot read %.400s:\n%s", e->name.c_str(), strerror(err));
            show_error(m);
            return;
        }
        run_tool(&v);
        return;
    }
    ttr::Str f = local_file(*e);
    if (f.empty())
        return;
    const char* pager = getenv("PAGER");
    char c[4400];
    snprintf(c, sizeof(c), "%s %s", pager && *pager ? pager : "more", quoted(f.c_str()).c_str());
    run(c);
}

/* ttre, or $VISUAL / $EDITOR (vi) when the configuration says so, on the
 * file in the shell. A file in an archive is edited as a temporary copy. */
ttr::Str App::editor_command(const char* file)
{
    ttr::Str prog("");
    if (config_.internal_editor) {
        prog = quoted(ttre_path().c_str());
    } else {
        const char* ed = getenv("VISUAL");
        if (!ed || !*ed)
            ed = getenv("EDITOR");
        prog = ed && *ed ? ed : "vi";
    }
    char c[4400];
    snprintf(c, sizeof(c), "%s %s", prog.c_str(), quoted(file).c_str());
    return ttr::Str(c);
}

void App::cmd_edit()
{
    const Entry* e = active()->current();
    if (!e || e->up || e->st.type == tf::FT_DIR)
        return;
    ttr::Str f = local_file(*e);
    if (f.empty())
        return;
    if (!tf::is_local_path(active_path().c_str()))
        message("%s", "Changes to a file in an archive stay in the temporary copy.");
    run(editor_command(f.c_str()).c_str());
}

/* ------------------------------------------------------------------ */
/* Menus and options                                                   */

void App::open_menu(int which)
{
    tu::MenuBar m;
    m.add("Left", k_left);
    m.add("File", k_file);
    m.add("Command", k_command);
    m.add("Options", k_options);
    m.add("Right", k_right);
    m.open(which);
    while (!quit_ && !m.done()) {
        draw();
        m.draw(screen_, theme_.menu, theme_.menu_bar, theme_.menu_hotkey);
        screen_.flush();
        bool timed_out = false;
        if (!wait_input(input_.pending() ? input_.timeout_ms() : 200, &timed_out)) {
            quit_ = true;
            return;
        }
        tu::Event ev;
        while (!m.done() && input_.next(&ev, timed_out))
            m.handle(ev);
    }
    if (m.selected() > 0)
        menu_command(m.selected());
}

void App::menu_command(int id)
{
    if (id >= M_LEFT_FULL && id <= M_RIGHT_REREAD) {
        Panel* p = id >= M_RIGHT_FULL ? panels_[1] : panels_[0];
        int k = id >= M_RIGHT_FULL ? id - M_RIGHT_FULL : id - M_LEFT_FULL;
        switch (k) {
        case 0: p->set_list_mode(LIST_FULL); break;
        case 1: p->set_list_mode(LIST_BRIEF); break;
        case 2: p->set_sort_mode(SORT_NAME, false); break;
        case 3: p->set_sort_mode(SORT_EXT, false); break;
        case 4: p->set_sort_mode(SORT_TIME, false); break;
        case 5: p->set_sort_mode(SORT_SIZE, false); break;
        case 6: p->set_sort_mode(SORT_UNSORTED, false); p->reload(); break;
        case 7: p->set_sort_mode(p->sort(), !p->reversed()); break;
        case 8: p->toggle_hidden(); break;
        case 9: p->reload(); break;
        default: break;
        }
        return;
    }
    switch (id) {
    case M_VIEW: cmd_view(); break;
    case M_EDIT: cmd_edit(); break;
    case M_COPY: cmd_copy(false); break;
    case M_MOVE: cmd_copy(true); break;
    case M_MKDIR: cmd_mkdir(); break;
    case M_DELETE: cmd_delete(); break;
    case M_CHMOD: cmd_chmod(); break;
    case M_CHOWN: cmd_chown(); break;
    case M_SYMLINK: cmd_symlink(); break;
    case M_SELECT: cmd_select('+'); break;
    case M_UNSELECT: cmd_select('-'); break;
    case M_INVERT: cmd_select('*'); break;
    case M_QUIT: request_quit(); break;
    case M_USERMENU: cmd_user_menu(); break;
    case M_HOTLIST: cmd_hotlist(); break;
    case M_FIND: cmd_find(); break;
    case M_COMPARE: cmd_compare(); break;
    case M_SWAP: {
        Panel* t = panels_[0];
        panels_[0] = panels_[1];
        panels_[1] = t;
        break;
    }
    case M_SHELL: {
        tu::Event ev;
        ev.key = tu::KEY_CHAR;
        ev.ch = 'o';
        ev.mods = tu::MOD_CTRL;
        handle(ev);
        break;
    }
    case M_CONFIG: cmd_config(); break;
    case M_APPEARANCE: cmd_appearance(); break;
    case M_SAVE:
        save_setup();
        message("%s", "Saved.");
        break;
    default: break;
    }
}

void App::cmd_config()
{
    tu::Dialog d("Configuration", 56, 17);
    tu::CheckBox* c1 = new tu::CheckBox("Confirm &delete", config_.confirm_delete);
    tu::CheckBox* c2 = new tu::CheckBox("Ask before &overwriting", config_.confirm_overwrite);
    tu::CheckBox* c9 = new tu::CheckBox("Ask before &quitting", config_.confirm_exit);
    tu::CheckBox* c3 = new tu::CheckBox("&Return to the panels after a command", config_.auto_return);
    tu::CheckBox* c4 = new tu::CheckBox("Show &hidden files", config_.show_hidden);
    tu::CheckBox* c5 = new tu::CheckBox("Re&member the panels' directories", config_.remember_dirs);
    tu::CheckBox* c6 = new tu::CheckBox("Copies keep modes and &times", config_.preserve_attrs);
    tu::CheckBox* c7 = new tu::CheckBox("F3: ttrc's own &viewer ($PAGER if off)", config_.internal_viewer);
    tu::CheckBox* c8 = new tu::CheckBox("F4: tt&re, ttr's editor ($EDITOR if off)", config_.internal_editor);
    d.add(c1, 4, 2, 48);
    d.add(c2, 4, 3, 48);
    d.add(c3, 4, 4, 48);
    d.add(c4, 4, 5, 48);
    d.add(c5, 4, 6, 48);
    d.add(c6, 4, 7, 48);
    d.add(c7, 4, 8, 48);
    d.add(c8, 4, 9, 48);
    d.add(c9, 4, 10, 48);
    d.add(new tu::Label("Saved in ~/.config/ttrc/settings"), 4, 12, 48);
    tu::Button* b[2] = { new tu::Button("&OK", 1, true), new tu::Button("&Cancel", 2) };
    d.add_buttons(14, b, 2);
    if (modal(d, theme_.dialog) != 1)
        return;
    config_.confirm_delete = c1->on;
    config_.confirm_overwrite = c2->on;
    config_.auto_return = c3->on;
    config_.remember_dirs = c5->on;
    config_.preserve_attrs = c6->on;
    config_.internal_viewer = c7->on;
    config_.internal_editor = c8->on;
    config_.confirm_exit = c9->on;
    if (c4->on != config_.show_hidden) {
        config_.show_hidden = c4->on;
        panels_[0]->set_hidden(c4->on);
        panels_[1]->set_hidden(c4->on);
    }
    save_setup();
}

void App::cmd_appearance()
{
    tu::Dialog d("Appearance", 78, 16);
    d.add(new tu::Label("Theme:"), 4, 2, 20);
    tu::RadioGroup* th = new tu::RadioGroup();
    th->add("&Automatic");
    const char* const* names = theme_names();
    int sel = 0;
    for (int i = 0; names[i]; i++) {
        th->add(names[i]);
        if (strcmp(names[i], config_.theme.c_str()) == 0)
            sel = i + 1;
    }
    th->selected = sel;
    d.add(th, 4, 3, 22, th->h);
    d.add(new tu::Label("Terminal:"), 28, 2, 24);
    tu::RadioGroup* tr = new tu::RadioGroup();
    static const char* const tiers[] = { "", "basic", "ansi", "256", "modern" };
    tr->add("Au&tomatic");
    tr->add("&Basic (no colour)");
    tr->add("&8/16 colours");
    tr->add("256 c&olours");
    tr->add("True colour, &Unicode");
    for (int i = 0; i < 5; i++)
        if (strcmp(tiers[i], config_.tier.c_str()) == 0)
            tr->selected = i;
    d.add(tr, 28, 3, 24, tr->h);
    d.add(new tu::Label("Images in the viewer:"), 54, 2, 22);
    tu::RadioGroup* im = new tu::RadioGroup();
    static const char* const images[] = { "", "blocks", "kitty", "sixel", "iterm", "off" };
    im->add("Auto&matic");
    im->add("&Half blocks");
    im->add("&kitty graphics");
    im->add("&Sixel");
    im->add("iTerm&2 inline");
    im->add("O&ff (the bytes)");
    for (int i = 0; i < 6; i++)
        if (strcmp(images[i], config_.images.c_str()) == 0)
            im->selected = i;
    d.add(im, 54, 3, 22, im->h);
    d.add(new tu::Label("Automatic: what the terminal says it can do."), 4, 10, 70);
    tu::Button* b[2] = { new tu::Button("&OK", 1, true), new tu::Button("&Cancel", 2) };
    d.add_buttons(13, b, 2);
    if (modal(d, theme_.dialog) != 1)
        return;
    config_.theme = th->selected > 0 ? names[th->selected - 1] : "";
    config_.tier = tiers[tr->selected];
    config_.images = images[im->selected];
    if (config_.tier.empty()) {
        /* Back to what the terminal said. */
        tu::Term probe;
        if (probe.open(term_.in_fd(), 1))
            term_.set_tier(probe.tier());
    }
    apply_look();
    save_setup();
}

void App::save_setup()
{
    config_.left_dir = panels_[0]->path().c_str();
    config_.right_dir = panels_[1]->path().c_str();
    config_.left_mode = panels_[0]->list_mode();
    config_.right_mode = panels_[1]->list_mode();
    config_.left_sort = panels_[0]->sort() | (panels_[0]->reversed() ? 16 : 0);
    config_.right_sort = panels_[1]->sort() | (panels_[1]->reversed() ? 16 : 0);
    make_config_dir();
    config_.save();
}

} /* namespace ttrc */

namespace ttrc {

/* The viewer full-screen, until it closes; its requests for a
 * dialog are answered here. */
void App::run_tool(FullScreen* tool)
{
    tool_ = tool;
    screen_.invalidate();
    bool done = false;
    bool repaint = true;        /* the tool's graphics, after a full redraw */
    while (!quit_ && !done) {
        draw();
        screen_.flush();
        tool->paint_over(term_, repaint);
        repaint = false;
        int cols = screen_.cols(), rows = screen_.rows();
        bool timed_out = false;
        if (!wait_input(input_.pending() ? input_.timeout_ms() : -1, &timed_out)) {
            quit_ = true;
            break;
        }
        if (cols != screen_.cols() || rows != screen_.rows())
            repaint = true;
        tu::Event ev;
        while (!done && input_.next(&ev, timed_out)) {
            ToolRequest r = tool->handle(ev);
            if (tool->take_redraw()) {
                screen_.invalidate();
                repaint = true;
            }
            if (r == REQ_SEARCH || r == REQ_GOTO) {
                /* Dialogs go over the tool's graphics: take them away. */
                tool->hide_over(term_);
                repaint = true;
            }
            switch (r) {
            case REQ_CLOSE:
                done = true;
                break;
            case REQ_SEARCH: {
                SearchQuery q;
                tool->search_defaults(&q);
                if (ask_search(&q))
                    tool->search(q);
                break;
            }
            case REQ_GOTO: {
                ttr::Str v("");
                if (ask_text(tool->goto_title(), tool->goto_label(), &v, 0) && !v.empty())
                    tool->go_to(v.c_str());
                break;
            }
            default:
                break;
            }
        }
    }
    tool->hide_over(term_);
    tool_ = 0;
    screen_.invalidate();
    panels_[0]->reload();
    panels_[1]->reload();
}

/* Shift-F4: edit a new file. */
void App::cmd_new_file()
{
    ttr::Str name("");
    if (!ask_text("New file", "Name of the file to edit:", &name, 0) || name.empty())
        return;
    if (!tf::is_local_path(active_path().c_str())) {
        show_error("A new file cannot go into an archive.");
        return;
    }
    tf::Path p = tf::Path::resolve(active_path(), name.c_str());
    run(editor_command(p.c_str()).c_str());
}

} /* namespace ttrc */
