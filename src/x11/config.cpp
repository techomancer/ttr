#include "x11/config.h"
#include "core/scale.h"
#include "core/settings.h"
#include "core/theme.h"
#include "core/vector.h"

#include <Xm/Xm.h>
#include <X11/StringDefs.h>
#include <X11/Xatom.h>

#include <dirent.h>

#include <ctype.h>
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
    String theme;
    String colors[k_ncolor_keys];
    String scroll_arrows;
    String scroll_grip;
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
    RES("fontPath", "FontPath", font_path, ""),
    RES("theme", "Theme", theme, "Classic"),
    RES("colorBackground", "Color", colors[0], ""),
    RES("colorForeground", "Color", colors[1], ""),
    RES("colorActive", "Color", colors[2], ""),
    RES("colorActiveText", "Color", colors[3], ""),
    RES("colorInactive", "Color", colors[4], ""),
    RES("colorInactiveText", "Color", colors[5], ""),
    RES("scrollArrows", "ScrollArrows", scroll_arrows, "ends"),
    RES("scrollGrip", "ScrollGrip", scroll_grip, "ridges")
};

#undef RES

Config::Config()
    : focus(FOCUS_CLICK), switch_style(SWITCH_WINDOWS),
      app_source(APPS_DESKTOP), app_source_name("auto"),
      terminal("xterm -e"), clock_format("%H:%M"), title_font(BOLD_12),
      menu_font(BOLD_12), taskbar_font(MEDIUM_12), desktops(4),
      dock_div(3), notification_ms(5000), autostart_script(true),
      xdg_autostart(false), ui_scale(100), scroll_arrows_together(false),
      scroll_grip("ridges")
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

/* Same folder, ignoring a trailing slash. */
static bool same_dir(const char* a, const char* b)
{
    int la = (int)strlen(a), lb = (int)strlen(b);
    while (la > 1 && a[la - 1] == '/')
        la--;
    while (lb > 1 && b[lb - 1] == '/')
        lb--;
    return la == lb && strncmp(a, b, la) == 0;
}

static bool listed(const Vector<Str>& v, const char* dir)
{
    int i;
    for (i = 0; i < v.size(); i++)
        if (same_dir(v[i].c_str(), dir))
            return true;
    return false;
}

static bool has_index(const char* dir)
{
    char path[1100];
    if (strlen(dir) > 1000)
        return false;
    sprintf(path, "%s/fonts.dir", dir);
    return access(path, R_OK) == 0;
}

/* Does 'dir' hold font files the X server could use? */
static bool has_font_files(const char* dir)
{
    static const char* const exts[] = {
        ".ttf", ".TTF", ".otf", ".ttc", ".pfa", ".pfb", ".pcf", ".pcf.gz",
        ".bdf", ".bdf.gz", 0
    };
    DIR* d = opendir(dir);
    if (!d)
        return false;
    bool found = false;
    struct dirent* de;
    while (!found && (de = readdir(d)) != 0) {
        int n = (int)strlen(de->d_name), k;
        for (k = 0; exts[k] && !found; k++) {
            int e = (int)strlen(exts[k]);
            found = n > e && strcmp(de->d_name + n - e, exts[k]) == 0;
        }
    }
    closedir(d);
    return found;
}

/* Font folders on Linux and IRIX. The classic ones come first, in this
 * order, so that a font name matches the same font it did before. */
static const char* const k_font_roots[] = {
    "/usr/share/fonts", "/usr/share/X11/fonts", "/usr/X11R6/lib/X11/fonts",
    "/usr/lib/X11/fonts", "/usr/local/share/fonts", "~/.local/share/fonts",
    "~/.fonts", 0
};
static const char* const k_first_dirs[] = {
    "misc", "75dpi", "100dpi", "Type1", 0
};

/* A folder with fonts.dir goes on 'out'; one with font files but no
 * index on 'unindexed'. */
static void consider(const char* dir, Vector<Str>* out,
                     Vector<Str>* unindexed)
{
    if (listed(*out, dir))
        return;
    if (has_index(dir))
        out->push_back(Str(dir));
    else if (has_font_files(dir))
        unindexed->push_back(Str(dir));
}

