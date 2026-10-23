/*
 * Color themes: a color for each of a few roles. ttr's view of th's UI
 * themes (th/theme.h), which every ttr program shares. Theme files use the
 * format of the IRIX desktop color schemes (data/schemes/irix/README.md),
 * X resource lines with one value per display gamma:
 *
 *   isDark: true
 *   gamma1_0.BasicBackground: #585858
 *
 * ttr's own themes may leave the gamma prefix out, and may name
 * themselves ("name: Windows 2000"; otherwise the file name is the name).
 * A role a theme leaves out is derived: the specific backgrounds (button,
 * list, ...) follow the background, text colors follow Motif's rule for
 * their background (core/color.h).
 */
#ifndef TTR_CORE_THEME_H
#define TTR_CORE_THEME_H

#include "core/color.h"
#include "core/str.h"
#include "core/vector.h"

#include "../../th/theme.h"

namespace ttr {

enum ThemeRole {
    TR_BACKGROUND,      /* widgets, menus, the taskbar */
    TR_FOREGROUND,      /* their text */
    TR_BUTTON,          /* push buttons */
    TR_TEXT_FIELD,      /* text fields */
    TR_LIST,            /* lists */
    TR_TROUGH,          /* scroll bar trough */
    TR_SCROLL_BAR,      /* scroll bar slider */
    TR_SELECT_FILL,     /* toggle and radio button fill */
    TR_INACTIVE,        /* window frames */
    TR_INACTIVE_TEXT,
    TR_ACTIVE,          /* the focused window's frame */
    TR_ACTIVE_TEXT,
    TR_COUNT
};

/* The role's name in theme files (IRIX's names: "BasicBackground", ...). */
const char* theme_role_name(ThemeRole role);

struct Theme {
    Str name;
    Str path;           /* empty for the built-in Classic theme */
    bool dark;
    Rgb color[TR_COUNT];
    bool set[TR_COUNT];

    Theme();

    /* ttr's own look: grey widgets, blue active frames. */
    static Theme classic();

    /* Read a theme file; false if it cannot be read or sets nothing. */
    bool load(const char* path);

    /* The color for 'role', set or derived. */
    Rgb get(ThemeRole role) const;

    /* Set a role. A new background also drops the specific backgrounds,
     * so they follow it. */
    void put(ThemeRole role, const Rgb& c);

    /* The same theme as th's. */
    th::Theme to_th() const;
    static Theme from_th(const th::Theme& t);
};

struct ThemeInfo {
    Str name;
    Str path;
    Str origin;         /* "ttr", "IRIX" or "yours" */
};

/* Themes in the user's folder (~/.config/ttr/themes), then ttr's own,
 * then the IRIX schemes, by name within each; a name is listed once. */
void list_themes(Vector<ThemeInfo*>* out);
void free_themes(Vector<ThemeInfo*>* list);

/* The theme called 'name' ("Classic" or empty for the built-in one).
 * False, with Classic in *out, if there is no such theme. */
bool find_theme(const char* name, Theme* out);

/* argv[0], so themes are found next to an uninstalled build. */
void set_program_path(const char* argv0);

/* "#rrggbb" (or "#rgb"); false if 's' is not one. */
bool parse_hex_color(const char* s, Rgb* out);
/* "#rrggbb" into buf (8 bytes). */
void format_hex_color(const Rgb& c, char* buf);

}

#endif
