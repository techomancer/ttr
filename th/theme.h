/*
 * th/theme.h - UI themes: a colour for each of a few roles. Theme files
 * use the format of the IRIX desktop colour schemes
 * (data/schemes/irix/README.md), X resource lines with one value per
 * display gamma:
 *
 *   isDark: true
 *   gamma1_0.BasicBackground: #585858
 *
 * ttr's own themes may leave the gamma prefix out, and may name
 * themselves ("name: Windows 2000"; otherwise the file name is the name).
 * A role a theme leaves out is derived: the specific backgrounds (button,
 * list, ...) follow the background, text colours follow Motif's rule for
 * their background.
 */
#ifndef TH_THEME_H
#define TH_THEME_H

#include "th.h"

namespace th {

enum Role {
    ROLE_BACKGROUND,        /* widgets, menus, the taskbar */
    ROLE_FOREGROUND,        /* their text */
    ROLE_BUTTON,            /* push buttons */
    ROLE_TEXT_FIELD,        /* text fields */
    ROLE_LIST,              /* lists */
    ROLE_TROUGH,            /* scroll bar trough */
    ROLE_SCROLL_BAR,        /* scroll bar slider */
    ROLE_SELECT_FILL,       /* toggle and radio button fill */
    ROLE_INACTIVE,          /* window frames */
    ROLE_INACTIVE_TEXT,
    ROLE_ACTIVE,            /* the focused window's frame */
    ROLE_ACTIVE_TEXT,
    ROLE_COUNT
};

/* The role's name in theme files (IRIX's names: "BasicBackground", ...). */
const char* role_name(Role role);

struct Theme {
    char name[128];
    char path[1024];        /* empty for the built-in Classic theme */
    bool dark;
    Color color[ROLE_COUNT];
    bool set[ROLE_COUNT];

    Theme();
    /* ttr's own look: grey widgets, blue active frames. */
    static Theme classic();
    /* Read a theme file; false if it cannot be read or sets nothing. */
    bool load(const char* path);
    /* The colour for 'role', set or derived. */
    Color get(Role role) const;
    /* Set a role. A new background also drops the specific backgrounds,
     * so they follow it. */
    void put(Role role, Color c);
};

/* Themes in the user's folder (~/.config/ttr/themes), then ttr's own,
 * then the IRIX schemes; "Classic" (built in) first. */
void list_themes(List* out);
/* The theme called 'name' ("Classic" or empty for the built-in one).
 * False, with Classic in *out, if there is no such theme. */
bool find_theme(const char* name, Theme* out);

} /* namespace th */

#endif
