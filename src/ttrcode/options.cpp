/*
 * ttrcode's Tools > Options dialog: the editor's and the terminal's fonts
 * (the font chooser every ttr program shares) and text, tabs and
 * indentation, what it shows (whitespace, the current line, line numbers,
 * a column marker) and the caret. OK and Apply apply at once and save
 * to ~/.config/ttrcode/settings.
 */
#include "app.h"

#include "../../tw/grid_widget.h"
#include "../../tx/fontchooser.h"
#include "../../tx/dialog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/Separator.h>
#include <Xm/TextF.h>
#include <Xm/ToggleB.h>

namespace ttrcode {

namespace {

enum { OPT_OK, OPT_APPLY, OPT_CANCEL };

const char* const k_shapes[3] = { "bar", "block", "underline" };
const char* const k_shape_labels[3] = { "Bar", "Block", "Underline" };
/* Text: tw's sharp (the font's pixels) or smooth (distance fields, GL). */
const char* const k_texts[3] = { "auto", "sharp", "smooth" };
const char* const k_text_labels[3] = { "Automatic", "Sharp", "Smooth" };
/* TwGrid's renderers, as ttrt's settings name them. */
const char* const k_renderers[4] = { "auto", "core", "soft", "gl" };
const char* const k_renderer_labels[4] = { "Automatic", "Core X11", "Software (TrueColor)",
                                           "OpenGL" };

/* An option menu of n labels into buttons[]. */
Widget option_menu(Widget parent, const char* name, const char* const* labels, int n,
                   Widget* buttons)
{
    Widget pd = XmCreatePulldownMenu(parent, (char*)name, 0, 0);
    for (int i = 0; i < n; i++)
        buttons[i] = XtVaCreateManagedWidget(labels[i], xmPushButtonWidgetClass, pd, (char*)0);
    Arg a[1];
    XtSetArg(a[0], XmNsubMenuId, pd);
    Widget m = XmCreateOptionMenu(parent, (char*)name, a, 1);
    XtManageChild(m);
    return m;
}

int option_index(Widget menu, const Widget* buttons, int n)
{
    Widget hist = 0;
    XtVaGetValues(menu, XmNmenuHistory, &hist, (char*)0);
    for (int i = 0; i < n; i++)
        if (hist == buttons[i])
            return i;
    return 0;
}

int text_index(const char* v)
{
    for (int i = 0; i < 3; i++)
        if (v && strcmp(v, k_texts[i]) == 0)
            return i;
    return 0;
}

/* TwGrid keeps the string's pointer: one that lives. */
const char* text_value(const char* t)
{
    return k_texts[text_index(t)];
}

struct Hook {
    App* app;
    int which;
};

void button_cb(Widget, XtPointer data, XtPointer)
{
    Hook* h = (Hook*)data;
    h->app->options_button(h->which);
}

/* A row: a label of fixed width, then whatever goes in it. */
Widget row(Widget parent, const char* label)
{
    Widget r = XtVaCreateManagedWidget("row", xmRowColumnWidgetClass, parent,
                                       XmNorientation, XmHORIZONTAL, XmNmarginHeight, 0,
                                       XmNmarginWidth, 0, XmNspacing, 6, (char*)0);
    XmString s = XmStringCreateLocalized((char*)label);
    XtVaCreateManagedWidget("label", xmLabelWidgetClass, r, XmNlabelString, s,
                            XmNwidth, 130, XmNrecomputeSize, False,
                            XmNalignment, XmALIGNMENT_END, (char*)0);
    XmStringFree(s);
    return r;
}

Widget field(Widget parent, int columns)
{
    return XtVaCreateManagedWidget("field", xmTextFieldWidgetClass, parent, XmNcolumns, columns,
                                   (char*)0);
}

Widget toggle(Widget parent, const char* label)
{
    return XtVaCreateManagedWidget(label, xmToggleButtonWidgetClass, parent, (char*)0);
}

void note(Widget parent, const char* text)
{
    XtVaCreateManagedWidget(text, xmLabelWidgetClass, parent, (char*)0);
}

void set_number(Widget f, int v)
{
    char b[32];
    sprintf(b, "%d", v);
    XmTextFieldSetString(f, b);
}

int get_number(Widget f, int fallback, int lo, int hi)
{
    char* s = XmTextFieldGetString(f);
    int v = s && *s ? atoi(s) : fallback;
    XtFree(s);
    return v < lo ? lo : v > hi ? hi : v;
}

} /* namespace */

/* The options in opt_ on the editor, every document and the terminal. */
void App::apply_look()
{
    EditLook l;
    l.highlight_line = opt_.highlight_line;
    l.ruler = opt_.ruler;
    l.blink = opt_.cursor_blink;
    l.cursor = strcmp(opt_.cursor_shape, "block") == 0       ? tw::CURSOR_BLOCK
               : strcmp(opt_.cursor_shape, "underline") == 0 ? tw::CURSOR_UNDERLINE
                                                             : tw::CURSOR_BAR;
    src_.set_look(l);
    src_.set_line_numbers(opt_.line_numbers);
    for (int i = 0; i < docs_.size(); i++) {
        te::View& v = docs_[i]->view;
        v.tab_width = opt_.tab_width;
        v.indent_width = opt_.indent_width;
        v.insert_spaces = opt_.insert_spaces;
        v.auto_indent = opt_.auto_indent;
        v.show_whitespace = opt_.show_whitespace;
    }
    set_scheme(scheme_.name);       /* the caret's colour */
    if (doc())
        doc()->view.follow();
    if (grid_)
        TwGridDamage(grid_);
}

/* TwGrid keeps the font name's pointer: a copy that lives until the
 * next one. */
void App::set_font(const char* xlfd)
{
    const char* f = xlfd && *xlfd ? xlfd : tt::pick_font(XtDisplay(top_));
    char* copy = (char*)malloc(strlen(f) + 1);
    if (!copy)
        return;
    strcpy(copy, f);
    XtVaSetValues(grid_, TwNfont, copy, (char*)0);
    /* The debugger's panes follow the editor. */
    CellList* lists[7] = { stack_list_, regs_list_, threads_list_, watch_list_, vars_list_,
                           mem_list_, quick_list_ };
    for (int i = 0; i < 7; i++)
        if (lists[i] && lists[i]->grid())
            XtVaSetValues(lists[i]->grid(), TwNfont, copy, (char*)0);
    /* The terminal follows unless it has a font of its own. */
    if (term_ && !(term_font_ && *term_font_))
        term_->set_font(copy);
    free(font_);
    font_ = copy;
    opt_.font = font_;
}

void App::set_terminal_font(const char* xlfd)
{
    const char* v = xlfd ? xlfd : "";
    char* copy = (char*)malloc(strlen(v) + 1);
    if (!copy)
        return;
    strcpy(copy, v);
    free(term_font_);
    term_font_ = copy;
    opt_.terminal_font = term_font_;
    if (term_)
        term_->set_font(*term_font_ ? term_font_ : font_ ? font_ : opt_.font);
}

void App::set_text_rendering(const char* editor, const char* terminal)
{
    text_ = text_value(editor);
    term_text_ = text_value(terminal);
    opt_.text_rendering = text_.c_str();
    opt_.terminal_text_rendering = term_text_.c_str();
    if (grid_)
        XtVaSetValues(grid_, TwNtextRendering, text_value(editor), (char*)0);
    if (term_)
        term_->set_text_rendering(text_value(terminal));
}

void App::show_options()
{
    if (!opt_dlg_) {
        static Hook hooks[3];
        Arg a[2];
        XtSetArg(a[0], XmNautoUnmanage, False);
        opt_dlg_ = XmCreateFormDialog(top_, (char*)"optionsDialog", a, 1);
        tx::fixed_size(opt_dlg_);
        XtVaSetValues(XtParent(opt_dlg_), XmNtitle, "Options", (char*)0);
        XtVaSetValues(opt_dlg_, XmNmarginWidth, 10, XmNmarginHeight, 10, (char*)0);
        Widget page = XtVaCreateManagedWidget("page", xmRowColumnWidgetClass, opt_dlg_,
                                              XmNorientation, XmVERTICAL, XmNspacing, 4,
                                              XmNtopAttachment, XmATTACH_FORM,
                                              XmNleftAttachment, XmATTACH_FORM,
                                              XmNrightAttachment, XmATTACH_FORM, (char*)0);
        Widget r = row(page, "Editor font:");
        opt_font_ = new tx::FontChooser(r, "font", tx::FontChooser::FIXED |
                                                       tx::FontChooser::UNICODE |
                                                       tx::FontChooser::AUTOMATIC);
        opt_font_->set_automatic_preview(tt::pick_font(XtDisplay(top_)));
        r = row(page, "Renderer:");
        opt_renderer_ = option_menu(r, "renderer", k_renderer_labels, 4, opt_renderers_);
        note(r, "(next start; the terminal too)");
        r = row(page, "Text:");
        opt_text_ = option_menu(r, "text", k_text_labels, 3, opt_texts_);
        note(r, "smooth needs OpenGL; automatic: smooth from 18 pixels");
        r = row(page, "Terminal font:");
        opt_term_font_ = new tx::FontChooser(r, "terminalFont", tx::FontChooser::FIXED |
                                                                    tx::FontChooser::UNICODE |
                                                                    tx::FontChooser::AUTOMATIC);
        opt_term_font_->set_automatic_label("Same as the editor");
        r = row(page, "Terminal text:");
        opt_term_text_ = option_menu(r, "terminalText", k_text_labels, 3, opt_term_texts_);

        r = row(page, "Tab width:");
        opt_tab_ = field(r, 3);
        note(r, "  Indent by:");
        opt_indent_ = field(r, 3);
        opt_spaces_ = toggle(row(page, ""), "Tab inserts spaces");
        opt_auto_ = toggle(row(page, ""), "Keep the indentation of the line above");

        opt_numbers_ = toggle(row(page, "Show:"), "Line numbers");
        opt_line_ = toggle(row(page, ""), "The current line");
        opt_ws_ = toggle(row(page, ""), "Spaces and tabs");
        r = row(page, "Column marker:");
        opt_ruler_ = field(r, 4);
        note(r, "(0: none)");

        r = row(page, "Cursor:");
        Widget pd = XmCreatePulldownMenu(r, (char*)"shapes", 0, 0);
        for (int i = 0; i < 3; i++)
            opt_shapes_[i] = XtVaCreateManagedWidget(k_shape_labels[i], xmPushButtonWidgetClass,
                                                     pd, (char*)0);
        Arg oa[1];
        XtSetArg(oa[0], XmNsubMenuId, pd);
        opt_shape_ = XmCreateOptionMenu(r, (char*)"shape", oa, 1);
        XtManageChild(opt_shape_);
        opt_blink_ = toggle(r, "Blink");
        r = row(page, "Cursor color:");
        opt_ccolor_ = field(r, 9);
        note(r, "#rrggbb; empty: the color scheme's");

        Widget sep = XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, opt_dlg_,
                                             XmNtopAttachment, XmATTACH_WIDGET,
                                             XmNtopWidget, page, XmNtopOffset, 10,
                                             XmNleftAttachment, XmATTACH_FORM,
                                             XmNrightAttachment, XmATTACH_FORM, (char*)0);
        Widget buttons = XtVaCreateManagedWidget("buttons", xmRowColumnWidgetClass, opt_dlg_,
                                                 XmNorientation, XmHORIZONTAL, XmNspacing, 6,
                                                 XmNmarginWidth, 0, XmNmarginHeight, 0,
                                                 XmNtopAttachment, XmATTACH_WIDGET,
                                                 XmNtopWidget, sep, XmNtopOffset, 8,
                                                 XmNrightAttachment, XmATTACH_FORM,
                                                 XmNbottomAttachment, XmATTACH_FORM, (char*)0);
        static const char* const names[3] = { "OK", "Apply", "Cancel" };
        static const int which[3] = { OPT_OK, OPT_APPLY, OPT_CANCEL };
        Widget ok = 0, cancel = 0;
        for (int i = 0; i < 3; i++) {
            Widget b = XtVaCreateManagedWidget(names[i], xmPushButtonWidgetClass, buttons,
                                               (char*)0);
            hooks[i].app = this;
            hooks[i].which = which[i];
            XtAddCallback(b, XmNactivateCallback, button_cb, (XtPointer)&hooks[i]);
            if (which[i] == OPT_OK)
                ok = b;
            if (which[i] == OPT_CANCEL)
                cancel = b;
        }
        XtVaSetValues(opt_dlg_, XmNdefaultButton, ok, XmNcancelButton, cancel, (char*)0);
    }
    /* The current values. */
    opt_font_->set(font_ ? font_ : "");
    opt_term_font_->set_automatic_preview(font_ ? font_ : opt_.font);
    opt_term_font_->set(term_font_ ? term_font_ : "");
    int rp = 0;
    for (int i = 0; i < 4; i++)
        if (opt_.renderer && strcmp(opt_.renderer, k_renderers[i]) == 0)
            rp = i;
    XtVaSetValues(opt_renderer_, XmNmenuHistory, opt_renderers_[rp], (char*)0);
    XtVaSetValues(opt_text_, XmNmenuHistory, opt_texts_[text_index(opt_.text_rendering)],
                  (char*)0);
    XtVaSetValues(opt_term_text_, XmNmenuHistory,
                  opt_term_texts_[text_index(opt_.terminal_text_rendering)], (char*)0);
    set_number(opt_tab_, opt_.tab_width);
    set_number(opt_indent_, opt_.indent_width);
    XmToggleButtonSetState(opt_spaces_, opt_.insert_spaces, False);
    XmToggleButtonSetState(opt_auto_, opt_.auto_indent, False);
    XmToggleButtonSetState(opt_numbers_, opt_.line_numbers, False);
    XmToggleButtonSetState(opt_line_, opt_.highlight_line, False);
    XmToggleButtonSetState(opt_ws_, opt_.show_whitespace, False);
    set_number(opt_ruler_, opt_.ruler);
    int sh = 0;
    for (int i = 0; i < 3; i++)
        if (strcmp(opt_.cursor_shape, k_shapes[i]) == 0)
            sh = i;
    XtVaSetValues(opt_shape_, XmNmenuHistory, opt_shapes_[sh], (char*)0);
    XmToggleButtonSetState(opt_blink_, opt_.cursor_blink, False);
    XmTextFieldSetString(opt_ccolor_, (char*)opt_.cursor_color);
    XtManageChild(opt_dlg_);
}

