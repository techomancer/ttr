/*
 * The debugger's windows (td/DESIGN.md section 8.2), as Visual C++ 6 had
 * them, on CellLists (celllist.h):
 *
 * - Watch: expressions typed in (Enter or typing on a row edits it, the
 *   empty last row adds one, Delete removes one), shown as trees.
 * - Variables: the chosen frame's variables, Auto (those the current and
 *   the previous line use) or Locals (all in scope), and the frame to
 *   show them for.
 * - Registers, with what changed since the last stop in red.
 * - Memory at an address (an expression, evaluated when Enter is typed),
 *   as bytes, shorts, longs, quads, characters or floats; Up, Down, Page
 *   Up and Page Down move through it.
 * - Call Stack: double-click a frame to show it.
 * - Threads: double-click one to switch to it.
 * - QuickWatch (Shift+F9): one expression in a dialog, to add to Watch.
 */
#include "app.h"

#include "core/settings.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/keysym.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/TextF.h>
#include <Xm/ToggleB.h>

namespace ttrcode {

namespace {

enum { MEM_BYTE, MEM_SHORT, MEM_LONG, MEM_QUAD, MEM_CHAR, MEM_REAL4, MEM_REAL8, MEM_FORMATS };
const char* const k_mem_formats[MEM_FORMATS] = {
    "Byte", "Short Hex", "Long Hex", "Quad Hex", "ASCII", "Real 4", "Real 8"
};
const int k_mem_unit[MEM_FORMATS] = { 1, 2, 4, 8, 1, 4, 8 };

/* The panes' ids (k_debug_panes in app.cpp). */
bool is_pane(Pane* p, const char* id)
{
    return !strcmp(p->id(), id);
}

struct Hook {
    App* app;
    int which;
};

/* Hooks for the frames in Variables' Context menu, the memory formats and
 * QuickWatch's buttons; the frames' grow as needed. */
Hook* g_frame_hooks;
int g_nframe_hooks;
Hook g_mem_hooks[MEM_FORMATS];
/* Variables' Context menu's buttons, one per frame. */
ttr::Vector<Widget> g_context_items;
Hook g_quick_hooks[3];

void context_cb(Widget, XtPointer data, XtPointer)
{
    Hook* h = (Hook*)data;
    h->app->frame_chosen(h->which);
}

void mode_cb(Widget, XtPointer data, XtPointer call)
{
    Hook* h = (Hook*)data;
    if (((XmToggleButtonCallbackStruct*)call)->set)
        h->app->vars_mode(h->which == 0);
}

void mem_addr_cb(Widget, XtPointer data, XtPointer)
{
    ((App*)data)->memory_address_entered();
}

void mem_format_cb(Widget, XtPointer data, XtPointer)
{
    Hook* h = (Hook*)data;
    h->app->memory_format_chosen(h->which);
}

void quick_cb(Widget, XtPointer data, XtPointer)
{
    Hook* h = (Hook*)data;
    h->app->quickwatch_button(h->which);
}

void quick_enter_cb(Widget, XtPointer data, XtPointer)
{
    ((App*)data)->quickwatch_button(0);
}

const char* const k_keywords[] = {
    "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else",
    "enum", "extern", "float", "for", "goto", "if", "int", "long", "register", "return",
    "short", "signed", "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned",
    "void", "volatile", "while", "bool", "true", "false", "class", "new", "delete", "inline",
    "namespace", "template", "public", "private", "protected", "virtual", "operator", "NULL",
    0
};

bool keyword(const char* w)
{
    int i;
    for (i = 0; k_keywords[i]; i++)
        if (!strcmp(w, k_keywords[i]))
            return true;
    return false;
}

/* The names a line of C uses, not calls or members (VC6's Auto). */
void line_names(const char* s, ttr::Vector<ttr::Str>* out)
{
    const char* p = s;
    while (*p) {
        if (p[0] == '/' && p[1] == '/')
            return;
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            while (*p && *p != q) {
                if (*p == '\\' && p[1])
                    p++;
                p++;
            }
            if (*p)
                p++;
            continue;
        }
        if (isalpha((unsigned char)*p) || *p == '_') {
            const char* start = p;
            while (isalnum((unsigned char)*p) || *p == '_')
                p++;
            /* After . or ->: a member, part of what came before. */
            const char* b = start;
            while (b > s && (b[-1] == ' ' || b[-1] == '\t'))
                b--;
            bool member = (b > s && b[-1] == '.') || (b > s + 1 && b[-1] == '>' && b[-2] == '-');
            const char* a = p;
            while (*a == ' ' || *a == '\t')
                a++;
            bool call = *a == '(';
            char w[128];
            size_t n = (size_t)(p - start);
            if (n >= sizeof(w) || member || call)
                continue;
            memcpy(w, start, n);
            w[n] = 0;
            if (keyword(w))
                continue;
            int i;
            bool seen = false;
            for (i = 0; i < out->size() && !seen; i++)
                seen = !strcmp((*out)[i].c_str(), w);
            if (!seen)
                out->push_back(ttr::Str(w));
            continue;
        }
        if (isdigit((unsigned char)*p)) {
            while (isalnum((unsigned char)*p) || *p == '.')
                p++;
            continue;
        }
        p++;
    }
}

/* Line n (from 1) of a file. */
bool file_line(const char* path, int n, char* buf, size_t size)
{
    FILE* f = fopen(path, "r");
    if (!f)
        return false;
    int line = 1;
    bool ok = false;
    while (fgets(buf, (int)size, f)) {
        if (line == n) {
            ok = true;
            break;
        }
        if (strchr(buf, '\n'))
            line++;
    }
    fclose(f);
    if (ok) {
        size_t l = strlen(buf);
        while (l && (buf[l - 1] == '\n' || buf[l - 1] == '\r'))
            buf[--l] = 0;
    }
    return ok;
}

bool dark(uint32_t c)
{
    unsigned r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    return r * 3 + g * 6 + b < 128 * 10;
}

void add_hex_digits(td::Text* t, uint64_t v, int digits)
{
    int i;
    for (i = digits - 1; i >= 0; i--)
        t->add_char("0123456789abcdef"[(v >> (i * 4)) & 15]);
}

} /* namespace */

