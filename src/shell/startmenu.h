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

private:
    enum Command { CMD_APP, CMD_RUN, CMD_RESTART, CMD_EXIT };
    struct Pending {
        StartMenu* menu;
        Command cmd;
        AppNode* app;
    };

    /* Menu callbacks only queue the command; it runs once the menu has
     * popped down, so the focus Motif restores does not undo ours. */
    void defer(Command cmd, AppNode* app);
    static void deferred_cb(XtPointer closure, XtIntervalId* id);
    void run_dialog();
    void exit_dialog();

    static void app_cb(Widget w, XtPointer closure, XtPointer call);
    static void run_cb(Widget w, XtPointer closure, XtPointer call);
    static void run_ok_cb(Widget w, XtPointer closure, XtPointer call);
    static void restart_cb(Widget w, XtPointer closure, XtPointer call);
    static void exit_cb(Widget w, XtPointer closure, XtPointer call);
    static void exit_ok_cb(Widget w, XtPointer closure, XtPointer call);
    static void unmap_cb(Widget w, XtPointer closure, XtPointer call);

    enum Glyph { GLYPH_NONE, GLYPH_APP, GLYPH_FOLDER };

    void build(Widget menu, AppFolder* folder);
    /* A menu button (or cascade, if 'sub') whose label is icon + text. */
    Widget item(Widget menu, const char* label, const char* icon,
                Glyph fallback, XtCallbackProc cb, XtPointer closure,
                Widget sub);
    Pixmap label_pixmap(const char* text, const Icon* icon, Glyph fallback);

    Widget parent_;
    WmLink* wm_;
    Widget menu_;
    Widget run_dialog_;
    Widget exit_dialog_;
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
