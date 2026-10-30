/*
 * ttrcode's debugger (td/DESIGN.md section 8.2): Visual C++ 6's keys and
 * windows on td::Session. F5 starts or continues, Shift+F5 stops,
 * Ctrl+Shift+F5 restarts, F9 sets or clears a breakpoint on the caret's
 * line, F10, F11 and Shift+F11 step over, into and out, Ctrl+F10 runs to
 * the caret. Breakpoints show as red dots in the margin and where the
 * program stopped as a yellow arrow; the panes fill from td
 * (debugpanes.cpp). While the program runs, td calls debug_idle, which
 * goes on handling the window's events (Debug > Break interrupts it).
 * Debug > Open Core File shows a crashed program's core file in the same
 * windows; it does not run. Debug > Attach to Process (attach.cpp) stops
 * a running program to debug it; Stop Debugging then lets it go on.
 */
#include "app.h"

#include "../../td/core.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/keysym.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/MessageB.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/Separator.h>
#include <Xm/TextF.h>

namespace ttrcode {

namespace {

/* The debugging layout until the user makes their own (ttrcode/DESIGN.md
 * 4.1). */
const char k_debug_layout[] =
    "left:220=workspace;bottom:220=program,terminal,results@340|variables,watch;"
    "right:260=callstack,threads@260|registers";

void idle_done(XtPointer data, XtIntervalId*)
{
    *(bool*)data = true;
}

struct SettingsHook {
    App* app;
    int which;
};

void settings_cb(Widget, XtPointer data, XtPointer)
{
    SettingsHook* h = (SettingsHook*)data;
    h->app->debug_settings_button(h->which);
}

/* The program as Debug > Settings names it: relative ones in the
 * workspace. */
ttr::Str program_path(const ttr::Str& prog, const ttr::Str& root)
{
    if (prog.c_str()[0] != '/' && strchr(prog.c_str(), '/')) {
        char p[4096];
        snprintf(p, sizeof(p), "%s/%s", root.c_str(), prog.c_str());
        return ttr::Str(p);
    }
    return prog;
}

/* A path as the file system has it, so the editor's and the line
 * tables' names of one file compare equal. */
ttr::Str real_path(const char* p)
{
    char buf[4096];
    if (p && *p && realpath(p, buf))
        return ttr::Str(buf);
    return ttr::Str(p ? p : "");
}

const char* signal_name(int sig)
{
    switch (sig) {
    case SIGINT: return "SIGINT";
    case SIGILL: return "SIGILL";
    case SIGTRAP: return "SIGTRAP";
    case SIGABRT: return "SIGABRT";
    case SIGFPE: return "SIGFPE";
    case SIGBUS: return "SIGBUS";
    case SIGSEGV: return "SIGSEGV";
    case SIGPIPE: return "SIGPIPE";
    case SIGTERM: return "SIGTERM";
    default: return "a signal";
    }
}

} /* namespace */

const char* default_debug_layout()
{
    return k_debug_layout;
}

/* ---- running ---- */

/* td waits for the program here: the window goes on, for 20 ms at a
 * time. A command that would start the program again is refused while
 * it runs (debug_command checks busy()). */
void App::debug_idle(void* ctx)
{
    App* a = (App*)ctx;
    XtAppContext app = XtWidgetToApplicationContext(a->top_);
    bool done = false;
    XtIntervalId id = XtAppAddTimeOut(app, 20, idle_done, &done);
    (void)id;
    while (!done)
        XtAppProcessEvent(app, XtIMAll);
}

void App::debug_status(const char* msg)
{
    dbg_msg_ = msg ? msg : "";
    update_ui();
}

bool App::debug_start()
{
    if (dbg_program_.empty()) {
        show_debug_settings();
        debug_status("Debug > Settings: the program to debug first.");
        return false;
    }
    /* The program and its arguments, split at spaces. */
    char line[4096];
    snprintf(line, sizeof(line), "%s %s", dbg_program_.c_str(), dbg_args_.c_str());
    char* argv[64];
    int argc = 0;
    char* p = line;
    while (*p && argc < 63) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
        if (*p)
            *p++ = 0;
    }
    argv[argc] = 0;
    if (!argc)
        return false;
    /* A relative program is in the workspace. */
    char prog[4096];
    snprintf(prog, sizeof(prog), "%s", program_path(ttr::Str(argv[0]), root_).c_str());
    argv[0] = prog;
    /* The debugging layout, with the Program pane in front: the program's
     * terminal, made with the pane. */
    edit_layout_ = docks_->layout();
    docks_->apply(debug_layout_.empty() ? k_debug_layout : debug_layout_.c_str());
    docks_->show(pane_program_);
    td::LaunchOptions lo;
    lo.cwd = dbg_dir_.empty() ? root_.c_str() : dbg_dir_.c_str();
    lo.tty = prog_term_ ? prog_term_->tty_name() : 0;
    dbg_ = new td::Session;
    dbg_->set_idle(debug_idle, this);
    ttr::Str err;
    if (!dbg_->start(argv, &err, lo)) {
        delete dbg_;
        dbg_ = 0;
        debug_layout_ = docks_->layout();
        docks_->apply(edit_layout_.c_str());
        error("Cannot debug: %s", err.c_str(), 0);
        debug_status("");
        return false;
    }
    int i;
    for (i = 0; i < bps_.size(); i++)
        dbg_->add_breakpoint(breakpoint_spec(bps_[i]).c_str(), 0);
    set_data_breakpoints();
    dbg_kill_req_ = false;
    have_last_regs_ = false;
    regs_stop_ = -1;
    debug_status("Running");
    return true;
}

