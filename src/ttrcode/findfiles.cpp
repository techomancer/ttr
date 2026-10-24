/*
 * ttrcode's Find in Files and Replace in Files (Ctrl+Shift+F, Ctrl+Shift+H)
 * and the bottom pane's Find Results page, as in VS6: "path(line): text"
 * rows; Enter or a double click goes there, F4 and Shift+F4 to the next
 * and previous one.
 *
 * The workspace is searched in this process, file by file. Open documents
 * are searched as they are in the editor, unsaved changes included, and
 * replaced there (undoably). Other files are read, and written back
 * through te::save_file when something was replaced. Hidden files, build
 * directories, binary files (a NUL in the first 8 KB) and files over 16 MB
 * are skipped.
 */
#include "app.h"

#include "../../te/file.h"
#include "../../tw/grid_widget.h"
#include "../../tx/dialog.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/MessageB.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/TextF.h>
#include <Xm/ToggleB.h>

namespace ttrcode {

namespace {

enum { FIF_FIND, FIF_REPLACE, FIF_CLOSE };
const int k_max_results = 20000;
const long k_max_file = 16L * 1024 * 1024;

struct Hook {
    App* app;
    int which;
};

void fif_cb(Widget, XtPointer data, XtPointer)
{
    Hook* h = (Hook*)data;
    h->app->fif_button(h->which);
}

void results_cb(Widget, XtPointer data, XtPointer call)
{
    ((App*)data)->result_chosen(((XmListCallbackStruct*)call)->item_position - 1);
}

void page_cb(Widget w, XtPointer data, XtPointer)
{
    XtPointer page = 0;
    XtVaGetValues(w, XmNuserData, &page, (char*)0);
    ((App*)data)->bottom_page_chosen((int)(long)page);
}

int* g_answer;
void answer_cb(Widget, XtPointer data, XtPointer)
{
    *g_answer = (int)(long)data;
}

bool word_char(char c)
{
    unsigned char u = (unsigned char)c;
    return u == '_' || u >= 0x80 || isalnum(u);
}

bool same(char a, char b, bool match_case)
{
    if (match_case)
        return a == b;
    return tolower((unsigned char)a) == tolower((unsigned char)b);
}

/* What te's search finds: text at s[i], not inside a longer word when
 * whole. */
bool match_at(const char* s, size_t len, size_t i, const char* t, size_t tl, bool match_case,
              bool whole)
{
    if (i + tl > len)
        return false;
    for (size_t j = 0; j < tl; j++)
        if (!same(s[i + j], t[j], match_case))
            return false;
    if (whole && ((i > 0 && word_char(s[i - 1]) && word_char(t[0])) ||
                  (i + tl < len && word_char(s[i + tl]) && word_char(t[tl - 1]))))
        return false;
    return true;
}

/* A shell pattern with * and ?, against a file name. */
bool glob(const char* p, const char* s)
{
    while (*p) {
        if (*p == '*') {
            p++;
            for (const char* t = s;; t++) {
                if (glob(p, t))
                    return true;
                if (!*t)
                    return false;
            }
        }
        if (!*s || (*p != '?' && *p != *s))
            return false;
        p++;
        s++;
    }
    return !*s;
}

/* "*.c *.h", "*.cpp,*.h" or empty (every file). */
bool type_matches(const char* types, const char* name)
{
    if (!types || !*types)
        return true;
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", types);
    for (char* t = strtok(buf, " ,;"); t; t = strtok(0, " ,;"))
        if (glob(t, name))
            return true;
    return false;
}

bool build_dir(const char* name)
{
    return strncmp(name, "build", 5) == 0 && (name[5] == 0 || name[5] == '-');
}

/* The file's bytes (malloc'd), or 0 for one to skip. */
char* read_file(const char* path, size_t* n)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return 0;
    struct stat st;
    if (fstat(fileno(f), &st) != 0 || st.st_size > k_max_file) {
        fclose(f);
        return 0;
    }
    size_t size = (size_t)st.st_size;
    char* buf = (char*)malloc(size + 1);
    if (!buf) {
        fclose(f);
        return 0;
    }
    *n = fread(buf, 1, size, f);
    fclose(f);
    size_t probe = *n < 8192 ? *n : 8192;
    if (memchr(buf, 0, probe)) {
        free(buf);
        return 0;
    }
    return buf;
}

} /* namespace */

