#include "shell/terminalpage.h"
#include "shell/colorpicker.h"
#include "shell/settings.h"
#include "core/scale.h"
#include "core/settings.h"
#include "core/theme.h"

#include <Xm/Xm.h>
#include <Xm/ComboBox.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/ToggleB.h>
#include <X11/Xatom.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ttr {

static const char* const k_color_labels[3] = {
    "Text:", "Background:", "Cursor:"
};
static const int k_color_settings[3] = {
    TS_FOREGROUND, TS_BACKGROUND, TS_CURSOR_COLOR
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

static Str setting_name(int i)
{
    char name[64];
    sprintf(name, "%s%s", TTRT_SETTING_PREFIX, k_term_settings[i].name);
    return Str(name);
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

TerminalPage::TerminalPage(SettingsDialog* dialog, Widget page)
    : top_(dialog->widget()), dpy_(XtDisplay(dialog->widget())), family_(0),
      family_w_(0), size_w_(0), preview_(0), preview_fg_(0), preview_bg_(0),
      have_preview_fg_(false), have_preview_bg_(false), picking_(-1)
{
    SettingsFile file;
    file.load(SettingsFile::default_path().c_str());
    const char* v[TS_COUNT];
    int i;
    for (i = 0; i < TS_COUNT; i++) {
        v[i] = file.get(setting_name(i).c_str());
        if (!v[i])
            v[i] = k_term_settings[i].fallback;
    }

    picker_ = new ColorPicker(picked, this);
    Colormap cmap = DefaultColormap(dpy_, DefaultScreen(dpy_));
    for (i = 0; i < C_COUNT; i++) {
        slots_[i].page = this;
        slots_[i].index = i;
        have_pixel_[i] = false;
        mine_[i] = false;
        const char* set = file.get(setting_name(k_color_settings[i]).c_str());
        XColor c;
        if (set && *set && XParseColor(dpy_, cmap, set, &c)) {
            mine_[i] = true;
            color_[i] = Rgb(c.red, c.green, c.blue);
            as_set_[i] = set;
        }
    }

    /* Font */
    load_fonts();
    Widget f = dialog->frame(page, "Font");
    Widget r = dialog->row(f, "Font:");
    family_w_ = dialog->drop_down(r, "termFamily", 0, 0, 15, 18);
    Dimension fw = 0;
    Arg args[8];
    XtSetArg(args[0], XmNwidth, &fw);
    XtGetValues(family_w_, args, 1);
    for (i = 0; i < families_.size(); i++) {
        XmString item = xmstr(families_[i]->label.c_str());
        XmComboBoxAddItem(family_w_, item, 0, False);
        XmStringFree(item);
    }
    XtSetArg(args[0], XmNwidth, fw);
    XtSetValues(family_w_, args, 1);
    size_w_ = dialog->drop_down(r, "termSize", 0, 0, 12, 9);
    XtAddCallback(family_w_, XmNselectionCallback, family_cb, (XtPointer)this);
    XtAddCallback(size_w_, XmNselectionCallback, size_cb, (XtPointer)this);

    r = dialog->row(f, "");
    int n = 0;
    XmString pt = xmstr(k_preview_text);
    XtSetArg(args[n], XmNlabelString, pt); n++;
    XtSetArg(args[n], XmNrecomputeSize, False); n++;
    XtSetArg(args[n], XmNwidth, scaled(400)); n++;
    XtSetArg(args[n], XmNheight, scaled(44)); n++;
    XtSetArg(args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
    XtSetArg(args[n], XmNmarginWidth, scaled(8)); n++;
    preview_ = XmCreateLabel(r, (char*)"termPreview", args, n);
    XmStringFree(pt);
    XtManageChild(preview_);
    label(dialog->row(f, ""), "Fixed-width fonts only; * scalable. Characters "
                              "a font lacks come from ttrt's own fonts.");
    show_current_font(file.get(setting_name(TS_FONT).c_str()));

    /* Colours */
    f = dialog->frame(page, "Colors");
    for (i = 0; i < C_COUNT; i++) {
        r = dialog->row(f, k_color_labels[i]);
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
    bold_bright_ = toggle(dialog->row(f, ""), "Show bold text in bright colors",
                          is_true(v[TS_BOLD_IS_BRIGHT]));

    /* Behaviour */
    f = dialog->frame(page, "Window and keys");
    r = dialog->row(f, "Scrollback:");
    save_lines_ = dialog->drop_down(r, "saveLines", k_save_lines,
                                    k_nsave_lines, k_nsave_lines, 6);
    label(r, "lines");
    XmString cur = xmstr(v[TS_SAVE_LINES]);
    Widget list = XtNameToWidget(save_lines_, "*List");
    if (list && !XmListItemExists(list, cur))
        XmComboBoxAddItem(save_lines_, cur, 0, False);
    XmStringFree(cur);
    select_item(save_lines_, v[TS_SAVE_LINES]);

    login_ = toggle(dialog->row(f, "Shell:"), "Start the shell as a login shell",
                    is_true(v[TS_LOGIN_SHELL]));
    menu_bar_ = toggle(dialog->row(f, "Menu bar:"), "Show the menu bar",
                       is_true(v[TS_MENU_BAR]));
    dialog->radio(dialog->row(f, "Backspace sends:"), "DEL (^?)", "BS (^H)",
                  &backspace_del_);
    bool del = strcmp(v[TS_BACKSPACE], "bs") != 0;
    XmToggleButtonSetState(del ? backspace_del_
                               : XtNameToWidget(XtParent(backspace_del_), "b"),
                           True, True);
    bell_ = dialog->drop_down(dialog->row(f, "Bell:"), "bell", k_bell_labels,
                              k_nbells, k_nbells, 14);
    int b = 0;
    for (i = 0; i < k_nbells; i++)
        if (strcmp(v[TS_BELL], k_bell_values[i]) == 0)
            b = i;
    select_item(bell_, k_bell_labels[b]);

    XmString note = xmstr("New terminal windows (ttrt) use these settings.");
    XtSetArg(args[0], XmNlabelString, note);
    XtManageChild(XmCreateLabel(page, (char*)"note", args, 1));
    XmStringFree(note);

    update_swatches();
}

TerminalPage::~TerminalPage()
{
    Colormap cmap = DefaultColormap(dpy_, DefaultScreen(dpy_));
    int i;
    for (i = 0; i < C_COUNT; i++)
        if (have_pixel_[i])
            XFreeColors(dpy_, cmap, &pixel_[i], 1, 0);
    if (have_preview_fg_)
        XFreeColors(dpy_, cmap, &preview_fg_, 1, 0);
    if (have_preview_bg_)
        XFreeColors(dpy_, cmap, &preview_bg_, 1, 0);
    delete picker_;
    for (i = 0; i < families_.size(); i++) {
        int k;
        for (k = 0; k < families_[i]->sizes.size(); k++)
            delete families_[i]->sizes[k];
        delete families_[i];
    }
}

/* ------------------------------------------------------------------ */
/* Fonts                                                               */

/* Every fixed-width (c and m spacing) upright, non-bold font in ISO 10646
 * or Latin-1, grouped by family; the size list names a font only once,
 * preferring Unicode and medium weight. */
void TerminalPage::load_fonts()
{
    Family* autof = new Family;
    autof->name = "";
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

TerminalPage::Family* TerminalPage::family_named(const char* name,
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

void TerminalPage::add_font(const char* xlfd)
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
void TerminalPage::finish_family(Family* f)
{
    int i, j;
    if (f->scalable) {
        char label[300];
        sprintf(label, "%.200s *", f->name.c_str());
        f->label = label;
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
        char label[64];
        if (shared && s->width > 0)
            sprintf(label, "%d (%dx%d)", s->px, s->width, s->px);
        else
            sprintf(label, "%d", s->px);
        s->label = label;
    }
}

/* Select the family and size of the font set now: names that are not
 * XLFDs (9x15, fixed) are resolved through the server. A font that is
 * not on the lists gets its own entry, so OK keeps it. */
void TerminalPage::show_current_font(const char* value)
{
    if (!value || !*value) {
        select_item(family_w_, families_[0]->label.c_str());
        family_ = 0;
        fill_sizes(0, 0, 0);
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
        char label[300];
        sprintf(label, "As set: %.200s", value);
        own->label = label;
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
        XmString item = xmstr(label);
        XmComboBoxAddItem(family_w_, item, 0, False);
        XmStringFree(item);
        fi = families_.size() - 1;
        px = 0;
        width = 0;
    }
    family_ = fi;
    select_item(family_w_, families_[fi]->label.c_str());
    fill_sizes(fi, px, width);
    font_as_set_ = value;
    font_shown_ = chosen_font();
    update_preview();
}

/* The sizes of family fi, selecting px (and width), or the nearest. */
void TerminalPage::fill_sizes(int fi, int px, int width)
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

Str TerminalPage::chosen_font() const
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
Str TerminalPage::preview_font() const
{
    Str f = chosen_font();
    if (!f.empty())
        return f;
    int i;
    for (i = 0; k_term_auto_fonts[i]; i++) {
        int n = 0;
        char** names = XListFonts(dpy_, k_term_auto_fonts[i], 1, &n);
        if (names)
            XFreeFontNames(names);
        if (n > 0)
            return Str(k_term_auto_fonts[i]);
    }
    return Str("fixed");
}

void TerminalPage::update_preview()
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
    Pixel fg = alloc_color(dpy_, color(C_FG), &preview_fg_, &have_preview_fg_);
    Pixel bg = alloc_color(dpy_, color(C_BG), &preview_bg_, &have_preview_bg_);
    XtSetArg(args[n], XmNforeground, fg); n++;
    XtSetArg(args[n], XmNbackground, bg); n++;
    XtSetValues(preview_, args, n);
    if (fl)
        XmFontListFree(fl);
}

void TerminalPage::family_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    TerminalPage* self = (TerminalPage*)closure;
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

void TerminalPage::size_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ((TerminalPage*)closure)->update_preview();
}

/* ------------------------------------------------------------------ */
/* Colours                                                             */

Rgb TerminalPage::color(int i) const
{
    if (mine_[i])
        return color_[i];
    if (i == C_CURSOR)
        return color(C_FG);
    XColor c;
    Colormap cmap = DefaultColormap(dpy_, DefaultScreen(dpy_));
    const char* d = k_term_settings[k_color_settings[i]].fallback;
    if (d && XParseColor(dpy_, cmap, d, &c))
        return Rgb(c.red, c.green, c.blue);
    return Rgb();
}

void TerminalPage::update_swatches()
{
    int i;
    for (i = 0; i < C_COUNT; i++) {
        Rgb c = color(i);
        char hex[16];
        format_hex_color(c, hex);
        XmString l = xmstr(hex);
        Arg args[1];
        XtSetArg(args[0], XmNlabelString, l);
        XtSetValues(swatch_[i], args, 1);
        XmStringFree(l);
        XmChangeColor(swatch_[i], alloc_color(dpy_, c, &pixel_[i],
                                              &have_pixel_[i]));
        XtSetSensitive(reset_[i], mine_[i]);
    }
    update_preview();
}

void TerminalPage::change_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    Slot* slot = (Slot*)closure;
    TerminalPage* self = slot->page;
    self->picking_ = slot->index;
    static const char* const titles[3] = {
        "Terminal Text", "Terminal Background", "Terminal Cursor"
    };
    self->picker_->show(self->top_, titles[slot->index],
                        self->color(slot->index));
}

void TerminalPage::picked(void* closure, const Rgb& c)
{
    TerminalPage* self = (TerminalPage*)closure;
    int i = self->picking_;
    if (i < 0)
        return;
    self->mine_[i] = true;
    self->color_[i] = c;
    self->as_set_[i] = "";
    self->update_swatches();
}

void TerminalPage::reset_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    Slot* slot = (Slot*)closure;
    slot->page->mine_[slot->index] = false;
    slot->page->as_set_[slot->index] = "";
    slot->page->update_swatches();
}

/* ------------------------------------------------------------------ */

/* Settings left at their defaults are removed from the file, so ttrt's X
 * resources (TTRt*...) still apply to them. */
void TerminalPage::save(SettingsFile* file)
{
    Str font = chosen_font();
    if (!font_as_set_.empty() && strcmp(font.c_str(), font_shown_.c_str()) == 0)
        font = font_as_set_;
    if (font.empty())
        file->remove(setting_name(TS_FONT).c_str());
    else
        file->set(setting_name(TS_FONT).c_str(), font.c_str());

    int i;
    for (i = 0; i < C_COUNT; i++) {
        Str name = setting_name(k_color_settings[i]);
        if (mine_[i] && !as_set_[i].empty()) {
            file->set(name.c_str(), as_set_[i].c_str());   /* "wheat" stays */
        } else if (mine_[i]) {
            char hex[16];
            format_hex_color(color_[i], hex);
            file->set(name.c_str(), hex);
        } else {
            file->remove(name.c_str());
        }
    }

    const char* v[TS_COUNT];
    for (i = 0; i < TS_COUNT; i++)
        v[i] = 0;
    v[TS_BOLD_IS_BRIGHT] = XmToggleButtonGetState(bold_bright_) ? "true" : "false";
    v[TS_LOGIN_SHELL] = XmToggleButtonGetState(login_) ? "true" : "false";
    v[TS_MENU_BAR] = XmToggleButtonGetState(menu_bar_) ? "true" : "false";
    v[TS_BACKSPACE] = XmToggleButtonGetState(backspace_del_) ? "del" : "bs";
    int b = selected_pos(bell_);
    v[TS_BELL] = k_bell_values[b >= 0 && b < k_nbells ? b : 0];
    Str lines = combo_text(save_lines_);
    const char* p = lines.c_str();
    bool digits = *p != 0;
    for (; *p; p++)
        if (!isdigit((unsigned char)*p))
            digits = false;
    if (digits)
        v[TS_SAVE_LINES] = lines.c_str();
    for (i = 0; i < TS_COUNT; i++) {
        if (!v[i])
            continue;
        Str name = setting_name(i);
        const char* d = k_term_settings[i].fallback;
        if (d && strcmp(d, v[i]) == 0)
            file->remove(name.c_str());
        else
            file->set(name.c_str(), v[i]);
    }
}

}