/* A core file, of the program it names (Linux), else of Debug >
 * Settings' program. */
void App::debug_open_core()
{
    ttr::Str path;
    if (!ask_file("Open Core File", root_.c_str(), &path))
        return;
    debug_end();
    ttr::Str prog;
    if (!dbg_program_.empty()) {
        /* The program is the first word of Settings' Program. */
        char first[4096];
        snprintf(first, sizeof(first), "%s", dbg_program_.c_str());
        char* sp = strchr(first, ' ');
        if (sp)
            *sp = 0;
        prog = program_path(ttr::Str(first), root_);
    }
    dbg_ = new td::Session;
    ttr::Str err;
    td::Stop why;
    bool ok = dbg_->open_core(path.c_str(), 0, &err, &why);
    if (!ok && !prog.empty()) {
        delete dbg_;
        dbg_ = new td::Session;
        err = "";
        ok = dbg_->open_core(path.c_str(), prog.c_str(), &err, &why);
    }
    if (!ok) {
        delete dbg_;
        dbg_ = 0;
        error("Cannot read the core file: %s", err.c_str(), 0);
        return;
    }
    edit_layout_ = docks_->layout();
    docks_->apply(debug_layout_.empty() ? k_debug_layout : debug_layout_.c_str());
    dbg_kill_req_ = false;
    have_last_regs_ = false;
    regs_stop_ = -1;
    debug_stopped(why);
}

void App::debug_end()
{
    tip_hide();
    if (!dbg_)
        return;
    if (dbg_->running()) {
        if (dbg_attached_)
            dbg_->detach();     /* it was running before: it runs on */
        else
            dbg_->kill();
    }
    dbg_attached_ = false;
    delete dbg_;
    dbg_ = 0;
    int i;
    for (i = 0; i < data_bps_.size(); i++) {
        data_bps_[i].id = 0;
        data_bps_[i].why = "";
    }
    fill_breakpoints();
    stop_path_ = "";
    frame_path_ = "";
    frames_.clear();
    thread_ids_.clear();
    thread_where_.clear();
    cur_frame_ = 0;
    have_last_regs_ = false;
    /* td's values go with the session; what is open stays. */
    watch_tree_.clear();
    vars_tree_.clear();
    quick_tree_.clear();
    debug_fill();
    debug_layout_ = docks_->layout();
    docks_->apply(edit_layout_.c_str());
    save_settings();
    TwGridDamage(grid_);
}