/* ---- the bottom pane's pages ---- */

void App::build_bottom()
{
    bottom_tabs_ = XtVaCreateManagedWidget("pages", xmRowColumnWidgetClass, bottom_slot_,
                                           XmNorientation, XmHORIZONTAL, XmNpacking,
                                           XmPACK_TIGHT, XmNspacing, 0, XmNmarginHeight, 0,
                                           XmNmarginWidth, 0,
                                           XmNtopAttachment, XmATTACH_FORM,
                                           XmNleftAttachment, XmATTACH_FORM,
                                           XmNrightAttachment, XmATTACH_FORM, (char*)0);
    static const char* const labels[2] = { "Terminal", "Find Results" };
    for (int i = 0; i < 2; i++) {
        page_tabs_[i] = XtVaCreateManagedWidget(labels[i], xmToggleButtonWidgetClass,
                                                bottom_tabs_, XmNindicatorOn, False,
                                                XmNshadowThickness, 2, XmNfillOnSelect, True,
                                                XmNselectColor, tab_fill(),
                                                XmNtraversalOn, False, XmNmarginWidth, 8,
                                                XmNuserData, (XtPointer)(long)i,
                                                XmNset, i == 0, (char*)0);
        XtAddCallback(page_tabs_[i], XmNvalueChangedCallback, page_cb, (XtPointer)this);
    }
    Arg args[6];
    Cardinal n = 0;
    XtSetArg(args[n], XmNselectionPolicy, XmBROWSE_SELECT); n++;
    XtSetArg(args[n], XmNscrollBarDisplayPolicy, XmAS_NEEDED); n++;
    XtSetArg(args[n], XmNlistSizePolicy, XmCONSTANT); n++;
    results_ = XmCreateScrolledList(bottom_slot_, (char*)"results", args, n);
    results_box_ = XtParent(results_);
    XtVaSetValues(results_box_, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, bottom_tabs_,
                  XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
                  XmNbottomAttachment, XmATTACH_FORM, (char*)0);
    XtAddCallback(results_, XmNdefaultActionCallback, results_cb, (XtPointer)this);
    XtManageChild(results_);
    XtUnmanageChild(results_box_);
}

void App::show_bottom_page(int page)
{
    bottom_page_ = page;
    for (int i = 0; i < 2; i++)
        XmToggleButtonSetState(page_tabs_[i], i == page, False);
    if (page == 0) {
        XtUnmanageChild(results_box_);
        if (term_)
            XtManageChild(term_->widget());
    } else {
        if (term_)
            XtUnmanageChild(term_->widget());
        XtManageChild(results_box_);
    }
    if (!bottom_on_)
        show_bottom(true);
}

void App::bottom_page_chosen(int page)
{
    show_bottom_page(page);
    if (page == 0 && term_)
        term_->focus();
    else if (page == 1)
        XmProcessTraversal(results_, XmTRAVERSE_CURRENT);
}

/* ---- the dialog ---- */