/* ---- making and dropping ---- */

ListColors App::list_colors() const
{
    ListColors c;
    c.text = scheme_.get(th::SC_FOREGROUND);
    c.background = scheme_.get(th::SC_BACKGROUND);
    c.selected_text = scheme_.get(th::SC_SELECTION_FG);
    c.selected_background = scheme_.get(th::SC_SELECTION_BG);
    c.header_text = scheme_.get(th::SC_GUTTER_FG);
    c.header_background = scheme_.get(th::SC_GUTTER_BG);
    c.lines = scheme_.get(th::SC_RULER);
    c.dim = scheme_.get(th::SC_GUTTER_FG);
    c.changed = dark(c.background) ? 0xff6b6b : 0xd00000;
    c.cursor = scheme_.get(th::SC_CURSOR);
    return c;
}

void App::recolor_lists()
{
    CellList* lists[7] = { stack_list_, regs_list_, threads_list_, watch_list_, vars_list_,
                           mem_list_, quick_list_ };
    ListColors c = list_colors();
    int i;
    for (i = 0; i < 7; i++)
        if (lists[i])
            lists[i]->set_colors(c);
}

CellList* App::make_list(Widget parent, const char* name, int tag)
{
    Arg args[8];
    Cardinal n = 0;
    XtSetArg(args[n], TwNfont, font_ ? font_ : opt_.font); n++;
    if (opt_.fallback_fonts) {
        XtSetArg(args[n], TwNfallbackFonts, opt_.fallback_fonts); n++;
    }
    if (opt_.renderer && *opt_.renderer) {
        XtSetArg(args[n], TwNrenderer, opt_.renderer); n++;
    }
    CellList* l = new CellList(parent, name, this, args, n);
    l->set_colors(list_colors());
    l->set_tag(tag);
    return l;
}

