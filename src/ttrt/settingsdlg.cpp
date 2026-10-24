#include "settingsdlg.h"

#include "../../tx/fontchooser.h"
#include "../../tx/dialog.h"

#include "../../tt/pane.h"
#include "../../tw/backgrounds.h"

#include "core/scale.h"
#include "core/settings.h"
#include "core/theme.h"
#include "shell/colorpicker.h"

#include <Xm/Xm.h>
#include <Xm/ComboBox.h>
#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/MessageB.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/Separator.h>
#include <Xm/ToggleB.h>
#include <X11/Xatom.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using ttr::Rgb;
using ttr::Str;
using ttr::scaled;

namespace ttrt {

static const int k_label_width = 130;

static const char* const k_color_labels[3] = {
    "Text:", "Background:", "Cursor:"
};
static const int k_color_settings[3] = {
    S_FOREGROUND, S_BACKGROUND, S_CURSOR_COLOR
};

static const char* const k_save_lines[] = {
    "500", "1000", "2000", "5000", "10000", "50000"
};
static const int k_nsave_lines = 6;

static const char* const k_bell_labels[] = {
    "Sound", "Flash the window", "Off"
};
static const char* const k_bell_values[] = { "audible", "visual", "off" };
static const int k_nbells = 3;

static const char* const k_renderer_labels[] = {
    "Automatic", "Core X11", "Software (TrueColor)", "OpenGL"
};
static const char* const k_renderer_values[] = { "auto", "core", "soft", "gl" };

/* Text: tw's sharp (the font's pixels) or smooth (distance fields, GL). */
static const char* const k_text_labels[] = {
    "Automatic", "Sharp", "Smooth"
};
static const char* const k_text_values[] = { "auto", "sharp", "smooth" };
static const int k_ntexts = 3;
static const int k_nrenderers = 4;

/* "None", then tw's animated backgrounds, named for people. */
static int background_choices(const char** labels, const char** values, int max)
{
    static char names[16][32];
    int n = 0;
    labels[n] = "None";
    values[n++] = "";
    const char* const* bg = tw::background_names();
    for (int i = 0; bg[i] && n < max && i < 16; i++) {
        snprintf(names[i], sizeof(names[i]), "%s", bg[i]);
        names[i][0] = (char)toupper((unsigned char)names[i][0]);
        labels[n] = names[i];
        values[n++] = bg[i];
    }
    return n;
}

static XmString xmstr(const char* s)
{
    return XmStringCreateLocalized((char*)s);
}

/* The list's selected position, from 0, or -1. */
static int selected_pos(Widget combo)
{
    Widget list = XtNameToWidget(combo, "*List");
    int* pos = 0;
    int n = 0;
    if (!list)
        return -1;
    Arg args[2];
    XtSetArg(args[0], XmNselectedPositions, &pos);
    XtSetArg(args[1], XmNselectedPositionCount, &n);
    XtGetValues(list, args, 2);
    return pos && n > 0 ? pos[0] - 1 : -1;
}

/* Select the entry equal to 'value' (or the first). */
static void select_item(Widget combo, const char* value)
{
    XmString s = xmstr(value);
    Widget list = XtNameToWidget(combo, "*List");
    if (list && XmListItemExists(list, s)) {
        XmComboBoxSelectItem(combo, s);
    } else if (list) {
        XmStringTable items = 0;
        int count = 0;
        Arg args[2];
        XtSetArg(args[0], XmNitems, &items);
        XtSetArg(args[1], XmNitemCount, &count);
        XtGetValues(list, args, 2);
        if (count > 0)
            XmComboBoxSelectItem(combo, items[0]);
    }
    XmStringFree(s);
}

/* The selected entry's text. */
static Str combo_text(Widget combo)
{
    Widget list = XtNameToWidget(combo, "*List");
    int* pos = 0;
    int n = 0;
    XmStringTable items = 0;
    int count = 0;
    Str result("");
    if (!list)
        return result;
    Arg args[4];
    XtSetArg(args[0], XmNselectedPositions, &pos);
    XtSetArg(args[1], XmNselectedPositionCount, &n);
    XtSetArg(args[2], XmNitems, &items);
    XtSetArg(args[3], XmNitemCount, &count);
    XtGetValues(list, args, 4);
    if (pos && n > 0 && pos[0] >= 1 && pos[0] <= count) {
        char* t = (char*)XmStringUnparse(items[pos[0] - 1], 0,
                                         XmCHARSET_TEXT, XmCHARSET_TEXT, 0, 0,
                                         XmOUTPUT_ALL);
        result = t ? t : "";
        XtFree(t);
    }
    return result;
}

static Widget toggle(Widget parent, const char* label, bool on)
{
    XmString l = xmstr(label);
    Arg args[1];
    XtSetArg(args[0], XmNlabelString, l);
    Widget t = XmCreateToggleButton(parent, (char*)"toggle", args, 1);
    XmStringFree(l);
    XmToggleButtonSetState(t, on, False);
    XtManageChild(t);
    return t;
}

static void label(Widget parent, const char* text)
{
    XmString l = xmstr(text);
    Arg args[1];
    XtSetArg(args[0], XmNlabelString, l);
    XtManageChild(XmCreateLabel(parent, (char*)"unit", args, 1));
    XmStringFree(l);
}

static Widget push(Widget parent, const char* text, XtCallbackProc cb,
                   XtPointer closure)
{
    XmString l = xmstr(text);
    Arg args[1];
    XtSetArg(args[0], XmNlabelString, l);
    Widget b = XmCreatePushButton(parent, (char*)"button", args, 1);
    XmStringFree(l);
    XtAddCallback(b, XmNactivateCallback, cb, closure);
    XtManageChild(b);
    return b;
}

static bool is_true(const char* v)
{
    return v && (strcmp(v, "true") == 0 || strcmp(v, "True") == 0 ||
                 strcmp(v, "on") == 0 || strcmp(v, "yes") == 0 ||
                 strcmp(v, "1") == 0);
}

SettingsDialog::SettingsDialog(Widget parent, SettingsHost* host)
    : parent_(parent), host_(host), dpy_(XtDisplay(parent)), dialog_(0),
      font_(0), preview_fg_(0),
      preview_bg_(0), have_preview_fg_(false), have_preview_bg_(false),
      picker_(new ttr::ColorPicker(picked, this)), picking_(-1), scheme_w_(0),
      renderer_w_(0), dynbg_w_(0), text_w_(0)
{
    int i;
    for (i = 0; i < C_COUNT; i++) {
        have_pixel_[i] = false;
        slots_[i].dialog = this;
        slots_[i].index = i;
    }
}

SettingsDialog::~SettingsDialog()
{
    destroy();
    delete picker_;
}

void SettingsDialog::show()
{
    destroy();
    build();
    XtManageChild(dialog_);
}

/* The widgets go; the colours they used are given back. */
void SettingsDialog::destroy()
{
    Colormap cmap = DefaultColormap(dpy_, DefaultScreen(dpy_));
    int i;
    for (i = 0; i < C_COUNT; i++)
        if (have_pixel_[i]) {
            XFreeColors(dpy_, cmap, &pixel_[i], 1, 0);
            have_pixel_[i] = false;
        }
    if (have_preview_fg_)
        XFreeColors(dpy_, cmap, &preview_fg_, 1, 0);
    if (have_preview_bg_)
        XFreeColors(dpy_, cmap, &preview_bg_, 1, 0);
    have_preview_fg_ = have_preview_bg_ = false;
    if (dialog_)
        XtDestroyWidget(XtParent(dialog_));
    dialog_ = 0;
    scheme_w_ = 0;
    delete font_;
    font_ = 0;
}

/* ------------------------------------------------------------------ */
/* Building blocks                                                     */

Widget SettingsDialog::frame(Widget parent, const char* title)
{
    Widget f = XmCreateFrame(parent, (char*)"frame", 0, 0);
    XmString l = xmstr(title);
    Arg args[3];
    XtSetArg(args[0], XmNlabelString, l);
    XtSetArg(args[1], XmNchildType, XmFRAME_TITLE_CHILD);
    XtManageChild(XmCreateLabel(f, (char*)"title", args, 2));
    XmStringFree(l);
    XtSetArg(args[0], XmNorientation, XmVERTICAL);
    XtSetArg(args[1], XmNmarginWidth, scaled(8));
    XtSetArg(args[2], XmNmarginHeight, scaled(6));
    Widget rc = XmCreateRowColumn(f, (char*)"rows", args, 3);
    XtManageChild(rc);
    XtManageChild(f);
    return rc;
}

/* A horizontal row starting with a fixed-width label. */
Widget SettingsDialog::row(Widget parent, const char* text)
{
    Arg args[4];
    XtSetArg(args[0], XmNorientation, XmHORIZONTAL);
    XtSetArg(args[1], XmNmarginWidth, 0);
    XtSetArg(args[2], XmNmarginHeight, 0);
    Widget rc = XmCreateRowColumn(parent, (char*)"row", args, 3);
    XmString l = xmstr(text);
    int n = 0;
    XtSetArg(args[n], XmNlabelString, l); n++;
    XtSetArg(args[n], XmNwidth, scaled(k_label_width)); n++;
    XtSetArg(args[n], XmNrecomputeSize, False); n++;
    XtSetArg(args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
    XtManageChild(XmCreateLabel(rc, (char*)"label", args, n));
    XmStringFree(l);
    XtManageChild(rc);
    return rc;
}

Widget SettingsDialog::drop_down(Widget parent, const char* name,
                                 const char* const* items, int count,
                                 int visible, int columns)
{
    XmString* strs = new XmString[count > 0 ? count : 1];
    int i;
    for (i = 0; i < count; i++)
        strs[i] = xmstr(items[i]);
    Arg args[4];
    int n = 0;
    XtSetArg(args[n], XmNvisibleItemCount, visible); n++;
    XtSetArg(args[n], XmNcolumns, columns); n++;
    if (count > 0) {    /* Motif reads items[0] even when there are none */
        XtSetArg(args[n], XmNitems, strs); n++;
        XtSetArg(args[n], XmNitemCount, count); n++;
    }
    Widget cb = XmCreateDropDownList(parent, (char*)name, args, n);
    for (i = 0; i < count; i++)
        XmStringFree(strs[i]);
    delete [] strs;
    XtManageChild(cb);
    return cb;
}

Widget SettingsDialog::radio(Widget parent, const char* a, const char* b,
                             Widget* first)
{
    Arg args[1];
    XtSetArg(args[0], XmNorientation, XmHORIZONTAL);
    Widget box = XmCreateRadioBox(parent, (char*)"radio", args, 1);
    XmString la = xmstr(a), lb = xmstr(b);
    XtSetArg(args[0], XmNlabelString, la);
    *first = XmCreateToggleButton(box, (char*)"a", args, 1);
    XtSetArg(args[0], XmNlabelString, lb);
    Widget second = XmCreateToggleButton(box, (char*)"b", args, 1);
    XmStringFree(la);
    XmStringFree(lb);
    XtManageChild(*first);
    XtManageChild(second);
    XtManageChild(box);
    return box;
}

/* ------------------------------------------------------------------ */
/* The dialog                                                          */

void SettingsDialog::build()
{
    int i;
    for (i = 0; i < S_COUNT; i++) {
        const char* c = host_->current(i);
        orig_[i] = c ? c : "";
    }
    Colormap cmap = DefaultColormap(dpy_, DefaultScreen(dpy_));
    for (i = 0; i < C_COUNT; i++) {
        /* A colour equal to the default shows as the default. */
        const char* set = orig_[k_color_settings[i]].c_str();
        const char* dflt = k_settings[k_color_settings[i]].fallback;
        XColor c;
        mine_[i] = false;
        as_set_[i] = "";
        if (*set && !(dflt && strcmp(set, dflt) == 0) &&
            XParseColor(dpy_, cmap, set, &c)) {
            mine_[i] = true;
            color_[i] = Rgb(c.red, c.green, c.blue);
            as_set_[i] = set;
        }
    }

    XmString title = xmstr("ttrt Settings");
    Arg args[10];
    int n = 0;
    XtSetArg(args[n], XmNdialogTitle, title); n++;
    XtSetArg(args[n], XmNautoUnmanage, False); n++;
    XtSetArg(args[n], XmNmarginWidth, scaled(10)); n++;
    XtSetArg(args[n], XmNmarginHeight, scaled(10)); n++;
    dialog_ = XmCreateFormDialog(parent_, (char*)"settingsDialog", args, n);
    tx::fixed_size(dialog_);
    XmStringFree(title);

    /* Two columns, so it fits a 1024x768 screen. */
    n = 0;
    XtSetArg(args[n], XmNorientation, XmHORIZONTAL); n++;
    XtSetArg(args[n], XmNmarginWidth, 0); n++;
    XtSetArg(args[n], XmNmarginHeight, 0); n++;
    XtSetArg(args[n], XmNspacing, scaled(10)); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    Widget page = XmCreateRowColumn(dialog_, (char*)"page", args, n);
    Widget column[2];
    for (i = 0; i < 2; i++) {
        n = 0;
        XtSetArg(args[n], XmNorientation, XmVERTICAL); n++;
        XtSetArg(args[n], XmNmarginWidth, 0); n++;
        XtSetArg(args[n], XmNmarginHeight, 0); n++;
        XtSetArg(args[n], XmNspacing, scaled(8)); n++;
        column[i] = XmCreateRowColumn(page, (char*)"column", args, n);
        XtManageChild(column[i]);
    }

    /* Font: the chooser every ttr program shares (tx). */
    Widget f = frame(column[0], "Font");
    Widget r = row(f, "Font:");
    font_ = new tx::FontChooser(r, "font", tx::FontChooser::FIXED | tx::FontChooser::UNICODE |
                                               tx::FontChooser::AUTOMATIC);
    font_->set_automatic_preview(tt::pick_font(dpy_));
    font_->set(orig_[S_FONT].c_str());
    label(row(f, ""), "Fixed-width fonts only; * scalable.");
    label(row(f, ""), "Characters a font lacks come from ttrt's.");

    /* Colours */
    f = frame(column[0], "Colors");
    schemes_.clear();
    th::list_schemes(&schemes_);
    {
        const char** names = new const char*[schemes_.count() + 1];
        names[0] = "None";
        for (i = 0; i < schemes_.count(); i++)
            names[i + 1] = schemes_[i].name;
        r = row(f, "Scheme:");
        scheme_w_ = drop_down(r, "scheme", names, schemes_.count() + 1,
                              schemes_.count() + 1 < 12 ? schemes_.count() + 1 : 12, 18);
        delete [] names;
        label(r, "shared with ttrcode");
        const char* cur_scheme = orig_[S_COLOR_SCHEME].c_str();
        select_item(scheme_w_, "None");
        for (i = 0; i < schemes_.count(); i++)
            if (*cur_scheme && th::compare_names(schemes_[i].name, cur_scheme) == 0)
                select_item(scheme_w_, schemes_[i].name);
        XtAddCallback(scheme_w_, XmNselectionCallback, scheme_cb, (XtPointer)this);
    }
    for (i = 0; i < C_COUNT; i++) {
        r = row(f, k_color_labels[i]);
        XmString l = xmstr("#000000");
        n = 0;
        XtSetArg(args[n], XmNlabelString, l); n++;
        XtSetArg(args[n], XmNwidth, scaled(90)); n++;
        XtSetArg(args[n], XmNrecomputeSize, False); n++;
        swatch_[i] = XmCreatePushButton(r, (char*)"swatch", args, n);
        XmStringFree(l);
        XtAddCallback(swatch_[i], XmNactivateCallback, change_cb,
                      (XtPointer)&slots_[i]);
        XtManageChild(swatch_[i]);
        reset_[i] = push(r, i == C_CURSOR ? "Same as text" : "Default",
                         reset_cb, (XtPointer)&slots_[i]);
    }
    bold_bright_ = toggle(row(f, ""), "Show bold text in bright colors",
                          is_true(orig_[S_BOLD_IS_BRIGHT].c_str()));

    /* Window and keys */
    f = frame(column[1], "Window and keys");
    r = row(f, "Scrollback:");
    save_lines_ = drop_down(r, "saveLines", k_save_lines, k_nsave_lines,
                            k_nsave_lines, 6);
    label(r, "lines (new windows)");
    XmString cur = xmstr(orig_[S_SAVE_LINES].c_str());
    Widget list = XtNameToWidget(save_lines_, "*List");
    if (list && !XmListItemExists(list, cur))
        XmComboBoxAddItem(save_lines_, cur, 0, False);
    XmStringFree(cur);
    select_item(save_lines_, orig_[S_SAVE_LINES].c_str());

    login_ = toggle(row(f, "Shell:"), "Login shell (new windows)",
                    is_true(orig_[S_LOGIN_SHELL].c_str()));
    menu_bar_ = toggle(row(f, "Menu bar:"), "Show (or Ctrl+right click)",
                       is_true(orig_[S_MENU_BAR].c_str()));
    radio(row(f, "Backspace sends:"), "DEL (^?)", "BS (^H)", &backspace_del_);
    bool del = strcmp(orig_[S_BACKSPACE].c_str(), "bs") != 0;
    XmToggleButtonSetState(del ? backspace_del_
                               : XtNameToWidget(XtParent(backspace_del_), "b"),
                           True, True);
    bell_ = drop_down(row(f, "Bell:"), "bell", k_bell_labels, k_nbells,
                      k_nbells, 14);
    int b = 0;
    for (i = 0; i < k_nbells; i++)
        if (strcmp(orig_[S_BELL].c_str(), k_bell_values[i]) == 0)
            b = i;
    select_item(bell_, k_bell_labels[b]);

    /* Drawing: tw's renderers and dash's animated backgrounds. */
    f = frame(column[1], "Drawing");
    r = row(f, "Renderer:");
    renderer_w_ = drop_down(r, "renderer", k_renderer_labels, k_nrenderers, k_nrenderers, 20);
    label(r, "(new windows)");
    int rp = 0;
    for (i = 0; i < k_nrenderers; i++)
        if (strcmp(orig_[S_RENDERER].c_str(), k_renderer_values[i]) == 0)
            rp = i;
    select_item(renderer_w_, k_renderer_labels[rp]);
    r = row(f, "Text:");
    text_w_ = drop_down(r, "textRendering", k_text_labels, k_ntexts, k_ntexts, 12);
    label(r, "smooth needs OpenGL");
    int tp = 0;
    for (i = 0; i < k_ntexts; i++)
        if (strcmp(orig_[S_TEXT_RENDERING].c_str(), k_text_values[i]) == 0)
            tp = i;
    select_item(text_w_, k_text_labels[tp]);
    label(row(f, ""), "Automatic: smooth from 18 pixels.");
    {
        const char* labels[17];
        const char* values[17];
        int nb = background_choices(labels, values, 17);
        r = row(f, "Background:");
        dynbg_w_ = drop_down(r, "dynamicBackground", labels, nb, nb, 12);
        int bp = 0;
        for (i = 0; i < nb; i++)
            if (strcmp(orig_[S_DYNAMIC_BACKGROUND].c_str(), values[i]) == 0)
                bp = i;
        select_item(dynbg_w_, labels[bp]);
        label(r, "animated, with OpenGL");
    }
    XtManageChild(page);

    /* Separator, the file it saves to, OK, Apply, Cancel. */
    n = 0;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget, page); n++;
    XtSetArg(args[n], XmNtopOffset, scaled(10)); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    Widget sep = XmCreateSeparator(dialog_, (char*)"sep", args, n);
    XtManageChild(sep);

    /* Where it saves, with the home directory as ~. */
    Str path = settings_path();
    const char* home = getenv("HOME");
    const char* shown = path.c_str();
    size_t hl = home ? strlen(home) : 0;
    char note[1200];
    if (hl > 1 && strncmp(shown, home, hl) == 0 && shown[hl] == '/')
        sprintf(note, "Saved for new windows in ~%.1100s", shown + hl);
    else
        sprintf(note, "Saved for new windows in %.1100s", shown);
    XmString nl = xmstr(note);
    n = 0;
    XtSetArg(args[n], XmNlabelString, nl); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget, sep); n++;
    XtSetArg(args[n], XmNtopOffset, scaled(10)); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtManageChild(XmCreateLabel(dialog_, (char*)"note", args, n));
    XmStringFree(nl);