void App::show_find_in_files(bool replace)
{
    if (!fif_dlg_) {
        static Hook hooks[3];
        Arg a[1];
        XtSetArg(a[0], XmNautoUnmanage, False);
        fif_dlg_ = XmCreateFormDialog(top_, (char*)"findInFiles", a, 1);
        tx::fixed_size(fif_dlg_);
        XtVaSetValues(fif_dlg_, XmNmarginWidth, 10, XmNmarginHeight, 10, (char*)0);
        Widget page = XtVaCreateManagedWidget("page", xmRowColumnWidgetClass, fif_dlg_,
                                              XmNorientation, XmVERTICAL, XmNspacing, 4,
                                              XmNtopAttachment, XmATTACH_FORM,
                                              XmNleftAttachment, XmATTACH_FORM,
                                              XmNrightAttachment, XmATTACH_FORM, (char*)0);
        static const char* const labels[4] = { "Find what:", "Replace with:", "In folder:",
                                               "File types:" };
        Widget* fields[4] = { &fif_what_, &fif_with_, &fif_folder_, &fif_types_ };
        for (int i = 0; i < 4; i++) {
            Widget r = XtVaCreateManagedWidget("row", xmRowColumnWidgetClass, page,
                                               XmNorientation, XmHORIZONTAL, XmNmarginHeight, 0,
                                               XmNmarginWidth, 0, (char*)0);
            XmString s = XmStringCreateLocalized((char*)labels[i]);
            Widget l = XtVaCreateManagedWidget("label", xmLabelWidgetClass, r,
                                               XmNlabelString, s, XmNwidth, 100,
                                               XmNrecomputeSize, False,
                                               XmNalignment, XmALIGNMENT_END, (char*)0);
            XmStringFree(s);
            *fields[i] = XtVaCreateManagedWidget("field", xmTextFieldWidgetClass, r,
                                                 XmNcolumns, 44, (char*)0);
            if (i == 1)
                fif_with_label_ = r;
            (void)l;
        }
        Widget r = XtVaCreateManagedWidget("row", xmRowColumnWidgetClass, page,
                                           XmNorientation, XmHORIZONTAL, XmNmarginWidth, 100,
                                           (char*)0);
        fif_case_ = XtVaCreateManagedWidget("Match case", xmToggleButtonWidgetClass, r,
                                            (char*)0);
        fif_word_ = XtVaCreateManagedWidget("Whole words", xmToggleButtonWidgetClass, r,
                                            (char*)0);
        XtVaCreateManagedWidget("File types: *.c *.h and so on; empty: every text file.",
                                xmLabelWidgetClass, page, (char*)0);
        /* The buttons in a row of their own, right-aligned under the page. */
        Widget buttons = XtVaCreateManagedWidget("buttons", xmRowColumnWidgetClass, fif_dlg_,
                                                 XmNorientation, XmHORIZONTAL, XmNspacing, 6,
                                                 XmNmarginWidth, 0, XmNmarginHeight, 0,
                                                 XmNtopAttachment, XmATTACH_WIDGET,
                                                 XmNtopWidget, page, XmNtopOffset, 10,
                                                 XmNrightAttachment, XmATTACH_FORM,
                                                 XmNbottomAttachment, XmATTACH_FORM, (char*)0);
        static const char* const names[3] = { "Find All", "Replace All", "Close" };
        static const int which[3] = { FIF_FIND, FIF_REPLACE, FIF_CLOSE };
        for (int i = 0; i < 3; i++) {
            Widget b = XtVaCreateManagedWidget(names[i], xmPushButtonWidgetClass, buttons,
                                               (char*)0);
            hooks[i].app = this;
            hooks[i].which = which[i];
            XtAddCallback(b, XmNactivateCallback, fif_cb, (XtPointer)&hooks[i]);
            if (which[i] == FIF_REPLACE)
                fif_replace_btn_ = b;
            if (which[i] == FIF_FIND)
                fif_find_btn_ = b;
            if (which[i] == FIF_CLOSE)
                XtVaSetValues(fif_dlg_, XmNcancelButton, b, (char*)0);
        }
        XmTextFieldSetString(fif_folder_, (char*)root_.c_str());
    }
    fif_replace_ = replace;
    XtVaSetValues(XtParent(fif_dlg_), XmNtitle, replace ? "Replace in Files" : "Find in Files",
                  (char*)0);
    if (replace) {
        XtManageChild(fif_with_label_);
        XtManageChild(fif_replace_btn_);
        XtUnmanageChild(fif_find_btn_);
        XtVaSetValues(fif_dlg_, XmNdefaultButton, fif_replace_btn_, (char*)0);
    } else {
        XtUnmanageChild(fif_with_label_);
        XtUnmanageChild(fif_replace_btn_);
        XtManageChild(fif_find_btn_);
        XtVaSetValues(fif_dlg_, XmNdefaultButton, fif_find_btn_, (char*)0);
    }
    /* The selection, if one line of it, is what to find. */
    Doc* d = doc();
    if (d && d->view.has_selection()) {
        size_t n;
        char* sel = d->view.copy(&n);
        if (sel && !memchr(sel, '\n', n))
            XmTextFieldSetString(fif_what_, sel);
        free(sel);
    }
    XtManageChild(fif_dlg_);
    XmProcessTraversal(fif_what_, XmTRAVERSE_CURRENT);
}