/* After every run: where it is, or how it ended. */
void App::debug_stopped(const td::Stop& s)
{
    if (dbg_kill_req_ || !dbg_) {
        debug_end();
        debug_status("Debugging stopped.");
        return;
    }
    char msg[600];
    if (s.kind == td::EV_EXITED) {
        debug_end();
        snprintf(msg, sizeof(msg), "The program exited with code %d.", s.status);
        debug_status(msg);
        return;
    }
    if (s.kind == td::EV_KILLED || s.kind == td::EV_ERROR) {
        debug_end();
        if (s.kind == td::EV_KILLED)
            snprintf(msg, sizeof(msg), "The program was killed by %s (%d).",
                     signal_name(s.sig), s.sig);
        else
            snprintf(msg, sizeof(msg), "The program is gone.");
        debug_status(msg);
        return;
    }
    stop_serial_++;
    load_threads();
    load_frames();
    /* Stopped from outside (Break, attaching): likely in the system's
     * code, so the first frame with source is shown. */
    int shown = s.kind == td::EV_PAUSED ? source_frame() : 0;
    /* After a command in Disassembly it keeps the focus; a stop where
     * there is no source shows it (VC6 offered to). */
    bool in_disassembly = insn_step_;
    insn_step_ = false;
    debug_show_frame(shown, !in_disassembly);
    if (in_disassembly && dis_list_)
        dis_list_->focus();
    else if (frames_.size() > shown && !frames_[shown].has_place)
        show_disassembly();
    ttr::Str where = frames_.size() ? td::format_place(frames_[shown]) : ttr::Str("??");
    if (thread_ids_.size() > 1) {
        /* Which thread, when there are several. */
        char t[1600];
        snprintf(t, sizeof(t), "%s, thread %d", where.c_str(), dbg_->thread_number(s.thread));
        where = t;
    }
    int i;
    if (s.kind == td::EV_SIGNAL && dbg_->is_core()) {
        snprintf(msg, sizeof(msg), "Core file of %s: %s in %s", dbg_->core()->program_name(),
                 signal_name(s.sig), where.c_str());
    } else if (s.kind == td::EV_SIGNAL) {
        snprintf(msg, sizeof(msg), "%s in %s", signal_name(s.sig), where.c_str());
    } else if (s.kind == td::EV_WATCH) {
        /* 'x' changed from 1 to 2. */
        const char* expr = "?";
        for (i = 0; i < data_bps_.size(); i++)
            if (data_bps_[i].id == s.watch)
                expr = data_bps_[i].expr.c_str();
        td::Value before, now;
        if (dbg_->watch_values(s.watch, &before, &now) && before.ok())
            snprintf(msg, sizeof(msg), "'%s' changed from %.120s to %.120s in %s", expr,
                     dbg_->format(before).c_str(), dbg_->format(now).c_str(), where.c_str());
        else
            snprintf(msg, sizeof(msg), "'%s' changed in %s", expr, where.c_str());
    } else if (s.kind == td::EV_WATCH_ENDED) {
        /* Its frame is gone, and with it the data breakpoint. */
        ttr::Str expr("?");
        for (i = 0; i < data_bps_.size(); i++)
            if (data_bps_[i].id == s.watch) {
                expr = data_bps_[i].expr;
                data_bps_.remove_at(i);
                break;
            }
        save_settings();
        fill_breakpoints();
        snprintf(msg, sizeof(msg), "The data breakpoint on '%s' ended with its frame: %s",
                 expr.c_str(), where.c_str());
    } else if (s.kind == td::EV_BREAK && s.bp > 0)
        snprintf(msg, sizeof(msg), "Breakpoint: %s", where.c_str());
    else
        snprintf(msg, sizeof(msg), "Stopped: %s", where.c_str());
    debug_status(msg);
}

