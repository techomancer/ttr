#include "shell/settings.h"
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

static const char* const k_sizes[] = {
    "8", "10", "11", "12", "13", "14", "16", "18", "20", "24"
};
static const int k_nsizes = (int)(sizeof(k_sizes) / sizeof(k_sizes[0]));

static const char* const k_sources[] = { "auto", "desktop", "toolchest" };

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

SettingsDialog::SettingsDialog(Widget parent, WmLink* wm, const Config& cfg)
    : parent_(parent), wm_(wm), cfg_(cfg), dialog_(0)
{
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
    XtSetArg(rargs[1], XmNmarginWidth, 8);
    XtSetArg(rargs[2], XmNmarginHeight, 6);
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
    XtSetArg(args[n], XmNwidth, k_label_width); n++;
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
    XtSetArg(args[0], XmNitems, strs);
    XtSetArg(args[1], XmNitemCount, count);
    XtSetArg(args[2], XmNvisibleItemCount, visible);
    XtSetArg(args[3], XmNcolumns, columns);
    Widget cb = XmCreateDropDownList(parent, (char*)name, args, 4);
    for (i = 0; i < count; i++)
        XmStringFree(strs[i]);
    delete [] strs;
    XtManageChild(cb);
    return cb;
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

void SettingsDialog::load_families()
{
    families_.clear();
    int n = 0;
    char** names = XListFonts(XtDisplay(parent_),
                              "-*-*-*-r-*--*-*-*-*-*-*-iso8859-1", 8000, &n);
    int i, j;
    for (i = 0; i < n; i++) {
        Str fam = xlfd_field(names[i], 2);
        if (fam.empty())
            continue;
        /* Insert sorted, skipping duplicates. */
        int at = families_.size();
        bool dup = false;
        for (j = 0; j < families_.size(); j++) {
            int c = compare_nocase(fam.c_str(), families_[j].c_str());
            if (c == 0) {
                dup = true;
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
        for (j = families_.size() - 1; j > at; j--)
            families_[j] = families_[j - 1];
        families_[at] = fam;
    }
    if (names)
        XFreeFontNames(names);
}

void SettingsDialog::font_row(FontRow* r, Widget parent, const char* label,
                              const char* current)
{
    r->dialog = this;
    Widget rc = row(parent, label);

    const char** fams = new const char*[families_.size() + 1];
    int i;
    for (i = 0; i < families_.size(); i++)
        fams[i] = families_[i].c_str();
    r->family = drop_down(rc, "family", fams, families_.size(), 15, 18);
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
    select_item(r->family, fam.empty() ? "helvetica" : fam.c_str());
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

void SettingsDialog::update_preview(FontRow* r)
{
    Str name = font_name(r);
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
    XtSetArg(args[n], XmNmarginWidth, 10); n++;
    XtSetArg(args[n], XmNmarginHeight, 10); n++;
    dialog_ = XmCreateFormDialog(parent_, (char*)"settingsDialog", args, n);
    XmStringFree(title);

    n = 0;
    XtSetArg(args[n], XmNorientation, XmVERTICAL); n++;
    XtSetArg(args[n], XmNspacing, 8); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    Widget content = XmCreateRowColumn(dialog_, (char*)"content", args, n);

    Widget f = frame(content, "Windows");
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

    desktops_ = drop_down(row(f, "Virtual desktops:"), "desktops",
                          k_desktop_counts, k_ncounts, 8, 3);
    char count[16];
    sprintf(count, "%d", cfg_.desktops);
    select_item(desktops_, count);

    f = frame(content, "Fonts");
    font_row(&title_, f, "Window titles:", cfg_.title_font.c_str());
    font_row(&menu_, f, "Menus and dialogs:", cfg_.menu_font.c_str());
    font_row(&taskbar_, f, "Taskbar:", cfg_.taskbar_font.c_str());

    f = frame(content, "Taskbar");
    clock_ = text_field(row(f, "Clock format:"), cfg_.clock_format.c_str(),
                        12);

    f = frame(content, "Start menu");
    source_ = drop_down(row(f, "Programs from:"), "source", k_sources, 3, 3,
                        10);
    select_item(source_, cfg_.app_source_name.c_str());
    toolchest_ = text_field(row(f, "Toolchest files:"),
                            cfg_.toolchest_setting.c_str(), 40);
    terminal_ = text_field(row(f, "Terminal command:"),
                           cfg_.terminal.c_str(), 20);
    XtManageChild(content);

    /* OK and Cancel along the bottom. */
    n = 0;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget, content); n++;
    XtSetArg(args[n], XmNtopOffset, 10); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    Widget sep = XmCreateSeparator(dialog_, (char*)"sep", args, n);
    XtManageChild(sep);

    XmString note = xmstr("OK saves the settings and restarts ttr.");
    n = 0;
    XtSetArg(args[n], XmNlabelString, note); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget, sep); n++;
    XtSetArg(args[n], XmNtopOffset, 10); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    Widget label = XmCreateLabel(dialog_, (char*)"note", args, n);
    XmStringFree(note);
    XtManageChild(label);

    XmString cancel = xmstr("Cancel"), ok = xmstr("OK");
    n = 0;
    XtSetArg(args[n], XmNlabelString, cancel); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget, sep); n++;
    XtSetArg(args[n], XmNtopOffset, 6); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    Widget cancel_b = XmCreatePushButton(dialog_, (char*)"cancel", args, n);
    n = 0;
    XtSetArg(args[n], XmNlabelString, ok); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget, sep); n++;
    XtSetArg(args[n], XmNtopOffset, 6); n++;
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
    file.set("desktops",
             selected_item(desktops_, k_desktop_counts, k_ncounts).c_str());
    file.set("titleFont", font_name(&title_).c_str());
    file.set("menuFont", font_name(&menu_).c_str());
    file.set("taskbarFont", font_name(&taskbar_).c_str());
    file.set("clockFormat", text_value(clock_).c_str());
    file.set("appSource", selected_item(source_, k_sources, 3).c_str());
    file.set("toolchestFiles", text_value(toolchest_).c_str());
    file.set("terminal", text_value(terminal_).c_str());

    if (!file.save(path.c_str())) {
        char msg[1200];
        sprintf(msg, "Could not write %.1000s", path.c_str());
        XmString m = xmstr(msg);
        Arg args[1];
        XtSetArg(args[0], XmNmessageString, m);
        Widget err = XmCreateErrorDialog(parent_, (char*)"saveError", args,
                                         1);
        XmStringFree(m);
        XtUnmanageChild(XtNameToWidget(err, "Help"));
        XtUnmanageChild(XtNameToWidget(err, "Cancel"));
        XtManageChild(err);
        return;
    }
    XtUnmanageChild(dialog_);
    wm_->restart();
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
