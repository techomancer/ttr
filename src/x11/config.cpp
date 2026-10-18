#include "x11/config.h"
#include "core/settings.h"

#include <X11/StringDefs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ttr {

#define BOLD_12 "-*-helvetica-bold-r-normal--12-*-*-*-*-*-iso8859-1"
#define MEDIUM_12 "-*-helvetica-medium-r-normal--12-*-*-*-*-*-iso8859-1"

static const char* k_fallback[] = {
    "*background: #b8b8b8",
    "*fontList: " BOLD_12,
    /* Menus hand this to their push button gadgets, not *fontList. */
    "*buttonFontList: " BOLD_12,
    /* Motif's derived select color barely shows on this grey. */
    "*XmToggleButton.selectColor: #3c5a8c",
    "*XmToggleButtonGadget.selectColor: #3c5a8c",
    0
};

static XrmOptionDescRec k_options[] = {
    { (char*)"-focus", (char*)"*focusPolicy", XrmoptionSepArg, 0 },
    { (char*)"-switch", (char*)"*switchStyle", XrmoptionSepArg, 0 }
};

/* For re-applying the command line over the settings file. */
static XrmOptionDescRec k_reapply[] = {
    { (char*)"-focus", (char*)"*focusPolicy", XrmoptionSepArg, 0 },
    { (char*)"-switch", (char*)"*switchStyle", XrmoptionSepArg, 0 },
    { (char*)"-xrm", 0, XrmoptionResArg, 0 }
};

struct Resources {
    String focus_policy;
    String switch_style;
    String app_source;
    String toolchest_files;
    String terminal;
    String clock_format;
    String title_font;
    String menu_font;
    String taskbar_font;
    String desktops;
    String dock_width;
    String notification_time;
    String notification_log;
};

#define RES(name, cls, field, def) \
    { (char*)name, (char*)cls, XtRString, sizeof(String), \
      XtOffsetOf(Resources, field), XtRString, (XtPointer)def }

static XtResource k_resources[] = {
    RES("focusPolicy", "FocusPolicy", focus_policy, "click"),
    RES("switchStyle", "SwitchStyle", switch_style, "windows"),
    RES("appSource", "AppSource", app_source, "auto"),
    RES("toolchestFiles", "ToolchestFiles", toolchest_files,
        "~/.auxchestrc:/usr/lib/X11/system.chestrc"),
    RES("terminal", "Terminal", terminal, "xterm -e"),
    RES("clockFormat", "ClockFormat", clock_format, "%H:%M"),
    RES("titleFont", "Font", title_font, BOLD_12),
    RES("menuFont", "Font", menu_font, BOLD_12),
    RES("taskbarFont", "Font", taskbar_font, MEDIUM_12),
    RES("desktops", "Desktops", desktops, "4"),
    RES("dockWidth", "DockWidth", dock_width, "third"),
    RES("notificationTime", "NotificationTime", notification_time, "5"),
    RES("notificationLog", "NotificationLog", notification_log, "")
};

#undef RES

Config::Config()
    : focus(FOCUS_CLICK), switch_style(SWITCH_WINDOWS),
      app_source(APPS_DESKTOP), app_source_name("auto"),
      terminal("xterm -e"), clock_format("%H:%M"), title_font(BOLD_12),
      menu_font(BOLD_12), taskbar_font(MEDIUM_12), desktops(4),
      dock_div(3), notification_ms(5000)
{
}

const char* config_default(const char* name)
{
    int i;
    for (i = 0; i < (int)(sizeof(k_resources) / sizeof(k_resources[0])); i++)
        if (strcmp(k_resources[i].resource_name, name) == 0)
            return (const char*)k_resources[i].default_addr;
    return "";
}

void merge_settings_file(Widget toplevel, int argc, char** argv)
{
    Display* dpy = XtDisplay(toplevel);
    XrmDatabase db = XtDatabase(dpy);

    SettingsFile file;
    Str path = SettingsFile::default_path();
    if (file.load(path.c_str())) {
        int i;
        for (i = 0; i < file.count(); i++) {
            char spec[256];
            if (strlen(file.name(i)) > 200)
                continue;
            sprintf(spec, "Ttr*%s", file.name(i));
            XrmPutStringResource(&db, spec, file.value(i));
        }
    }

    /* The command line wins over the file. */
    char** copy = new char*[argc + 1];
    int i;
    for (i = 0; i < argc; i++)
        copy[i] = argv[i];
    copy[argc] = 0;
    int n = argc;
    XrmDatabase cmd = 0;
    XrmParseCommand(&cmd, k_reapply,
                    (int)(sizeof(k_reapply) / sizeof(k_reapply[0])), "ttr",
                    &n, copy);
    if (cmd)
        XrmCombineDatabase(cmd, &db, True);
    delete [] copy;
}

