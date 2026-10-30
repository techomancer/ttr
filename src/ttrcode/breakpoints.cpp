/*
 * ttrcode's Breakpoints dialog (Debug > Breakpoints, td/DESIGN.md
 * section 8.2): Visual C++ 6's Edit > Breakpoints on one page. Every
 * breakpoint in a list; "Break at" adds one on a line or a function, and
 * "Break when this expression changes" a data breakpoint (td's
 * watchpoints). Data breakpoints are kept with the breakpoints and set
 * when debugging starts; one on a local is set while its frame is there
 * and ends when the frame returns.
 */
#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/TextF.h>

namespace ttrcode {

namespace {

enum { BP_ADD_AT, BP_ADD_DATA, BP_REMOVE, BP_REMOVE_ALL, BP_GOTO, BP_CLOSE, BP_BUTTONS };

struct BpHook {
    App* app;
    int which;
};
BpHook g_bp_hooks[BP_BUTTONS];

void bp_button_cb(Widget, XtPointer data, XtPointer)
{
    BpHook* h = (BpHook*)data;
    h->app->breakpoints_button(h->which);
}

void bp_at_cb(Widget, XtPointer data, XtPointer)
{
    ((App*)data)->breakpoints_entered(false);
}

void bp_expr_cb(Widget, XtPointer data, XtPointer)
{
    ((App*)data)->breakpoints_entered(true);
}

void bp_list_cb(Widget, XtPointer data, XtPointer)
{
    ((App*)data)->breakpoints_button(BP_GOTO);
}

const char* base_name(const char* p)
{
    const char* s = strrchr(p, '/');
    return s ? s + 1 : p;
}

/* The selected item (from 0), -1 if none. */
int selected_item(Widget list)
{
    int* pos = 0;
    int n = 0;
    XtVaGetValues(list, XmNselectedPositions, &pos, XmNselectedPositionCount, &n, (char*)0);
    return n > 0 && pos ? pos[0] - 1 : -1;
}

ttr::Str field_text(Widget w)
{
    char* s = XmTextFieldGetString(w);
    ttr::Str t(s ? s : "");
    XtFree(s);
    return t;
}

/* "file.c:12" (line from 1), or 0 for a function's name. */
bool parse_location(const char* spec, ttr::Str* file, long* line)
{
    const char* colon = strrchr(spec, ':');
    if (!colon || colon == spec || !colon[1])
        return false;
    const char* p;
    for (p = colon + 1; *p; p++)
        if (*p < '0' || *p > '9')
            return false;
    char buf[4096];
    size_t n = (size_t)(colon - spec);
    if (n >= sizeof(buf))
        n = sizeof(buf) - 1;
    memcpy(buf, spec, n);
    buf[n] = 0;
    *file = buf;
    *line = atol(colon + 1) - 1;
    return *line >= 0;
}

} /* namespace */

ttr::Str App::breakpoint_spec(const SourceBreakpoint& b) const
{
    if (!b.func.empty())
        return b.func;
    char spec[4200];
    snprintf(spec, sizeof(spec), "%s:%ld", b.path.c_str(), b.line + 1);
    return ttr::Str(spec);
}

void App::show_breakpoints()
{
    if (!bp_dlg_) {
        Arg a[1];
        XtSetArg(a[0], XmNautoUnmanage, False);
        bp_dlg_ = XmCreateFormDialog(top_, (char*)"breakpoints", a, 1);
        XtVaSetValues(XtParent(bp_dlg_), XmNtitle, "Breakpoints", (char*)0);
        XtVaSetValues(bp_dlg_, XmNmarginWidth, 10, XmNmarginHeight, 10, (char*)0);
        int i;
        for (i = 0; i < BP_BUTTONS; i++) {
            g_bp_hooks[i].app = this;
            g_bp_hooks[i].which = i;
        }
        /* Break at: [field] [Add] */
        Widget l1 = XtVaCreateManagedWidget("Break at (file:line or function):", xmLabelWidgetClass,
                                            bp_dlg_, XmNtopAttachment, XmATTACH_FORM,
                                            XmNleftAttachment, XmATTACH_FORM, (char*)0);
        Widget add_at = XtVaCreateManagedWidget("Add", xmPushButtonWidgetClass, bp_dlg_,
                                                XmNtopAttachment, XmATTACH_WIDGET,
                                                XmNtopWidget, l1,
                                                XmNrightAttachment, XmATTACH_FORM, (char*)0);
        XtAddCallback(add_at, XmNactivateCallback, bp_button_cb, (XtPointer)&g_bp_hooks[BP_ADD_AT]);
        bp_at_w_ = XtVaCreateManagedWidget("at", xmTextFieldWidgetClass, bp_dlg_, XmNcolumns, 50,
                                           XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, l1,
                                           XmNleftAttachment, XmATTACH_FORM,
                                           XmNrightAttachment, XmATTACH_WIDGET,
                                           XmNrightWidget, add_at, XmNrightOffset, 8, (char*)0);
        XtAddCallback(bp_at_w_, XmNactivateCallback, bp_at_cb, (XtPointer)this);
        /* Break when this expression changes: [field] [Add] */
        Widget l2 = XtVaCreateManagedWidget("Break when this expression changes:",
                                            xmLabelWidgetClass, bp_dlg_,
                                            XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget,
                                            bp_at_w_, XmNtopOffset, 8,
                                            XmNleftAttachment, XmATTACH_FORM, (char*)0);
        Widget add_data = XtVaCreateManagedWidget("Add", xmPushButtonWidgetClass, bp_dlg_,
                                                  XmNtopAttachment, XmATTACH_WIDGET,
                                                  XmNtopWidget, l2,
                                                  XmNrightAttachment, XmATTACH_FORM, (char*)0);
        XtAddCallback(add_data, XmNactivateCallback, bp_button_cb,
                      (XtPointer)&g_bp_hooks[BP_ADD_DATA]);
        bp_expr_w_ = XtVaCreateManagedWidget("expression", xmTextFieldWidgetClass, bp_dlg_,
                                             XmNcolumns, 50, XmNtopAttachment, XmATTACH_WIDGET,
                                             XmNtopWidget, l2, XmNleftAttachment, XmATTACH_FORM,
                                             XmNrightAttachment, XmATTACH_WIDGET,
                                             XmNrightWidget, add_data, XmNrightOffset, 8,
                                             (char*)0);
        XtAddCallback(bp_expr_w_, XmNactivateCallback, bp_expr_cb, (XtPointer)this);
        /* The list, with Remove, Remove All and Go To Code beside it. */
        Widget l3 = XtVaCreateManagedWidget("Breakpoints:", xmLabelWidgetClass, bp_dlg_,
                                            XmNtopAttachment, XmATTACH_WIDGET,
                                            XmNtopWidget, bp_expr_w_, XmNtopOffset, 10,
                                            XmNleftAttachment, XmATTACH_FORM, (char*)0);
        Widget side = XtVaCreateManagedWidget("side", xmRowColumnWidgetClass, bp_dlg_,
                                              XmNorientation, XmVERTICAL, XmNspacing, 4,
                                              XmNpacking, XmPACK_COLUMN,
                                              XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, l3,
                                              XmNrightAttachment, XmATTACH_FORM, (char*)0);
        static const char* const side_names[3] = { "Remove", "Remove All", "Go To Code" };
        for (i = 0; i < 3; i++) {
            Widget b = XtVaCreateManagedWidget(side_names[i], xmPushButtonWidgetClass, side,
                                               (char*)0);
            XtAddCallback(b, XmNactivateCallback, bp_button_cb,
                          (XtPointer)&g_bp_hooks[BP_REMOVE + i]);
        }
        Widget close = XtVaCreateManagedWidget("Close", xmPushButtonWidgetClass, bp_dlg_,
                                               XmNrightAttachment, XmATTACH_FORM,
                                               XmNbottomAttachment, XmATTACH_FORM, (char*)0);
        XtAddCallback(close, XmNactivateCallback, bp_button_cb, (XtPointer)&g_bp_hooks[BP_CLOSE]);
        bp_note_w_ = XtVaCreateManagedWidget(" ", xmLabelWidgetClass, bp_dlg_,
                                             XmNalignment, XmALIGNMENT_BEGINNING,
                                             XmNleftAttachment, XmATTACH_FORM,
                                             XmNrightAttachment, XmATTACH_WIDGET,
                                             XmNrightWidget, close, XmNrightOffset, 8,
                                             XmNbottomAttachment, XmATTACH_FORM, (char*)0);
        Arg la[2];
        XtSetArg(la[0], XmNvisibleItemCount, 10);
        XtSetArg(la[1], XmNselectionPolicy, XmBROWSE_SELECT);
        bp_list_w_ = XmCreateScrolledList(bp_dlg_, (char*)"list", la, 2);
        XtVaSetValues(XtParent(bp_list_w_), XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, l3,
                      XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_WIDGET,
                      XmNrightWidget, side, XmNrightOffset, 8,
                      XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, close,
                      XmNbottomOffset, 8, (char*)0);
        XtAddCallback(bp_list_w_, XmNdefaultActionCallback, bp_list_cb, (XtPointer)this);
        XtManageChild(bp_list_w_);
        XtVaSetValues(bp_dlg_, XmNcancelButton, close, (char*)0);
    }
    fill_breakpoints();
    XtManageChild(bp_dlg_);
    XmProcessTraversal(bp_at_w_, XmTRAVERSE_CURRENT);
}

void App::fill_breakpoints()
{
    if (!bp_dlg_)
        return;
    int keep = selected_item(bp_list_w_) + 1;
    XmListDeleteAllItems(bp_list_w_);
    int i;
    for (i = 0; i < bps_.size() + data_bps_.size(); i++) {
        char text[4400];
        if (i < bps_.size()) {
            const SourceBreakpoint& b = bps_[i];
            if (!b.func.empty())
                snprintf(text, sizeof(text), "at function %s", b.func.c_str());
            else
                snprintf(text, sizeof(text), "at %s, line %ld", base_name(b.path.c_str()),
                         b.line + 1);
        } else {
            const DataBreakpoint& d = data_bps_[i - bps_.size()];
            snprintf(text, sizeof(text), "when '%s' changes%s%s%s", d.expr.c_str(),
                     d.id && d.local ? " (until its frame returns)" : "",
                     !d.id && !d.why.empty() ? " (not set: " : "",
                     !d.id && !d.why.empty() ? d.why.c_str() : "");
            if (!d.id && !d.why.empty())
                strncat(text, ")", sizeof(text) - strlen(text) - 1);
        }
        XmString s = XmStringCreateLocalized(text);
        XmListAddItemUnselected(bp_list_w_, s, 0);
        XmStringFree(s);
    }
    int n = bps_.size() + data_bps_.size();
    if (keep > n)
        keep = n;
    if (keep > 0)
        XmListSelectPos(bp_list_w_, keep, False);
}

/* "file.c:12" or a function. A relative file is looked for among the
 * open documents, then in the workspace. */
bool App::add_location_breakpoint(const char* spec)
{
    SourceBreakpoint b;
    ttr::Str file;
    long line = 0;
    if (parse_location(spec, &file, &line)) {
        ttr::Str path = file;
        if (file.c_str()[0] != '/') {
            int i;
            path = "";
            for (i = 0; i < docs_.size() && path.empty(); i++) {
                const char* p = docs_[i]->path.c_str();
                const char* tail = p + strlen(p) - strlen(file.c_str());
                if (tail >= p && !strcmp(tail, file.c_str()) && (tail == p || tail[-1] == '/'))
                    path = p;
            }
            if (path.empty()) {
                char buf[4200];
                snprintf(buf, sizeof(buf), "%s/%s", root_.c_str(), file.c_str());
                path = buf;
            }
        }
        char real[4096];
        b.path = realpath(path.c_str(), real) ? real : path.c_str();
        b.line = line;
    } else {
        b.func = spec;
        b.line = 0;
    }
    int i;
    for (i = 0; i < bps_.size(); i++)
        if (!strcmp(breakpoint_spec(bps_[i]).c_str(), breakpoint_spec(b).c_str()))
            return true;    /* there already */
    if (dbg_ && !dbg_->busy() && !dbg_->is_core()) {
        ttr::Str err;
        if (!dbg_->add_breakpoint(breakpoint_spec(b).c_str(), &err)) {
            XmString s = XmStringCreateLocalized((char*)err.c_str());
            XtVaSetValues(bp_note_w_, XmNlabelString, s, (char*)0);
            XmStringFree(s);
            return false;
        }
    }
    bps_.push_back(b);
    save_settings();
    TwGridDamage(grid_);
    return true;
}

/* Set now if the program is stopped (in the frame Call Stack shows), else
 * when debugging starts. */
bool App::add_data_breakpoint(const char* expr)
{
    DataBreakpoint d;
    d.expr = expr;
    d.id = 0;
    d.local = false;
    const td::Frame* f = frame();
    if (dbg_ && !dbg_->busy() && f) {
        ttr::Str err;
        d.id = dbg_->add_watch(*f, expr, &err);
        if (!d.id) {
            XmString s = XmStringCreateLocalized((char*)err.c_str());
            XtVaSetValues(bp_note_w_, XmNlabelString, s, (char*)0);
            XmStringFree(s);
            return false;
        }
        const ttr::Vector<td::Watchpoint>& w = dbg_->watchpoints();
        d.local = w.size() && w[w.size() - 1].scope_sp != 0;
    }
    data_bps_.push_back(d);
    save_settings();
    return true;
}

/* At the start: the data breakpoints td can set there (globals). */
void App::set_data_breakpoints()
{
    ttr::Vector<td::Frame> fr;
    bool have = dbg_ && dbg_->backtrace(&fr, 1) > 0;
    int i;
    for (i = 0; i < data_bps_.size(); i++) {
        DataBreakpoint& d = data_bps_[i];
        d.id = 0;
        d.local = false;
        d.why = "";
        if (!have)
            continue;
        ttr::Str err;
        d.id = dbg_->add_watch(fr[0], d.expr.c_str(), &err);
        if (!d.id)
            d.why = err;
        else
            d.local = dbg_->watchpoints()[dbg_->watchpoints().size() - 1].scope_sp != 0;
    }
    fill_breakpoints();
}

void App::remove_breakpoint_at(int item)
{
    if (item < 0)
        return;
    if (item < bps_.size()) {
        if (dbg_ && !dbg_->busy()) {
            ttr::Str spec = breakpoint_spec(bps_[item]);
            const ttr::Vector<td::Breakpoint>& b = dbg_->breakpoints();
            int k;
            for (k = 0; k < b.size(); k++)
                if (b[k].id > 0 && !strcmp(b[k].where.c_str(), spec.c_str())) {
                    dbg_->remove_breakpoint(b[k].id);
                    break;
                }
        }
        bps_.remove_at(item);
    } else if (item - bps_.size() < data_bps_.size()) {
        int i = item - bps_.size();
        if (dbg_ && !dbg_->busy() && data_bps_[i].id)
            dbg_->remove_breakpoint(data_bps_[i].id);
        data_bps_.remove_at(i);
    }
}

void App::breakpoints_entered(bool data)
{
    breakpoints_button(data ? BP_ADD_DATA : BP_ADD_AT);
}

void App::breakpoints_button(int which)
{
    if (dbg_ && dbg_->busy() && which != BP_CLOSE && which != BP_GOTO) {
        XmString s = XmStringCreateLocalized((char*)"The program is running: Debug > Break first.");
        XtVaSetValues(bp_note_w_, XmNlabelString, s, (char*)0);
        XmStringFree(s);
        return;
    }
    XmString blank = XmStringCreateLocalized((char*)" ");
    XtVaSetValues(bp_note_w_, XmNlabelString, blank, (char*)0);
    XmStringFree(blank);
    int sel = selected_item(bp_list_w_);
    switch (which) {
    case BP_ADD_AT:
    case BP_ADD_DATA: {
        Widget w = which == BP_ADD_AT ? bp_at_w_ : bp_expr_w_;
        ttr::Str t = field_text(w);
        const char* p = t.c_str();
        while (*p == ' ')
            p++;
        if (!*p)
            return;
        if (which == BP_ADD_AT ? add_location_breakpoint(p) : add_data_breakpoint(p)) {
            XmTextFieldSetString(w, (char*)"");
            fill_breakpoints();
            XmListSelectPos(bp_list_w_,
                            which == BP_ADD_AT ? bps_.size() : bps_.size() + data_bps_.size(),
                            False);
        }
        break;
    }
    case BP_REMOVE:
        remove_breakpoint_at(sel);
        save_settings();
        fill_breakpoints();
        TwGridDamage(grid_);
        break;
    case BP_REMOVE_ALL:
        while (bps_.size() + data_bps_.size() > 0)
            remove_breakpoint_at(0);
        save_settings();
        fill_breakpoints();
        TwGridDamage(grid_);
        break;
    case BP_GOTO:
        if (sel >= 0 && sel < bps_.size()) {
            const SourceBreakpoint& b = bps_[sel];
            if (b.func.empty()) {
                goto_source(b.path.c_str(), b.line);
            } else if (dbg_) {
                /* A function's place: where td put its breakpoint. */
                const ttr::Vector<td::Breakpoint>& tb = dbg_->breakpoints();
                int k;
                for (k = 0; k < tb.size(); k++)
                    if (tb[k].id > 0 && tb[k].addr && !strcmp(tb[k].where.c_str(), b.func.c_str())) {
                        td::Frame f;
                        dbg_->describe(tb[k].addr, false, &f);
                        if (f.has_place && f.place.file)
                            goto_source(f.place.file, f.place.line - 1);
                        break;
                    }
            }
        }
        break;
    case BP_CLOSE:
        XtUnmanageChild(bp_dlg_);
        break;
    }
}

} /* namespace ttrcode */
