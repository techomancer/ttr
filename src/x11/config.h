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
 */
#ifndef TTR_X11_CONFIG_H
#define TTR_X11_CONFIG_H

#include "core/str.h"

#include <X11/Intrinsic.h>

namespace ttr {

/* Background of Motif widgets and our panels (0xRRGGBB). Keep in sync
 * with the "*background" fallback resource in config.cpp. */
static const unsigned long k_widget_bg = 0xb8b8b8;

static const int k_max_desktops = 16;

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
    Str title_font;
    Str menu_font;
    Str taskbar_font;
    int desktops;

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

/* Defaults, for the Settings dialog's "Default" choices. */
const char* config_default(const char* name);

}

#endif