/* Each root, then its classic subfolders, then the others by name. */
static void scan_font_roots(Vector<Str>* out, Vector<Str>* unindexed)
{
    int r, k, i, j;
    for (r = 0; k_font_roots[r]; r++) {
        Str root = expand_home(k_font_roots[r]);
        if (root.length() > 900)
            continue;
        DIR* d = opendir(root.c_str());
        if (!d)
            continue;
        consider(root.c_str(), out, unindexed);
        char sub[1100];
        for (k = 0; k_first_dirs[k]; k++) {
            sprintf(sub, "%s/%s", root.c_str(), k_first_dirs[k]);
            consider(sub, out, unindexed);
        }
        Vector<Str> names;
        struct dirent* de;
        while ((de = readdir(d)) != 0) {
            if (de->d_name[0] == '.' || strlen(de->d_name) > 150)
                continue;
            sprintf(sub, "%s/%s", root.c_str(), de->d_name);
            DIR* t = opendir(sub);
            if (!t)
                continue;   /* not a folder */
            closedir(t);
            names.push_back(Str(de->d_name));
        }
        closedir(d);
        /* By name, so the path is the same every time. */
        for (i = 1; i < names.size(); i++)
            for (j = i; j > 0 && strcmp(names[j - 1].c_str(),
                                        names[j].c_str()) > 0; j--) {
                Str t = names[j];
                names[j] = names[j - 1];
                names[j - 1] = t;
            }
        for (i = 0; i < names.size(); i++) {
            sprintf(sub, "%s/%s", root.c_str(), names[i].c_str());
            consider(sub, out, unindexed);
        }
    }
}

/* Set the server's font path; false if it refused it. A folder the
 * server cannot read is an X error, and Xt's handler would exit, so the
 * error is caught. */
static bool set_font_path(Display* dpy, const Vector<Str>& dirs)
{
    char** path = new char*[dirs.size() > 0 ? dirs.size() : 1];
    int i;
    for (i = 0; i < dirs.size(); i++)
        path[i] = (char*)dirs[i].c_str();
    XSync(dpy, False);
    g_font_path_error = 0;
    XErrorHandler prev = XSetErrorHandler(font_path_error);
    XSetFontPath(dpy, path, dirs.size());
    XSync(dpy, False);
    XSetErrorHandler(prev);
    delete [] path;
    return !g_font_path_error;
}

/* Add font folders to the end of the X server's font path, like xset
 * fp+: every folder with a fonts.dir index under the usual font roots,
 * then the ones in 'user' (colon separated, ~ expanded). The server only
 * reads its font folders when it starts, and leaves out the ones that are
 * missing or empty then, so fonts installed later, and fonts in folders
 * it never knew (TrueType ones in /usr/share/fonts/<family>), need this.
 * Folders already on the path are left alone. */
static void update_font_path(Display* dpy, const char* user)
{
    int n = 0, i;
    char** old = XGetFontPath(dpy, &n);
    Vector<Str> path;
    for (i = 0; i < n; i++)
        path.push_back(Str(old[i]));
    if (old)
        XFreeFontPath(old);

    Vector<Str> found, unindexed;
    scan_font_roots(&found, &unindexed);
    if (user && *user) {
        Str list = expand_home(user);
        const char* p = list.c_str();
        while (*p) {
            const char* e = strchr(p, ':');
            int len = e ? (int)(e - p) : (int)strlen(p);
            if (len > 0 && len < 1000) {
                char dir[1024];
                memcpy(dir, p, len);
                dir[len] = 0;
                if (has_index(dir)) {
                    if (!listed(found, dir))
                        found.push_back(Str(dir));
                } else {
                    fprintf(stderr, "ttr: font folder %s has no fonts.dir; "
                                    "run mkfontscale and mkfontdir in it\n",
                            dir);
                }
            }
            if (!e)
                break;
            p = e + 1;
        }
    }

    Vector<Str> add;
    for (i = 0; i < found.size(); i++)
        if (!listed(path, found[i].c_str()))
            add.push_back(found[i]);

    int had = path.size();
    for (i = 0; i < add.size(); i++)
        path.push_back(add[i]);
    if (add.size() > 0 && !set_font_path(dpy, path)) {
        /* One bad folder spoils the request; add them one at a time. */
        while (path.size() > had)
            path.remove_at(path.size() - 1);
        for (i = 0; i < add.size(); i++) {
            path.push_back(add[i]);
            if (!set_font_path(dpy, path)) {
                fprintf(stderr, "ttr: the X server cannot use the fonts in "
                                "%s\n", add[i].c_str());
                path.remove_at(path.size() - 1);
            }
        }
    }
    if (path.size() > had) {
        fprintf(stderr, "ttr: added %d font folder%s to the X font path:",
                path.size() - had, path.size() - had == 1 ? "" : "s");
        for (i = had; i < path.size(); i++)
            fprintf(stderr, " %s", path[i].c_str());
        fprintf(stderr, "\n");
    }

    if (unindexed.size() > 0) {
        fprintf(stderr, "ttr: %d font folder%s without fonts.dir, so the X "
                        "server cannot use %s (run mkfontscale and "
                        "mkfontdir in %s):", unindexed.size(),
                unindexed.size() == 1 ? "" : "s",
                unindexed.size() == 1 ? "it" : "them",
                unindexed.size() == 1 ? "it" : "each");
        for (i = 0; i < unindexed.size() && i < 4; i++)
            fprintf(stderr, " %s", unindexed[i].c_str());
        if (unindexed.size() > 4)
            fprintf(stderr, " ...");
        fprintf(stderr, "\n");
    }
}