void App::fif_button(int which)
{
    if (which == FIF_CLOSE) {
        XtUnmanageChild(fif_dlg_);
        return;
    }
    char* what = XmTextFieldGetString(fif_what_);
    char* with = XmTextFieldGetString(fif_with_);
    char* folder = XmTextFieldGetString(fif_folder_);
    char* types = XmTextFieldGetString(fif_types_);
    bool match_case = XmToggleButtonGetState(fif_case_) != False;
    bool whole = XmToggleButtonGetState(fif_word_) != False;
    const char* dir = folder && *folder ? folder : root_.c_str();
    if (what && *what) {
        XtUnmanageChild(fif_dlg_);
        if (which == FIF_REPLACE) {
            replace_in_files(what, with ? with : "", dir, types, match_case, whole);
        } else {
            int files = 0;
            int n = find_in_files(what, dir, types, match_case, whole, &files);
            char m[200];
            snprintf(m, sizeof(m), "%d found in %d files (%d searched).", n, files, scanned_);
            XmString s = XmStringCreateLocalized(m);
            XtVaSetValues(status_, XmNlabelString, s, (char*)0);
            XmStringFree(s);
            if (n > 0)
                goto_result(0);
        }
    }
    XtFree(what);
    XtFree(with);
    XtFree(folder);
    XtFree(types);
}

/* ---- searching ---- */

void App::clear_results()
{
    for (int i = 0; i < results_list_.size(); i++) {
        free(results_list_[i]->path);
        delete results_list_[i];
    }
    results_list_.clear();
    cur_result_ = -1;
}

/* One file's matches into results_list_: from the open document if it
 * is one (unsaved changes count), else from the file. */
void App::search_file(const char* path, const char* what, bool match_case, bool whole)
{
    size_t tl = strlen(what);
    if (tl == 0)
        return;
    int before = results_list_.size();
    Doc* open = 0;
    for (int i = 0; i < docs_.size(); i++)
        if (strcmp(docs_[i]->path.c_str(), path) == 0)
            open = docs_[i];
    char* data = 0;
    size_t n = 0;
    if (!open) {
        data = read_file(path, &n);
        if (!data)
            return;
    }
    scanned_++;
    long lines = open ? open->text.lines() : 0;
    size_t pos = 0;
    for (long line = 0; open ? line < lines : pos <= n; line++) {
        const char* s;
        size_t len;
        if (open) {
            s = open->text.line(line);
            len = open->text.length(line);
        } else {
            if (pos >= n && line > 0)
                break;
            s = data + pos;
            const char* e = (const char*)memchr(s, '\n', n - pos);
            len = e ? (size_t)(e - s) : n - pos;
            pos += len + 1;
            if (len > 0 && s[len - 1] == '\r')
                len--;
        }
        for (size_t i = 0; i + tl <= len; i++) {
            if (!match_at(s, len, i, what, tl, match_case, whole))
                continue;
            if (results_list_.size() >= k_max_results)
                break;
            Result* r = new Result;
            r->path = strdup(path);
            r->line = line;
            r->col = i;
            r->len = tl;
            results_list_.push_back(r);
            i += tl - 1;
        }
    }
    free(data);
    if (results_list_.size() > before)
        result_files_++;
}

void App::walk(const char* dir, const char* what, const char* types, bool match_case, bool whole,
               int depth)
{
    if (depth > 32 || results_list_.size() >= k_max_results)
        return;
    DIR* dp = opendir(dir);
    if (!dp)
        return;
    /* Sorted, so results come in the tree's order. */
    ttr::Vector<char*> names;
    struct dirent* e;
    while ((e = readdir(dp)) != 0)
        if (e->d_name[0] != '.')
            names.push_back(strdup(e->d_name));
    closedir(dp);
    for (int i = 1; i < names.size(); i++)
        for (int j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) {
            char* t = names[j];
            names[j] = names[j - 1];
            names[j - 1] = t;
        }
    for (int i = 0; i < names.size(); i++) {
        char path[2400];
        snprintf(path, sizeof(path), "%s/%s", strcmp(dir, "/") == 0 ? "" : dir, names[i]);
        struct stat st;
        if (stat(path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                if (!build_dir(names[i]))
                    walk(path, what, types, match_case, whole, depth + 1);
            } else if (S_ISREG(st.st_mode) && type_matches(types, names[i])) {
                search_file(path, what, match_case, whole);
            }
        }
        free(names[i]);
    }
}