    static const char* const names[3] = { "Cancel", "Apply", "OK" };
    static const XtCallbackProc cbs[3] = { cancel_cb, apply_cb, ok_cb };
    Widget buttons[3];
    for (i = 0; i < 3; i++) {
        XmString l = xmstr(names[i]);
        n = 0;
        XtSetArg(args[n], XmNlabelString, l); n++;
        XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
        XtSetArg(args[n], XmNtopWidget, sep); n++;
        XtSetArg(args[n], XmNtopOffset, scaled(6)); n++;
        if (i == 0) {
            XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
        } else {
            XtSetArg(args[n], XmNrightAttachment, XmATTACH_WIDGET); n++;
            XtSetArg(args[n], XmNrightWidget, buttons[i - 1]); n++;
            XtSetArg(args[n], XmNrightOffset, scaled(6)); n++;
        }
        buttons[i] = XmCreatePushButton(dialog_, (char*)names[i], args, n);
        XmStringFree(l);
        XtAddCallback(buttons[i], XmNactivateCallback, cbs[i], (XtPointer)this);
        XtManageChild(buttons[i]);
    }
    Arg dargs[2];
    XtSetArg(dargs[0], XmNdefaultButton, buttons[2]);
    XtSetArg(dargs[1], XmNcancelButton, buttons[0]);
    XtSetValues(dialog_, dargs, 2);

