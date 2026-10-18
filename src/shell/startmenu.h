/*
 * The start menu: Programs (from an AppSource), Run..., and leaving ttr.
 * Motif popup menus with cascades; Motif 2.1 buttons cannot show an icon
 * and a label together, so entries are text only.
 */
#ifndef TTR_SHELL_STARTMENU_H
#define TTR_SHELL_STARTMENU_H

#include "core/color.h"
#include "core/vector.h"
#include "core/wmlink.h"

#include <X11/Intrinsic.h>

namespace ttr {

class Icon;
class IconLoader;
class SettingsDialog;
class XPainter;
struct AppFolder;
struct AppNode;
struct Config;

class StartMenu {
public:
    StartMenu(Widget parent, WmLink* wm, const Config& cfg);
    ~StartMenu();

    /* Post with the menu's bottom left corner at (x, y_bottom). */
    void post(int x, int y_bottom, unsigned long time);
    /* Called whenever the menu goes away. */
    void on_unpost(void (*fn)(void*), void* closure);
    /* Mark the current desktop in the Desktops submenu. */
    void set_desktop(int desktop);

private:
    enum Command {
        CMD_APP, CMD_RUN, CMD_SETTINGS, CMD_RESTART, CMD_EXIT, CMD_DESKTOP
    };
    struct Pending {
        StartMenu* menu;
        Command cmd;
        AppNode* app;
        int desktop;
    };
    struct DeskItem {
        StartMenu* menu;
        int desktop;
        Widget button;
    };

    /* Menu callbacks only queue the command; it runs once the menu has
     * popped down, so the focus Motif restores does not undo ours. */
    void defer(Command cmd, AppNode* app, int desktop = 0);
    static void desktop_cb(Widget w, XtPointer closure, XtPointer call);
    static void deferred_cb(XtPointer closure, XtIntervalId* id);
    void run_dialog();
    void exit_dialog();

    static void app_cb(Widget w, XtPointer closure, XtPointer call);
    static void run_cb(Widget w, XtPointer closure, XtPointer call);
    static void settings_cb(Widget w, XtPointer closure, XtPointer call);
    static void run_ok_cb(Widget w, XtPointer closure, XtPointer call);
    static void restart_cb(Widget w, XtPointer closure, XtPointer call);
    static void exit_cb(Widget w, XtPointer closure, XtPointer call);
    static void exit_ok_cb(Widget w, XtPointer closure, XtPointer call);
    static void map_cb(Widget w, XtPointer closure, XtPointer call);
    static void unmap_cb(Widget w, XtPointer closure, XtPointer call);

    enum Glyph { GLYPH_NONE, GLYPH_APP, GLYPH_FOLDER };

    void build(Widget menu, AppFolder* folder);
    /* A menu button (or cascade, if 'sub') whose label is icon + text. */
    Widget item(Widget menu, const char* label, const char* icon,
                Glyph fallback, XtCallbackProc cb, XtPointer closure,
                Widget sub);
    Pixmap label_pixmap(const char* text, const Icon* icon, Glyph fallback);
    int label_height();
    int menu_rows();

    Widget parent_;
    WmLink* wm_;
    Widget menu_;
    Widget run_dialog_;
    Widget exit_dialog_;
    SettingsDialog* settings_;
    int desktops_;
    DeskItem* desk_items_;
    AppFolder* apps_;
    XPainter* painter_;
    IconLoader* icons_;
    ColorSet cs_;               /* the menu's own colors */
    Vector<Pixmap> pixmaps_;
    void (*unpost_)(void*);
    void* unpost_closure_;
};

}

#endif
