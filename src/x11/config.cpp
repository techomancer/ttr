#include "x11/config.h"
#include "core/scale.h"
#include "core/settings.h"
#include "core/vector.h"

#include <X11/StringDefs.h>
#include <X11/Xatom.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace ttr {

#define BOLD_12 "-*-helvetica-bold-r-normal--12-*-*-*-*-*-iso8859-1"
#define MEDIUM_12 "-*-helvetica-medium-r-normal--12-*-*-*-*-*-iso8859-1"

static const char* k_fallback[] = {
    "*background: #b8b8b8",
    /* No *fontList here: read_config() sets it to a font that is
     * installed, before any widget that uses it is created. */
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
    String autostart_script;
    String xdg_autostart;
    String ui_scale;
    String font_path;
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
    RES("notificationLog", "NotificationLog", notification_log, ""),
    RES("autostartScript", "AutostartScript", autostart_script, "true"),
    RES("xdgAutostart", "XdgAutostart", xdg_autostart, "false"),
    RES("uiScale", "UiScale", ui_scale, "100"),
    RES("fontPath", "FontPath", font_path, "")
};

#undef RES

Config::Config()
    : focus(FOCUS_CLICK), switch_style(SWITCH_WINDOWS),
      app_source(APPS_DESKTOP), app_source_name("auto"),
      terminal("xterm -e"), clock_format("%H:%M"), title_font(BOLD_12),
      menu_font(BOLD_12), taskbar_font(MEDIUM_12), desktops(4),
      dock_div(3), notification_ms(5000), autostart_script(true),
      xdg_autostart(false), ui_scale(100)
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

/* Split an XLFD into its 14 fields; false if it is not one. */
static bool xlfd_split(const char* name, char fields[14][128])
{
    if (name[0] != '-')
        return false;
    const char* p = name + 1;
    int f;
    for (f = 0; f < 14; f++) {
        const char* e = strchr(p, '-');
        if (f == 13)
            e = p + strlen(p);
        if (!e || e - p >= 128)
            return false;
        memcpy(fields[f], p, e - p);
        fields[f][e - p] = 0;
        p = *e ? e + 1 : e;
    }
    return true;
}

static Str xlfd_join(char fields[14][128])
{
    char buf[14 * 129 + 2];
    char* o = buf;
    int f;
    for (f = 0; f < 14; f++) {
        *o++ = '-';
        strcpy(o, fields[f]);
        o += strlen(o);
    }
    *o = 0;
    return Str(buf);
}

static bool loadable(Display* dpy, const char* name)
{
    XFontStruct* f = XLoadQueryFont(dpy, name);
    if (f)
        XFreeFont(dpy, f);
    return f != 0;
}

/* The full XLFD of an alias such as "fixed", from its FONT property, or
 * empty. */
static Str alias_xlfd(Display* dpy, const char* name)
{
    XFontStruct* f = XLoadQueryFont(dpy, name);
    if (!f)
        return Str("");
    Str r("");
    unsigned long atom = 0;
    if (XGetFontProperty(f, XA_FONT, &atom) && atom) {
        char* n = XGetAtomName(dpy, (Atom)atom);
        if (n) {
            r = n;
            XFree(n);
        }
    }
    XFreeFont(dpy, f);
    return r;
}

/* Families to try, in order, when the one asked for is not installed or
 * has no size near the one wanted: common sans serif fonts (bitmap, or
 * scalable through the X server's font path), then the misc fixed fonts,
 * then anything. */
static const char* const k_substitutes[] = {
    "helvetica", "nimbus sans", "nimbus sans l", "liberation sans",
    "dejavu sans", "arial", "luxi sans", "bitstream vera sans", "noto sans",
    "droid sans", "fixed", "*", 0
};

/* The installed font nearest to 'target' pixels with these fields (the
 * size fields are replaced). *dist is how far off it is, -1 if there is
 * none. */
static Str nearest_font(Display* dpy, char fields[14][128], int target,
                        int* dist)
{
    char f[14][128];
    memcpy(f, fields, sizeof(f));
    sprintf(f[6], "%d", target);
    strcpy(f[7], "*");
    strcpy(f[8], "*");
    strcpy(f[9], "*");
    strcpy(f[11], "*");
    Str want = xlfd_join(f);
    if (loadable(dpy, want.c_str())) {
        *dist = 0;      /* that size, or a scalable font */
        return want;
    }

    /* Bitmap fonts come in a few sizes: take the nearest one there is. */
    strcpy(f[6], "*");
    Str pattern = xlfd_join(f);
    int n = 0;
    char** names = XListFonts(dpy, pattern.c_str(), 2000, &n);
    Str best("");
    int best_d = -1, i;
    for (i = 0; i < n; i++) {
        char f2[14][128];
        if (!xlfd_split(names[i], f2))
            continue;
        int size = atoi(f2[6]);
        if (size <= 0)
            continue;
        int d = size > target ? size - target : target - size;
        if (best_d < 0 || d < best_d) {
            best_d = d;
            best = names[i];
        }
    }
    if (names)
        XFreeFontNames(names);
    *dist = best_d;
    return best;
}