    update_swatches();
}

/* The preview in the text colours chosen so far. */
void SettingsDialog::update_preview()
{
    if (!font_)
        return;
    Pixel fg = ttr::alloc_color(dpy_, color(C_FG), &preview_fg_, &have_preview_fg_);
    Pixel bg = ttr::alloc_color(dpy_, color(C_BG), &preview_bg_, &have_preview_bg_);
    font_->set_preview_colors(fg, bg);
}

/* ------------------------------------------------------------------ */
/* Colours                                                             */

bool SettingsDialog::chosen_scheme(th::Scheme* out) const
{
    int pos = scheme_w_ ? selected_pos(scheme_w_) : -1;
    if (pos < 1 || pos > schemes_.count())
        return false;
    return th::find_scheme(schemes_[pos - 1].name, out);
}

void SettingsDialog::scheme_cb(Widget, XtPointer closure, XtPointer)
{
    ((SettingsDialog*)closure)->update_swatches();
}

/* A colour of its own, else the scheme's, else the default. */
Rgb SettingsDialog::color(int i) const
{
    if (mine_[i])
        return color_[i];
    th::Scheme s;
    if (chosen_scheme(&s))
        return Rgb::hex(s.get(i == C_FG ? th::SC_FOREGROUND
                              : i == C_BG ? th::SC_BACKGROUND : th::SC_CURSOR));
    if (i == C_CURSOR)
        return color(C_FG);
    XColor c;
    Colormap cmap = DefaultColormap(dpy_, DefaultScreen(dpy_));
    const char* d = k_settings[k_color_settings[i]].fallback;
    if (d && XParseColor(dpy_, cmap, d, &c))
        return Rgb(c.red, c.green, c.blue);
    return Rgb();
}