Widget App::build_debug_pane(Pane* p, Widget parent)
{
    int tag = debug_panes_.find(p);
    drop_debug_pane(p);
    if (is_pane(p, "callstack")) {
        stack_list_ = make_list(parent, "callstack", tag);
        stack_list_->set_marks(true);
        fill_callstack();
        return stack_list_->widget();
    }
    if (is_pane(p, "registers")) {
        regs_list_ = make_list(parent, "registers", tag);
        fill_registers();
        return regs_list_->widget();
    }
    if (is_pane(p, "threads")) {
        threads_list_ = make_list(parent, "threads", tag);
        threads_list_->set_marks(true);
        static const char* const titles[2] = { "ID", "Location" };
        static const int widths[2] = { 14, 40 };
        threads_list_->set_columns(2, titles, widths);
        fill_threads();
        return threads_list_->widget();
    }
    static const char* const value_titles[3] = { "Name", "Value", "Type" };
    static const int value_widths[3] = { 22, 36, 20 };
    if (is_pane(p, "watch")) {
        watch_list_ = make_list(parent, "watch", tag);
        watch_list_->set_columns(3, value_titles, value_widths);
        fill_watch();
        return watch_list_->widget();
    }
    if (is_pane(p, "variables")) {
        static Hook mode_hooks[2];
        vars_box_ = XtVaCreateWidget("variables", xmFormWidgetClass, parent, (char*)0);
        Widget bar = XtVaCreateManagedWidget("bar", xmRowColumnWidgetClass, vars_box_,
                                             XmNorientation, XmHORIZONTAL, XmNmarginHeight, 0,
                                             XmNtopAttachment, XmATTACH_FORM,
                                             XmNleftAttachment, XmATTACH_FORM,
                                             XmNrightAttachment, XmATTACH_FORM, (char*)0);
        vars_menu_ = XmCreatePulldownMenu(bar, (char*)"contextMenu", 0, 0);
        g_context_items.clear();
        g_context_items.push_back(XtVaCreateManagedWidget("(not running)",
                                                          xmPushButtonWidgetClass, vars_menu_,
                                                          (char*)0));
        Arg a[2];
        XmString l = XmStringCreateLocalized((char*)"Context:");
        XtSetArg(a[0], XmNsubMenuId, vars_menu_);
        XtSetArg(a[1], XmNlabelString, l);
        vars_context_ = XmCreateOptionMenu(bar, (char*)"context", a, 2);
        XmStringFree(l);
        XtManageChild(vars_context_);
        Widget radio = XmCreateRadioBox(bar, (char*)"mode", 0, 0);
        XtVaSetValues(radio, XmNorientation, XmHORIZONTAL, XmNmarginHeight, 0, (char*)0);
        static const char* const modes[2] = { "Auto", "Locals" };
        Widget* ws[2] = { &vars_auto_w_, &vars_locals_w_ };
        int i;
        for (i = 0; i < 2; i++) {
            mode_hooks[i].app = this;
            mode_hooks[i].which = i;
            *ws[i] = XtVaCreateManagedWidget(modes[i], xmToggleButtonWidgetClass, radio,
                                             XmNset, (i == 0) == vars_auto_, (char*)0);
            XtAddCallback(*ws[i], XmNvalueChangedCallback, mode_cb, (XtPointer)&mode_hooks[i]);
        }
        XtManageChild(radio);
        vars_list_ = make_list(vars_box_, "list", tag);
        vars_list_->set_columns(3, value_titles, value_widths);
        XtVaSetValues(vars_list_->widget(), XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, bar,
                      XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
                      XmNbottomAttachment, XmATTACH_FORM, (char*)0);
        XtManageChild(vars_box_);
        vars_menu_stop_ = -1;
        fill_variables();
        return vars_box_;
    }
    if (is_pane(p, "memory")) {
        mem_box_ = XtVaCreateWidget("memory", xmFormWidgetClass, parent, (char*)0);
        /* Address: [expression        ] Format: [Byte] */
        Widget bar = XtVaCreateManagedWidget("bar", xmFormWidgetClass, mem_box_,
                                             XmNtopAttachment, XmATTACH_FORM,
                                             XmNleftAttachment, XmATTACH_FORM,
                                             XmNrightAttachment, XmATTACH_FORM, (char*)0);
        Widget menu = XmCreatePulldownMenu(bar, (char*)"formatMenu", 0, 0);
        Widget chosen = 0;
        int i;
        for (i = 0; i < MEM_FORMATS; i++) {
            g_mem_hooks[i].app = this;
            g_mem_hooks[i].which = i;
            Widget b = XtVaCreateManagedWidget(k_mem_formats[i], xmPushButtonWidgetClass, menu,
                                               (char*)0);
            XtAddCallback(b, XmNactivateCallback, mem_format_cb, (XtPointer)&g_mem_hooks[i]);
            if (i == mem_format_)
                chosen = b;
        }
        Arg a[6];
        XmString l = XmStringCreateLocalized((char*)"Format:");
        XtSetArg(a[0], XmNsubMenuId, menu);
        XtSetArg(a[1], XmNlabelString, l);
        XtSetArg(a[2], XmNmenuHistory, chosen);
        XtSetArg(a[3], XmNtopAttachment, XmATTACH_FORM);
        XtSetArg(a[4], XmNrightAttachment, XmATTACH_FORM);
        mem_format_w_ = XmCreateOptionMenu(bar, (char*)"format", a, 5);
        XmStringFree(l);
        XtManageChild(mem_format_w_);
        Widget label = XtVaCreateManagedWidget("Address:", xmLabelWidgetClass, bar,
                                               XmNtopAttachment, XmATTACH_FORM,
                                               XmNbottomAttachment, XmATTACH_FORM,
                                               XmNleftAttachment, XmATTACH_FORM,
                                               XmNleftOffset, 4, (char*)0);
        mem_addr_w_ = XtVaCreateManagedWidget("address", xmTextFieldWidgetClass, bar,
                                              XmNcolumns, 12, XmNtopAttachment, XmATTACH_FORM,
                                              XmNtopOffset, 2,
                                              XmNleftAttachment, XmATTACH_WIDGET,
                                              XmNleftWidget, label,
                                              XmNrightAttachment, XmATTACH_WIDGET,
                                              XmNrightWidget, mem_format_w_, (char*)0);
        XmTextFieldSetString(mem_addr_w_, (char*)mem_expr_.c_str());
        XtAddCallback(mem_addr_w_, XmNactivateCallback, mem_addr_cb, (XtPointer)this);
        mem_list_ = make_list(mem_box_, "list", tag);
        XtVaSetValues(mem_list_->widget(), XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, bar,
                      XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
                      XmNbottomAttachment, XmATTACH_FORM, (char*)0);
        XtManageChild(mem_box_);
        fill_memory();
        return mem_box_;
    }
    Widget f = XtVaCreateManagedWidget(p->id(), xmFormWidgetClass, parent, (char*)0);
    return f;
}

/* Its lists go (and their widgets with them). */
void App::drop_debug_pane(Pane* p)
{
    if (is_pane(p, "callstack")) {
        delete stack_list_;
        stack_list_ = 0;
    } else if (is_pane(p, "registers")) {
        delete regs_list_;
        regs_list_ = 0;
    } else if (is_pane(p, "threads")) {
        delete threads_list_;
        threads_list_ = 0;
    } else if (is_pane(p, "watch")) {
        delete watch_list_;
        watch_list_ = 0;
    } else if (is_pane(p, "variables")) {
        delete vars_list_;
        vars_list_ = 0;
        if (vars_box_)
            XtDestroyWidget(vars_box_);
        vars_box_ = vars_context_ = vars_menu_ = vars_auto_w_ = vars_locals_w_ = 0;
        g_context_items.clear();
    } else if (is_pane(p, "memory")) {
        if (mem_addr_w_) {
            char* s = XmTextFieldGetString(mem_addr_w_);
            mem_expr_ = s ? s : "";
            XtFree(s);
        }
        delete mem_list_;
        mem_list_ = 0;
        if (mem_box_)
            XtDestroyWidget(mem_box_);
        mem_box_ = mem_addr_w_ = mem_format_w_ = 0;
    } else if (p->widget) {
        XtDestroyWidget(p->widget);
    }
}

/* ---- filling ---- */

void App::debug_fill()
{
    fill_callstack();
    fill_registers();
    fill_threads();
    fill_variables();
    fill_watch();
    fill_memory();
    if (quick_dlg_ && XtIsManaged(quick_dlg_))
        fill_quickwatch();
}

void App::fill_callstack()
{
    if (!stack_list_)
        return;
    stack_list_->clear();
    int i;
    for (i = 0; dbg_ && i < frames_.size(); i++) {
        ttr::Str w = td::format_place(frames_[i]);
        char h[24], line[1600];
        snprintf(line, sizeof(line), "%s  %s", w.c_str(), td::hex(frames_[i].pc, h));
        int r = stack_list_->add_row();
        stack_list_->set_cell(r, 0, line);
        /* VC6: a yellow arrow where it stopped, a green one for the frame
         * chosen. */
        if (i == 0)
            stack_list_->set_mark(r, 0x25b6, 0xe0b000);
        else if (i == cur_frame_)
            stack_list_->set_mark(r, 0x25b6, 0x00a000);
    }
    stack_list_->done();
    if (dbg_ && cur_frame_ < frames_.size())
        stack_list_->select(cur_frame_);
}

