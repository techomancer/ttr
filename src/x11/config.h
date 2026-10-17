/*
 * Settings, read as X resources the way mwm does: from app-defaults,
 * ~/.Xresources (xrdb), or -xrm on the command line. For example:
 *
 *   Ttr*focusPolicy:   click | pointer
 *   Ttr*switchStyle:   windows | stack
 *   Ttr*appSource:     auto | desktop | toolchest
 *   Ttr*toolchestFiles: ~/.auxchestrc:/usr/lib/X11/system.chestrc
 *   Ttr*terminal:      xterm -e
 *   Ttr*clockFormat:   %H:%M
 *
 * -focus and -switch are command line shortcuts for the first two.
 */
#ifndef TTR_X11_CONFIG_H
#define TTR_X11_CONFIG_H

#include "core/str.h"

#include <X11/Intrinsic.h>

namespace ttr {

/* Background of Motif widgets and our panels (0xRRGGBB). Keep in sync
 * with the "*background" fallback resource in config.cpp. */
static const unsigned long k_widget_bg = 0xb8b8b8;

struct Config {
    enum FocusPolicy { FOCUS_CLICK, FOCUS_POINTER };
    enum SwitchStyle { SWITCH_WINDOWS, SWITCH_STACK };
    enum AppSourceKind { APPS_DESKTOP, APPS_TOOLCHEST };

    FocusPolicy focus;
    SwitchStyle switch_style;
    AppSourceKind app_source;
    Str toolchest_files;    /* colon separated, ~ expanded */
    Str terminal;
    Str clock_format;

    Config();
};

/* Arguments for XtOpenApplication. */
XrmOptionDescRec* config_options();
int config_option_count();
String* config_fallback_resources();

void read_config(Widget toplevel, Config* cfg);

}

#endif