void SettingsDialog::update_swatches()
{
    int i;
    for (i = 0; i < C_COUNT; i++) {
        Rgb c = color(i);
        char hex[16];
        ttr::format_hex_color(c, hex);
        XmString l = xmstr(hex);
        Arg args[1];
        XtSetArg(args[0], XmNlabelString, l);
        XtSetValues(swatch_[i], args, 1);
        XmStringFree(l);
        XmChangeColor(swatch_[i], ttr::alloc_color(dpy_, c, &pixel_[i],
                                                   &have_pixel_[i]));
        XtSetSensitive(reset_[i], mine_[i]);
    }
    update_preview();
}

void SettingsDialog::change_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    Slot* slot = (Slot*)closure;
    SettingsDialog* self = slot->dialog;
    self->picking_ = slot->index;
    static const char* const titles[3] = {
        "Text Color", "Background Color", "Cursor Color"
    };
    self->picker_->show(self->dialog_, titles[slot->index],
                        self->color(slot->index));
}

void SettingsDialog::picked(void* closure, const Rgb& c)
{
    SettingsDialog* self = (SettingsDialog*)closure;
    int i = self->picking_;
    if (i < 0 || !self->dialog_)
        return;
    self->mine_[i] = true;
    self->color_[i] = c;
    self->as_set_[i] = "";
    self->update_swatches();
}

