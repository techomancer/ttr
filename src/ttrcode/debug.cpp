/*
 * ttrcode's debugger (td/DESIGN.md section 8.2): Visual C++ 6's keys and
 * windows on td::Session. F5 starts or continues, Shift+F5 stops,
 * Ctrl+Shift+F5 restarts, F9 sets or clears a breakpoint on the caret's
 * line, F10, F11 and Shift+F11 step over, into and out, Ctrl+F10 runs to
 * the caret. Breakpoints show as red dots in the margin and where the
 * program stopped as a yellow arrow; Call Stack, Registers and Threads
 * fill from td. While the program runs, td calls debug_idle, which goes
 * on handling the window's events (Debug > Break interrupts it).
 */
#include "app.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

void callstack_cb(Widget, XtPointer data, XtPointer call)
{
    ((App*)data)->frame_chosen(((XmListCallbackStruct*)call)->item_position - 1);
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

void set_list(Widget list, const ttr::Vector<ttr::Str>& items, int selected)
{
    if (!list)
        return;
    XmListDeleteAllItems(list);
    int i;
    for (i = 0; i < items.size(); i++) {
        XmString s = XmStringCreateLocalized((char*)items[i].c_str());
        XmListAddItem(list, s, 0);
        XmStringFree(s);
    }
    if (selected >= 0 && selected < items.size())
        XmListSelectPos(list, selected + 1, False);
}

} /* namespace */

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
    if (argv[0][0] != '/' && strchr(argv[0], '/')) {
        snprintf(prog, sizeof(prog), "%s/%s", root_.c_str(), argv[0]);
        argv[0] = prog;
    }
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
    for (i = 0; i < bps_.size(); i++) {
        char spec[4200];
        snprintf(spec, sizeof(spec), "%s:%ld", bps_[i].path.c_str(), bps_[i].line + 1);
        dbg_->add_breakpoint(spec, 0);
    }
    dbg_kill_req_ = false;
    have_last_regs_ = false;
    debug_status("Running");
    return true;
}

void App::debug_end()
{
    if (!dbg_)
        return;
    if (dbg_->running())
        dbg_->kill();
    delete dbg_;
    dbg_ = 0;
    stop_path_ = "";
    frame_path_ = "";
    frames_.clear();
    cur_frame_ = 0;
    have_last_regs_ = false;
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
    frames_.clear();
    dbg_->backtrace(&frames_, 64);
    cur_frame_ = 0;
    stop_path_ = "";
    if (frames_.size() > 0 && frames_[0].has_place && frames_[0].place.file) {
        stop_path_ = real_path(frames_[0].place.file);
        stop_line_ = frames_[0].place.line - 1;
    }
    debug_show_frame(0, true);
    ttr::Str where = frames_.size() ? td::format_place(frames_[0]) : ttr::Str("??");
    if (s.kind == td::EV_SIGNAL)
        snprintf(msg, sizeof(msg), "%s in %s", signal_name(s.sig), where.c_str());
    else if (s.kind == td::EV_BREAK && s.bp > 0)
        snprintf(msg, sizeof(msg), "Breakpoint: %s", where.c_str());
    else
        snprintf(msg, sizeof(msg), "Stopped: %s", where.c_str());
    debug_status(msg);
}

void App::debug_command(int cmd)
{
    if (cmd == CMD_DEBUG_TOGGLE_BP) {
        toggle_breakpoint();
        return;
    }
    if (cmd == CMD_DEBUG_SETTINGS) {
        show_debug_settings();
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
            debug_end();
            debug_status("Debugging stopped.");
        }
        return;
    }
    if (cmd == CMD_DEBUG_BREAK)
        return;
    if (cmd == CMD_DEBUG_RESTART) {
        debug_end();
        cmd = CMD_DEBUG_GO;
    }
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