void App::fill_registers()
{
    if (dbg_ && frames_.size() > 0 && regs_stop_ != stop_serial_) {
        /* A new stop: the last one's registers are what changed compares
         * with. */
        int thread = dbg_->current_thread();
        last_regs_ = cur_regs_;
        have_last_regs_ = regs_stop_ >= 0 && regs_thread_ == thread;
        cur_regs_ = frames_[0].regs;
        cur_regs_.pc = frames_[0].pc;
        regs_stop_ = stop_serial_;
        regs_thread_ = thread;
    }
    if (!regs_list_)
        return;
    regs_list_->clear();
    if (!dbg_ || frames_.size() == 0) {
        regs_list_->done();
        return;
    }
    const td::Frame& f = frames_[cur_frame_];
    const td::Arch* a = dbg_->target()->arch();
    int digits = a->reg_size * 2;
    /* "rax = 0000000000000000" side by side, as many as fit. */
    int each = 5 + 3 + digits + 2;
    int per = regs_list_->width() / each;
    if (per < 1)
        per = 1;
    td::Text line;
    int on_line = 0;
    int i;
    for (i = 0; i <= a->nregs; i++) {
        const char* n;
        uint64_t v;
        bool changed;
        if (i == a->nregs) {
            n = "pc";
            v = f.pc;
            changed = cur_frame_ == 0 && have_last_regs_ && last_regs_.pc != f.pc;
        } else {
            n = a->reg_name(i);
            if (!n || !f.regs.valid[i])
                continue;
            v = f.regs.r[i];
            changed = cur_frame_ == 0 && have_last_regs_ && last_regs_.valid[i] &&
                      last_regs_.r[i] != v;
        }
        if (on_line)
            line.add("  ");
        line.addf("%-5s= ", n);
        if (changed)
            line.add(LIST_CHANGED);
        add_hex_digits(&line, v, digits);
        if (changed)
            line.add(LIST_PLAIN);
        if (++on_line == per) {
            int r = regs_list_->add_row();
            regs_list_->set_cell(r, 0, line.c_str());
            line.clear();
            on_line = 0;
        }
    }
    if (on_line) {
        int r = regs_list_->add_row();
        regs_list_->set_cell(r, 0, line.c_str());
    }
    regs_list_->done();
}

void App::fill_threads()
{
    if (!threads_list_)
        return;
    threads_list_->clear();
    int cur = dbg_ ? dbg_->current_thread() : -1, i, sel = -1;
    for (i = 0; dbg_ && i < thread_ids_.size(); i++) {
        char id[48];
        snprintf(id, sizeof(id), "%d  (%d)", dbg_->thread_number(thread_ids_[i]),
                 thread_ids_[i]);
        int r = threads_list_->add_row();
        threads_list_->set_cell(r, 0, id);
        threads_list_->set_cell(r, 1, thread_where_[i].c_str());
        if (thread_ids_[i] == cur) {
            threads_list_->set_mark(r, 0x25b6, 0xe0b000);
            sel = r;
        }
    }
    threads_list_->done();
    if (sel >= 0)
        threads_list_->select(sel);
}

/* VC6's Auto: what the current line and the one before use. */
void App::auto_values(ttr::Vector<td::Value>* out)
{
    const td::Frame* f = frame();
    if (!f || !f->has_place || !f->place.file)
        return;
    ttr::Vector<ttr::Str> names;
    char buf[2048];
    int ln;
    for (ln = f->place.line - 1; ln <= f->place.line; ln++)
        if (ln > 0 && file_line(f->place.file, ln, buf, sizeof(buf)))
            line_names(buf, &names);
    int i;
    for (i = 0; i < names.size(); i++) {
        td::Value v = dbg_->evaluate(*f, names[i].c_str());
        const td::Type* t = td::strip(v.type);
        if (!v.ok() || v.where == td::VW_CONSTANT || (t && t->kind == td::TK_FUNCTION))
            continue;
        out->push_back(v);
    }
}

void App::fill_variables()
{
    if (!vars_list_)
        return;
    /* Context: the frames, rebuilt at each stop. */
    if (vars_menu_ && vars_menu_stop_ != (dbg_ ? frames_serial_ : -2)) {
        vars_menu_stop_ = dbg_ ? frames_serial_ : -2;
        ttr::Vector<Widget> old;
        int k;
        for (k = 0; k < g_context_items.size(); k++)
            old.push_back(g_context_items[k]);
        g_context_items.clear();
        int n = dbg_ ? frames_.size() : 0;
        if (n > g_nframe_hooks) {
            Hook* h = (Hook*)realloc(g_frame_hooks, sizeof(Hook) * (size_t)n);
            if (h) {
                g_frame_hooks = h;
                g_nframe_hooks = n;
            }
        }
        Widget chosen = 0;
        int i;
        for (i = 0; i < n && i < g_nframe_hooks; i++) {
            ttr::Str w = td::format_place(frames_[i]);
            Widget b = XtVaCreateManagedWidget(w.c_str(), xmPushButtonWidgetClass, vars_menu_,
                                               (char*)0);
            g_frame_hooks[i].app = this;
            g_frame_hooks[i].which = i;
            XtAddCallback(b, XmNactivateCallback, context_cb, (XtPointer)&g_frame_hooks[i]);
            g_context_items.push_back(b);
            if (i == cur_frame_ || !chosen)
                chosen = b;
        }
        if (!n) {
            chosen = XtVaCreateManagedWidget("(not running)", xmPushButtonWidgetClass, vars_menu_,
                                             (char*)0);
            g_context_items.push_back(chosen);
        }
        XtVaSetValues(vars_context_, XmNmenuHistory, chosen, (char*)0);
        for (i = 0; i < old.size(); i++)
            XtDestroyWidget(old[i]);
    } else if (vars_menu_ && dbg_ && cur_frame_ < g_context_items.size()) {
        XtVaSetValues(vars_context_, XmNmenuHistory, g_context_items[cur_frame_], (char*)0);
    }
    vars_list_->clear();
    const td::Frame* f = frame();
    if (f) {
        ttr::Vector<td::Value> vals;
        if (vars_auto_)
            auto_values(&vals);
        else
            dbg_->locals(*f, &vals);
        char ctx[64];
        snprintf(ctx, sizeof(ctx), "%s#%d", f->func ? f->func : "?", vars_auto_ ? 1 : 0);
        vars_tree_.set(dbg_, vals, stop_serial_, ctx);
        vars_tree_.fill(vars_list_, list_colors());
    }
    vars_list_->done();
}