void App::debug_command(int cmd)
{
    tip_hide();
    if (cmd == CMD_DEBUG_TOGGLE_BP) {
        toggle_breakpoint();
        return;
    }
    if (cmd == CMD_DEBUG_SETTINGS) {
        show_debug_settings();
        return;
    }
    if (cmd == CMD_DEBUG_QUICKWATCH) {
        show_quickwatch();
        return;
    }
    if (cmd == CMD_DEBUG_BREAKPOINTS) {
        show_breakpoints();
        return;
    }
    if (dbg_ && dbg_->busy()) {
        /* Running: only Break and Stop. */
        if (cmd == CMD_DEBUG_BREAK) {
            dbg_->interrupt();
        } else if (cmd == CMD_DEBUG_STOP) {
            dbg_kill_req_ = true;
            dbg_->interrupt();
        }
        return;
    }
    if (cmd == CMD_DEBUG_STOP) {
        if (dbg_) {
            bool attached = dbg_attached_ && dbg_->running();
            debug_end();
            debug_status(attached ? "Detached: the program runs on." : "Debugging stopped.");
        }
        return;
    }
    if (cmd == CMD_DEBUG_BREAK)
        return;
    if (cmd == CMD_DEBUG_CORE) {
        debug_open_core();
        return;
    }
    if (cmd == CMD_DEBUG_ATTACH) {
        show_attach();
        return;
    }
    if (dbg_ && dbg_->is_core() && cmd != CMD_DEBUG_RESTART) {
        debug_status("A core file does not run: Shift+F5 closes it, Ctrl+Shift+F5 runs the "
                     "program.");
        return;
    }
    if (cmd == CMD_DEBUG_RESTART) {
        debug_end();
        cmd = CMD_DEBUG_GO;
    }
    /* Disassembly keeps the focus through F5 too, as in VC6. */
    insn_step_ = dis_list_ && dis_list_->has_focus();
    bool fresh = false;
    if (!dbg_) {
        if (!debug_start())
            return;
        fresh = true;
    }
    td::Stop s;
    memset(&s, 0, sizeof(s));
    s.kind = td::EV_ERROR;
    ttr::Str err;
    switch (cmd) {
    case CMD_DEBUG_GO:
        debug_status("Running");
        s = dbg_->cont();
        break;
    case CMD_DEBUG_STEP_INTO:
    case CMD_DEBUG_STEP_OVER:
        if (fresh) {
            /* Stepping at the start: to main's first line, as VC6. */
            int id = dbg_->add_breakpoint("main", &err);
            s = dbg_->cont();
            if (id)
                dbg_->remove_breakpoint(id);
        } else {
            s = dbg_->step_line(cmd == CMD_DEBUG_STEP_OVER);
        }
        break;
    case CMD_DEBUG_STEP_OUT:
        s = fresh ? dbg_->cont() : dbg_->finish();
        break;
    case CMD_DEBUG_STEP_INSN:
    case CMD_DEBUG_NEXT_INSN:
    case CMD_DEBUG_RUN_TO_INSN: {
        td::Addr a = cmd == CMD_DEBUG_RUN_TO_INSN ? disassembly_selected() : 0;
        insn_step_ = true;
        if (fresh) {
            /* As F10 at the start: to main. */
            int id = dbg_->add_breakpoint("main", &err);
            s = dbg_->cont();
            if (id)
                dbg_->remove_breakpoint(id);
        } else if (cmd == CMD_DEBUG_STEP_INSN) {
            s = dbg_->step_instruction();
        } else if (cmd == CMD_DEBUG_NEXT_INSN) {
            s = dbg_->next_instruction();
        } else {
            char spec[40], h[24];
            snprintf(spec, sizeof(spec), "*%s", td::hex(a, h));
            int id = a ? dbg_->add_breakpoint(spec, &err) : 0;
            s = dbg_->cont();
            if (id)
                dbg_->remove_breakpoint(id);
        }
        break;
    }
    case CMD_DEBUG_RUN_TO_CURSOR: {
        Doc* d = doc();
        if (!d || d->path.empty()) {
            s = dbg_->cont();
            break;
        }
        char spec[4200];
        snprintf(spec, sizeof(spec), "%s:%ld", real_path(d->path.c_str()).c_str(),
                 d->view.cursor().line + 1);
        int id = dbg_->add_breakpoint(spec, &err);
        s = dbg_->cont();
        if (id)
            dbg_->remove_breakpoint(id);
        break;
    }
    default:
        return;
    }
    debug_stopped(s);
}