int App::find_in_files(const char* what, const char* dir, const char* types, bool match_case,
                       bool whole, int* files)
{
    clear_results();
    scanned_ = 0;
    result_files_ = 0;
    *files = 0;
    if (!what || !*what) {
        fill_results();
        return 0;
    }
    fif_what_text_ = what;
    fif_case_on_ = match_case;
    fif_word_on_ = whole;
    Widget busy = top_;
    Cursor watch = XCreateFontCursor(XtDisplay(busy), 150);     /* XC_watch */
    XDefineCursor(XtDisplay(busy), XtWindow(busy), watch);
    XFlush(XtDisplay(busy));
    walk(dir, what, types, match_case, whole, 0);
    XUndefineCursor(XtDisplay(busy), XtWindow(busy));
    XFreeCursor(XtDisplay(busy), watch);
    fill_results();
    show_bottom_page(1);
    *files = result_files_;
    return results_list_.size();
}

/* The results as "path(line): text", the path relative to the workspace. */
void App::fill_results()
{
    XmListDeleteAllItems(results_);
    int n = results_list_.size();
    if (!n) {
        XmString s = XmStringCreateLocalized((char*)"No matches.");
        XmListAddItem(results_, s, 0);
        XmStringFree(s);
        return;
    }
    XmString* items = (XmString*)XtMalloc((Cardinal)(sizeof(XmString) * (size_t)n));
    size_t rl = strlen(root_.c_str());
    char* cached_path = 0;
    char* data = 0;
    size_t dn = 0;
    for (int i = 0; i < n; i++) {
        Result* r = results_list_[i];
        const char* shown = r->path;
        if (strncmp(shown, root_.c_str(), rl) == 0 && shown[rl] == '/')
            shown += rl + 1;
        /* The line's text: from the open document, or the file. */
        char text[160];
        text[0] = 0;
        const char* s = 0;
        size_t len = 0;
        Doc* open = 0;
        for (int k = 0; k < docs_.size(); k++)
            if (strcmp(docs_[k]->path.c_str(), r->path) == 0)
                open = docs_[k];
        if (open && r->line < open->text.lines()) {
            s = open->text.line(r->line);
            len = open->text.length(r->line);
        } else {
            if (!cached_path || strcmp(cached_path, r->path) != 0) {
                free(cached_path);
                free(data);
                cached_path = strdup(r->path);
                data = read_file(r->path, &dn);
            }
            const char* p = data;
            for (long l = 0; p && l < r->line; l++) {
                const char* e = (const char*)memchr(p, '\n', dn - (size_t)(p - data));
                p = e ? e + 1 : 0;
            }
            if (p) {
                s = p;
                const char* e = (const char*)memchr(p, '\n', dn - (size_t)(p - data));
                len = e ? (size_t)(e - p) : dn - (size_t)(p - data);
            }
        }
        size_t o = 0, k = 0;
        while (s && k < len && (s[k] == ' ' || s[k] == '\t'))
            k++;
        for (; s && k < len && o < sizeof(text) - 1; k++)
            text[o++] = s[k] == '\t' ? ' ' : (s[k] == '\r' ? ' ' : s[k]);
        text[o] = 0;
        char row[2700];
        snprintf(row, sizeof(row), "%s(%ld): %s", shown, r->line + 1, text);
        items[i] = XmStringCreateLocalized(row);
    }
    free(cached_path);
    free(data);
    XmListAddItems(results_, items, n, 0);
    for (int i = 0; i < n; i++)
        XmStringFree(items[i]);
    XtFree((char*)items);
}

void App::result_chosen(int i)
{
    goto_result(i);
}

