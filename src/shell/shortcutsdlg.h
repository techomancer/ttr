/*
 * The shortcuts editor (Settings > Edit Shortcuts...): an ordered list of
 * shortcuts with a name, command, icon, key and quick launch checkbox.
 * New ones come from the start menu's programs or are typed in; commands
 * and icons can be browsed for. OK saves ~/.config/ttr/shortcuts and
 * restarts ttr.
 */
#ifndef TTR_SHELL_SHORTCUTSDLG_H
#define TTR_SHELL_SHORTCUTSDLG_H

#include "core/color.h"
#include "core/shortcuts.h"
#include "core/str.h"
#include "core/vector.h"
#include "core/wmlink.h"

#include <X11/Intrinsic.h>

namespace ttr {

class IconLoader;
class XPainter;
struct AppFolder;
struct AppNode;

class ShortcutsDialog {
public:
    ShortcutsDialog(Widget parent, WmLink* wm, const AppFolder* apps);
    ~ShortcutsDialog();
    void show();

private:
    void build();
    Widget field_row(Widget parent, const char* label, Widget* text,
                     int columns);
    void refresh_list();
    void select(int i);
    void load_fields();
    void store_fields();
    void update_preview();
    void update_buttons();
    Str label_for(const Shortcut* s);
    void collect_apps(const AppFolder* f, const char* prefix);
    void add_app(const AppNode* app);
    void error(const char* msg);
    void save();

    static void list_cb(Widget w, XtPointer closure, XtPointer call);
    static void field_cb(Widget w, XtPointer closure, XtPointer call);
    static void icon_done_cb(Widget w, XtPointer closure, XtPointer call);
    static void add_cb(Widget w, XtPointer closure, XtPointer call);
    static void add_program_cb(Widget w, XtPointer closure, XtPointer call);
    static void program_ok_cb(Widget w, XtPointer closure, XtPointer call);
    static void remove_cb(Widget w, XtPointer closure, XtPointer call);
    static void up_cb(Widget w, XtPointer closure, XtPointer call);
    static void down_cb(Widget w, XtPointer closure, XtPointer call);
    static void browse_command_cb(Widget w, XtPointer closure, XtPointer call);
    static void browse_icon_cb(Widget w, XtPointer closure, XtPointer call);
    static void browsed_cb(Widget w, XtPointer closure, XtPointer call);
    static void ok_cb(Widget w, XtPointer closure, XtPointer call);
    static void cancel_cb(Widget w, XtPointer closure, XtPointer call);

    Widget parent_;
    WmLink* wm_;
    const AppFolder* apps_;
    XPainter* painter_;
    IconLoader* icons_;
    ColorSet cs_;

    ShortcutList model_;
    int current_;
    bool loading_;              /* filling fields; ignore their callbacks */

    Widget dialog_;
    Widget list_;
    Widget name_, command_, icon_, key_, quick_;
    Widget preview_;
    Pixmap preview_pm_;
    Widget remove_b_, up_b_, down_b_;
    Widget fields_;             /* the editing area, insensitive if empty */
    Widget programs_;           /* "Add from Programs" picker */
    Vector<Str> app_labels_;
    Vector<const AppNode*> app_nodes_;
    Widget browser_;            /* file selection, for command or icon */
    Widget browse_target_;
};

}

#endif