/* A font that cannot be loaded falls back to the default. */
static Str usable_font(Display* dpy, const char* name, const char* def)
{
    XFontStruct* f = name && *name ? XLoadQueryFont(dpy, name) : 0;
    if (f) {
        XFreeFont(dpy, f);
        return Str(name);
    }
    if (name && *name && strcmp(name, def) != 0)
        fprintf(stderr, "ttr: cannot load font '%s', using default\n", name);
    return Str(def);
}

XrmOptionDescRec* config_options()
{
    return k_options;
}

int config_option_count()
{
    return (int)(sizeof(k_options) / sizeof(k_options[0]));
}

String* config_fallback_resources()
{
    return (String*)k_fallback;
}

/* Replace a leading ~ in each colon separated element with $HOME. */
static Str expand_home(const char* list)
{
    const char* home = getenv("HOME");
    if (!home)
        home = "";
    int extra = 0;
    const char* p;
    for (p = list; *p; p++)
        if (*p == '~')
            extra += (int)strlen(home);
    char* out = new char[strlen(list) + extra + 1];
    int o = 0;
    bool start = true;
    for (p = list; *p; p++) {
        if (*p == '~' && start) {
            strcpy(out + o, home);
            o += (int)strlen(home);
        } else {
            out[o++] = *p;
        }
        start = *p == ':';
    }
    out[o] = 0;
    Str s(out);
    delete [] out;
    return s;
}

static bool is(const char* a, const char* b)
{
    return a && strcmp(a, b) == 0;
}

void read_config(Widget toplevel, Config* cfg)
{
    Resources r;
    XtGetApplicationResources(toplevel, &r, k_resources,
                              (Cardinal)(sizeof(k_resources) /
                                         sizeof(k_resources[0])), 0, 0);

    if (is(r.focus_policy, "pointer"))
        cfg->focus = Config::FOCUS_POINTER;
    else if (!is(r.focus_policy, "click"))
        fprintf(stderr, "ttr: unknown focusPolicy '%s', using click\n",
                r.focus_policy);

    if (is(r.switch_style, "stack"))
        cfg->switch_style = Config::SWITCH_STACK;
    else if (!is(r.switch_style, "windows"))
        fprintf(stderr, "ttr: unknown switchStyle '%s', using windows\n",
                r.switch_style);

    cfg->app_source_name = r.app_source;
    if (is(r.app_source, "toolchest"))
        cfg->app_source = Config::APPS_TOOLCHEST;
    else if (is(r.app_source, "desktop"))
        cfg->app_source = Config::APPS_DESKTOP;
    else {
        cfg->app_source_name = "auto";
#if defined(__sgi)
        cfg->app_source = Config::APPS_TOOLCHEST;
#else
        cfg->app_source = Config::APPS_DESKTOP;
#endif
    }

    cfg->toolchest_setting = r.toolchest_files;
    cfg->toolchest_files = expand_home(r.toolchest_files);
    cfg->terminal = r.terminal;
    cfg->clock_format = r.clock_format;

    if (is(r.dock_width, "half"))
        cfg->dock_div = 2;
    else if (!is(r.dock_width, "third"))
        fprintf(stderr, "ttr: unknown dockWidth '%s', using third\n",
                r.dock_width);

    int secs = r.notification_time ? atoi(r.notification_time) : 5;
    cfg->notification_ms = (secs < 1 ? 1 : secs > 300 ? 300 : secs) * 1000;
    cfg->notification_log = expand_home(r.notification_log ? r.notification_log
                                                            : "");

    cfg->desktops = r.desktops ? atoi(r.desktops) : 4;
    if (cfg->desktops < 1)
        cfg->desktops = 1;
    if (cfg->desktops > k_max_desktops)
        cfg->desktops = k_max_desktops;

    Display* dpy = XtDisplay(toplevel);
    cfg->title_font = usable_font(dpy, r.title_font, BOLD_12);
    cfg->menu_font = usable_font(dpy, r.menu_font, BOLD_12);
    cfg->taskbar_font = usable_font(dpy, r.taskbar_font, MEDIUM_12);

    /* Motif widgets created from now on use the menu font. */
    XrmDatabase db = XtDatabase(dpy);
    XrmPutStringResource(&db, "*fontList", cfg->menu_font.c_str());
    XrmPutStringResource(&db, "*buttonFontList", cfg->menu_font.c_str());
}

}