/* ---- breakpoints and the margin ---- */

void App::toggle_breakpoint()
{
    Doc* d = doc();
    if (!d || d->path.empty()) {
        debug_status("Save the file first: breakpoints are set by file and line.");
        return;
    }
    ttr::Str path = real_path(d->path.c_str());
    long line = d->view.cursor().line;
    int i;
    for (i = 0; i < bps_.size(); i++)
        if (bps_[i].line == line && !strcmp(bps_[i].path.c_str(), path.c_str())) {
            bps_.remove_at(i);
            /* td's breakpoints for it go at the next stop: they are
             * found by place there. */
            if (dbg_ && !dbg_->busy()) {
                char spec[4200];
                snprintf(spec, sizeof(spec), "%s:%ld", path.c_str(), line + 1);
                const ttr::Vector<td::Breakpoint>& b = dbg_->breakpoints();
                int k;
                for (k = 0; k < b.size(); k++)
                    if (b[k].id > 0 && !strcmp(b[k].where.c_str(), spec)) {
                        dbg_->remove_breakpoint(b[k].id);
                        break;
                    }
            }
            TwGridDamage(grid_);
            save_settings();
            return;
        }
    SourceBreakpoint b;
    b.path = path;
    b.line = line;
    bps_.push_back(b);
    if (dbg_ && !dbg_->busy()) {
        char spec[4200];
        snprintf(spec, sizeof(spec), "%s:%ld", path.c_str(), line + 1);
        ttr::Str err;
        if (!dbg_->add_breakpoint(spec, &err))
            debug_status(err.c_str());
    }
    TwGridDamage(grid_);
    save_settings();
}

/* F5, F9, F10, F11 and their Shift and Ctrl forms; Shift+F9 QuickWatch. */
bool App::debug_key(KeySym ks, unsigned state)
{
    bool ctrl = (state & ControlMask) != 0, shift = (state & ShiftMask) != 0;
    bool alt = (state & Mod1Mask) != 0;
    if (alt)
        return false;
    int dc = -1;
    if (ks == XK_F5)
        dc = ctrl && shift ? CMD_DEBUG_RESTART : shift ? CMD_DEBUG_STOP : ctrl ? -1 : CMD_DEBUG_GO;
    else if (ks == XK_F9 && !ctrl)
        dc = shift ? CMD_DEBUG_QUICKWATCH : CMD_DEBUG_TOGGLE_BP;
    else if (ks == XK_F10 && !shift)
        dc = ctrl ? CMD_DEBUG_RUN_TO_CURSOR : CMD_DEBUG_STEP_OVER;
    else if (ks == XK_F11 && !ctrl)
        dc = shift ? CMD_DEBUG_STEP_OUT : CMD_DEBUG_STEP_INTO;
    if (dc < 0)
        return false;
    command(dc);
    return true;
}

