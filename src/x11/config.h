/*
 * Settings, read as X resources the way mwm does. In increasing order of
 * precedence they come from app-defaults, ~/.Xresources (xrdb), the
 * settings file ~/.config/ttr/settings (core/settings.h, written by the
 * Settings dialog), and the command line (-focus, -switch, -xrm):
 *
 *   Ttr*focusPolicy:    click | pointer
 *   Ttr*switchStyle:    windows | stack
 *   Ttr*appSource:      auto | desktop | toolchest
 *   Ttr*toolchestFiles: ~/.auxchestrc:/usr/lib/X11/system.chestrc
 *   Ttr*terminal:       xterm -e
 *   Ttr*clockFormat:    %H:%M
 *   Ttr*titleFont:      core X font (XLFD) for window titles
 *   Ttr*menuFont:       for menus and dialogs (Motif)
 *   Ttr*taskbarFont:    for task buttons and the clock
 *   Ttr*desktops:       number of virtual desktops, 1 .. 16
 *   Ttr*dockWidth:      third | half (docked and snapped windows)
 *   Ttr*notificationTime: seconds a notification stays up
 *   Ttr*notificationLog:  a file whose new lines become notifications
 *                         (e.g. /var/adm/SYSLOG on IRIX); empty: none
 *   Ttr*autostartScript:  true | false: run ~/.config/ttr/autostart.sh
 *                         at startup (core/startup.h)
 *   Ttr*xdgAutostart:     true | false: start ~/.config/autostart and
 *                         /etc/xdg/autostart entries (core/xdgautostart.h)
 *   Ttr*uiScale:          100 .. 300 percent: everything ttr draws, and its
 *                         fonts (core/scale.h)
 *   Ttr*fontPath:         folders added to the X server's font path, for
 *                         Type1 and TrueType fonts (colon separated)
 *   Ttr*theme:            color theme name (core/theme.h), default Classic
 *   Ttr*colorBackground, colorForeground, colorActive, colorActiveText,
 *   colorInactive, colorInactiveText: the user's colors over the theme
 *
 * With the Classic theme, mwm's Mwm*activeBackground, activeForeground,
 * background and foreground resources color the frames.
 */
#ifndef TTR_X11_CONFIG_H
#define TTR_X11_CONFIG_H

#include "core/str.h"
#include "core/theme.h"

#include <X11/Intrinsic.h>

namespace ttr {


static const int k_max_desktops = 16;

/* The user's colors: settings names and the roles they set. */
static const int k_ncolor_keys = 6;
extern const char* const k_color_keys[k_ncolor_keys];
extern const ThemeRole k_color_roles[k_ncolor_keys];

struct Config {
    enum FocusPolicy { FOCUS_CLICK, FOCUS_POINTER };
    enum SwitchStyle { SWITCH_WINDOWS, SWITCH_STACK };
    enum AppSourceKind { APPS_DESKTOP, APPS_TOOLCHEST };

    FocusPolicy focus;
    SwitchStyle switch_style;
    AppSourceKind app_source;
    Str app_source_name;    /* as set: auto, desktop or toolchest */
    Str toolchest_setting;  /* as set, before ~ expansion */
    Str toolchest_files;    /* colon separated, ~ expanded */
    Str terminal;
    Str clock_format;
    Str title_font;         /* as set, at 100% */
    Str menu_font;
    Str taskbar_font;
    Str title_xfont;        /* to load: scaled to ui_scale */
    Str menu_xfont;
    Str taskbar_xfont;
    int desktops;
    int dock_div;           /* 3: thirds, 2: halves */
    int notification_ms;
    Str notification_log;
    bool autostart_script;
    bool xdg_autostart;
    int ui_scale;           /* percent */
    Str font_path;          /* as set */
    Str theme_name;         /* as set */
    Str colors[k_ncolor_keys];  /* as set, "" for the theme's */
    Theme theme;            /* with the user's colors applied */

    Config();
};

/* Arguments for XtOpenApplication. */
XrmOptionDescRec* config_options();
int config_option_count();
String* config_fallback_resources();

/* Merge the settings file into the resource database, below the command
 * line (pass the argv from before XtOpenApplication). Call before any
 * other widget is created. */
void merge_settings_file(Widget toplevel, int argc, char** argv);

/* Read the settings, and point Motif's fonts at menuFont. */
void read_config(Widget toplevel, Config* cfg);

/* An XLFD at 'percent' of its size: the nearest size the server has. Names
 * that are not XLFDs, or have no size, are returned as they are. */
Str scale_font(Display* dpy, const char* name, int percent);

/* Defaults, for the Settings dialog's "Default" choices. */
const char* config_default(const char* name);

}

#endif
