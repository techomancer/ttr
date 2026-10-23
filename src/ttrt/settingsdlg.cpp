#include "settingsdlg.h"

#include "../../tt/pane.h"

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

/* Sizes offered for scalable fonts. */
static const int k_scalable_sizes[] = {
    8, 9, 10, 11, 12, 13, 14, 15, 16, 18, 20, 22, 24, 28, 32
};
static const int k_nscalable =
    (int)(sizeof(k_scalable_sizes) / sizeof(k_scalable_sizes[0]));

static const char* const k_preview_text = "$ ls -l   AaBbYyZz 0123 {}[]|~";

static XmString xmstr(const char* s)
{
    return XmStringCreateLocalized((char*)s);
}

static int compare_nocase(const char* a, const char* b)
{
    while (*a && *b) {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb)
            return ca - cb;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* Field i (1-14) of an XLFD, or "" if it is not one. */
static Str field(const char* xlfd, int i)
{
    if (!xlfd || xlfd[0] != '-')
        return Str("");
    int dashes = 0;
    const char* p;
    for (p = xlfd; *p; p++)
        if (*p == '-')
            dashes++;
    if (dashes != 14)
        return Str("");
    p = xlfd + 1;
    int k;
    for (k = 1; k < i; k++) {
        p = strchr(p, '-');
        if (!p)
            return Str("");
        p++;
    }
    const char* e = strchr(p, '-');
    int n = e ? (int)(e - p) : (int)strlen(p);
    char buf[128];
    if (n > 127)
        n = 127;
    memcpy(buf, p, n);
    buf[n] = 0;
    return Str(buf);
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
      family_(0), family_w_(0), size_w_(0), preview_(0), preview_fg_(0),
      preview_bg_(0), have_preview_fg_(false), have_preview_bg_(false),
      picker_(new ttr::ColorPicker(picked, this)), picking_(-1)
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
    preview_ = 0;
    free_fonts();
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
    XmStringFree(title);

    n = 0;
    XtSetArg(args[n], XmNorientation, XmVERTICAL); n++;
    XtSetArg(args[n], XmNmarginWidth, 0); n++;
    XtSetArg(args[n], XmNmarginHeight, 0); n++;
    XtSetArg(args[n], XmNspacing, scaled(8)); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    Widget page = XmCreateRowColumn(dialog_, (char*)"page", args, n);

    /* Font */
    load_fonts();
    Widget f = frame(page, "Font");
    Widget r = row(f, "Font:");
    family_w_ = drop_down(r, "family", 0, 0, 15, 18);
    Dimension fw = 0;
    XtSetArg(args[0], XmNwidth, &fw);
    XtGetValues(family_w_, args, 1);
    for (i = 0; i < families_.size(); i++) {
        XmString item = xmstr(families_[i]->label.c_str());
        XmComboBoxAddItem(family_w_, item, 0, False);
        XmStringFree(item);
    }
    XtSetArg(args[0], XmNwidth, fw);
    XtSetValues(family_w_, args, 1);
    size_w_ = drop_down(r, "size", 0, 0, 12, 9);
    XtAddCallback(family_w_, XmNselectionCallback, family_cb, (XtPointer)this);
    XtAddCallback(size_w_, XmNselectionCallback, size_cb, (XtPointer)this);

    r = row(f, "");
    n = 0;
    XmString pt = xmstr(k_preview_text);
    XtSetArg(args[n], XmNlabelString, pt); n++;
    XtSetArg(args[n], XmNrecomputeSize, False); n++;
    XtSetArg(args[n], XmNwidth, scaled(400)); n++;
    XtSetArg(args[n], XmNheight, scaled(44)); n++;
    XtSetArg(args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
    XtSetArg(args[n], XmNmarginWidth, scaled(8)); n++;
    preview_ = XmCreateLabel(r, (char*)"preview", args, n);
    XmStringFree(pt);
    XtManageChild(preview_);
    label(row(f, ""), "Fixed-width fonts only; * scalable. Characters a "
                      "font lacks come from ttrt's own fonts.");
    show_current_font(orig_[S_FONT].c_str());

    /* Colours */
    f = frame(page, "Colors");
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
    f = frame(page, "Window and keys");
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

    login_ = toggle(row(f, "Shell:"), "Start the shell as a login shell "
                                      "(new windows)",
                    is_true(orig_[S_LOGIN_SHELL].c_str()));
    menu_bar_ = toggle(row(f, "Menu bar:"), "Show the menu bar "
                                            "(Ctrl+right click opens the menu)",
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

/* ------------------------------------------------------------------ */
/* Fonts                                                               */

void SettingsDialog::free_fonts()
{
    int i, k;
    for (i = 0; i < families_.size(); i++) {
        for (k = 0; k < families_[i]->sizes.size(); k++)
            delete families_[i]->sizes[k];
        delete families_[i];
    }
    families_.clear();
    family_ = 0;
}

/* Every fixed-width (c and m spacing) upright, non-bold font in ISO 10646
 * or Latin-1, grouped by family; the size list names a font only once,
 * preferring Unicode and medium weight. */
void SettingsDialog::load_fonts()
{
    free_fonts();
    Family* autof = new Family;
    autof->label = "Automatic";
    autof->scalable = false;
    autof->scalable_rank = 0;
    families_.push_back(autof);

    static const char* const patterns[] = {
        "-*-*-*-r-*-*-*-*-*-*-c-*-*-*",
        "-*-*-*-r-*-*-*-*-*-*-m-*-*-*",
        0
    };
    int i, k;
    for (i = 0; patterns[i]; i++) {
        int n = 0;
        char** names = XListFonts(dpy_, patterns[i], 30000, &n);
        for (k = 0; k < n; k++)
            add_font(names[k]);
        if (names)
            XFreeFontNames(names);
    }
    for (i = 1; i < families_.size(); i++)
        finish_family(families_[i]);
}

SettingsDialog::Family* SettingsDialog::family_named(const char* name,
                                                     bool create)
{
    int i;
    int at = families_.size();
    for (i = 1; i < families_.size(); i++) {
        int c = compare_nocase(name, families_[i]->name.c_str());
        if (c == 0)
            return families_[i];
        if (c < 0) {
            at = i;
            break;
        }
    }
    if (!create)
        return 0;
    Family* f = new Family;
    f->name = name;
    f->label = name;
    f->scalable = false;
    f->scalable_rank = 1000;
    families_.push_back(f);
    for (i = families_.size() - 1; i > at; i--)
        families_[i] = families_[i - 1];
    families_[at] = f;
    return f;
}

void SettingsDialog::add_font(const char* xlfd)
{
    Str fam = field(xlfd, 2), weight = field(xlfd, 3), width = field(xlfd, 5),
        px = field(xlfd, 7), avg = field(xlfd, 12), reg = field(xlfd, 13),
        enc = field(xlfd, 14);
    if (fam.empty() || px.empty())
        return;
    const char* w = weight.c_str();
    if (strstr(w, "bold") || strstr(w, "demi") || strstr(w, "black") ||
        strstr(w, "heavy"))
        return;
    bool unicode = strcmp(reg.c_str(), "iso10646") == 0;
    if (strcmp(enc.c_str(), "1") != 0 ||
        (!unicode && strcmp(reg.c_str(), "iso8859") != 0))
        return;
    int rank = (unicode ? 0 : 10) +
               (strcmp(w, "medium") == 0 ? 0
                : strcmp(w, "regular") == 0 || strcmp(w, "normal") == 0 ? 1
                : 3) +
               (strcmp(width.c_str(), "normal") == 0 ? 0 : 1);
    Family* f = family_named(fam.c_str(), true);
    int pixels = atoi(px.c_str());
    if (pixels == 0) {
        f->scalable = true;
        if (rank < f->scalable_rank) {
            f->scalable_rank = rank;
            f->scalable_xlfd = xlfd;
        }
        return;
    }
    int avgw = (atoi(avg.c_str()) + 5) / 10;
    int i;
    for (i = 0; i < f->sizes.size(); i++) {
        Size* s = f->sizes[i];
        if (s->px == pixels && s->width == avgw) {
            if (rank < s->rank) {
                s->rank = rank;
                s->xlfd = xlfd;
            }
            return;
        }
    }
    Size* s = new Size;
    s->px = pixels;
    s->width = avgw;
    s->rank = rank;
    s->xlfd = xlfd;
    f->sizes.push_back(s);
}

/* Scalable sizes, the order, and the labels. */
void SettingsDialog::finish_family(Family* f)
{
    int i, j;
    if (f->scalable) {
        char text[300];
        sprintf(text, "%.200s *", f->name.c_str());
        f->label = text;
        const char* x = f->scalable_xlfd.c_str();
        for (i = 0; i < k_nscalable; i++) {
            bool have = false;
            for (j = 0; j < f->sizes.size(); j++)
                if (f->sizes[j]->px == k_scalable_sizes[i])
                    have = true;
            if (have)
                continue;
            char name[400];
            sprintf(name, "-%.60s-%.100s-%.40s-%.10s-%.40s-%.40s-%d-*-*-*-%.10s-*-%.40s-%.20s",
                    field(x, 1).c_str(), field(x, 2).c_str(), field(x, 3).c_str(),
                    field(x, 4).c_str(), field(x, 5).c_str(), field(x, 6).c_str(),
                    k_scalable_sizes[i], field(x, 11).c_str(),
                    field(x, 13).c_str(), field(x, 14).c_str());
            Size* s = new Size;
            s->px = k_scalable_sizes[i];
            s->width = 0;
            s->rank = 0;
            s->xlfd = name;
            f->sizes.push_back(s);
        }
    }
    /* Insertion sort by height, then width. */
    for (i = 1; i < f->sizes.size(); i++) {
        Size* s = f->sizes[i];
        for (j = i; j > 0; j--) {
            Size* p = f->sizes[j - 1];
            if (p->px < s->px || (p->px == s->px && p->width <= s->width))
                break;
            f->sizes[j] = p;
        }
        f->sizes[j] = s;
    }
    /* "13", or "13 (6x13)" where one height comes in several widths. */
    for (i = 0; i < f->sizes.size(); i++) {
        Size* s = f->sizes[i];
        bool shared = (i > 0 && f->sizes[i - 1]->px == s->px) ||
                      (i + 1 < f->sizes.size() && f->sizes[i + 1]->px == s->px);
        char text[64];
        if (shared && s->width > 0)
            sprintf(text, "%d (%dx%d)", s->px, s->width, s->px);
        else
            sprintf(text, "%d", s->px);
        s->label = text;
    }
}

/* Select the family and size of the font in use: names that are not
 * XLFDs (9x15, fixed) are resolved through the server. A font that is
 * not on the lists gets its own entry, so OK keeps it. */
void SettingsDialog::show_current_font(const char* value)
{
    if (!value || !*value) {
        select_item(family_w_, families_[0]->label.c_str());
        family_ = 0;
        fill_sizes(0, 0, 0);
        font_shown_ = "";
        update_preview();
        return;
    }
    Str real(value);
    XFontStruct* fs = XLoadQueryFont(dpy_, value);
    if (fs) {
        unsigned long atom = 0;
        if (XGetFontProperty(fs, XA_FONT, &atom) && atom) {
            char* name = XGetAtomName(dpy_, (Atom)atom);
            if (name) {
                real = name;
                XFree(name);
            }
        }
        XFreeFont(dpy_, fs);
    }
    Family* f = family_named(field(real.c_str(), 2).c_str(), false);
    int px = atoi(field(real.c_str(), 7).c_str());
    int width = (atoi(field(real.c_str(), 12).c_str()) + 5) / 10;
    int fi = -1;
    int i;
    if (f) {
        for (i = 0; i < f->sizes.size(); i++)
            if (f->sizes[i]->px == px &&
                (f->sizes[i]->width == width || f->sizes[i]->width == 0))
                break;
        if (i < f->sizes.size())
            for (fi = 0; fi < families_.size(); fi++)
                if (families_[fi] == f)
                    break;
    }
    if (fi < 0 || fi >= families_.size()) {
        Family* own = new Family;
        own->name = value;
        char text[300];
        sprintf(text, "As set: %.200s", value);
        own->label = text;
        own->scalable = false;
        own->scalable_rank = 0;
        Size* s = new Size;
        s->px = 0;
        s->width = 0;
        s->rank = 0;
        s->label = "-";
        s->xlfd = value;
        own->sizes.push_back(s);
        families_.push_back(own);
        XmString item = xmstr(text);
        XmComboBoxAddItem(family_w_, item, 0, False);
        XmStringFree(item);
        fi = families_.size() - 1;
        px = 0;
        width = 0;
    }
    family_ = fi;
    select_item(family_w_, families_[fi]->label.c_str());
    fill_sizes(fi, px, width);
    font_shown_ = chosen_font();
    update_preview();
}

/* The sizes of family fi, selecting px (and width), or the nearest. */
void SettingsDialog::fill_sizes(int fi, int px, int width)
{
    Widget list = XtNameToWidget(size_w_, "*List");
    if (!list)
        return;
    XmListDeleteAllItems(list);
    Family* f = families_[fi];
    if (f->sizes.size() == 0) {
        XmString s = xmstr("-");
        XmListAddItemUnselected(list, s, 0);
        XmComboBoxSelectItem(size_w_, s);
        XmStringFree(s);
        XtSetSensitive(size_w_, False);
        return;
    }
    XtSetSensitive(size_w_, f->sizes.size() > 1);
    int best = 0, best_d = 1 << 30;
    int i;
    for (i = 0; i < f->sizes.size(); i++) {
        XmString s = xmstr(f->sizes[i]->label.c_str());
        XmListAddItemUnselected(list, s, 0);
        XmStringFree(s);
        int d = abs(f->sizes[i]->px - px) * 100 + abs(f->sizes[i]->width - width);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    select_item(size_w_, f->sizes[best]->label.c_str());
}

Str SettingsDialog::chosen_font() const
{
    if (family_ <= 0 || family_ >= families_.size())
        return Str("");
    Family* f = families_[family_];
    int si = selected_pos(size_w_);
    if (si < 0 || si >= f->sizes.size())
        si = 0;
    return f->sizes.size() > 0 ? f->sizes[si]->xlfd : Str("");
}

/* The font ttrt would use: the chosen one, or the first automatic one
 * the server has. */
Str SettingsDialog::preview_font() const
{
    Str f = chosen_font();
    if (!f.empty())
        return f;
    return Str(tt::pick_font(dpy_));
}

void SettingsDialog::update_preview()
{
    if (!preview_)
        return;
    Str name = preview_font();
    XmFontListEntry e = XmFontListEntryLoad(dpy_, (char*)name.c_str(),
                                            XmFONT_IS_FONT,
                                            (char*)XmFONTLIST_DEFAULT_TAG);
    Arg args[3];
    int n = 0;
    XmFontList fl = 0;
    if (e) {
        fl = XmFontListAppendEntry(0, e);
        XmFontListEntryFree(&e);
        XtSetArg(args[n], XmNfontList, fl); n++;
    }
    Pixel fg = ttr::alloc_color(dpy_, color(C_FG), &preview_fg_,
                                &have_preview_fg_);
    Pixel bg = ttr::alloc_color(dpy_, color(C_BG), &preview_bg_,
                                &have_preview_bg_);
    XtSetArg(args[n], XmNforeground, fg); n++;
    XtSetArg(args[n], XmNbackground, bg); n++;
    XtSetValues(preview_, args, n);
    if (fl)
        XmFontListFree(fl);
}

void SettingsDialog::family_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    SettingsDialog* self = (SettingsDialog*)closure;
    int fi = selected_pos(self->family_w_);
    if (fi < 0 || fi >= self->families_.size() || fi == self->family_)
        return;
    /* Keep the size, or the nearest the new family has. */
    int px = 13, width = 0;
    Family* old = self->families_[self->family_];
    int si = selected_pos(self->size_w_);
    if (si >= 0 && si < old->sizes.size() && old->sizes[si]->px > 0) {
        px = old->sizes[si]->px;
        width = old->sizes[si]->width;
    }
    self->family_ = fi;
    self->fill_sizes(fi, px, width);
    self->update_preview();
}

void SettingsDialog::size_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ((SettingsDialog*)closure)->update_preview();
}

/* ------------------------------------------------------------------ */
/* Colours                                                             */

Rgb SettingsDialog::color(int i) const
{
    if (mine_[i])
        return color_[i];
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
    Str font = chosen_font();
    v[S_FONT] = strcmp(font.c_str(), font_shown_.c_str()) == 0 ? orig_[S_FONT]
                                                               : font;
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
    font_shown_ = chosen_font();
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