void App::vars_mode(bool autos)
{
    if (vars_auto_ == autos)
        return;
    vars_auto_ = autos;
    fill_variables();
}

void App::fill_watch()
{
    if (!watch_list_)
        return;
    watch_list_->clear();
    const td::Frame* f = frame();
    int i;
    if (f) {
        ttr::Vector<td::Value> vals;
        for (i = 0; i < watches_.size(); i++)
            vals.push_back(dbg_->evaluate(*f, watches_[i].c_str()));
        watch_tree_.set(dbg_, vals, stop_serial_, "watch");
        watch_tree_.fill(watch_list_, list_colors());
    } else {
        for (i = 0; i < watches_.size(); i++) {
            int r = watch_list_->add_row();
            watch_list_->set_cell(r, 0, watches_[i].c_str());
        }
    }
    /* The empty row to type a new one in. */
    watch_list_->add_row();
    watch_list_->done();
}

void App::add_watch(const char* expr)
{
    if (!expr || !*expr)
        return;
    watches_.push_back(ttr::Str(expr));
    save_settings();
    fill_watch();
    if (watch_list_)
        watch_list_->select(watch_list_->row_count() - 2);
}

void App::save_watches(ttr::SettingsFile* f)
{
    td::Text t;
    int i;
    for (i = 0; i < watches_.size(); i++) {
        if (i)
            t.add(";");
        t.add(watches_[i].c_str());
    }
    if (t.length())
        f->set("debugWatch", t.c_str());
    else
        f->remove("debugWatch");
    t.clear();
    for (i = 0; i < bps_.size(); i++) {
        if (i)
            t.add(";");
        t.add(breakpoint_spec(bps_[i]).c_str());
    }
    if (t.length())
        f->set("debugBreakpoints", t.c_str());
    else
        f->remove("debugBreakpoints");
    t.clear();
    for (i = 0; i < data_bps_.size(); i++) {
        if (i)
            t.add(";");
        t.add(data_bps_[i].expr.c_str());
    }
    if (t.length())
        f->set("debugDataBreakpoints", t.c_str());
    else
        f->remove("debugDataBreakpoints");
}

/* ---- Memory ---- */

void App::memory_address_entered()
{
    if (!mem_addr_w_)
        return;
    char* s = XmTextFieldGetString(mem_addr_w_);
    mem_expr_ = s ? s : "";
    XtFree(s);
    const td::Frame* f = frame();
    if (!f || mem_expr_.empty()) {
        debug_status(f ? "" : "Memory: the program is not stopped.");
        return;
    }
    /* What it points to, or where it is (an array, a struct). */
    td::Value v = dbg_->evaluate(*f, mem_expr_.c_str());
    const td::Type* t = td::strip(v.type);
    uint64_t a = 0;
    if (v.ok() && v.where == td::VW_MEMORY && t && t->kind != td::TK_POINTER &&
        t->kind != td::TK_INT && t->kind != td::TK_ENUM) {
        a = v.addr;
    } else {
        td::Text e;
        e.addf("(unsigned long long)(%s)", mem_expr_.c_str());
        td::Value n = dbg_->evaluate(*f, e.c_str());
        unsigned char b[8];
        if (!v.ok() || !n.ok() || !dbg_->value_bytes(n, b, 8)) {
            td::Text m;
            m.addf("Memory: %s", v.ok() ? "not an address." : v.error.c_str());
            debug_status(m.c_str());
            return;
        }
        const td::Arch* arch = dbg_->target()->arch();
        int i;
        for (i = 0; i < 8; i++)
            a = arch->big_endian ? (a << 8) | b[i] : a | ((uint64_t)b[i] << (8 * i));
    }
    mem_addr_ = a;
    fill_memory();
    if (mem_list_)
        mem_list_->focus();
}

void App::memory_format_chosen(int f)
{
    mem_format_ = f;
    fill_memory();
}