int App::line_marks(const te::View* v, long line) const
{
    const Doc* d = 0;
    int i;
    for (i = 0; i < docs_.size(); i++)
        if (&docs_[i]->view == v)
            d = docs_[i];
    if (!d || d->path.empty() || (bps_.empty() && stop_path_.empty()))
        return 0;
    ttr::Str path = real_path(d->path.c_str());
    int m = 0;
    for (i = 0; i < bps_.size(); i++)
        if (bps_[i].line == line && !strcmp(bps_[i].path.c_str(), path.c_str()))
            m |= MARK_BREAKPOINT;
    if (line == stop_line_ && !strcmp(stop_path_.c_str(), path.c_str()))
        m |= MARK_CURRENT;
    if (!frame_path_.empty() && line == frame_line_ && !strcmp(frame_path_.c_str(), path.c_str()))
        m |= MARK_FRAME;
    return m;
}

/* ---- the panes ---- */

void App::goto_source(const char* path, long line)
{
    if (!path || !*path)
        return;
    open_file(path);
    Doc* d = doc();
    if (!d || strcmp(real_path(d->path.c_str()).c_str(), real_path(path).c_str()) != 0)
        return;
    d->view.goto_line(line + 1);
    after_edit(true);
}

void App::debug_show_frame(int i, bool focus_editor)
{
    if (i < 0 || i >= frames_.size())
        return;
    cur_frame_ = i;
    const td::Frame& f = frames_[i];
    frame_path_ = "";
    if (i > 0 && f.has_place && f.place.file) {
        frame_path_ = real_path(f.place.file);
        frame_line_ = f.place.line - 1;
    }
    if (f.has_place && f.place.file)
        goto_source(f.place.file, f.place.line - 1);
    debug_fill();
    TwGridDamage(grid_);
    if (focus_editor)
        XmProcessTraversal(grid_, XmTRAVERSE_CURRENT);
}

void App::frame_chosen(int i)
{
    if (dbg_ && !dbg_->busy())
        debug_show_frame(i, false);
}

/* The current thread's stack, and the yellow arrow where it is. */
void App::load_frames()
{
    frames_.clear();
    dbg_->backtrace(&frames_, 64);
    frames_serial_++;
    cur_frame_ = 0;
    stop_path_ = "";
    if (frames_.size() > 0 && frames_[0].has_place && frames_[0].place.file) {
        stop_path_ = real_path(frames_[0].place.file);
        stop_line_ = frames_[0].place.line - 1;
    }
}

/* The innermost frame with source (0 if none has). */
int App::source_frame() const
{
    int i;
    for (i = 0; i < frames_.size(); i++)
        if (frames_[i].has_place && frames_[i].place.file)
            return i;
    return 0;
}

/* Every thread and where it is, the current one staying current. */
void App::load_threads()
{
    thread_ids_.clear();
    thread_where_.clear();
    dbg_->threads(&thread_ids_);
    int cur = dbg_->current_thread(), i;
    for (i = 0; i < thread_ids_.size(); i++) {
        td::Regs r;
        td::Frame f;
        ttr::Str w("??");
        if (dbg_->select_thread(thread_ids_[i]) && dbg_->regs(&r) &&
            dbg_->describe(r.pc, false, &f))
            w = td::format_place(f);
        thread_where_.push_back(w);
    }
    dbg_->select_thread(cur);
}

/* Threads pane: double-click switches to that thread (VC6's Switch To):
 * its stack, its values, the arrow at its line. */
void App::thread_chosen(int row)
{
    if (!dbg_ || dbg_->busy() || row < 0 || row >= thread_ids_.size())
        return;
    if (!dbg_->select_thread(thread_ids_[row]))
        return;
    load_frames();
    /* Its registers are another thread's: nothing to show as changed. */
    have_last_regs_ = false;
    /* A thread waiting in the system's code: its first frame with source
     * is what there is to look at. */
    int i = source_frame();
    debug_show_frame(i, false);
    char msg[600];
    ttr::Str where = frames_.size() ? td::format_place(frames_[i]) : ttr::Str("??");
    snprintf(msg, sizeof(msg), "Thread %d: %s", dbg_->thread_number(thread_ids_[row]),
             where.c_str());
    debug_status(msg);
}

