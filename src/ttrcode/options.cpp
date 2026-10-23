/*
 * ttrcode's Tools > Options dialog: the editor's font, tabs and
 * indentation, what it shows (whitespace, the current line, line numbers,
 * a column marker) and the caret. OK and Apply apply at once and save
 * to ~/.config/ttrcode/settings.
 */
#include "app.h"

#include "../../tw/grid_widget.h"

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
    if (term_)
        term_->set_font(copy);
    free(font_);
    font_ = copy;
    opt_.font = font_;
}

void App::show_options()
{
    if (!opt_dlg_) {
        static Hook hooks[3];
        Arg a[2];
        XtSetArg(a[0], XmNautoUnmanage, False);
        opt_dlg_ = XmCreateFormDialog(top_, (char*)"optionsDialog", a, 1);
        XtVaSetValues(XtParent(opt_dlg_), XmNtitle, "Options", (char*)0);
        XtVaSetValues(opt_dlg_, XmNmarginWidth, 10, XmNmarginHeight, 10, (char*)0);
        Widget page = XtVaCreateManagedWidget("page", xmRowColumnWidgetClass, opt_dlg_,
                                              XmNorientation, XmVERTICAL, XmNspacing, 4,
                                              XmNtopAttachment, XmATTACH_FORM,
                                              XmNleftAttachment, XmATTACH_FORM,
                                              XmNrightAttachment, XmATTACH_FORM, (char*)0);
        Widget r = row(page, "Font:");
        opt_font_ = field(r, 44);
        note(row(page, ""), "An X font name; empty: the best fixed-width font.");

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
    XmTextFieldSetString(opt_font_, (char*)(font_ ? font_ : ""));
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
    char* f = XmTextFieldGetString(opt_font_);
    if (strcmp(f ? f : "", font_ ? font_ : "") != 0)
        set_font(f);
    XtFree(f);
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
