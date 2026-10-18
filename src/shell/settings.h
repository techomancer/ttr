/*
 * The Settings dialog (Start > Settings...). It edits the settings file;
 * OK saves it and restarts ttr, which keeps every window, since fonts can
 * only be applied to Motif widgets when they are created.
 */
#ifndef TTR_SHELL_SETTINGS_H
#define TTR_SHELL_SETTINGS_H

#include "core/str.h"
#include "core/vector.h"
#include "core/wmlink.h"

#include <X11/Intrinsic.h>

namespace ttr {

class ShortcutsDialog;
class StartupPage;
class XdgAutostartPage;
struct AppFolder;
struct Config;

class SettingsDialog {
public:
    SettingsDialog(Widget parent, WmLink* wm, const Config& cfg,
                   const AppFolder* apps);
    ~SettingsDialog();
    void show();

    /* Building blocks for pages. */
    Widget widget() const { return dialog_; }
    Widget frame(Widget parent, const char* title);
    Widget row(Widget parent, const char* label);
    Widget text_field(Widget parent, const char* value, int columns);

private:
    struct FontRow {
        SettingsDialog* dialog;
        Widget family;
        Widget size;
        Widget bold;
        Widget preview;
    };

    void build();
    Widget radio(Widget parent, const char* a, const char* b, Widget* first);
    Widget drop_down(Widget parent, const char* name,
                     const char* const* items, int count, int visible,
                     int columns);
    /* 'current' as set, 'resolved' as loaded (x11/config.h). */
    void font_row(FontRow* r, Widget parent, const char* label,
                  const char* current, const char* resolved);
    Str font_name(FontRow* r);
    void update_preview(FontRow* r);
    void load_families();
    void save();
    void error(const char* msg);

    static void font_changed_cb(Widget w, XtPointer closure, XtPointer call);
    static void ok_cb(Widget w, XtPointer closure, XtPointer call);
    static void shortcuts_cb(Widget w, XtPointer closure, XtPointer call);
    static void page_cb(Widget w, XtPointer closure, XtPointer call);

    enum { PAGE_WINDOWS, PAGE_DESKTOPS, PAGE_APPEARANCE, PAGE_TASKBAR,
           PAGE_START, PAGE_STARTUP, PAGE_XDG, PAGE_COUNT };
    Widget page(Widget parent, int index);
    void show_page(int index);
    static void cancel_cb(Widget w, XtPointer closure, XtPointer call);

    Widget parent_;
    WmLink* wm_;
    const Config& cfg_;
    const AppFolder* apps_;
    Widget dialog_;
    Widget click_focus_;
    Widget windows_switch_;
    Widget dock_third_;
    Widget desktops_;
    Widget scale_;
    Widget font_path_;
    FontRow title_, menu_, taskbar_;
    Widget clock_;
    Widget notify_time_;
    Widget source_;
    Widget toolchest_;
    Widget terminal_;
    Vector<Str> families_;
    Widget categories_;
    Widget pages_[PAGE_COUNT];
    int current_page_;          /* kept across opening the dialog */
    ShortcutsDialog* shortcuts_;
    StartupPage* startup_;      /* rebuilt with the dialog */
    XdgAutostartPage* xdg_;
};

}

#endif