/* A font that can be loaded, as near as there is to 'name' at 'percent'
 * of its size. Fields are numbered from 0 here: 1 is the family, 2 the
 * weight, 6 the pixel size, 7 the point size (in tenths). */
static Str resolve_font(Display* dpy, const char* name, int percent,
                        bool warn)
{
    if (percent == 100 && loadable(dpy, name))
        return Str(name);   /* as before the scale existed */
    char fields[14][128];
    if (!xlfd_split(name, fields)) {
        Str real = alias_xlfd(dpy, name);
        if (real.empty() || !xlfd_split(real.c_str(), fields))
            return Str(name);
    }

    int px = atoi(fields[6]), pt = atoi(fields[7]);
    /* Without a pixel size, take points (in tenths) as pixels. */
    int base = px > 0 ? px : pt > 0 ? (pt + 5) / 10 : 12;
    int target = (base * percent + 50) / 100;
    int close = target / 10;    /* near enough to keep the family */

    /* The family asked for, then the substitutes; all of them first in
     * the weight asked for, then in any weight. */
    Str best("");
    int best_d = -1, pass, i;
    for (pass = 0; pass < 2; pass++) {
        for (i = -1; i < 0 || k_substitutes[i]; i++) {
            char f[14][128];
            memcpy(f, fields, sizeof(f));
            if (i >= 0) {
                strcpy(f[0], "*");
                strcpy(f[1], k_substitutes[i]);
            }
            if (pass == 1)
                strcpy(f[2], "*");
            strcpy(f[3], "r");
            int d = -1;
            Str got = nearest_font(dpy, f, target, &d);
            if (d >= 0 && (best_d < 0 || d < best_d)) {
                best_d = d;
                best = got;
            }
            if (best_d >= 0 && best_d <= close)
                break;
        }
        if (best_d >= 0 && best_d <= close)
            break;
    }
    if (best.empty())
        return Str(name);

    if (warn) {
        char bf[14][128];
        int size = xlfd_split(best.c_str(), bf) ? atoi(bf[6]) : 0;
        if (size > 0 && size < target * 8 / 10)
            fprintf(stderr, "ttr: no font near %d pixels is installed "
                            "(using %d); see README.md, Fonts\n",
                    target, size);
        else if (size > 0 && strcmp(bf[1], fields[1]) != 0)
            fprintf(stderr, "ttr: font family '%s' is not installed, "
                            "using '%s'\n", fields[1], bf[1]);
    }
    return best;
}

Str scale_font(Display* dpy, const char* name, int percent)
{
    return resolve_font(dpy, name, percent, false);
}

/* Motif sizes its widgets from their fonts, but shadows, focus
 * highlights and scroll bars are in pixels. Scaled per class, at Motif's
 * defaults, so classes that have none (labels, layout managers) do not
 * get one. */