void App::fill_memory()
{
    if (!mem_list_)
        return;
    mem_list_->clear();
    if (!dbg_ || !frames_.size() || mem_expr_.empty()) {
        mem_list_->done();
        return;
    }
    const td::Arch* arch = dbg_->target()->arch();
    int unit = k_mem_unit[mem_format_];
    /* Addresses as VC6 showed them, without 0x: 8 digits, 12 for the
     * 64-bit ones (user space), 16 when needed. */
    int addr_digits = arch->ptr_size == 4 ? 8 : 12;
    if ((mem_addr_ >> 47) != 0)
        addr_digits = 16;
    int addr_w = addr_digits + 2;
    /* Cells per unit: hex digits and a space; ASCII and bytes add a
     * character each. */
    int unit_w = mem_format_ == MEM_CHAR    ? 1
                 : mem_format_ == MEM_REAL4 ? 15
                 : mem_format_ == MEM_REAL8 ? 24
                                            : unit * 2 + 1;
    int extra = mem_format_ == MEM_BYTE ? 1 : 0;
    int room = mem_list_->width() - addr_w - 3;
    int per = room / (unit_w + extra);
    if (per < 1)
        per = 1;
    /* A power of two, so rows start at round addresses. */
    int p2 = 1;
    while (p2 * 2 <= per && p2 < 64)
        p2 *= 2;
    per = p2;
    mem_per_row_ = per * unit;
    int rows = mem_list_->visible_rows();
    size_t n = (size_t)mem_per_row_ * (size_t)rows;
    int w[3];
    w[0] = addr_w;
    w[1] = per * unit_w + 1;
    w[2] = per * extra + 1;
    mem_list_->set_columns(mem_format_ == MEM_BYTE ? 3 : 2, 0, w);
    /* The bytes, and which could be read. */
    unsigned char* buf = (unsigned char*)malloc(n ? n : 1);
    char* ok = (char*)malloc(n ? n : 1);
    if (!buf || !ok) {
        free(buf);
        free(ok);
        mem_list_->done();
        return;
    }
    size_t i;
    for (i = 0; i < n; i += 16) {
        size_t k = n - i < 16 ? n - i : 16;
        bool got = dbg_->target()->read(mem_addr_ + i, buf + i, k);
        size_t j;
        for (j = 0; j < k; j++)
            ok[i + j] = got ? 1 : 0;
        /* Part of it unmapped: byte by byte. */
        if (!got)
            for (j = 0; j < k; j++)
                ok[i + j] = dbg_->target()->read(mem_addr_ + i + j, buf + i + j, 1) ? 1 : 0;
    }
    if (mem_prev_stop_ != stop_serial_) {
        mem_prev_.clear();
        for (i = 0; i < (size_t)mem_cur_.size(); i++)
            mem_prev_.push_back(mem_cur_[(int)i]);
        mem_prev_addr_ = mem_cur_addr_;
        mem_prev_stop_ = stop_serial_;
    }
    mem_cur_.clear();
    for (i = 0; i < n; i++)
        mem_cur_.push_back(ok[i] ? buf[i] : 0);
    mem_cur_addr_ = mem_addr_;
    int r;
    for (r = 0; r < rows; r++) {
        td::Addr ra = mem_addr_ + (td::Addr)r * (td::Addr)mem_per_row_;
        td::Text at, data, text;
        add_hex_digits(&at, ra, addr_digits);
        int u;
        for (u = 0; u < per; u++) {
            size_t off = (size_t)r * (size_t)mem_per_row_ + (size_t)(u * unit);
            bool readable = true, changed = false;
            int b;
            for (b = 0; b < unit; b++) {
                size_t o = off + (size_t)b;
                if (!ok[o])
                    readable = false;
                td::Addr ba = mem_addr_ + o;
                if (ba >= mem_prev_addr_ && ba - mem_prev_addr_ < (td::Addr)mem_prev_.size() &&
                    mem_prev_[(int)(ba - mem_prev_addr_)] != buf[o])
                    changed = true;
            }
            if (u && mem_format_ != MEM_CHAR)
                data.add(" ");
            if (changed && readable)
                data.add(LIST_CHANGED);
            if (!readable) {
                int k;
                for (k = 0; k < (mem_format_ == MEM_CHAR ? 1 : unit_w - 1); k++)
                    data.add("?");
            } else if (mem_format_ == MEM_CHAR) {
                data.add_char(buf[off] >= 32 && buf[off] < 127 ? (char)buf[off] : '.');
            } else {
                uint64_t v = 0;
                for (b = 0; b < unit; b++)
                    v = arch->big_endian ? (v << 8) | buf[off + b]
                                         : v | ((uint64_t)buf[off + b] << (8 * b));
                if (mem_format_ == MEM_REAL4 || mem_format_ == MEM_REAL8) {
                    char f[40];
                    if (unit == 4) {
                        uint32_t x = (uint32_t)v;
                        float fl;
                        memcpy(&fl, &x, 4);
                        snprintf(f, sizeof(f), "%*.7g", unit_w - 1, (double)fl);
                    } else {
                        double d;
                        memcpy(&d, &v, 8);
                        snprintf(f, sizeof(f), "%*.16g", unit_w - 1, d);
                    }
                    data.add(f);
                } else {
                    add_hex_digits(&data, v, unit * 2);
                }
            }
            if (changed && readable)
                data.add(LIST_PLAIN);
            if (mem_format_ == MEM_BYTE)
                text.add_char(!ok[off] ? '?' : buf[off] >= 32 && buf[off] < 127 ? (char)buf[off]
                                                                                 : '.');
        }
        int row = mem_list_->add_row();
        mem_list_->set_cell(row, 0, at.c_str());
        mem_list_->set_cell(row, 1, data.c_str());
        if (mem_format_ == MEM_BYTE)
            mem_list_->set_cell(row, 2, text.c_str());
    }
    free(buf);
    free(ok);
    mem_list_->done();
}

/* ---- QuickWatch ---- */

