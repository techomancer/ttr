/*
 * ttrt's settings: its own file, $XDG_CONFIG_HOME/ttrt/settings (or
 * ~/.config/ttrt/settings), one "name: value" per line with the names of
 * its X resources (TTRt*name). The Settings dialog (settingsdlg.h)
 * writes it. The file wins over X resources and the command line wins
 * over both (ttrt/DESIGN.md section 9). ttrt does not need ttr: the
 * window manager has no say in these.
 */
#ifndef TTRT_SETTINGS_H
#define TTRT_SETTINGS_H

#include "core/str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ttrt {

/* Each setting: its name and the value used when nothing sets it. A null
 * default means "ttrt decides" (the font: the best one the X server has;
 * the cursor: the text colour). */
struct Setting {
    const char* name;
    const char* fallback;
};

enum {
    S_FONT,             /* an X font name */
    S_FOREGROUND,       /* colours, as X colour names or #rrggbb */
    S_BACKGROUND,
    S_CURSOR_COLOR,
    S_SAVE_LINES,       /* lines of scrollback */
    S_LOGIN_SHELL,      /* true, false */
    S_MENU_BAR,         /* true, false */
    S_BOLD_IS_BRIGHT,   /* bold text in colours 0-7 uses 8-15 */
    S_BACKSPACE,        /* del, bs */
    S_BELL,             /* audible, visual, off */
    S_COLOR_SCHEME,     /* a text colour scheme (th/scheme.h); "": none */
    S_RENDERER,         /* auto, core, soft, gl (tw's renderers) */
    S_DYNAMIC_BACKGROUND, /* an animated background (gl), or "" */
    S_TEXT_RENDERING,   /* sharp, smooth, auto (tw's text) */
    S_COUNT
};

static const Setting k_settings[S_COUNT] = {
    { "font", 0 },
    { "foreground", "gray90" },
    { "background", "black" },
    { "cursorColor", 0 },
    { "saveLines", "2000" },
    { "loginShell", "false" },
    { "menuBar", "true" },
    { "boldIsBright", "true" },
    { "backspace", "del" },
    { "bell", "audible" },
    { "colorScheme", "" },
    { "renderer", "auto" },
    { "dynamicBackground", "" },
    { "textRendering", "auto" }
};

/* $XDG_CONFIG_HOME/ttrt/settings, or ~/.config/ttrt/settings. */
inline ttr::Str settings_path()
{
    const char* xdg = getenv("XDG_CONFIG_HOME");
    const char* home = getenv("HOME");
    char path[1100];
    if (xdg && *xdg && strlen(xdg) < 1000)
        sprintf(path, "%s/ttrt/settings", xdg);
    else
        sprintf(path, "%.1000s/.config/ttrt/settings", home ? home : ".");
    return ttr::Str(path);
}

}

#endif
