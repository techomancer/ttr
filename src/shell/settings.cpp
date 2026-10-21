#include "shell/settings.h"
#include "shell/colorspage.h"
#include "shell/shortcutsdlg.h"
#include "shell/startuppage.h"
#include "shell/terminalpage.h"
#include "core/scale.h"
#include "core/settings.h"
#include "x11/config.h"

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
#include <Xm/TextF.h>
#include <Xm/ToggleB.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace ttr {

static const int k_label_width = 140;

/* Bitmap fonts have some of these; scalable (Type1, TrueType) ones all. */
static const char* const k_sizes[] = {
    "8", "9", "10", "11", "12", "13", "14", "15", "16", "17", "18", "20",
    "22", "24", "26", "28", "32", "36"
};
static const int k_nsizes = (int)(sizeof(k_sizes) / sizeof(k_sizes[0]));

static const char* const k_sources[] = { "auto", "desktop", "toolchest" };

static const char* const k_grips[] = { "none", "line", "ridges", "dot" };
static const int k_ngrips = 4;

static const char* const k_scales[] = {
    "100", "125", "150", "175", "200", "250", "300"
};
static const int k_nscales = (int)(sizeof(k_scales) / sizeof(k_scales[0]));

static const char* const k_notify_times[] = {
    "3", "5", "8", "10", "15", "30", "60"
};
static const int k_nnotify =
    (int)(sizeof(k_notify_times) / sizeof(k_notify_times[0]));

static const char* const k_desktop_counts[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14",
    "15", "16"
};
static const int k_ncounts =
    (int)(sizeof(k_desktop_counts) / sizeof(k_desktop_counts[0]));

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

/* Field i of an XLFD ("-foundry-family-weight-slant-..."), or "". */
static Str xlfd_field(const char* name, int i)
{
    if (name[0] != '-')
        return Str("");
    const char* p = name + 1;
    int f = 1;
    while (f < i && *p) {
        if (*p == '-')
            f++;
        p++;
    }
    if (f != i)
        return Str("");
    const char* e = strchr(p, '-');
    int n = e ? (int)(e - p) : (int)strlen(p);
    char buf[128];
    if (n >= (int)sizeof(buf))
        n = sizeof(buf) - 1;
    memcpy(buf, p, n);
    buf[n] = 0;
    return Str(buf);
}

SettingsDialog::SettingsDialog(Widget parent, WmLink* wm, const Config& cfg,
                               const AppFolder* apps)
    : parent_(parent), wm_(wm), cfg_(cfg), apps_(apps), dialog_(0),
      categories_(0), current_page_(0),
      shortcuts_(new ShortcutsDialog(parent, wm, apps)), startup_(0),
      xdg_(0), colors_(0), terminal_page_(0)
{
}

SettingsDialog::~SettingsDialog()
{
    delete terminal_page_;
    delete colors_;
    delete xdg_;
    delete startup_;
    delete shortcuts_;
}

/* One page: a column of frames in the page area, shown alone. */
Widget SettingsDialog::page(Widget parent, int index)
{
    Arg args[6];
    int n = 0;
    XtSetArg(args[n], XmNorientation, XmVERTICAL); n++;
    XtSetArg(args[n], XmNspacing, scaled(6)); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    pages_[index] = XmCreateRowColumn(parent, (char*)"page", args, n);
    return pages_[index];
}

void SettingsDialog::show_page(int index)
{
    if (index < 0 || index >= PAGE_COUNT)
        index = 0;
    current_page_ = index;
    int i;
    for (i = 0; i < PAGE_COUNT; i++)
        if (i != index)
            XtUnmanageChild(pages_[i]);
    XtManageChild(pages_[index]);
    XmListSelectPos(categories_, index + 1, False);
}

void SettingsDialog::page_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    XmListCallbackStruct* cbs = (XmListCallbackStruct*)call;
    ((SettingsDialog*)closure)->show_page(cbs->item_position - 1);
}

void SettingsDialog::shortcuts_cb(Widget w, XtPointer closure,
                                  XtPointer call)
{
    (void)w;
    (void)call;
    ((SettingsDialog*)closure)->shortcuts_->show();
}