/* Call Stack, Registers, Threads: from what td has now. */
void App::debug_fill()
{
    ttr::Vector<ttr::Str> items;
    int i;
    for (i = 0; i < frames_.size(); i++) {
        char h[24];
        ttr::Str w = td::format_place(frames_[i]);
        char line[1600];
        snprintf(line, sizeof(line), "%s  %s", w.c_str(), td::hex(frames_[i].pc, h));
        items.push_back(ttr::Str(line));
    }
    set_list(callstack_, items, cur_frame_);

    items.clear();
    td::Regs r;
    if (dbg_ && frames_.size() > 0) {
        r = frames_[cur_frame_].regs;
        const td::Arch* a = dbg_->target()->arch();
        for (i = 0; i < a->nregs; i++) {
            const char* n = a->reg_name(i);
            if (!n || !r.valid[i])
                continue;
            char h[24], line[80];
            /* Changed since the last stop: marked, as VC6 drew them red. */
            bool changed = cur_frame_ == 0 && have_last_regs_ && last_regs_.valid[i] &&
                           last_regs_.r[i] != r.r[i];
            snprintf(line, sizeof(line), "%-5s %s%s", n, td::hex(r.r[i], h), changed ? "  *" : "");
            items.push_back(ttr::Str(line));
        }
        char h[24], line[80];
        snprintf(line, sizeof(line), "%-5s %s", "pc", td::hex(frames_[cur_frame_].pc, h));
        items.push_back(ttr::Str(line));
    }
    set_list(registers_, items, -1);
    if (dbg_ && frames_.size() > 0 && cur_frame_ == 0) {
        last_regs_ = frames_[0].regs;
        have_last_regs_ = true;
    }

    items.clear();
    if (dbg_ && frames_.size() > 0) {
        char line[1600];
        ttr::Str w = td::format_place(frames_[0]);
        snprintf(line, sizeof(line), "1  %d  %s", dbg_->target()->pid(), w.c_str());
        items.push_back(ttr::Str(line));
    }
    set_list(threads_, items, 0);
}

Widget App::build_debug_pane(Pane* p, Widget parent)
{
    const char* id = p->id();
    Widget* slot = !strcmp(id, "callstack") ? &callstack_
                   : !strcmp(id, "registers") ? &registers_
                   : !strcmp(id, "threads")   ? &threads_
                                              : 0;
    if (slot) {
        if (*slot)
            XtDestroyWidget(XtParent(*slot));
        Arg args[4];
        Cardinal n = 0;
        XtSetArg(args[n], XmNselectionPolicy, XmBROWSE_SELECT); n++;
        XtSetArg(args[n], XmNscrollBarDisplayPolicy, XmAS_NEEDED); n++;
        XtSetArg(args[n], XmNlistSizePolicy, XmCONSTANT); n++;
        *slot = XmCreateScrolledList(parent, (char*)id, args, n);
        if (slot == &callstack_)
            XtAddCallback(*slot, XmNdefaultActionCallback, callstack_cb, (XtPointer)this);
        XtManageChild(*slot);
        debug_fill();
        return XtParent(*slot);
    }
    /* Variables, Watch, Memory: values come with td's next milestone. */
    Widget f = XtVaCreateManagedWidget(id, xmFormWidgetClass, parent, (char*)0);
    XmString l = XmStringCreateLocalized(
        (char*)"Values come with the debugger's types (td milestone 3).");
    XtVaCreateManagedWidget("note", xmLabelWidgetClass, f, XmNlabelString, l,
                            XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                            XmNtopOffset, 6, XmNleftOffset, 6, (char*)0);
    XmStringFree(l);
    return f;
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
        XtVaSetValues(dbg_dlg_, XmNmarginWidth, 10, XmNmarginHeight, 10, (char*)0);
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
            XmNtopOffset, 6, XmNleftAttachment, XmATTACH_FORM, (char*)0);
        Widget buttons = XtVaCreateManagedWidget("buttons", xmRowColumnWidgetClass, dbg_dlg_,
                                                 XmNorientation, XmHORIZONTAL, XmNspacing, 6,
                                                 XmNtopAttachment, XmATTACH_WIDGET,
                                                 XmNtopWidget, note, XmNtopOffset, 10,
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
