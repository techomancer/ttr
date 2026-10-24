#include "fontchooser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xatom.h>
#include <Xm/ComboBox.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/RowColumn.h>
#include <Xm/ToggleB.h>

using ttr::Str;

namespace tx {

namespace {

/* Sizes offered for scalable fonts. */
const int k_scalable_sizes[] = { 8, 9, 10, 11, 12, 13, 14, 15, 16, 18, 20, 22, 24, 28, 32, 36 };
const int k_nscalable = (int)(sizeof(k_scalable_sizes) / sizeof(k_scalable_sizes[0]));

XmString xmstr(const char* s)
{
    return XmStringCreateLocalized((char*)s);
}

int compare_nocase(const char* a, const char* b)
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
Str field(const char* xlfd, int i)
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
    for (int k = 1; k < i; k++) {
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
    memcpy(buf, p, (size_t)n);
    buf[n] = 0;
    return Str(buf);
}

bool is_bold(const char* weight)
{
    return strstr(weight, "bold") || strstr(weight, "demi") || strstr(weight, "black") ||
           strstr(weight, "heavy");
}

/* The list's selected position, from 0, or -1. */
int selected_pos(Widget combo)
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
void select_item(Widget combo, const char* value)
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

Widget drop_down(Widget parent, const char* name, int visible, int columns)
{
    Arg args[2];
    XtSetArg(args[0], XmNvisibleItemCount, visible);
    XtSetArg(args[1], XmNcolumns, columns);
    Widget cb = XmCreateDropDownList(parent, (char*)name, args, 2);
    XtManageChild(cb);
    return cb;
}

/* The real name of a font the server loads (an alias, a pattern), or the
 * name itself. */
Str resolve(Display* dpy, const char* name)
{
    Str real(name);
    XFontStruct* fs = XLoadQueryFont(dpy, name);
    if (!fs)
        return real;
    unsigned long atom = 0;
    if (XGetFontProperty(fs, XA_FONT, &atom) && atom) {
        char* n = XGetAtomName(dpy, (Atom)atom);
        if (n) {
            /* Font names are case-insensitive; IRIX answers in mixed case
             * (-Adobe-Helvetica-Bold-R-...), the lists in lower case. */
            for (char* p = n; *p; p++)
                *p = (char)tolower((unsigned char)*p);
            real = n;
            XFree(n);
        }
    }
    XFreeFont(dpy, fs);
    return real;
}

} /* namespace */

FontChooser::FontChooser(Widget parent, const char* name, unsigned flags)
    : dpy_(XtDisplay(parent)), flags_(flags), box_(0), family_w_(0), size_w_(0), bold_w_(0),
      preview_(0), family_(0), set_bold_(false), changed_fn_(0), changed_data_(0),
      preview_fn_(0), preview_data_(0)
{
    load();
    Arg args[6];
    int n = 0;
    XtSetArg(args[n], XmNorientation, XmVERTICAL); n++;
    XtSetArg(args[n], XmNmarginWidth, 0); n++;
    XtSetArg(args[n], XmNmarginHeight, 0); n++;
    XtSetArg(args[n], XmNspacing, 4); n++;
    box_ = XmCreateRowColumn(parent, (char*)name, args, n);
    n = 0;
    XtSetArg(args[n], XmNorientation, XmHORIZONTAL); n++;
    XtSetArg(args[n], XmNmarginWidth, 0); n++;
    XtSetArg(args[n], XmNmarginHeight, 0); n++;
    XtSetArg(args[n], XmNspacing, 6); n++;
    Widget row = XmCreateRowColumn(box_, (char*)"row", args, n);

    /* A combo box widens to its longest entry, and some families have
     * long names: keep the width 18 columns give an empty one. */
    family_w_ = drop_down(row, "family", 15, 18);
    Dimension fw = 0;
    XtSetArg(args[0], XmNwidth, &fw);
    XtGetValues(family_w_, args, 1);
    for (int i = 0; i < families_.size(); i++) {
        XmString item = xmstr(families_[i]->label.c_str());
        XmComboBoxAddItem(family_w_, item, 0, False);
        XmStringFree(item);
    }
    XtSetArg(args[0], XmNwidth, fw);
    XtSetValues(family_w_, args, 1);
    size_w_ = drop_down(row, "size", 12, 9);
    if (flags_ & BOLD) {
        XmString l = xmstr("Bold");
        XtSetArg(args[0], XmNlabelString, l);
        bold_w_ = XmCreateToggleButton(row, (char*)"bold", args, 1);
        XmStringFree(l);
        XtManageChild(bold_w_);
        XtAddCallback(bold_w_, XmNvalueChangedCallback, size_cb, (XtPointer)this);
    }
    XtManageChild(row);
    XtAddCallback(family_w_, XmNselectionCallback, family_cb, (XtPointer)this);
    XtAddCallback(size_w_, XmNselectionCallback, size_cb, (XtPointer)this);

    if (!(flags_ & NO_PREVIEW)) {
        n = 0;
        XmString pt = xmstr((flags_ & FIXED) ? "$ ls -l   AaBbYyZz 0123 {}[]|~" : "AaBbYyZz 0123");
        XtSetArg(args[n], XmNlabelString, pt); n++;
        XtSetArg(args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
        XtSetArg(args[n], XmNmarginWidth, 8); n++;
        XtSetArg(args[n], XmNmarginHeight, 4); n++;
        preview_ = XmCreateLabel(box_, (char*)"preview", args, n);
        XmStringFree(pt);
        XtManageChild(preview_);
    }
    XtManageChild(box_);
    set("");
}

FontChooser::~FontChooser()
{
    for (int i = 0; i < families_.size(); i++) {
        for (int k = 0; k < families_[i]->sizes.size(); k++)
            delete families_[i]->sizes[k];
        delete families_[i];
    }
}

/* Every upright, non-bold font in Latin-1 (and ISO 10646 with UNICODE),
 * fixed-width ones only with FIXED, grouped by family. */
void FontChooser::load()
{
    if (flags_ & AUTOMATIC) {
        Family* a = new Family;
        a->label = "Automatic";
        a->scalable = false;
        a->scalable_rank = 0;
        families_.push_back(a);
    }
    static const char* const fixed[] = {
        "-*-*-*-r-*-*-*-*-*-*-c-*-*-*", "-*-*-*-r-*-*-*-*-*-*-m-*-*-*", 0
    };
    static const char* const any[] = { "-*-*-*-r-*-*-*-*-*-*-*-*-*-*", 0 };
    const char* const* patterns = (flags_ & FIXED) ? fixed : any;
    for (int i = 0; patterns[i]; i++) {
        int n = 0;
        char** names = XListFonts(dpy_, patterns[i], 30000, &n);
        for (int k = 0; k < n; k++)
            add_font(names[k]);
        if (names)
            XFreeFontNames(names);
    }
    for (int i = (flags_ & AUTOMATIC) ? 1 : 0; i < families_.size(); i++)
        finish_family(families_[i]);
}

FontChooser::Family* FontChooser::family_named(const char* name, bool create)
{
    int first = (flags_ & AUTOMATIC) ? 1 : 0;
    int at = families_.size();
    for (int i = first; i < families_.size(); i++) {
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
    for (int i = families_.size() - 1; i > at; i--)
        families_[i] = families_[i - 1];
    families_[at] = f;
    return f;
}

void FontChooser::add_font(const char* xlfd)
{
    Str fam = field(xlfd, 2), weight = field(xlfd, 3), width = field(xlfd, 5),
        px = field(xlfd, 7), avg = field(xlfd, 12), reg = field(xlfd, 13), enc = field(xlfd, 14);
    if (fam.empty() || px.empty())
        return;
    const char* w = weight.c_str();
    if (is_bold(w))
        return;
    bool unicode = strcmp(reg.c_str(), "iso10646") == 0;
    if (strcmp(enc.c_str(), "1") != 0 || (!unicode && strcmp(reg.c_str(), "iso8859") != 0) ||
        (unicode && !(flags_ & UNICODE)))
        return;
    int rank = (unicode ? 0 : 10) +
               (strcmp(w, "medium") == 0                                 ? 0
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
    /* Fixed-width fonts of one height can differ in width (6x13, 7x13);
     * a proportional font's average width says nothing: one per height. */
    int avgw = (flags_ & FIXED) ? (atoi(avg.c_str()) + 5) / 10 : 0;
    for (int i = 0; i < f->sizes.size(); i++) {
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
void FontChooser::finish_family(Family* f)
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
                    k_scalable_sizes[i], field(x, 11).c_str(), field(x, 13).c_str(),
                    field(x, 14).c_str());
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

void FontChooser::set(const char* value)
{
    set_value_ = value ? value : "";
    set_bold_ = false;
    if (!value || !*value) {
        if (flags_ & AUTOMATIC) {
            family_ = 0;
            select_item(family_w_, families_[0]->label.c_str());
            fill_sizes(0, 0, 0);
        } else {
            family_ = 0;
            select_item(family_w_, families_.size() ? families_[0]->label.c_str() : "");
            fill_sizes(0, 13, 0);
        }
        if (bold_w_)
            XmToggleButtonSetState(bold_w_, False, False);
        set_shown_ = chosen();
        update_preview();
        return;
    }
    Str real = resolve(dpy_, value);
    /* A pattern the server does not resolve still names its family and
     * size. */
    if (field(real.c_str(), 2).empty())
        real = value;
    set_bold_ = is_bold(field(real.c_str(), 3).c_str());
    Family* f = family_named(field(real.c_str(), 2).c_str(), false);
    int px = atoi(field(real.c_str(), 7).c_str());
    int width = (atoi(field(real.c_str(), 12).c_str()) + 5) / 10;
    int fi = -1;
    if (f) {
        int i;
        for (i = 0; i < f->sizes.size(); i++)
            if (f->sizes[i]->px == px || px == 0)
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
        set_bold_ = false;
    }
    family_ = fi;
    select_item(family_w_, families_[fi]->label.c_str());
    fill_sizes(fi, px, width);
    if (bold_w_)
        XmToggleButtonSetState(bold_w_, set_bold_ ? True : False, False);
    set_shown_ = chosen();
    update_preview();
}

/* The sizes of family fi, selecting px (and width), or the nearest. */
void FontChooser::fill_sizes(int fi, int px, int width)
{
    Widget list = XtNameToWidget(size_w_, "*List");
    if (!list)
        return;
    XmListDeleteAllItems(list);
    if (fi < 0 || fi >= families_.size() || families_[fi]->sizes.size() == 0) {
        XmString s = xmstr("-");
        XmListAddItemUnselected(list, s, 0);
        XmComboBoxSelectItem(size_w_, s);
        XmStringFree(s);
        XtSetSensitive(size_w_, False);
        return;
    }
    Family* f = families_[fi];
    XtSetSensitive(size_w_, f->sizes.size() > 1);
    int best = 0, best_d = 1 << 30;
    for (int i = 0; i < f->sizes.size(); i++) {
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

/* The chosen family and size's font, made bold when Bold is on. */
Str FontChooser::chosen() const
{
    if (family_ < 0 || family_ >= families_.size())
        return Str("");
    Family* f = families_[family_];
    if (f->sizes.size() == 0)
        return Str("");             /* Automatic */
    int si = selected_pos(size_w_);
    if (si < 0 || si >= f->sizes.size())
        si = 0;
    Str x = f->sizes[si]->xlfd;
    if (bold_w_ && XmToggleButtonGetState(bold_w_))
        return bolden(x.c_str());
    return x;
}

/* The bold font of the same family and size the server has, or the font
 * itself: families name their bold weights differently. */
Str FontChooser::bolden(const char* xlfd) const
{
    if (field(xlfd, 2).empty())
        return Str(xlfd);
    static const char* const weights[] = { "bold", "demibold", "semibold", "black", 0 };
    for (int i = 0; weights[i]; i++) {
        char pat[400];
        sprintf(pat, "-%.60s-%.100s-%s-%.10s-%.40s-*-%.10s-*-*-*-%.10s-*-%.40s-%.20s",
                field(xlfd, 1).c_str(), field(xlfd, 2).c_str(), weights[i],
                field(xlfd, 4).c_str(), field(xlfd, 5).c_str(), field(xlfd, 7).c_str(),
                field(xlfd, 11).c_str(), field(xlfd, 13).c_str(), field(xlfd, 14).c_str());
        int n = 0;
        char** found = XListFonts(dpy_, pat, 1, &n);
        if (found && n > 0) {
            Str r(found[0]);
            XFreeFontNames(found);
            return r;
        }
        if (found)
            XFreeFontNames(found);
    }
    return Str(xlfd);
}

Str FontChooser::get() const
{
    Str c = chosen();
    /* Unchanged: what was set, as it was written. */
    if (strcmp(c.c_str(), set_shown_.c_str()) == 0)
        return set_value_;
    return c;
}

void FontChooser::set_automatic_preview(const char* font)
{
    automatic_ = font ? font : "";
    update_preview();
}

void FontChooser::set_automatic_label(const char* label)
{
    if (!(flags_ & AUTOMATIC) || !families_.size())
        return;
    bool shown = family_ == 0;
    XmString old = xmstr(families_[0]->label.c_str());
    families_[0]->label = label;
    XmString s = xmstr(label);
    XmComboBoxDeletePos(family_w_, 1);
    XmComboBoxAddItem(family_w_, s, 1, False);
    if (shown)
        XmComboBoxSelectItem(family_w_, s);
    XmStringFree(s);
    XmStringFree(old);
}

void FontChooser::set_preview_colors(Pixel fg, Pixel bg)
{
    if (!preview_)
        return;
    Arg args[2];
    XtSetArg(args[0], XmNforeground, fg);
    XtSetArg(args[1], XmNbackground, bg);
    XtSetValues(preview_, args, 2);
}

void FontChooser::set_preview_text(const char* text)
{
    if (!preview_)
        return;
    XmString s = xmstr(text);
    Arg args[1];
    XtSetArg(args[0], XmNlabelString, s);
    XtSetValues(preview_, args, 1);
    XmStringFree(s);
}

void FontChooser::set_preview_name(PreviewName fn, void* data)
{
    preview_fn_ = fn;
    preview_data_ = data;
    update_preview();
}

void FontChooser::on_changed(Changed fn, void* data)
{
    changed_fn_ = fn;
    changed_data_ = data;
}

void FontChooser::update_preview()
{
    if (!preview_)
        return;
    Str name = chosen();
    if (name.empty())
        name = automatic_;
    if (name.empty())
        return;
    if (preview_fn_)
        name = preview_fn_(name.c_str(), preview_data_);
    XmFontListEntry e = XmFontListEntryLoad(dpy_, (char*)name.c_str(), XmFONT_IS_FONT,
                                            (char*)XmFONTLIST_DEFAULT_TAG);
    if (!e)
        return;
    XmFontList fl = XmFontListAppendEntry(0, e);
    XmFontListEntryFree(&e);
    Arg args[1];
    XtSetArg(args[0], XmNfontList, fl);
    XtSetValues(preview_, args, 1);
    XmFontListFree(fl);
}

void FontChooser::changed()
{
    update_preview();
    if (changed_fn_)
        changed_fn_(this, changed_data_);
}

void FontChooser::family_cb(Widget, XtPointer closure, XtPointer)
{
    FontChooser* self = (FontChooser*)closure;
    int fi = selected_pos(self->family_w_);
    if (fi < 0 || fi >= self->families_.size() || fi == self->family_)
        return;
    /* Keep the size, or the nearest the new family has. */
    int px = 13, width = 0;
    if (self->family_ >= 0 && self->family_ < self->families_.size()) {
        Family* old = self->families_[self->family_];
        int si = selected_pos(self->size_w_);
        if (si >= 0 && si < old->sizes.size() && old->sizes[si]->px > 0) {
            px = old->sizes[si]->px;
            width = old->sizes[si]->width;
        }
    }
    self->family_ = fi;
    self->fill_sizes(fi, px, width);
    self->changed();
}

void FontChooser::size_cb(Widget, XtPointer closure, XtPointer)
{
    ((FontChooser*)closure)->changed();
}

} /* namespace tx */