void SettingsDialog::show()
{
    /* Rebuilt every time, so it shows the current values. */
    if (dialog_)
        XtDestroyWidget(XtParent(dialog_));
    build();
    XtManageChild(dialog_);
}

/* ------------------------------------------------------------------ */
/* Building blocks                                                     */

Widget SettingsDialog::frame(Widget parent, const char* title)
{
    Widget f = XmCreateFrame(parent, (char*)"frame", 0, 0);
    XmString l = xmstr(title);
    Arg args[2];
    XtSetArg(args[0], XmNlabelString, l);
    XtSetArg(args[1], XmNchildType, XmFRAME_TITLE_CHILD);
    XtManageChild(XmCreateLabel(f, (char*)"title", args, 2));
    XmStringFree(l);

    Arg rargs[3];
    XtSetArg(rargs[0], XmNorientation, XmVERTICAL);
    XtSetArg(rargs[1], XmNmarginWidth, scaled(8));
    XtSetArg(rargs[2], XmNmarginHeight, scaled(6));
    Widget rc = XmCreateRowColumn(f, (char*)"rows", rargs, 3);
    XtManageChild(rc);
    XtManageChild(f);
    return rc;
}

/* A horizontal row starting with a fixed-width label. */
Widget SettingsDialog::row(Widget parent, const char* label)
{
    Arg args[4];
    XtSetArg(args[0], XmNorientation, XmHORIZONTAL);
    XtSetArg(args[1], XmNmarginWidth, 0);
    XtSetArg(args[2], XmNmarginHeight, 0);
    Widget rc = XmCreateRowColumn(parent, (char*)"row", args, 3);

    XmString l = xmstr(label);
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

/* Two radio buttons; returns the box, *first is the first button. */
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

/* The selected entry's text. */
static Str selected_item(Widget combo, const char* const* items, int count)
{
    Widget list = XtNameToWidget(combo, "*List");
    int* pos = 0;
    int n = 0;
    Str result(count > 0 ? items[0] : "");
    if (!list)
        return result;
    Arg args[2];
    XtSetArg(args[0], XmNselectedPositions, &pos);  /* the list's own */
    XtSetArg(args[1], XmNselectedPositionCount, &n);
    XtGetValues(list, args, 2);
    if (pos && n > 0 && pos[0] >= 1 && pos[0] <= count)
        result = items[pos[0] - 1];
    return result;
}

/* The selected entry's text, read from the list itself. */
Str combo_text(Widget combo)
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

Widget SettingsDialog::text_field(Widget parent, const char* value,
                                  int columns)
{
    Arg args[1];
    XtSetArg(args[0], XmNcolumns, columns);
    Widget t = XmCreateTextField(parent, (char*)"text", args, 1);
    XmTextFieldSetString(t, (char*)value);
    XtManageChild(t);
    return t;
}

/* ------------------------------------------------------------------ */
/* Fonts                                                               */

/* The families the X server has, sorted. Scalable ones (Type1, TrueType:
 * pixel size 0 in their names) are marked with a *, since they come in
 * every size. */
void SettingsDialog::load_families()
{
    families_.clear();
    family_labels_.clear();
    Vector<int> scalable;
    int n = 0;
    char** names = XListFonts(XtDisplay(parent_),
                              "-*-*-*-r-*--*-*-*-*-*-*-iso8859-1", 60000, &n);
    int i, j;
    for (i = 0; i < n; i++) {
        Str fam = xlfd_field(names[i], 2);
        if (fam.empty())
            continue;
        int is_scalable = strcmp(xlfd_field(names[i], 7).c_str(), "0") == 0;
        /* Insert sorted, skipping duplicates. */
        int at = families_.size();
        bool dup = false;
        for (j = 0; j < families_.size(); j++) {
            int c = compare_nocase(fam.c_str(), families_[j].c_str());
            if (c == 0) {
                dup = true;
                if (is_scalable)
                    scalable[j] = 1;
                break;
            }
            if (c < 0) {
                at = j;
                break;
            }
        }
        if (dup)
            continue;
        families_.push_back(fam);
        scalable.push_back(is_scalable);
        for (j = families_.size() - 1; j > at; j--) {
            families_[j] = families_[j - 1];
            scalable[j] = scalable[j - 1];
        }
        families_[at] = fam;
        scalable[at] = is_scalable;
    }
    if (names)
        XFreeFontNames(names);
    for (i = 0; i < families_.size(); i++) {
        char label[300];
        sprintf(label, "%.200s%s", families_[i].c_str(),
                scalable[i] ? " *" : "");
        family_labels_.push_back(Str(label));
    }
}

int SettingsDialog::family_index(const char* family)
{
    int k;
    for (k = 0; k < families_.size(); k++)
        if (compare_nocase(families_[k].c_str(), family) == 0)
            return k;
    return -1;
}

void SettingsDialog::font_row(FontRow* r, Widget parent, const char* label,
                              const char* current, const char* resolved)
{
    r->dialog = this;
    Widget rc = row(parent, label);

    const char** fams = new const char*[families_.size() + 1];
    int i;
    for (i = 0; i < families_.size(); i++)
        fams[i] = family_labels_[i].c_str();
    /* A combo box widens to its longest entry, and some families have
     * long names: keep the width 18 columns give an empty one. */
    r->family = drop_down(rc, "family", fams, 0, 15, 18);
    Dimension fw = 0;
    Arg wargs[1];
    XtSetArg(wargs[0], XmNwidth, &fw);
    XtGetValues(r->family, wargs, 1);
    for (i = 0; i < families_.size(); i++) {
        XmString item = xmstr(fams[i]);
        XmComboBoxAddItem(r->family, item, 0, False);
        XmStringFree(item);
    }
    XtSetArg(wargs[0], XmNwidth, fw);
    XtSetValues(r->family, wargs, 1);
    delete [] fams;
    r->size = drop_down(rc, "size", k_sizes, k_nsizes, k_nsizes, 3);

    XmString l = xmstr("Bold");
    Arg args[1];
    XtSetArg(args[0], XmNlabelString, l);
    r->bold = XmCreateToggleButton(rc, (char*)"bold", args, 1);
    XmStringFree(l);
    XtManageChild(r->bold);

    l = xmstr("AaBbYyZz 0123");
    XtSetArg(args[0], XmNlabelString, l);
    r->preview = XmCreateLabel(rc, (char*)"preview", args, 1);
    XmStringFree(l);
    XtManageChild(r->preview);

    /* Show the current font; names that are not XLFDs show the default. */
    Str fam = xlfd_field(current, 2), weight = xlfd_field(current, 3),
        size = xlfd_field(current, 7);
    /* A family that is not installed shows as the one ttr uses instead,
     * so OK does not replace it with the first one on the list. */
    if (family_index(fam.c_str()) < 0) {
        Str used = xlfd_field(resolved, 2);
        if (!used.empty())
            fam = used;
    }
    int k = family_index(fam.empty() ? "helvetica" : fam.c_str());
    select_item(r->family, k >= 0 ? family_labels_[k].c_str() : "");
    select_item(r->size, size.empty() || size.c_str()[0] == '*'
                             ? "12" : size.c_str());
    XmToggleButtonSetState(r->bold, strstr(weight.c_str(), "bold") != 0,
                           False);

    XtAddCallback(r->family, XmNselectionCallback, font_changed_cb,
                  (XtPointer)r);
    XtAddCallback(r->size, XmNselectionCallback, font_changed_cb,
                  (XtPointer)r);
    XtAddCallback(r->bold, XmNvalueChangedCallback, font_changed_cb,
                  (XtPointer)r);
    update_preview(r);
}

/* The XLFD for a row. Families name their regular weight differently,
 * so try the usual names and keep the first one the server has. */
Str SettingsDialog::font_name(FontRow* r)
{
    const char** fams = new const char*[families_.size() + 1];
    int i;
    for (i = 0; i < families_.size(); i++)
        fams[i] = families_[i].c_str();
    Str fam = selected_item(r->family, fams, families_.size());
    delete [] fams;
    Str size = selected_item(r->size, k_sizes, k_nsizes);

    static const char* const regular[] = {
        "medium", "regular", "book", "normal", "*", 0
    };
    static const char* const bold[] = { "bold", "demibold", "*", 0 };
    const char* const* weights = XmToggleButtonGetState(r->bold) ? bold
                                                                 : regular;
    char buf[300];
    for (i = 0; weights[i]; i++) {
        sprintf(buf, "-*-%.100s-%s-r-normal--%s-*-*-*-*-*-iso8859-1",
                fam.c_str(), weights[i], size.c_str());
        int n = 0;
        char** found = XListFonts(XtDisplay(parent_), buf, 1, &n);
        if (found)
            XFreeFontNames(found);
        if (n > 0)
            return Str(buf);
    }
    return Str(buf);
}

/* At the scale ttr runs at, like the rest of the dialog; a new scale
 * applies after OK. */
void SettingsDialog::update_preview(FontRow* r)
{
    Str name = scale_font(XtDisplay(parent_), font_name(r).c_str(),
                          cfg_.ui_scale);
    XmFontListEntry e = XmFontListEntryLoad(XtDisplay(parent_),
                                            (char*)name.c_str(),
                                            XmFONT_IS_FONT,
                                            (char*)XmFONTLIST_DEFAULT_TAG);
    if (!e)
        return;
    XmFontList fl = XmFontListAppendEntry(0, e);
    XmFontListEntryFree(&e);
    Arg args[1];
    XtSetArg(args[0], XmNfontList, fl);
    XtSetValues(r->preview, args, 1);
    XmFontListFree(fl);
}

void SettingsDialog::font_changed_cb(Widget w, XtPointer closure,
                                     XtPointer call)
{
    (void)w;
    (void)call;
    FontRow* r = (FontRow*)closure;
    r->dialog->update_preview(r);
}

/* ------------------------------------------------------------------ */
/* The dialog                                                          */

void SettingsDialog::build()
{
    load_families();

    XmString title = xmstr("Settings");
    Arg args[6];
    int n = 0;
    XtSetArg(args[n], XmNdialogTitle, title); n++;
    XtSetArg(args[n], XmNautoUnmanage, False); n++;
    XtSetArg(args[n], XmNmarginWidth, scaled(10)); n++;
    XtSetArg(args[n], XmNmarginHeight, scaled(10)); n++;
    dialog_ = XmCreateFormDialog(parent_, (char*)"settingsDialog", args, n);
    XmStringFree(title);

    /* Categories on the left, the chosen page on the right. */
    static const char* const names[PAGE_COUNT] = {
        "Windows", "Desktops", "Appearance", "Colors", "Taskbar",
        "Terminal", "Start Menu", "Startup", "XDG Autostart"
    };
    XmString items[PAGE_COUNT];
    int i;
    for (i = 0; i < PAGE_COUNT; i++)
        items[i] = xmstr(names[i]);
    n = 0;
    XtSetArg(args[n], XmNitems, items); n++;
    XtSetArg(args[n], XmNitemCount, PAGE_COUNT); n++;
    XtSetArg(args[n], XmNvisibleItemCount, PAGE_COUNT); n++;
    XtSetArg(args[n], XmNselectionPolicy, XmBROWSE_SELECT); n++;
    XtSetArg(args[n], XmNlistMarginWidth, scaled(4)); n++;
    categories_ = XmCreateScrolledList(dialog_, (char*)"categories", args, n);
    for (i = 0; i < PAGE_COUNT; i++)
        XmStringFree(items[i]);
    XtAddCallback(categories_, XmNbrowseSelectionCallback, page_cb,
                  (XtPointer)this);
    n = 0;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetValues(XtParent(categories_), args, n);
    XtManageChild(categories_);

    n = 0;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNleftWidget, XtParent(categories_)); n++;
    XtSetArg(args[n], XmNleftOffset, scaled(10)); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    Widget content = XmCreateForm(dialog_, (char*)"pages", args, n);

    /* Windows */
    Widget pg = page(content, PAGE_WINDOWS);
    Widget f = frame(pg, "Focus and switching");
    Widget unused;
    radio(row(f, "Focus:"), "Click to focus", "Focus follows mouse",
          &click_focus_);
    XmToggleButtonSetState(click_focus_, cfg_.focus == Config::FOCUS_CLICK,
                           True);
    if (cfg_.focus != Config::FOCUS_CLICK)
        XmToggleButtonSetState(XtNameToWidget(XtParent(click_focus_), "b"),
                               True, True);
    radio(row(f, "Alt+Tab:"), "Windows style (icon list)",
          "Stack (mwm / CDE style)", &windows_switch_);
    unused = XtNameToWidget(XtParent(windows_switch_), "b");
    XmToggleButtonSetState(cfg_.switch_style == Config::SWITCH_WINDOWS
                               ? windows_switch_ : unused, True, True);
    f = frame(pg, "Docking and snapping");
    radio(row(f, "Docked width:"), "1/3 of the screen", "1/2 of the screen",
          &dock_third_);
    XmToggleButtonSetState(cfg_.dock_div == 3 ? dock_third_
                               : XtNameToWidget(XtParent(dock_third_), "b"),
                           True, True);

    /* Desktops */
    pg = page(content, PAGE_DESKTOPS);
    f = frame(pg, "Virtual desktops");
    desktops_ = drop_down(row(f, "Number of desktops:"), "desktops",
                          k_desktop_counts, k_ncounts, 8, 3);
    char count[16];
    sprintf(count, "%d", cfg_.desktops);
    select_item(desktops_, count);

    /* Appearance */
    pg = page(content, PAGE_APPEARANCE);
    f = frame(pg, "Size");
    Widget srow0 = row(f, "UI scale:");
    scale_ = drop_down(srow0, "uiScale", k_scales, k_nscales, k_nscales, 3);
    XmString pct = xmstr("%   for high-resolution screens");
    Arg pargs[1];
    XtSetArg(pargs[0], XmNlabelString, pct);
    XtManageChild(XmCreateLabel(srow0, (char*)"unit", pargs, 1));
    XmStringFree(pct);
    /* A scale set by hand that is not on the list is added to it, so OK
     * keeps it. */
    char scale_now[16];
    sprintf(scale_now, "%d", cfg_.ui_scale);
    XmString cur = xmstr(scale_now);
    Widget scale_list = XtNameToWidget(scale_, "*List");
    if (scale_list && !XmListItemExists(scale_list, cur))
        XmComboBoxAddItem(scale_, cur, 0, False);
    XmStringFree(cur);
    select_item(scale_, scale_now);

    f = frame(pg, "Scroll bars");
    arrows_ends_ = 0;
    grip_ = 0;
    if (scroll_bar_styles()) {
        radio(row(f, "Arrows:"), "At both ends", "Together", &arrows_ends_);
        XmToggleButtonSetState(cfg_.scroll_arrows_together
                                   ? XtNameToWidget(XtParent(arrows_ends_),
                                                    "b")
                                   : arrows_ends_, True, True);
        grip_ = drop_down(row(f, "Slider grip:"), "grip", k_grips, k_ngrips,
                          k_ngrips, 7);
        select_item(grip_, cfg_.scroll_grip.c_str());
    } else {
        XmString l = xmstr("SGI's enhanced look draws the scroll bars here.");
        XtSetArg(pargs[0], XmNlabelString, l);
        XtManageChild(XmCreateLabel(f, (char*)"note", pargs, 1));
        XmStringFree(l);
    }
    f = frame(pg, "Fonts");
    font_row(&title_, f, "Window titles:", cfg_.title_font.c_str(),
             cfg_.title_xfont.c_str());
    font_row(&menu_, f, "Menus and dialogs:", cfg_.menu_font.c_str(),
             cfg_.menu_xfont.c_str());
    font_row(&taskbar_, f, "Taskbar:", cfg_.taskbar_font.c_str(),
             cfg_.taskbar_xfont.c_str());
    Widget fprow = row(f, "Font folders:");
    font_path_ = text_field(fprow, cfg_.font_path.c_str(), 40);
    XmString fpl = xmstr("for Type1 and TrueType fonts");
    XtSetArg(pargs[0], XmNlabelString, fpl);
    XtManageChild(XmCreateLabel(fprow, (char*)"unit", pargs, 1));
    XmStringFree(fpl);
    fpl = xmstr("* scalable (Type1, TrueType): every size looks sharp");
    XtSetArg(pargs[0], XmNlabelString, fpl);
    XtManageChild(XmCreateLabel(row(f, ""), (char*)"note", pargs, 1));
    XmStringFree(fpl);

    /* Colors */
    pg = page(content, PAGE_COLORS);
    delete colors_;
    colors_ = new ColorsPage(this, pg, cfg_);

    /* Taskbar */
    pg = page(content, PAGE_TASKBAR);
    f = frame(pg, "Clock");
    clock_ = text_field(row(f, "Clock format:"), cfg_.clock_format.c_str(),
                        12);
    f = frame(pg, "Notifications");
    Widget nrow = row(f, "Show for:");
    notify_time_ = drop_down(nrow, "notifyTime", k_notify_times, k_nnotify,
                             k_nnotify, 3);
    XmString secs_label = xmstr("seconds");
    Arg largs[1];
    XtSetArg(largs[0], XmNlabelString, secs_label);
    XtManageChild(XmCreateLabel(nrow, (char*)"unit", largs, 1));
    XmStringFree(secs_label);
    char secs[16];
    sprintf(secs, "%d", cfg_.notification_ms / 1000);
    select_item(notify_time_, secs);

    /* Terminal */
    pg = page(content, PAGE_TERMINAL);
    delete terminal_page_;
    terminal_page_ = new TerminalPage(this, pg);

    /* Start menu */
    pg = page(content, PAGE_START);
    f = frame(pg, "Programs");
    source_ = drop_down(row(f, "Programs from:"), "source", k_sources, 3, 3,
                        10);
    select_item(source_, cfg_.app_source_name.c_str());
    toolchest_ = text_field(row(f, "Toolchest files:"),
                            cfg_.toolchest_setting.c_str(), 40);
    terminal_ = text_field(row(f, "Terminal command:"),
                           cfg_.terminal.c_str(), 20);
    f = frame(pg, "Shortcuts");
    Widget srow = row(f, "Programs to launch:");
    XmString sl = xmstr("Edit Shortcuts...");
    Arg sargs[1];
    XtSetArg(sargs[0], XmNlabelString, sl);
    Widget sb = XmCreatePushButton(srow, (char*)"shortcuts", sargs, 1);
    XmStringFree(sl);
    XtAddCallback(sb, XmNactivateCallback, shortcuts_cb, (XtPointer)this);
    XtManageChild(sb);

    /* Startup */
    pg = page(content, PAGE_STARTUP);
    delete startup_;
    startup_ = new StartupPage(this, pg, cfg_, apps_);

    /* XDG Autostart */
    pg = page(content, PAGE_XDG);
    delete xdg_;
    xdg_ = new XdgAutostartPage(this, pg, cfg_);

    /* The page area is as big as the biggest page, so switching pages
     * does not resize the dialog. */
    Dimension pw = 0, ph = 0;
    for (i = 0; i < PAGE_COUNT; i++) {
        XtWidgetGeometry pref;
        XtQueryGeometry(pages_[i], 0, &pref);
        if (pref.width > pw)
            pw = pref.width;
        if (pref.height > ph)
            ph = pref.height;
    }
    n = 0;
    XtSetArg(args[n], XmNwidth, pw); n++;
    XtSetArg(args[n], XmNheight, ph); n++;
    XtSetArg(args[n], XmNresizePolicy, XmRESIZE_NONE); n++;
    XtSetValues(content, args, n);
    XtManageChild(content);
    show_page(current_page_);

    /* OK and Cancel along the bottom. */
    n = 0;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget, content); n++;
    XtSetArg(args[n], XmNtopOffset, scaled(10)); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    Widget sep = XmCreateSeparator(dialog_, (char*)"sep", args, n);
    XtManageChild(sep);

    XmString note = xmstr("OK saves the settings and restarts ttr.");
    n = 0;
    XtSetArg(args[n], XmNlabelString, note); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget, sep); n++;
    XtSetArg(args[n], XmNtopOffset, scaled(10)); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    Widget label = XmCreateLabel(dialog_, (char*)"note", args, n);
    XmStringFree(note);
    XtManageChild(label);

    XmString cancel = xmstr("Cancel"), ok = xmstr("OK");
    n = 0;
    XtSetArg(args[n], XmNlabelString, cancel); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget, sep); n++;
    XtSetArg(args[n], XmNtopOffset, scaled(6)); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    Widget cancel_b = XmCreatePushButton(dialog_, (char*)"cancel", args, n);
    n = 0;
    XtSetArg(args[n], XmNlabelString, ok); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget, sep); n++;
    XtSetArg(args[n], XmNtopOffset, scaled(6)); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNrightWidget, cancel_b); n++;
    Widget ok_b = XmCreatePushButton(dialog_, (char*)"ok", args, n);
    XmStringFree(cancel);
    XmStringFree(ok);
    XtAddCallback(ok_b, XmNactivateCallback, ok_cb, (XtPointer)this);
    XtAddCallback(cancel_b, XmNactivateCallback, cancel_cb, (XtPointer)this);
    XtManageChild(ok_b);
    XtManageChild(cancel_b);

    Arg dargs[2];
    XtSetArg(dargs[0], XmNdefaultButton, ok_b);
    XtSetArg(dargs[1], XmNcancelButton, cancel_b);
    XtSetValues(dialog_, dargs, 2);
}

