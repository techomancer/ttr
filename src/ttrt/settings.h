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
    { "bell", "audible" }
};

/* The fonts ttrt tries in order when no font is set: the ISO 10646 ones
 * cover Unicode on Linux; IRIX servers have only Latin-1 fonts. */
static const char* const k_auto_fonts[] = {
    "-misc-fixed-medium-r-normal--15-140-75-75-c-90-iso10646-1",
    "-misc-fixed-medium-r-normal--13-120-75-75-c-80-iso10646-1",
    "-sgi-screen-medium-r-normal--15-150-72-72-m-90-iso8859-1",
    "9x15",
    "fixed",
    0
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
