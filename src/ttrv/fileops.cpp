/*
 * ttrv's file operations' dialogs (fileops.h).
 */
#include "fileops.h"

#include "filelist.h"

#include "../../tx/dialog.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/MessageB.h>
#include <Xm/PushB.h>
#include <Xm/Scale.h>
#include <Xm/SelectioB.h>
#include <Xm/Separator.h>
#include <Xm/TextF.h>

namespace ttrv {

namespace {

Ops* g_ops = 0;

void answer_cb(Widget, XtPointer closure, XtPointer)
{
    if (g_ops)
        g_ops->answered((int)(long)closure);
}

/* The stem selected once the prompt is up (before, the text field
 * drops a selection it does not own yet). */
void select_stem_cb(Widget dlg, XtPointer closure, XtPointer)
{
    Widget text = XtNameToWidget(dlg, "Text");
    XmTextPosition end = (XmTextPosition)(long)closure;
    XmTextFieldSetSelection(text, 0, end, XtLastTimestampProcessed(XtDisplay(dlg)));
    XmTextFieldSetInsertionPosition(text, end);
}

double now()
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

void set_text(Widget w, const char* text)
{
    XmString s = XmStringGenerate((XtPointer)text, (char*)XmFONTLIST_DEFAULT_TAG, XmCHARSET_TEXT, 0);
    XtVaSetValues(w, XmNlabelString, s, (char*)0);
    XmStringFree(s);
}

void set_title(Widget dialog, const char* title)
{
    XtVaSetValues(XtParent(dialog), XmNtitle, title, (char*)0);
}

void time_text(long t, char* out, size_t n)
{
    time_t tt = (time_t)t;
    struct tm* tm = localtime(&tt);
    if (!tm || !strftime(out, n, "%Y-%m-%d %H:%M", tm))
        snprintf(out, n, "?");
}

/* A long path shortened in its middle, so the dialog keeps its size. */
void middle(const char* s, char* out, size_t n, size_t keep)
{
    size_t l = strlen(s);
    if (l <= keep || keep < 10)
        snprintf(out, n, "%s", s);
    else
        snprintf(out, n, "%.*s...%s", (int)(keep / 2 - 2), s, s + l - (keep / 2 - 1));
}

} /* namespace */

Ops::Ops(Widget top)
    : top_(top), answer_(-1), p_what_(0), p_from_(0), p_to_(0), p_bar_(0), p_count_(0)
{
    g_ops = this;
}

int Ops::wait(Widget dlg)
{
    answer_ = -1;
    XtVaSetValues(dlg, XmNdialogStyle, XmDIALOG_FULL_APPLICATION_MODAL, (char*)0);
    tx::fixed_size(dlg);
    XtManageChild(dlg);
    XtAppContext ctx = XtWidgetToApplicationContext(top_);
    while (answer_ < 0 && XtIsManaged(dlg))
        XtAppProcessEvent(ctx, XtIMAll);
    return answer_;
}

bool Ops::ask_name(const char* title, const char* label, ttr::Str* inout)
{
    Widget dlg = XmCreatePromptDialog(top_, (char*)"name", 0, 0);
    set_title(dlg, title);
    XmString l = XmStringCreateLocalized((char*)label);
    XmString t = XmStringCreateLocalized((char*)inout->c_str());
    XtVaSetValues(dlg, XmNselectionLabelString, l, XmNtextString, t, (char*)0);
    XmStringFree(l);
    XmStringFree(t);
    XtUnmanageChild(XtNameToWidget(dlg, "Help"));
    XtAddCallback(dlg, XmNokCallback, answer_cb, (XtPointer)0);
    XtAddCallback(dlg, XmNcancelCallback, answer_cb, (XtPointer)1);
    Widget text = XtNameToWidget(dlg, "Text");
    /* The name without its extension selected: typing replaces it. */
    const char* s = inout->c_str();
    const char* dot = strrchr(s, '.');
    XmTextPosition end = dot && dot != s ? (XmTextPosition)(dot - s) : (XmTextPosition)strlen(s);
    XtVaSetValues(text, XmNcolumns, 40, (char*)0);
    XtAddCallback(dlg, XmNmapCallback, select_stem_cb, (XtPointer)(long)end);
    bool ok = wait(dlg) == 0;
    if (ok) {
        char* v = XmTextFieldGetString(text);
        *inout = v ? v : "";
        XtFree(v);
    }
    XtDestroyWidget(XtParent(dlg));
    return ok;
}

int Ops::choose(const char* title, const char* message, const char* const* buttons)
{
    int n = 0;
    while (buttons[n])
        n++;
    Widget dlg = XmCreateQuestionDialog(top_, (char*)"question", 0, 0);
    set_title(dlg, title);
    XmString m = XmStringGenerate((XtPointer)message, (char*)XmFONTLIST_DEFAULT_TAG,
                                  XmCHARSET_TEXT, 0);
    XmString first = XmStringCreateLocalized((char*)buttons[0]);
    XtVaSetValues(dlg, XmNmessageString, m, XmNokLabelString, first, (char*)0);
    XmStringFree(m);
    XmStringFree(first);
    XtAddCallback(dlg, XmNokCallback, answer_cb, (XtPointer)0);
    XtUnmanageChild(XtNameToWidget(dlg, "Help"));
    /* The ones between the first and the last: buttons of the message
     * box's own, which it lays out after OK. */
    for (int i = 1; i < n - 1; i++) {
        XmString l = XmStringCreateLocalized((char*)buttons[i]);
        Widget b = XtVaCreateManagedWidget("button", xmPushButtonWidgetClass, dlg,
                                           XmNlabelString, l, (char*)0);
        XmStringFree(l);
        XtAddCallback(b, XmNactivateCallback, answer_cb, (XtPointer)(long)i);
    }
    if (n > 1) {
        XmString last = XmStringCreateLocalized((char*)buttons[n - 1]);
        XtVaSetValues(dlg, XmNcancelLabelString, last, (char*)0);
        XmStringFree(last);
        XtAddCallback(dlg, XmNcancelCallback, answer_cb, (XtPointer)(long)(n - 1));
    } else {
        XtUnmanageChild(XtNameToWidget(dlg, "Cancel"));
    }
    int a = wait(dlg);
    XtDestroyWidget(XtParent(dlg));
    return a;
}

bool Ops::confirm(const char* title, const char* message, const char* yes)
{
    const char* b[3] = { yes, "Cancel", 0 };
    return choose(title, message, b) == 0;
}

void Ops::error(const char* title, const char* message)
{
    Widget dlg = XmCreateErrorDialog(top_, (char*)"error", 0, 0);
    set_title(dlg, title);
    XmString m = XmStringGenerate((XtPointer)message, (char*)XmFONTLIST_DEFAULT_TAG,
                                  XmCHARSET_TEXT, 0);
    XtVaSetValues(dlg, XmNmessageString, m, (char*)0);
    XmStringFree(m);
    XtUnmanageChild(XtNameToWidget(dlg, "Cancel"));
    XtUnmanageChild(XtNameToWidget(dlg, "Help"));
    XtAddCallback(dlg, XmNokCallback, answer_cb, (XtPointer)0);
    wait(dlg);
    XtDestroyWidget(XtParent(dlg));
}

/* The progress dialog: what, from, to, a bar, the counts, Cancel. */
void Ops::progress(Widget* dp, tf::Job& job, const char* title)
{
    if (!*dp) {
        Arg a[2];
        XtSetArg(a[0], XmNautoUnmanage, False);
        XtSetArg(a[1], XmNdialogStyle, XmDIALOG_FULL_APPLICATION_MODAL);
        Widget d = XmCreateFormDialog(top_, (char*)"progress", a, 2);
        set_title(d, title);
        Widget prev = 0;
        Widget* labels[3] = { &p_what_, &p_from_, &p_to_ };
        for (int i = 0; i < 3; i++) {
            *labels[i] = XtVaCreateManagedWidget(
                "label", xmLabelWidgetClass, d, XmNalignment, XmALIGNMENT_BEGINNING,
                XmNrecomputeSize, False, XmNwidth, 460, XmNleftAttachment, XmATTACH_FORM,
                XmNleftOffset, 12, XmNrightAttachment, XmATTACH_FORM, XmNrightOffset, 12,
                XmNtopAttachment, prev ? XmATTACH_WIDGET : XmATTACH_FORM, XmNtopWidget, prev,
                XmNtopOffset, prev ? 2 : 12, (char*)0);
            set_text(*labels[i], " ");
            prev = *labels[i];
        }
        p_bar_ = XtVaCreateManagedWidget(
            "bar", xmScaleWidgetClass, d, XmNorientation, XmHORIZONTAL, XmNminimum, 0,
            XmNmaximum, 1000, XmNvalue, 0, XmNshowValue, False, XmNeditable, False,
            XmNslidingMode, XmTHERMOMETER, XmNsliderVisual, XmSHADOWED_BACKGROUND,
            XmNtraversalOn, False, XmNleftAttachment, XmATTACH_FORM, XmNleftOffset, 12,
            XmNrightAttachment, XmATTACH_FORM, XmNrightOffset, 12, XmNtopAttachment,
            XmATTACH_WIDGET, XmNtopWidget, prev, XmNtopOffset, 10, (char*)0);
        p_count_ = XtVaCreateManagedWidget(
            "count", xmLabelWidgetClass, d, XmNalignment, XmALIGNMENT_BEGINNING,
            XmNrecomputeSize, False, XmNleftAttachment, XmATTACH_FORM, XmNleftOffset, 12,
            XmNrightAttachment, XmATTACH_FORM, XmNrightOffset, 12, XmNtopAttachment,
            XmATTACH_WIDGET, XmNtopWidget, p_bar_, XmNtopOffset, 4, (char*)0);
        set_text(p_count_, " ");
        Widget sep = XtVaCreateManagedWidget(
            "sep", xmSeparatorWidgetClass, d, XmNleftAttachment, XmATTACH_FORM,
            XmNrightAttachment, XmATTACH_FORM, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget,
            p_count_, XmNtopOffset, 10, (char*)0);
        XmString l = XmStringCreateLocalized((char*)"Cancel");
        Widget cancel = XtVaCreateManagedWidget(
            "cancel", xmPushButtonWidgetClass, d, XmNlabelString, l, XmNtopAttachment,
            XmATTACH_WIDGET, XmNtopWidget, sep, XmNtopOffset, 8, XmNbottomAttachment,
            XmATTACH_FORM, XmNbottomOffset, 8, XmNrightAttachment, XmATTACH_FORM,
            XmNrightOffset, 12, XmNwidth, 100, (char*)0);
        XmStringFree(l);
        XtAddCallback(cancel, XmNactivateCallback, answer_cb, (XtPointer)1);
        XtVaSetValues(d, XmNcancelButton, cancel, XmNdefaultButton, cancel, (char*)0);
        tx::fixed_size(d);
        XtManageChild(d);
        *dp = d;
    }
    char b[1300];
    if (job.counting()) {
        snprintf(b, sizeof(b), "Counting: %d files", job.files_total());
        set_text(p_what_, b);
        return;
    }
    set_text(p_what_, title);
    char m[1100];
    middle(job.source() ? job.source() : "", m, sizeof(m), 64);
    snprintf(b, sizeof(b), "From: %s", m);
    set_text(p_from_, b);
    const char* to = job.target() ? job.target() : "";
    middle(to, m, sizeof(m), 64);
    snprintf(b, sizeof(b), "%s%s", *to ? "To: " : "", m);
    set_text(p_to_, *to ? b : " ");
    char done[32], total[32];
    format_size(job.bytes_done(), done, sizeof(done));
    format_size(job.bytes_total(), total, sizeof(total));
    snprintf(b, sizeof(b), "%d of %d files, %s of %s", job.files_done(), job.files_total(), done,
             total);
    set_text(p_count_, b);
    double f = job.bytes_total() ? (double)job.bytes_done() / (double)job.bytes_total()
               : job.files_total() ? (double)job.files_done() / job.files_total()
                                   : 0;
    XtVaSetValues(p_bar_, XmNvalue, (int)(f * 1000 + 0.5) > 1000 ? 1000 : (int)(f * 1000 + 0.5),
                  (char*)0);
}

bool Ops::run(tf::Job& job, const char* title)
{
    XtAppContext ctx = XtWidgetToApplicationContext(top_);
    Widget d = 0;
    double start = now(), drawn = 0;
    answer_ = -1;
    for (;;) {
        tf::Job::State st = job.step(50);
        if (st == tf::Job::DONE || st == tf::Job::ABORTED)
            break;
        if (st == tf::Job::ASK_OVERWRITE) {
            char q[3000], ss[32], ts[32], st1[32], tt[32], name[1100];
            format_size(job.source_stat().size, ss, sizeof(ss));
            format_size(job.target_stat().size, ts, sizeof(ts));
            time_text(job.source_stat().mtime, st1, sizeof(st1));
            time_text(job.target_stat().mtime, tt, sizeof(tt));
            const char* t = job.target() ? job.target() : "";
            const char* slash = strrchr(t, '/');
            char dir[1100];
            snprintf(dir, sizeof(dir), "%.*s", slash ? (int)(slash - t) : 0, t);
            middle(dir[0] ? dir : "/", name, sizeof(name), 70);
            snprintf(q, sizeof(q),
                     "%.300s is already in\n%s.\n\nThe new one: %s, %s\nThe one there: %s, %s\n\n"
                     "Replace it?",
                     slash ? slash + 1 : t, name, ss, st1, ts, tt);
            static const char* const b[] = { "Replace", "Replace All", "Skip", "Skip All",
                                             "Cancel", 0 };
            static const tf::Answer a[] = { tf::ANS_YES, tf::ANS_ALL, tf::ANS_NO, tf::ANS_NONE,
                                            tf::ANS_ABORT };
            int r = choose("File Exists", q, b);
            job.answer(r >= 0 ? a[r] : tf::ANS_ABORT);
            answer_ = -1;
            continue;
        }
        if (st == tf::Job::ASK_ERROR) {
            char q[3000], name[1100];
            const char* what = job.what() ? job.what() : "";
            const char* p = job.target() && *job.target() && strcmp(what, "read") != 0 &&
                                    strcmp(what, "open") != 0 && strncmp(what, "delete", 6) != 0
                                ? job.target()
                                : job.source();
            middle(p ? p : "", name, sizeof(name), 70);
            snprintf(q, sizeof(q), "Cannot %s\n%s:\n\n%s", what, name, strerror(job.error()));
            static const char* const b[] = { "Retry", "Skip", "Skip All", "Cancel", 0 };
            static const tf::Answer a[] = { tf::ANS_RETRY, tf::ANS_SKIP, tf::ANS_SKIP_ALL,
                                            tf::ANS_ABORT };
            int r = choose("Error", q, b);
            job.answer(r >= 0 ? a[r] : tf::ANS_ABORT);
            answer_ = -1;
            continue;
        }
        /* Quick ones never show a dialog; the rest show one that keeps
         * up with the job ten times a second. */
        double t = now();
        if (!d && t - start < 0.4)
            continue;
        if (!d || t - drawn > 0.1) {
            progress(&d, job, title);
            XmUpdateDisplay(d);
            drawn = t;
        }
        while (XtAppPending(ctx))
            XtAppProcessEvent(ctx, XtIMAll);
        if (answer_ == 1) {
            answer_ = -1;
            if (confirm(title, "Stop now? What is done so far stays done.", "Stop"))
                job.answer(tf::ANS_ABORT);
            answer_ = -1;
        }
    }
    if (d)
        XtDestroyWidget(XtParent(d));
    p_what_ = p_from_ = p_to_ = p_bar_ = p_count_ = 0;
    if (job.failures() > 0) {
        char b[100];
        snprintf(b, sizeof(b), "%d file%s skipped.", job.failures(), job.failures() == 1 ? "" : "s");
        error(title, b);
    }
    return job.state() == tf::Job::DONE;
}

} /* namespace ttrv */