void App::show_quickwatch()
{
    /* The selection, or the word at the caret. */
    Doc* d = doc();
    ttr::Str word;
    if (d && d->view.has_selection()) {
        size_t n = 0;
        char* s = d->view.copy(&n);
        if (s && n && !memchr(s, '\n', n))
            word = s;
        free(s);
    } else if (d) {
        te::Pos c = d->view.cursor();
        const char* line = d->text.line(c.line);
        size_t len = d->text.length(c.line);
        size_t a = c.col, b = c.col;
        while (a > 0 && (isalnum((unsigned char)line[a - 1]) || line[a - 1] == '_'))
            a--;
        while (b < len && (isalnum((unsigned char)line[b]) || line[b] == '_'))
            b++;
        if (b > a && b - a < 256) {
            char w[256];
            memcpy(w, line + a, b - a);
            w[b - a] = 0;
            word = w;
        }
    }
    if (!quick_dlg_) {
        Arg a[1];
        XtSetArg(a[0], XmNautoUnmanage, False);
        quick_dlg_ = XmCreateFormDialog(top_, (char*)"quickWatch", a, 1);
        XtVaSetValues(XtParent(quick_dlg_), XmNtitle, "QuickWatch", (char*)0);
        XtVaSetValues(quick_dlg_, XmNmarginWidth, 8, XmNmarginHeight, 8, (char*)0);
        Widget buttons = XtVaCreateManagedWidget("buttons", xmRowColumnWidgetClass, quick_dlg_,
                                                 XmNorientation, XmVERTICAL, XmNspacing, 4,
                                                 XmNpacking, XmPACK_COLUMN,
                                                 XmNtopAttachment, XmATTACH_FORM,
                                                 XmNrightAttachment, XmATTACH_FORM, (char*)0);
        static const char* const names[3] = { "Recalculate", "Add Watch", "Close" };
        Widget b[3];
        int i;
        for (i = 0; i < 3; i++) {
            g_quick_hooks[i].app = this;
            g_quick_hooks[i].which = i;
            b[i] = XtVaCreateManagedWidget(names[i], xmPushButtonWidgetClass, buttons, (char*)0);
            XtAddCallback(b[i], XmNactivateCallback, quick_cb, (XtPointer)&g_quick_hooks[i]);
        }
        Widget label = XtVaCreateManagedWidget("Expression:", xmLabelWidgetClass, quick_dlg_,
                                               XmNtopAttachment, XmATTACH_FORM,
                                               XmNleftAttachment, XmATTACH_FORM, (char*)0);
        quick_expr_ = XtVaCreateManagedWidget("expression", xmTextFieldWidgetClass, quick_dlg_,
                                              XmNcolumns, 50, XmNtopAttachment, XmATTACH_WIDGET,
                                              XmNtopWidget, label,
                                              XmNleftAttachment, XmATTACH_FORM,
                                              XmNrightAttachment, XmATTACH_WIDGET,
                                              XmNrightWidget, buttons, XmNrightOffset, 8,
                                              (char*)0);
        XtAddCallback(quick_expr_, XmNactivateCallback, quick_enter_cb, (XtPointer)this);
        Widget cur = XtVaCreateManagedWidget("Current value:", xmLabelWidgetClass, quick_dlg_,
                                             XmNtopAttachment, XmATTACH_WIDGET,
                                             XmNtopWidget, quick_expr_, XmNtopOffset, 6,
                                             XmNleftAttachment, XmATTACH_FORM, (char*)0);
        quick_list_ = make_list(quick_dlg_, "values", -1);
        static const char* const titles[3] = { "Name", "Value", "Type" };
        static const int widths[3] = { 22, 36, 20 };
        quick_list_->set_columns(3, titles, widths);
        XtVaSetValues(quick_list_->grid(), TwNcolumns, 84, TwNrows, 14, (char*)0);
        XtVaSetValues(quick_list_->widget(), XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, cur,
                      XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
                      XmNbottomAttachment, XmATTACH_FORM, (char*)0);
        XtVaSetValues(quick_dlg_, XmNdefaultButton, b[0], XmNcancelButton, b[2], (char*)0);
    }
    if (!word.empty())
        quick_text_ = word;
    XmTextFieldSetString(quick_expr_, (char*)quick_text_.c_str());
    XtManageChild(quick_dlg_);
    fill_quickwatch();
    XmProcessTraversal(quick_expr_, XmTRAVERSE_CURRENT);
    /* Selected, so typing replaces it. */
    XmTextPosition end = (XmTextPosition)quick_text_.length();
    XmTextFieldSetInsertionPosition(quick_expr_, end);
    XmTextFieldSetSelection(quick_expr_, 0, end, XtLastTimestampProcessed(XtDisplay(top_)));
}

void App::fill_quickwatch()
{
    if (!quick_list_)
        return;
    quick_list_->clear();
    const td::Frame* f = frame();
    if (f && !quick_text_.empty()) {
        ttr::Vector<td::Value> vals;
        vals.push_back(dbg_->evaluate(*f, quick_text_.c_str()));
        quick_tree_.set(dbg_, vals, stop_serial_, "quick");
        quick_tree_.fill(quick_list_, list_colors());
    } else if (!quick_text_.empty()) {
        int r = quick_list_->add_row();
        quick_list_->set_cell(r, 0, quick_text_.c_str());
        quick_list_->set_cell(r, 1, "<the program is not stopped>", list_colors().dim);
    }
    quick_list_->done();
}

void App::quickwatch_button(int which)
{
    char* s = XmTextFieldGetString(quick_expr_);
    quick_text_ = s ? s : "";
    XtFree(s);
    if (which == 0) {
        fill_quickwatch();
    } else if (which == 1) {
        add_watch(quick_text_.c_str());
        XtUnmanageChild(quick_dlg_);
        if (debug_panes_.size() > 0)
            show_pane(debug_panes_[0]);     /* Watch */
    } else {
        XtUnmanageChild(quick_dlg_);
    }
}

/* ---- the lists' input ---- */