/* ---- Debug > Settings ---- */

void App::show_debug_settings()
{
    if (!dbg_dlg_) {
        static SettingsHook hooks[2];
        Arg a[1];
        XtSetArg(a[0], XmNautoUnmanage, False);
        dbg_dlg_ = XmCreateFormDialog(top_, (char*)"debugSettings", a, 1);
        XtVaSetValues(XtParent(dbg_dlg_), XmNtitle, "Debug Settings", (char*)0);
        XtVaSetValues(dbg_dlg_, XmNmarginWidth, scaled(10), XmNmarginHeight, scaled(10), (char*)0);
        Widget rc = XtVaCreateManagedWidget("rows", xmRowColumnWidgetClass, dbg_dlg_,
                                            XmNnumColumns, 3, XmNpacking, XmPACK_COLUMN,
                                            XmNorientation, XmHORIZONTAL,
                                            XmNtopAttachment, XmATTACH_FORM,
                                            XmNleftAttachment, XmATTACH_FORM,
                                            XmNrightAttachment, XmATTACH_FORM, (char*)0);
        static const char* const labels[3] = { "Program:", "Arguments:", "Directory:" };
        Widget* fields[3] = { &dbg_prog_, &dbg_args_w_, &dbg_dir_w_ };
        int i;
        for (i = 0; i < 3; i++) {
            XtVaCreateManagedWidget(labels[i], xmLabelWidgetClass, rc, (char*)0);
            *fields[i] = XtVaCreateManagedWidget("field", xmTextFieldWidgetClass, rc,
                                                 XmNcolumns, 50, (char*)0);
        }
        Widget note = XtVaCreateManagedWidget(
            "The program runs with its input and output in the terminal pane; an empty "
            "directory is the workspace.",
            xmLabelWidgetClass, dbg_dlg_, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, rc,
            XmNtopOffset, scaled(6), XmNleftAttachment, XmATTACH_FORM, (char*)0);
        Widget buttons = XtVaCreateManagedWidget("buttons", xmRowColumnWidgetClass, dbg_dlg_,
                                                 XmNorientation, XmHORIZONTAL,
                                                 XmNspacing, scaled(6),
                                                 XmNtopAttachment, XmATTACH_WIDGET,
                                                 XmNtopWidget, note, XmNtopOffset, scaled(10),
                                                 XmNrightAttachment, XmATTACH_FORM,
                                                 XmNbottomAttachment, XmATTACH_FORM, (char*)0);
        static const char* const names[2] = { "OK", "Cancel" };
        Widget b[2];
        for (i = 0; i < 2; i++) {
            b[i] = XtVaCreateManagedWidget(names[i], xmPushButtonWidgetClass, buttons, (char*)0);
            hooks[i].app = this;
            hooks[i].which = i;
            XtAddCallback(b[i], XmNactivateCallback, settings_cb, (XtPointer)&hooks[i]);
        }
        XtVaSetValues(dbg_dlg_, XmNdefaultButton, b[0], XmNcancelButton, b[1], (char*)0);
    }
    XmTextFieldSetString(dbg_prog_, (char*)dbg_program_.c_str());
    XmTextFieldSetString(dbg_args_w_, (char*)dbg_args_.c_str());
    XmTextFieldSetString(dbg_dir_w_, (char*)dbg_dir_.c_str());
    XtManageChild(dbg_dlg_);
}

void App::debug_settings_button(int which)
{
    if (which == 0) {
        char* s = XmTextFieldGetString(dbg_prog_);
        dbg_program_ = s ? s : "";
        XtFree(s);
        s = XmTextFieldGetString(dbg_args_w_);
        dbg_args_ = s ? s : "";
        XtFree(s);
        s = XmTextFieldGetString(dbg_dir_w_);
        dbg_dir_ = s ? s : "";
        XtFree(s);
        save_settings();
    }
    XtUnmanageChild(dbg_dlg_);
}

} /* namespace ttrcode */