static Str text_value(Widget t)
{
    char* s = XmTextFieldGetString(t);
    Str r(s ? s : "");
    XtFree(s);
    return r;
}

void SettingsDialog::save()
{
    SettingsFile file;
    Str path = SettingsFile::default_path();
    file.load(path.c_str());    /* keep what is there */

    file.set("focusPolicy", XmToggleButtonGetState(click_focus_) ? "click"
                                                                 : "pointer");
    file.set("switchStyle", XmToggleButtonGetState(windows_switch_)
                                ? "windows" : "stack");
    file.set("dockWidth", XmToggleButtonGetState(dock_third_) ? "third"
                                                             : "half");
    file.set("desktops",
             selected_item(desktops_, k_desktop_counts, k_ncounts).c_str());
    file.set("titleFont", font_name(&title_).c_str());
    file.set("menuFont", font_name(&menu_).c_str());
    file.set("taskbarFont", font_name(&taskbar_).c_str());
    file.set("uiScale", combo_text(scale_).c_str());
    file.set("fontPath", text_value(font_path_).c_str());
    if (arrows_ends_) {
        file.set("scrollArrows", XmToggleButtonGetState(arrows_ends_)
                                     ? "ends" : "together");
        file.set("scrollGrip",
                 selected_item(grip_, k_grips, k_ngrips).c_str());
    }
    colors_->save(&file);
    terminal_page_->save(&file);
    file.set("clockFormat", text_value(clock_).c_str());
    file.set("notificationTime",
             selected_item(notify_time_, k_notify_times, k_nnotify).c_str());
    file.set("appSource", selected_item(source_, k_sources, 3).c_str());
    file.set("toolchestFiles", text_value(toolchest_).c_str());
    file.set("terminal", text_value(terminal_).c_str());
    file.set("autostartScript", startup_->run_script() ? "true" : "false");
    file.set("xdgAutostart", xdg_->enabled() ? "true" : "false");

    Str failed;
    if (!startup_->save(&failed) || !xdg_->save(&failed)) {
        error(failed.c_str());
        return;
    }
    if (!file.save(path.c_str())) {
        char msg[1200];
        sprintf(msg, "Could not write %.1000s", path.c_str());
        error(msg);
        return;
    }
    XtUnmanageChild(dialog_);
    wm_->restart();
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
    ((SettingsDialog*)closure)->save();
}

void SettingsDialog::cancel_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    XtUnmanageChild(((SettingsDialog*)closure)->dialog_);
}

}