static void scale_motif_resources(XrmDatabase* db)
{
    static const struct { const char* spec; int base; } k_res[] = {
        { "*XmPushButton.shadowThickness", 2 },
        { "*XmPushButtonGadget.shadowThickness", 2 },
        { "*XmCascadeButton.shadowThickness", 2 },
        { "*XmCascadeButtonGadget.shadowThickness", 2 },
        { "*XmArrowButton.shadowThickness", 2 },
        { "*XmText.shadowThickness", 2 },
        { "*XmTextField.shadowThickness", 2 },
        { "*XmList.shadowThickness", 2 },
        { "*XmScrollBar.shadowThickness", 2 },
        { "*XmScale.shadowThickness", 2 },
        { "*XmComboBox.shadowThickness", 2 },
        { "*XmMenuShell*XmRowColumn.shadowThickness", 2 },
        { "*XmPushButton.highlightThickness", 2 },
        { "*XmToggleButton.highlightThickness", 2 },
        { "*XmText.highlightThickness", 2 },
        { "*XmTextField.highlightThickness", 2 },
        { "*XmList.highlightThickness", 2 },
        { "*XmScrollBar.width", 15 },
        { "*XmScrollBar.height", 15 },
        { "*XmComboBox.arrowSize", 14 }
    };
    int i;
    for (i = 0; i < (int)(sizeof(k_res) / sizeof(k_res[0])); i++) {
        char v[16];
        int base = k_res[i].base;
        sprintf(v, "%d", strstr(k_res[i].spec, "Thickness") ? scaled_line(base)
                                                             : scaled(base));
        XrmPutStringResource(db, k_res[i].spec, v);
    }
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

static int g_font_path_error;

static int font_path_error(Display* dpy, XErrorEvent* ev)
{
    (void)dpy;
    (void)ev;
    g_font_path_error = 1;
    return 0;
}

/* Add the folders in 'list' (colon separated, ~ expanded) to the end of
 * the X server's font path, like xset fp+, so the fonts in them can be
 * used: Type1 and TrueType ones at any size. A folder needs a fonts.dir
 * index (made by mkfontscale and mkfontdir); others are skipped with a
 * message. Folders already on the path are left alone. */
static void add_font_path(Display* dpy, const char* list)
{
    if (!list || !*list)
        return;
    int n = 0;
    char** old = XGetFontPath(dpy, &n);
    Vector<Str> dirs;
    int i;
    for (i = 0; i < n; i++)
        dirs.push_back(Str(old[i]));
    if (old)
        XFreeFontPath(old);
    int had = dirs.size();

    Str expanded = expand_home(list);
    const char* p = expanded.c_str();
    while (*p) {
        const char* e = strchr(p, ':');
        int len = e ? (int)(e - p) : (int)strlen(p);
        char dir[1024], index[1100];
        if (len > 0 && len < (int)sizeof(dir)) {
            memcpy(dir, p, len);
            dir[len] = 0;
            while (len > 1 && dir[len - 1] == '/')
                dir[--len] = 0;
            bool present = false;
            for (i = 0; i < dirs.size(); i++) {
                const char* d = dirs[i].c_str();
                int dl = (int)strlen(d);
                if (dl > 0 && d[dl - 1] == '/')
                    dl--;
                if (dl == len && strncmp(d, dir, len) == 0)
                    present = true;
            }
            sprintf(index, "%s/fonts.dir", dir);
            if (!present && access(index, R_OK) != 0)
                fprintf(stderr, "ttr: fontPath: %s has no fonts.dir; run "
                                "mkfontscale and mkfontdir in it\n", dir);
            else if (!present)
                dirs.push_back(Str(dir));
        }
        if (!e)
            break;
        p = e + 1;
    }
    if (dirs.size() == had)
        return;

    char** path = new char*[dirs.size()];
    for (i = 0; i < dirs.size(); i++)
        path[i] = (char*)dirs[i].c_str();
    /* A folder the server cannot read is an X error, and Xt's handler
     * would exit; catch it instead. */
    XSync(dpy, False);
    g_font_path_error = 0;
    XErrorHandler prev = XSetErrorHandler(font_path_error);
    XSetFontPath(dpy, path, dirs.size());
    XSync(dpy, False);
    XSetErrorHandler(prev);
    delete [] path;
    if (g_font_path_error)
        fprintf(stderr, "ttr: the X server refused the fontPath folders\n");
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

    cfg->autostart_script = !is(r.autostart_script, "false");
    cfg->xdg_autostart = is(r.xdg_autostart, "true");

    cfg->desktops = r.desktops ? atoi(r.desktops) : 4;
    if (cfg->desktops < 1)
        cfg->desktops = 1;
    if (cfg->desktops > k_max_desktops)
        cfg->desktops = k_max_desktops;

    Display* dpy = XtDisplay(toplevel);
    add_font_path(XtDisplay(toplevel), r.font_path);
    cfg->font_path = r.font_path ? r.font_path : "";

    /* As set; what is loaded is resolved below. */
    cfg->title_font = r.title_font && *r.title_font ? r.title_font : BOLD_12;
    cfg->menu_font = r.menu_font && *r.menu_font ? r.menu_font : BOLD_12;
    cfg->taskbar_font = r.taskbar_font && *r.taskbar_font ? r.taskbar_font
                                                          : MEDIUM_12;

    int scale = r.ui_scale ? atoi(r.ui_scale) : 100;
    if (scale < k_min_scale || scale > k_max_scale) {
        fprintf(stderr, "ttr: uiScale %s is outside %d..%d\n",
                r.ui_scale ? r.ui_scale : "", k_min_scale, k_max_scale);
        scale = scale < k_min_scale ? k_min_scale : k_max_scale;
    }
    cfg->ui_scale = scale;
    set_ui_scale(scale);
    cfg->title_xfont = resolve_font(dpy, cfg->title_font.c_str(), scale,
                                    true);
    cfg->menu_xfont = resolve_font(dpy, cfg->menu_font.c_str(), scale, true);
    cfg->taskbar_xfont = resolve_font(dpy, cfg->taskbar_font.c_str(), scale,
                                      true);

    /* Motif widgets created from now on use the menu font. */
    XrmDatabase db = XtDatabase(dpy);
    XrmPutStringResource(&db, "*fontList", cfg->menu_xfont.c_str());
    XrmPutStringResource(&db, "*buttonFontList", cfg->menu_xfont.c_str());
    if (scale != 100)
        scale_motif_resources(&db);
}

}
