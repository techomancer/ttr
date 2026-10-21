/*
 * ttrt's settings in ttr's settings file (core/settings.h): "ttrt.<name>:
 * value" lines, read by ttrt at startup and written by ttr's Settings >
 * Terminal page. The same names work as X resources (TTRt*<name>) and the
 * command line wins over both (ttrt/DESIGN.md section 9).
 */
#ifndef TTR_CORE_TERMSETTINGS_H
#define TTR_CORE_TERMSETTINGS_H

namespace ttr {

/* The prefix of ttrt's lines in the settings file. */
#define TTRT_SETTING_PREFIX "ttrt."

/* Each setting: its name and the value ttrt uses when nothing sets it.
 * A null default means "ttrt decides" (the font: the best one the X
 * server has; the cursor: the text colour). */
struct TermSetting {
    const char* name;
    const char* fallback;
};

enum {
    TS_FONT,            /* an X font name */
    TS_FOREGROUND,      /* colours, as X colour names or #rrggbb */
    TS_BACKGROUND,
    TS_CURSOR_COLOR,
    TS_SAVE_LINES,      /* lines of scrollback */
    TS_LOGIN_SHELL,     /* true, false */
    TS_MENU_BAR,        /* true, false */
    TS_BOLD_IS_BRIGHT,  /* bold text in colours 0-7 uses 8-15 */
    TS_BACKSPACE,       /* del, bs */
    TS_BELL,            /* audible, visual, off */
    TS_COUNT
};

static const TermSetting k_term_settings[TS_COUNT] = {
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
static const char* const k_term_auto_fonts[] = {
    "-misc-fixed-medium-r-normal--15-140-75-75-c-90-iso10646-1",
    "-misc-fixed-medium-r-normal--13-120-75-75-c-80-iso10646-1",
    "-sgi-screen-medium-r-normal--15-150-72-72-m-90-iso8859-1",
    "9x15",
    "fixed",
    0
};

}

#endif
