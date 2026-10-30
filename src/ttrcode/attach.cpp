/*
 * ttrcode's Attach to Process dialog (Debug > Attach to Process,
 * td/DESIGN.md section 8.2), as Visual C++ 6's Build > Start Debug >
 * Attach to Process: the user's processes, newest last; Attach (or a
 * double-click) stops the chosen one, all its threads, and shows where
 * it is. Stop Debugging lets it go on (it is detached, not killed).
 */
#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Xm/List.h>
#include <Xm/PushB.h>
#include <Xm/SelectioB.h>
#include <Xm/TextF.h>

namespace ttrcode {

namespace {

enum { ATTACH_OK, ATTACH_REFRESH, ATTACH_CANCEL };

struct AttachHook {
    App* app;
    int which;
};
AttachHook g_attach_hooks[3];

/* A selection box's part by its name ("*ItemsList", "Text", "Apply",
 * "Help"): XmSelectionBoxGetChild is deprecated in Motif 2.3. */
Widget part(Widget box, const char* name)
{
    return XtNameToWidget(box, name);
}

void attach_cb(Widget, XtPointer data, XtPointer)
{
    AttachHook* h = (AttachHook*)data;
    h->app->attach_button(h->which);
}

} /* namespace */

void App::show_attach()
{
    if (dbg_ && dbg_->busy())
        return;
    if (!attach_dlg_) {
        Arg a[2];
        XtSetArg(a[0], XmNautoUnmanage, False);
        XtSetArg(a[1], XmNmustMatch, False);
        attach_dlg_ = XmCreateSelectionDialog(top_, (char*)"attach", a, 2);
        XtVaSetValues(XtParent(attach_dlg_), XmNtitle, "Attach to Process", (char*)0);
        int i;
        for (i = 0; i < 3; i++) {
            g_attach_hooks[i].app = this;
            g_attach_hooks[i].which = i;
        }
        XmString s1 = XmStringCreateLocalized((char*)"Processes (name [ID] command), the newest last:");
        XmString s2 = XmStringCreateLocalized((char*)"Process ID:");
        XmString ok = XmStringCreateLocalized((char*)"Attach");
        XmString ap = XmStringCreateLocalized((char*)"Refresh");
        XtVaSetValues(attach_dlg_, XmNlistLabelString, s1, XmNselectionLabelString, s2,
                      XmNokLabelString, ok, XmNapplyLabelString, ap, XmNlistVisibleItemCount,
                      16, (char*)0);
        XmStringFree(s1);
        XmStringFree(s2);
        XmStringFree(ok);
        XmStringFree(ap);
        XtManageChild(part(attach_dlg_, "Apply"));
        XtUnmanageChild(part(attach_dlg_, "Help"));
        XtAddCallback(attach_dlg_, XmNokCallback, attach_cb, (XtPointer)&g_attach_hooks[ATTACH_OK]);
        XtAddCallback(attach_dlg_, XmNapplyCallback, attach_cb,
                      (XtPointer)&g_attach_hooks[ATTACH_REFRESH]);
        XtAddCallback(attach_dlg_, XmNcancelCallback, attach_cb,
                      (XtPointer)&g_attach_hooks[ATTACH_CANCEL]);
    }
    fill_attach();
    XtManageChild(attach_dlg_);
}

/* "  1234  name  command line", the newest last and shown. */
void App::fill_attach()
{
    attach_procs_.clear();
    td::Target::processes(&attach_procs_);
    Widget list = part(attach_dlg_, "*ItemsList");
    XmListDeleteAllItems(list);
    int i;
    for (i = 0; i < attach_procs_.size(); i++) {
        const td::ProcessInfo& p = attach_procs_[i];
        char line[400];
        snprintf(line, sizeof(line), "%s  [%d]  %.70s%s", p.name.c_str(), p.pid,
                 p.args.c_str(), strlen(p.args.c_str()) > 70 ? "..." : "");
        XmString s = XmStringCreateLocalized(line);
        XmListAddItemUnselected(list, s, 0);
        XmStringFree(s);
    }
    if (attach_procs_.size())
        XmListSetBottomPos(list, attach_procs_.size());
    XmTextFieldSetString(part(attach_dlg_, "Text"), (char*)"");
}

void App::attach_button(int which)
{
    if (which == ATTACH_REFRESH) {
        fill_attach();
        return;
    }
    if (which == ATTACH_CANCEL) {
        XtUnmanageChild(attach_dlg_);
        return;
    }
    /* The field holds the chosen line ("name [ID] ...") or an ID typed
     * in. */
    char* t = XmTextFieldGetString(part(attach_dlg_, "Text"));
    const char* b = t ? strchr(t, '[') : 0;
    int pid = b ? atoi(b + 1) : t ? atoi(t) : 0;
    XtFree(t);
    if (pid <= 0)
        return;
    ttr::Str name;
    int i;
    for (i = 0; i < attach_procs_.size(); i++)
        if (attach_procs_[i].pid == pid)
            name = attach_procs_[i].name;
    XtUnmanageChild(attach_dlg_);
    debug_attach(pid, name.c_str());
}

/* Stop a running process and debug it: the debugging layout, the
 * breakpoints set, and where it was. */
bool App::debug_attach(int pid, const char* name)
{
    if (dbg_ && dbg_->busy())
        return false;
    debug_end();
    edit_layout_ = docks_->layout();
    docks_->apply(debug_layout_.empty() ? default_debug_layout() : debug_layout_.c_str());
    dbg_ = new td::Session;
    dbg_->set_idle(debug_idle, this);
    ttr::Str err;
    if (!dbg_->attach(pid, &err)) {
        delete dbg_;
        dbg_ = 0;
        debug_layout_ = docks_->layout();
        docks_->apply(edit_layout_.c_str());
        char what[600];
        snprintf(what, sizeof(what), "%d: %.500s", pid, err.c_str());
        error("Cannot attach to %s", what, 0);
        return false;
    }
    dbg_attached_ = true;
    int i;
    for (i = 0; i < bps_.size(); i++)
        dbg_->add_breakpoint(breakpoint_spec(bps_[i]).c_str(), 0);
    set_data_breakpoints();
    dbg_kill_req_ = false;
    have_last_regs_ = false;
    regs_stop_ = -1;
    td::Stop s;
    memset(&s, 0, sizeof(s));
    s.kind = td::EV_PAUSED;
    s.thread = dbg_->current_thread();
    td::Regs r;
    if (dbg_->regs(&r))
        s.pc = r.pc;
    debug_stopped(s);
    char msg[600];
    snprintf(msg, sizeof(msg), "Attached to %s (%d): Shift+F5 lets it go on.",
             name && *name ? name : "the process", pid);
    debug_status(msg);
    return true;
}

} /* namespace ttrcode */