void SettingsDialog::reset_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    Slot* slot = (Slot*)closure;
    slot->dialog->mine_[slot->index] = false;
    slot->dialog->as_set_[slot->index] = "";
    slot->dialog->update_swatches();
}

/* ------------------------------------------------------------------ */
/* OK, Apply, Cancel                                                   */

/* Every setting's value as the dialog shows it, in the window's own
 * spelling where the user did not change it ("wheat", "9x15"). */
void SettingsDialog::collect(Str* v) const
{
    v[S_FONT] = font_->get();
    int i;
    for (i = 0; i < C_COUNT; i++) {
        int s = k_color_settings[i];
        if (mine_[i] && !as_set_[i].empty()) {
            v[s] = as_set_[i];
        } else if (mine_[i]) {
            char hex[16];
            ttr::format_hex_color(color_[i], hex);
            v[s] = hex;
        } else {
            v[s] = k_settings[s].fallback ? k_settings[s].fallback : "";
        }
    }
    int rp = selected_pos(renderer_w_);
    v[S_RENDERER] = k_renderer_values[rp >= 0 && rp < k_nrenderers ? rp : 0];
    int tp = selected_pos(text_w_);
    v[S_TEXT_RENDERING] = k_text_values[tp >= 0 && tp < k_ntexts ? tp : 0];
    {
        const char* labels[17];
        const char* values[17];
        int nb = background_choices(labels, values, 17);
        int bp = selected_pos(dynbg_w_);
        v[S_DYNAMIC_BACKGROUND] = values[bp >= 0 && bp < nb ? bp : 0];
    }
    int sp = selected_pos(scheme_w_);
    v[S_COLOR_SCHEME] = sp >= 1 && sp <= schemes_.count() ? schemes_[sp - 1].name : "";
    v[S_BOLD_IS_BRIGHT] = XmToggleButtonGetState(bold_bright_) ? "true" : "false";
    v[S_LOGIN_SHELL] = XmToggleButtonGetState(login_) ? "true" : "false";
    v[S_MENU_BAR] = XmToggleButtonGetState(menu_bar_) ? "true" : "false";
    v[S_BACKSPACE] = XmToggleButtonGetState(backspace_del_) ? "del" : "bs";
    int b = selected_pos(bell_);
    v[S_BELL] = k_bell_values[b >= 0 && b < k_nbells ? b : 0];
    Str lines = combo_text(save_lines_);
    const char* p = lines.c_str();
    bool digits = *p != 0;
    for (; *p; p++)
        if (!isdigit((unsigned char)*p))
            digits = false;
    v[S_SAVE_LINES] = digits ? lines : orig_[S_SAVE_LINES];
}

