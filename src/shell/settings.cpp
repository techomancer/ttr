#include "shell/settings.h"
#include "../../tx/dialog.h"
#include "../../tx/fontchooser.h"
#include "../../tx/pages.h"
#include "../../tx/scale.h"
#include "shell/colorspage.h"
#include "shell/shortcutsdlg.h"
#include "shell/startuppage.h"
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

static const char* const k_sources[] = { "auto", "desktop", "toolchest" };

static const char* const k_grips[] = { "none", "line", "ridges", "dot" };
static const int k_ngrips = 4;

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

SettingsDialog::SettingsDialog(Widget parent, WmLink* wm, const Config& cfg,
                               const AppFolder* apps)
    : parent_(parent), wm_(wm), cfg_(cfg), apps_(apps), dialog_(0), title_font_(0),
      menu_font_(0), taskbar_font_(0),
      shortcuts_(new ShortcutsDialog(parent, wm, apps)), startup_(0),
      xdg_(0), colors_(0)
{
}

SettingsDialog::~SettingsDialog()
{
    drop_fonts();
    delete colors_;
    delete xdg_;
    delete startup_;
    delete shortcuts_;
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
    pages_.destroy();
    dialog_ = 0;
    drop_fonts();
    build();
    XtManageChild(dialog_);
}

/* ------------------------------------------------------------------ */
/* Building blocks                                                     */

Widget SettingsDialog::frame(Widget parent, const char* title)
{
    return tx::frame(parent, title);
}

Widget SettingsDialog::row(Widget parent, const char* label)
{
    return tx::row(parent, label);
}

Widget SettingsDialog::radio(Widget parent, const char* a, const char* b,
                             Widget* first)
{
    return tx::radio(parent, a, b, first);
}

Widget SettingsDialog::drop_down(Widget parent, const char* name,
                                 const char* const* items, int count,
                                 int visible, int columns)
{
    return tx::drop_down(parent, name, items, count, visible, columns);
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

/* The selected entry's text, read from the list itself. */
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

/* The chooser every ttr program shares (tx): any family, Latin-1 (Motif
 * labels), with Bold. A font the server does not have shows as set. */
tx::FontChooser* SettingsDialog::font_row(Widget parent, const char* label, const char* current)
{
    Widget rc = row(parent, label);
    tx::FontChooser* c = new tx::FontChooser(rc, "font", tx::FontChooser::BOLD);
    c->set_preview_name(preview_name, this);
    c->set(current);
    return c;
}

/* At the scale ttr runs at, like the rest of the dialog; a new scale
 * applies after OK. */
Str SettingsDialog::preview_name(const char* xlfd, void* data)
{
    SettingsDialog* self = (SettingsDialog*)data;
    return tx::scale_font(XtDisplay(self->parent_), xlfd, self->cfg_.ui_scale);
}

void SettingsDialog::drop_fonts()
{
    delete title_font_;
    delete menu_font_;
    delete taskbar_font_;
    title_font_ = menu_font_ = taskbar_font_ = 0;
}

/* ------------------------------------------------------------------ */
/* The dialog                                                          */

void SettingsDialog::build()
{

    static const char* const names[PAGE_COUNT] = {
        "Windows", "Desktops", "Appearance", "Colors", "Taskbar",
        "Start Menu", "Startup", "XDG Autostart"
    };
    pages_.create(parent_, "settingsDialog", "Settings", names, PAGE_COUNT);
    dialog_ = pages_.dialog();

    /* Windows */
    Widget pg = pages_.page(PAGE_WINDOWS);
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
    pg = pages_.page(PAGE_DESKTOPS);
    f = frame(pg, "Virtual desktops");
    desktops_ = drop_down(row(f, "Number of desktops:"), "desktops",
                          k_desktop_counts, k_ncounts, 8, 3);
    char count[16];
    sprintf(count, "%d", cfg_.desktops);
    select_item(desktops_, count);

    /* Appearance */
    pg = pages_.page(PAGE_APPEARANCE);
    f = frame(pg, "Size");
    Widget srow0 = row(f, "UI scale:");
    scale_ = drop_down(srow0, "uiScale", tx::k_ui_scales, tx::k_ui_scale_count,
                       tx::k_ui_scale_count, 3);
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
    title_font_ = font_row(f, "Window titles:", cfg_.title_font.c_str());
    menu_font_ = font_row(f, "Menus and dialogs:", cfg_.menu_font.c_str());
    taskbar_font_ = font_row(f, "Taskbar:", cfg_.taskbar_font.c_str());
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
    pg = pages_.page(PAGE_COLORS);
    delete colors_;
    colors_ = new ColorsPage(this, pg, cfg_);

    /* Taskbar */
    pg = pages_.page(PAGE_TASKBAR);
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

    /* Start menu */
    pg = pages_.page(PAGE_START);
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
    pg = pages_.page(PAGE_STARTUP);
    delete startup_;
    startup_ = new StartupPage(this, pg, cfg_, apps_);

    /* XDG Autostart */
    pg = pages_.page(PAGE_XDG);
    delete xdg_;
    xdg_ = new XdgAutostartPage(this, pg, cfg_);

    pages_.finish("OK saves the settings and restarts ttr.",
                  tx::PagedDialog::OK | tx::PagedDialog::CANCEL, pressed, this);
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
    file.set("titleFont", title_font_->get().c_str());
    file.set("menuFont", menu_font_->get().c_str());
    file.set("taskbarFont", taskbar_font_->get().c_str());
    file.set("uiScale", combo_text(scale_).c_str());
    file.set("fontPath", text_value(font_path_).c_str());
    if (arrows_ends_) {
        file.set("scrollArrows", XmToggleButtonGetState(arrows_ends_)
                                     ? "ends" : "together");
        file.set("scrollGrip",
                 selected_item(grip_, k_grips, k_ngrips).c_str());
    }
    colors_->save(&file);
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

void SettingsDialog::pressed(void* data, int which)
{
    SettingsDialog* self = (SettingsDialog*)data;
    if (which == tx::PagedDialog::OK)
        self->save();
    else
        XtUnmanageChild(self->dialog_);
}

}
