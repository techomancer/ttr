/*
 * Settings > Startup: the programs started with ttr, in order, each of
 * which can be turned off, and whether to run autostart.sh after them
 * (core/startup.h). The list is read when the page is built and written
 * by save().
 */
#ifndef TTR_SHELL_STARTUPPAGE_H
#define TTR_SHELL_STARTUPPAGE_H

#include "core/startup.h"
#include "core/str.h"

#include <X11/Intrinsic.h>

namespace ttr {

class FileBrowser;
class ProgramPicker;
class SettingsDialog;
struct AppFolder;
struct AppNode;
struct Config;

class StartupPage {
public:
    /* Builds into 'page', a column of the Settings dialog 'dialog'. */
    StartupPage(SettingsDialog* dialog, Widget page, const Config& cfg,
                const AppFolder* apps);
    ~StartupPage();

    bool run_script() const;
    /* False, with *error set, if the list cannot be written. */
    bool save(Str* error);

private:
    StartupPage(const StartupPage&);
    StartupPage& operator=(const StartupPage&);

    Widget push(Widget parent, const char* label, XtCallbackProc cb);
    Str label_for(const StartupItem* s);
    void refresh_list();
    void select(int i);
    void load_fields();
    void store_fields();
    void update_buttons();
    void add(const char* command);
    void edit_script();

    static void list_cb(Widget w, XtPointer closure, XtPointer call);
    static void field_cb(Widget w, XtPointer closure, XtPointer call);
    static void add_cb(Widget w, XtPointer closure, XtPointer call);
    static void add_program_cb(Widget w, XtPointer closure, XtPointer call);
    static void program_chosen(void* closure, const AppNode* app);
    static void remove_cb(Widget w, XtPointer closure, XtPointer call);
    static void up_cb(Widget w, XtPointer closure, XtPointer call);
    static void down_cb(Widget w, XtPointer closure, XtPointer call);
    static void browse_cb(Widget w, XtPointer closure, XtPointer call);
    static void browsed(void* closure, const char* path);
    static void edit_script_cb(Widget w, XtPointer closure, XtPointer call);

    Widget top_;                /* the dialog widget, for popups */
    const Config& cfg_;
    StartupList model_;
    int current_;
    bool loading_;              /* filling fields; ignore their callbacks */

    Widget list_;
    Widget command_, enabled_;
    Widget fields_;             /* insensitive while nothing is selected */
    Widget remove_b_, up_b_, down_b_;
    Widget script_;
    ProgramPicker* programs_;
    FileBrowser* browser_;
};

}

#endif