/* Open the result's file at the match, selected. */
void App::goto_result(int i)
{
    if (i < 0 || i >= results_list_.size())
        return;
    cur_result_ = i;
    Result* r = results_list_[i];
    open_file(r->path);
    Doc* d = doc();
    if (d && strcmp(d->path.c_str(), r->path) == 0) {
        d->view.set_cursor(te::Pos(r->line, r->col), false);
        d->view.set_cursor(te::Pos(r->line, r->col + r->len), true);
        d->view.goto_line(r->line + 1);
        d->view.set_cursor(te::Pos(r->line, r->col), false);
        d->view.set_cursor(te::Pos(r->line, r->col + r->len), true);
        after_edit(true);
    }
    XmListSelectPos(results_, i + 1, False);
    int top = 0, visible = 0;
    XtVaGetValues(results_, XmNtopItemPosition, &top, XmNvisibleItemCount, &visible, (char*)0);
    if (i + 1 < top || i + 1 >= top + visible)
        XmListSetPos(results_, i + 1 > 2 ? i - 1 : 1);
    XmProcessTraversal(grid_, XmTRAVERSE_CURRENT);
}

/* ---- replacing ---- */

bool App::confirm(const char* message)
{
    Widget dlg = XmCreateQuestionDialog(top_, (char*)"confirm", 0, 0);
    XmString s = XmStringCreateLocalized((char*)message);
    XmString ok = XmStringCreateLocalized((char*)"Replace");
    XtVaSetValues(dlg, XmNmessageString, s, XmNokLabelString, ok,
                  XmNdialogStyle, XmDIALOG_FULL_APPLICATION_MODAL, (char*)0);
    XmStringFree(s);
    XmStringFree(ok);
    XtUnmanageChild(XtNameToWidget(dlg, "Help"));
    XtVaSetValues(XtParent(dlg), XmNtitle, "Replace in Files", (char*)0);
    int answer = -1;
    g_answer = &answer;
    XtAddCallback(dlg, XmNokCallback, answer_cb, (XtPointer)0);
    XtAddCallback(dlg, XmNcancelCallback, answer_cb, (XtPointer)1);
    XtManageChild(dlg);
    XtAppContext ctx = XtWidgetToApplicationContext(top_);
    while (answer < 0 && XtIsManaged(dlg))
        XtAppProcessEvent(ctx, XtIMAll);
    XtDestroyWidget(XtParent(dlg));
    return answer == 0;
}

void App::replace_in_files(const char* what, const char* with, const char* dir,
                           const char* types, bool match_case, bool whole)
{
    int files = 0;
    int n = find_in_files(what, dir, types, match_case, whole, &files);
    if (n == 0) {
        XmString s = XmStringCreateLocalized((char*)"Nothing to replace.");
        XtVaSetValues(status_, XmNlabelString, s, (char*)0);
        XmStringFree(s);
        return;
    }
    char m[700];
    snprintf(m, sizeof(m), "Replace %d matches of \"%.200s\" in %d files with \"%.200s\"?\n"
             "Open files can be undone; the others are saved.", n, what, files, with);
    if (!confirm(m))
        return;
    int replaced = 0, failed = 0;
    const char* last = 0;
    for (int i = 0; i < results_list_.size(); i++) {
        const char* path = results_list_[i]->path;
        if (last && strcmp(last, path) == 0)
            continue;
        last = path;
        Doc* open = 0;
        for (int k = 0; k < docs_.size(); k++)
            if (strcmp(docs_[k]->path.c_str(), path) == 0)
                open = docs_[k];
        if (open) {
            replaced += open->view.replace_all(what, with, match_case, whole);
            update_tab(open);
            continue;
        }
        size_t len;
        char* data = read_file(path, &len);
        if (!data) {
            failed++;
            continue;
        }
        te::Document text;
        text.set_text(data, len);
        free(data);
        int k = text.replace_all(what, with, match_case, whole);
        if (k > 0 && te::save_file(text, path, 0) != 0)
            failed++;
        else
            replaced += k;
    }
    snprintf(m, sizeof(m), "%d replaced in %d files%s.", replaced, files,
             failed ? "; some files could not be written" : "");
    XmString s = XmStringCreateLocalized(m);
    XtVaSetValues(status_, XmNlabelString, s, (char*)0);
    XmStringFree(s);
    /* The results now show the new text. */
    find_in_files(with, dir, types, match_case, whole, &files);
    TwGridDamage(grid_);
}

} /* namespace ttrcode */