/* ------------------------------------------------------------------ */
/* Colors                                                              */

const char* const k_color_keys[k_ncolor_keys] = {
    "colorBackground", "colorForeground", "colorActive", "colorActiveText",
    "colorInactive", "colorInactiveText"
};
const ThemeRole k_color_roles[k_ncolor_keys] = {
    TR_BACKGROUND, TR_FOREGROUND, TR_ACTIVE, TR_ACTIVE_TEXT, TR_INACTIVE,
    TR_INACTIVE_TEXT
};

/* A color by name or #rrggbb, as X parses it. */
static bool parse_color(Display* dpy, const char* s, Rgb* out)
{
    if (!s || !*s)
        return false;
    XColor c;
    if (!XParseColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)), s, &c))
        return false;
    *out = Rgb(c.red, c.green, c.blue);
    return true;
}

/* An mwm color resource (Mwm*activeBackground), unless it only comes from
 * a line for every program (*background), which is not meant for us. */
static bool mwm_color(Display* dpy, XrmDatabase db, const char* res,
                      Rgb* out)
{
    char name[128], cls[128], any[128];
    sprintf(name, "Mwm.client.%s", res);
    sprintf(any, "NoSuchProgram.client.%s", res);
    sprintf(cls, "Mwm.Client.%c%s", toupper((unsigned char)res[0]), res + 1);
    char* type = 0;
    XrmValue v, w;
    if (!XrmGetResource(db, name, cls, &type, &v) || !v.addr)
        return false;
    Str mine((const char*)v.addr);
    sprintf(cls, "NoSuchProgram.Client.%c%s", toupper((unsigned char)res[0]),
            res + 1);
    if (XrmGetResource(db, any, cls, &type, &w) && w.addr &&
        strcmp((const char*)w.addr, mine.c_str()) == 0)
        return false;
    return parse_color(dpy, mine.c_str(), out);
}

static void put_color(XrmDatabase* db, const char* spec, const Rgb& c)
{
    char hex[8];
    format_hex_color(c, hex);
    XrmPutStringResource(db, spec, hex);
}

/* Motif widgets get the theme through the resource database: what the
 * theme sets, nothing else, so Motif derives the rest as usual. */
static void apply_theme(XrmDatabase* db, const Theme& t)
{
    put_color(db, "*background", t.get(TR_BACKGROUND));
    if (t.set[TR_FOREGROUND])
        put_color(db, "*foreground", t.color[TR_FOREGROUND]);
    if (t.set[TR_BUTTON])
        put_color(db, "*XmPushButton.background", t.color[TR_BUTTON]);
    if (t.set[TR_TEXT_FIELD]) {
        put_color(db, "*XmTextField.background", t.color[TR_TEXT_FIELD]);
        put_color(db, "*XmText.background", t.color[TR_TEXT_FIELD]);
    }
    if (t.set[TR_LIST])
        put_color(db, "*XmList.background", t.color[TR_LIST]);
    if (t.set[TR_TROUGH])
        put_color(db, "*XmScrollBar.troughColor", t.color[TR_TROUGH]);
    if (t.set[TR_SCROLL_BAR])
        put_color(db, "*XmScrollBar.background", t.color[TR_SCROLL_BAR]);
    if (t.set[TR_SELECT_FILL]) {
        put_color(db, "*XmToggleButton.selectColor", t.color[TR_SELECT_FILL]);
        put_color(db, "*XmToggleButtonGadget.selectColor",
                  t.color[TR_SELECT_FILL]);
    }
}

