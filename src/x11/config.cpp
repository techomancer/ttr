#include "x11/config.h"

#include <X11/StringDefs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ttr {

static const char* k_fallback[] = {
    "*background: #b8b8b8",
    "*fontList: -*-helvetica-bold-r-normal--12-*-*-*-*-*-iso8859-1",
    /* Menus hand this to their push button gadgets, not *fontList. */
    "*buttonFontList: -*-helvetica-bold-r-normal--12-*-*-*-*-*-iso8859-1",
    0
};

static XrmOptionDescRec k_options[] = {
    { (char*)"-focus", (char*)"*focusPolicy", XrmoptionSepArg, 0 },
    { (char*)"-switch", (char*)"*switchStyle", XrmoptionSepArg, 0 }
};

struct Resources {
    String focus_policy;
    String switch_style;
    String app_source;
    String toolchest_files;
    String terminal;
    String clock_format;
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
    RES("clockFormat", "ClockFormat", clock_format, "%H:%M")
};

#undef RES

Config::Config()
    : focus(FOCUS_CLICK), switch_style(SWITCH_WINDOWS),
      app_source(APPS_DESKTOP), terminal("xterm -e"), clock_format("%H:%M")
{
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

    if (is(r.app_source, "toolchest"))
        cfg->app_source = Config::APPS_TOOLCHEST;
    else if (is(r.app_source, "desktop"))
        cfg->app_source = Config::APPS_DESKTOP;
    else {
#if defined(__sgi)
        cfg->app_source = Config::APPS_TOOLCHEST;
#else
        cfg->app_source = Config::APPS_DESKTOP;
#endif
    }

    cfg->toolchest_files = expand_home(r.toolchest_files);
    cfg->terminal = r.terminal;
    cfg->clock_format = r.clock_format;
}

}