void App::options_button(int which)
{
    if (which == OPT_CANCEL) {
        XtUnmanageChild(opt_dlg_);
        return;
    }
    ttr::Str f = opt_font_->get();
    if (strcmp(f.c_str(), font_ ? font_ : "") != 0)
        set_font(f.c_str());
    ttr::Str tf = opt_term_font_->get();
    if (strcmp(tf.c_str(), term_font_ ? term_font_ : "") != 0)
        set_terminal_font(tf.c_str());
    /* Takes effect at the next start: the grids chose theirs when made. */
    const char* rend = k_renderers[option_index(opt_renderer_, opt_renderers_, 4)];
    if (!opt_.renderer || strcmp(rend, opt_.renderer) != 0) {
        opt_.renderer = rend;
        renderer_chosen_ = true;
    }
    set_text_rendering(k_texts[option_index(opt_text_, opt_texts_, 3)],
                       k_texts[option_index(opt_term_text_, opt_term_texts_, 3)]);
    opt_.tab_width = get_number(opt_tab_, opt_.tab_width, 1, 32);
    opt_.indent_width = get_number(opt_indent_, opt_.indent_width, 0, 32);
    opt_.insert_spaces = XmToggleButtonGetState(opt_spaces_) != False;
    opt_.auto_indent = XmToggleButtonGetState(opt_auto_) != False;
    opt_.line_numbers = XmToggleButtonGetState(opt_numbers_) != False;
    opt_.highlight_line = XmToggleButtonGetState(opt_line_) != False;
    opt_.show_whitespace = XmToggleButtonGetState(opt_ws_) != False;
    opt_.ruler = get_number(opt_ruler_, opt_.ruler, 0, 1000);
    Widget hist = 0;
    XtVaGetValues(opt_shape_, XmNmenuHistory, &hist, (char*)0);
    for (int i = 0; i < 3; i++)
        if (hist == opt_shapes_[i])
            cursor_shape_ = k_shapes[i];
    opt_.cursor_shape = cursor_shape_.c_str();
    opt_.cursor_blink = XmToggleButtonGetState(opt_blink_) != False;
    char* c = XmTextFieldGetString(opt_ccolor_);
    th::Color dummy;
    cursor_color_ = c && *c && th::parse_hex(c, &dummy) ? c : "";
    XtFree(c);
    opt_.cursor_color = cursor_color_.c_str();
    apply_look();
    save_settings();
    if (which == OPT_OK)
        XtUnmanageChild(opt_dlg_);
}

} /* namespace ttrcode */