/* Save what changed and apply it to the window. Settings left alone are
 * not written, so the file only holds what the user chose, and a value
 * set back to its default is removed, so X resources apply again. */
bool SettingsDialog::commit()
{
    Str v[S_COUNT];
    bool changed[S_COUNT];
    bool any = false;
    collect(v);
    int i;
    for (i = 0; i < S_COUNT; i++) {
        changed[i] = strcmp(v[i].c_str(), orig_[i].c_str()) != 0;
        any = any || changed[i];
    }
    if (!any)
        return true;

    static const char* const header[] = {
        "# ttrt settings.",
        "#",
        "# Edited by Terminal > Settings in ttrt, or by hand. One \"name: value\"",
        "# per line, with the names of ttrt's X resources (TTRt*name). Values",
        "# here win over ~/.Xresources; command line options win over this file.",
        "",
        0
    };
    ttr::SettingsFile file;
    file.set_header(header);
    Str path = settings_path();
    file.load(path.c_str());
    for (i = 0; i < S_COUNT; i++) {
        if (!changed[i])
            continue;
        const char* d = k_settings[i].fallback;
        if (v[i].empty() || (d && strcmp(d, v[i].c_str()) == 0))
            file.remove(k_settings[i].name);
        else
            file.set(k_settings[i].name, v[i].c_str());
    }
    if (!file.save(path.c_str())) {
        char msg[1200];
        sprintf(msg, "Could not write %.1100s", path.c_str());
        error(msg);
        return false;
    }
    host_->apply(v, changed);
    for (i = 0; i < S_COUNT; i++)
        orig_[i] = v[i];
    font_->set(v[S_FONT].c_str());
    for (i = 0; i < C_COUNT; i++)
        if (mine_[i] && as_set_[i].empty())
            as_set_[i] = v[k_color_settings[i]];
    return true;
}

void SettingsDialog::error(const char* msg)
{
    XmString m = xmstr(msg);
    Arg args[1];
    XtSetArg(args[0], XmNmessageString, m);
    Widget err = XmCreateErrorDialog(dialog_, (char*)"saveError", args, 1);
    XmStringFree(m);
    XtUnmanageChild(XtNameToWidget(err, "Help"));
    XtUnmanageChild(XtNameToWidget(err, "Cancel"));
    XtManageChild(err);
}

void SettingsDialog::ok_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    SettingsDialog* self = (SettingsDialog*)closure;
    if (self->commit())
        XtUnmanageChild(self->dialog_);
}

void SettingsDialog::apply_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ((SettingsDialog*)closure)->commit();
}

void SettingsDialog::cancel_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    XtUnmanageChild(((SettingsDialog*)closure)->dialog_);
}

}
