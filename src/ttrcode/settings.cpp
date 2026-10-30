/*
 * ttrcode's Tools > Settings, in pages as ttr's and ttrt's (tx/pages.h):
 * the editor (tabs and indentation, what it shows, the caret), appearance
 * (the UI scale, the editor's and the terminal's fonts, the renderer) and
 * colours (the text colour scheme and the UI theme). OK and Apply apply
 * at once, but for the UI scale and the renderer, and save to
 * ~/.config/ttrcode/settings.
 */
#include "app.h"

#include "../../tw/grid_widget.h"
#include "../../tx/fontchooser.h"
#include "../../tx/scale.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Xm/TextF.h>
#include <Xm/ToggleB.h>

namespace ttrcode {

namespace {

enum { PAGE_EDITOR, PAGE_APPEARANCE, PAGE_COLORS, PAGE_COUNT };

const char* const k_shapes[3] = { "bar", "block", "underline" };
const char* const k_shape_labels[3] = { "Bar", "Block", "Underline" };
/* Text: tw's sharp (the font's pixels) or smooth (distance fields, GL). */
const char* const k_texts[3] = { "auto", "sharp", "smooth" };
const char* const k_text_labels[3] = { "Automatic", "Sharp", "Smooth" };
/* TwGrid's renderers, as ttrt's settings name them. */
const char* const k_renderers[4] = { "auto", "core", "soft", "gl" };
const char* const k_renderer_labels[4] = { "Automatic", "Core X11", "Software (TrueColor)",
                                           "OpenGL" };

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

/* The drop-down's choice, within 0..n-1. */
int chosen_in(Widget w, int n)
{
    int i = tx::chosen(w);
    return i >= 0 && i < n ? i : 0;
}

void pressed(void* data, int which)
{
    ((App*)data)->settings_button(which);
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

/* font_ is the font chosen, "" for the automatic one (auto_font_);
 * TwGrid keeps the name's pointer, so what it is given lives until the
 * next one. */
void App::set_font(const char* xlfd)
{
    const char* v = xlfd ? xlfd : "";
    char* copy = (char*)malloc(strlen(v) + 1);
    if (!copy)
        return;
    strcpy(copy, v);
    const char* f = *copy ? copy : auto_font_.c_str();
    XtVaSetValues(grid_, TwNfont, f, (char*)0);
    /* The debugger's panes follow the editor. */
    CellList* lists[8] = { stack_list_, regs_list_, threads_list_, watch_list_, vars_list_,
                           mem_list_, quick_list_, dis_list_ };
    for (int i = 0; i < 8; i++)
        if (lists[i] && lists[i]->grid())
            XtVaSetValues(lists[i]->grid(), TwNfont, f, (char*)0);
    /* The terminal follows unless it has a font of its own. */
    if (term_ && !(term_font_ && *term_font_))
        term_->set_font(f);
    free(font_);
    font_ = copy;
    opt_.font = f;
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
        term_->set_font(*term_font_ ? term_font_ : opt_.font);
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

/* Made anew each time, from the current values. */
void App::show_settings()
{
    using tx::frame;
    using tx::note;
    using tx::row;
    settings_.destroy();
    delete opt_font_;
    delete opt_term_font_;
    static const char* const pages[PAGE_COUNT] = { "Editor", "Appearance", "Colors" };
    settings_.create(top_, "settingsDialog", "Settings", pages, PAGE_COUNT);
    int i;

    /* Editor */
    Widget pg = settings_.page(PAGE_EDITOR);
    Widget f = frame(pg, "Tabs and indentation");
    Widget r = row(f, "Tab width:");
    opt_tab_ = field(r, 3);
    note(r, "  Indent by:");
    opt_indent_ = field(r, 3);
    opt_spaces_ = toggle(row(f, ""), "Tab inserts spaces");
    opt_auto_ = toggle(row(f, ""), "Keep the indentation of the line above");
    f = frame(pg, "Show");
    opt_numbers_ = toggle(row(f, ""), "Line numbers");
    opt_line_ = toggle(row(f, ""), "The current line");
    opt_ws_ = toggle(row(f, ""), "Spaces and tabs");
    r = row(f, "Column marker:");
    opt_ruler_ = field(r, 4);
    note(r, "(0: none)");
    f = frame(pg, "Cursor");
    r = row(f, "Shape:");
    opt_shape_ = tx::drop_down(r, "shape", k_shape_labels, 3, 3, 10);
    opt_blink_ = toggle(r, "Blink");
    r = row(f, "Color:");
    opt_ccolor_ = field(r, 9);
    note(r, "#rrggbb; empty: the color scheme's");

    /* Appearance */
    pg = settings_.page(PAGE_APPEARANCE);
    f = frame(pg, "Size");
    r = row(f, "UI scale:");
    opt_scale_ = tx::scale_drop_down(r, opt_.ui_scale);
    note(r, "menus, dialogs and automatic fonts (next start)");
    f = frame(pg, "Fonts");
    r = row(f, "Editor:");
    opt_font_ = new tx::FontChooser(r, "font", tx::FontChooser::FIXED |
                                                   tx::FontChooser::UNICODE |
                                                   tx::FontChooser::AUTOMATIC);
    opt_font_->set_automatic_preview(auto_font_.c_str());
    r = row(f, "Editor text:");
    opt_text_ = tx::drop_down(r, "text", k_text_labels, 3, 3, 12);
    note(r, "smooth needs OpenGL; automatic: smooth from 18 pixels");
    r = row(f, "Terminal:");
    opt_term_font_ = new tx::FontChooser(r, "terminalFont", tx::FontChooser::FIXED |
                                                                tx::FontChooser::UNICODE |
                                                                tx::FontChooser::AUTOMATIC);
    opt_term_font_->set_automatic_label("Same as the editor");
    r = row(f, "Terminal text:");
    opt_term_text_ = tx::drop_down(r, "terminalText", k_text_labels, 3, 3, 12);
    f = frame(pg, "Drawing");
    r = row(f, "Renderer:");
    opt_renderer_ = tx::drop_down(r, "renderer", k_renderer_labels, 4, 4, 20);
    note(r, "(next start; the terminal too)");

    /* Colors */
    pg = settings_.page(PAGE_COLORS);
    f = frame(pg, "Colors");
    th::list_schemes(&schemes_);
    th::list_themes(&themes_);
    {
        const char** names = new const char*[schemes_.count() + themes_.count() + 2];
        for (i = 0; i < schemes_.count(); i++)
            names[i] = schemes_[i].name;
        r = row(f, "Color scheme:");
        opt_scheme_ = tx::drop_down(r, "scheme", names, schemes_.count(),
                                    schemes_.count() < 12 ? schemes_.count() : 12, 18);
        note(r, "the editor and the terminal; shared with ttrt");
        names[0] = "Same as ttr";
        for (i = 0; i < themes_.count(); i++)
            names[i + 1] = themes_[i].name;
        r = row(f, "UI theme:");
        opt_theme_ = tx::drop_down(r, "theme", names, themes_.count() + 1, 12, 18);
        note(r, "menus, panes and dialogs");
        delete [] names;
    }
    settings_.finish(0, tx::PagedDialog::OK | tx::PagedDialog::APPLY | tx::PagedDialog::CANCEL,
                     pressed, this);

    /* The current values. */
    opt_font_->set(font_ ? font_ : "");
    opt_term_font_->set_automatic_preview(opt_.font);
    opt_term_font_->set(term_font_ ? term_font_ : "");
    int rp = 0;
    for (i = 0; i < 4; i++)
        if (opt_.renderer && strcmp(opt_.renderer, k_renderers[i]) == 0)
            rp = i;
    tx::choose(opt_renderer_, rp);
    tx::choose(opt_text_, text_index(opt_.text_rendering));
    tx::choose(opt_term_text_, text_index(opt_.terminal_text_rendering));
    set_number(opt_tab_, opt_.tab_width);
    set_number(opt_indent_, opt_.indent_width);
    XmToggleButtonSetState(opt_spaces_, opt_.insert_spaces, False);
    XmToggleButtonSetState(opt_auto_, opt_.auto_indent, False);
    XmToggleButtonSetState(opt_numbers_, opt_.line_numbers, False);
    XmToggleButtonSetState(opt_line_, opt_.highlight_line, False);
    XmToggleButtonSetState(opt_ws_, opt_.show_whitespace, False);
    set_number(opt_ruler_, opt_.ruler);
    int sh = 0;
    for (i = 0; i < 3; i++)
        if (strcmp(opt_.cursor_shape, k_shapes[i]) == 0)
            sh = i;
    tx::choose(opt_shape_, sh);
    XmToggleButtonSetState(opt_blink_, opt_.cursor_blink, False);
    XmTextFieldSetString(opt_ccolor_, (char*)opt_.cursor_color);
    for (i = 0; i < schemes_.count(); i++)
        if (th::compare_names(schemes_[i].name, scheme_.name) == 0)
            tx::choose(opt_scheme_, i);
    tx::choose(opt_theme_, 0);
    for (i = 0; !theme_name_.empty() && i < themes_.count(); i++)
        if (th::compare_names(themes_[i].name, theme_name_.c_str()) == 0)
            tx::choose(opt_theme_, i + 1);
    XtManageChild(settings_.dialog());
}

void App::settings_button(int which)
{
    if (which == tx::PagedDialog::CANCEL) {
        XtUnmanageChild(settings_.dialog());
        return;
    }
    ttr::Str f = opt_font_->get();
    if (strcmp(f.c_str(), font_ ? font_ : "") != 0)
        set_font(f.c_str());
    ttr::Str tf = opt_term_font_->get();
    if (strcmp(tf.c_str(), term_font_ ? term_font_ : "") != 0)
        set_terminal_font(tf.c_str());
    /* Takes effect at the next start: the grids chose theirs when made. */
    const char* rend = k_renderers[chosen_in(opt_renderer_, 4)];
    if (!opt_.renderer || strcmp(rend, opt_.renderer) != 0) {
        opt_.renderer = rend;
        renderer_chosen_ = true;
    }
    set_text_rendering(k_texts[chosen_in(opt_text_, 3)], k_texts[chosen_in(opt_term_text_, 3)]);
    ui_scale_ = tx::scale_drop_down_value(opt_scale_);
    opt_.ui_scale = ui_scale_.c_str();
    opt_.tab_width = get_number(opt_tab_, opt_.tab_width, 1, 32);
    opt_.indent_width = get_number(opt_indent_, opt_.indent_width, 0, 32);
    opt_.insert_spaces = XmToggleButtonGetState(opt_spaces_) != False;
    opt_.auto_indent = XmToggleButtonGetState(opt_auto_) != False;
    opt_.line_numbers = XmToggleButtonGetState(opt_numbers_) != False;
    opt_.highlight_line = XmToggleButtonGetState(opt_line_) != False;
    opt_.show_whitespace = XmToggleButtonGetState(opt_ws_) != False;
    opt_.ruler = get_number(opt_ruler_, opt_.ruler, 0, 1000);
    cursor_shape_ = k_shapes[chosen_in(opt_shape_, 3)];
    opt_.cursor_shape = cursor_shape_.c_str();
    opt_.cursor_blink = XmToggleButtonGetState(opt_blink_) != False;
    char* c = XmTextFieldGetString(opt_ccolor_);
    th::Color dummy;
    cursor_color_ = c && *c && th::parse_hex(c, &dummy) ? c : "";
    XtFree(c);
    opt_.cursor_color = cursor_color_.c_str();
    int sc = tx::chosen(opt_scheme_);
    if (sc >= 0 && sc < schemes_.count() && th::compare_names(schemes_[sc].name, scheme_.name))
        set_scheme(schemes_[sc].name);
    int tm = tx::chosen(opt_theme_);
    ttr::Str theme = tm >= 1 && tm <= themes_.count() ? themes_[tm - 1].name : "";
    if (strcmp(theme.c_str(), theme_name_.c_str()) != 0)
        set_theme(theme.c_str());
    apply_look();
    save_settings();
    if (which == tx::PagedDialog::OK)
        XtUnmanageChild(settings_.dialog());
}

} /* namespace ttrcode */