namespace {

ValueTree* tree_of(CellList* l, CellList* watch, CellList* vars, CellList* quick, ValueTree* wt,
                   ValueTree* vt, ValueTree* qt)
{
    return l == watch ? wt : l == vars ? vt : l == quick ? qt : 0;
}

} /* namespace */

void App::list_toggle(CellList* l, int row)
{
    ValueTree* t = tree_of(l, watch_list_, vars_list_, quick_list_, &watch_tree_, &vars_tree_,
                           &quick_tree_);
    if (!t || !t->has_row(row))
        return;
    t->toggle(row);
    l->clear();
    t->fill(l, list_colors());
    if (l == watch_list_)
        l->add_row();
    l->done();
}

void App::list_activate(CellList* l, int row)
{
    if (l == stack_list_) {
        frame_chosen(row);
        return;
    }
    if (l == threads_list_) {
        thread_chosen(row);
        return;
    }
    if (l == watch_list_) {
        /* A watch, or the empty row: edit it. */
        int w = watch_index(row);
        if (w >= 0)
            l->begin_edit(row, 0, watches_[w].c_str());
        else if (row == l->row_count() - 1)
            l->begin_edit(row, 0, "");
        else
            list_toggle(l, row);
        return;
    }
    list_toggle(l, row);
}

bool App::list_key(CellList* l, TwGridCallbackStruct* cb)
{
    KeySym ks = cb->keysym;
    unsigned state = cb->state;
    if (global_key(ks, state) || debug_key(ks, state))
        return true;
    /* The grid takes Escape before the dialog's Cancel would see it. */
    if (l == quick_list_ && ks == XK_Escape) {
        quickwatch_button(2);
        return true;
    }
    bool ctrl = (state & ControlMask) != 0, alt = (state & Mod1Mask) != 0;
    if (l == mem_list_ && dbg_) {
        int rows = mem_list_->visible_rows();
        td::Addr step = 0;
        bool back = false;
        switch (ks) {
        case XK_Up: case XK_KP_Up: step = (td::Addr)mem_per_row_; back = true; break;
        case XK_Down: case XK_KP_Down: step = (td::Addr)mem_per_row_; break;
        case XK_Prior: case XK_KP_Prior: step = (td::Addr)(mem_per_row_ * rows); back = true; break;
        case XK_Next: case XK_KP_Next: step = (td::Addr)(mem_per_row_ * rows); break;
        default: break;
        }
        if (step) {
            mem_addr_ = back ? (mem_addr_ >= step ? mem_addr_ - step : 0) : mem_addr_ + step;
            fill_memory();
            return true;
        }
        return false;
    }
    if (l != watch_list_ || ctrl || alt)
        return false;
    int row = l->selected();
    if (row < 0)
        return false;
    int w = watch_index(row);
    bool blank = row == l->row_count() - 1;
    if (ks == XK_Delete || ks == XK_KP_Delete) {
        if (w >= 0) {
            watches_.remove_at(w);
            save_settings();
            fill_watch();
        }
        return true;
    }
    if (ks == XK_F2 || ks == XK_Insert) {
        if (w >= 0 && ks == XK_F2)
            l->begin_edit(row, 0, watches_[w].c_str());
        else
            l->begin_edit(l->row_count() - 1, 0, "");
        return true;
    }
    if (cb->text_len > 0 && (unsigned char)cb->text[0] > 32 && cb->text[0] != '+' &&
        cb->text[0] != '-' && (w >= 0 || blank)) {
        char t[65];
        memcpy(t, cb->text, (size_t)cb->text_len);
        t[cb->text_len] = 0;
        l->begin_edit(row, 0, t);
        return true;
    }
    return false;
}

/* Which watch a row of Watch is: -1 for the empty row and for rows of
 * members under a watch. */
int App::watch_index(int row) const
{
    if (!frame())
        return row < watches_.size() ? row : -1;
    if (!watch_tree_.has_row(row))
        return -1;
    int w = watch_tree_.root_of(row);
    int r;
    for (r = 0; watch_tree_.has_row(r); r++)
        if (watch_tree_.root_of(r) == w)
            return r == row ? w : -1;
    return -1;
}

void App::list_edited(CellList* l, int row, const char* text)
{
    if (l != watch_list_)
        return;
    int w = watch_index(row);
    /* Spaces around it are not part of it. */
    while (*text == ' ')
        text++;
    char t[512];
    strncpy(t, text, sizeof(t) - 1);
    t[sizeof(t) - 1] = 0;
    size_t n = strlen(t);
    while (n && t[n - 1] == ' ')
        t[--n] = 0;
    for (n = 0; t[n]; n++)
        if (t[n] == ';')
            t[n] = ',';
    if (w >= 0) {
        if (*t)
            watches_[w] = t;
        else
            watches_.remove_at(w);
    } else if (*t) {
        watches_.push_back(ttr::Str(t));
    } else {
        return;
    }
    save_settings();
    fill_watch();
}

void App::list_resized(CellList* l)
{
    if ((l == watch_list_ || l == vars_list_ || l == quick_list_) && !l->user_sized()) {
        /* Name, Value and Type share the width until the user drags them. */
        int w = l->width();
        int widths[3];
        widths[0] = w * 3 / 10;
        if (widths[0] > 30)
            widths[0] = 30;
        if (widths[0] < 10)
            widths[0] = 10;
        widths[1] = (w - widths[0]) * 2 / 3;
        if (widths[1] < 12)
            widths[1] = 12;
        widths[2] = 16;
        static const char* const titles[3] = { "Name", "Value", "Type" };
        l->set_columns(3, titles, widths);
    }
    if (l == regs_list_)
        fill_registers();
    else if (l == mem_list_)
        fill_memory();
}

} /* namespace ttrcode */