static void read_colors(Display* dpy, const Resources& r, Config* cfg)
{
    cfg->theme_name = r.theme ? r.theme : "Classic";
    if (!find_theme(cfg->theme_name.c_str(), &cfg->theme))
        fprintf(stderr, "ttr: no theme '%s', using Classic\n",
                cfg->theme_name.c_str());

    /* mwm's resources, for people who have them, over the default look
     * only: a theme someone chose wins. */
    XrmDatabase db = XtDatabase(dpy);
    if (cfg->theme.path.empty()) {
        static const char* const mwm_res[4] = {
            "activeBackground", "activeForeground", "background", "foreground"
        };
        static const ThemeRole mwm_roles[4] = {
            TR_ACTIVE, TR_ACTIVE_TEXT, TR_INACTIVE, TR_INACTIVE_TEXT
        };
        int i;
        for (i = 0; i < 4; i++) {
            Rgb c;
            if (mwm_color(dpy, db, mwm_res[i], &c))
                cfg->theme.put(mwm_roles[i], c);
        }
    }

    /* The user's own colors, over the theme. */
    int i;
    for (i = 0; i < k_ncolor_keys; i++) {
        cfg->colors[i] = r.colors[i] ? r.colors[i] : "";
        Rgb c;
        if (cfg->colors[i].empty())
            continue;
        if (parse_color(dpy, cfg->colors[i].c_str(), &c))
            cfg->theme.put(k_color_roles[i], c);
        else
            fprintf(stderr, "ttr: %s: unknown color '%s'\n",
                    k_color_keys[i], cfg->colors[i].c_str());
    }
    apply_theme(&db, cfg->theme);
}

/* ------------------------------------------------------------------ */
/* Scroll bars                                                         */

bool scroll_bar_styles()
{
#if defined(XmNsliderMark)
    return true;
#else
    return false;
#endif
}

static const int k_scroll_bar_width = 17;  /* at 100% */

static const char* const k_grips[4][2] = {
    { "none", "NONE" }, { "line", "ETCHED_LINE" }, { "ridges", "THUMB_MARK" },
    { "dot", "ROUND_MARK" }
};

/* Where the arrows go and the grip on the slider. Motif 2.2 added both
 * (XmNshowArrows as XmMAX_SIDE, XmNsliderMark); older Motif would only
 * warn about values it cannot convert. On IRIX, SGI's enhanced look has
 * scroll bars of its own. */
static void apply_scroll_bars(XrmDatabase* db, const Resources& r,
                              Config* cfg)
{
    /* Thicker than Motif's 11 pixels, at every scale: about Windows 2000's
     * 16 plus Motif's bevel. A slider's bar (volume, color picker) has a
     * thickness of its own, which a scroll bar's width does not reach. */
    char w[16];
    sprintf(w, "%d", scaled(k_scroll_bar_width));
    XrmPutStringResource(db, "*XmScrollBar.width", w);
    XrmPutStringResource(db, "*XmScrollBar.height", w);
    XrmPutStringResource(db, "*XmScale.scaleHeight", w);

    cfg->scroll_arrows_together = r.scroll_arrows &&
                                  strcmp(r.scroll_arrows, "together") == 0;
    cfg->scroll_grip = "ridges";
    int i;
    for (i = 0; i < 4; i++)
        if (r.scroll_grip && strcmp(r.scroll_grip, k_grips[i][0]) == 0)
            cfg->scroll_grip = k_grips[i][0];
    if (scroll_bar_styles()) {
        if (cfg->scroll_arrows_together)
            XrmPutStringResource(db, "*XmScrollBar.showArrows", "MAX_SIDE");
        for (i = 0; i < 4; i++) {
            if (strcmp(cfg->scroll_grip.c_str(), k_grips[i][0]) != 0)
                continue;
            XrmPutStringResource(db, "*XmScrollBar.sliderMark",
                                 k_grips[i][1]);
            XrmPutStringResource(db, "*XmScale.sliderMark", k_grips[i][1]);
        }
    }
#if defined(__sgi)
    XrmPutStringResource(db, "*sgiMode", "True");
    XrmPutStringResource(db, "*useEnhancedSGILook", "True");
#endif
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
    update_font_path(XtDisplay(toplevel), r.font_path);
    read_colors(XtDisplay(toplevel), r, cfg);
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
    apply_scroll_bars(&db, r, cfg);
}

}
