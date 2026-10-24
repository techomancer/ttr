/*
 * ttrc's settings and files, in $XDG_CONFIG_HOME/ttrc/ or ~/.config/ttrc/
 * (ttrc/DESIGN.md section 8): "settings" ("name: value" lines, as ttr and
 * ttrt keep theirs), "hotlist", "menu" (the F2 user menu), "assoc" (what
 * Enter does with a file) and "history".
 */
#ifndef TTRC_CONFIG_H
#define TTRC_CONFIG_H

#include "core/str.h"

namespace ttrc {

struct Config {
    bool confirm_delete;
    bool confirm_overwrite;     /* ask; otherwise overwrite */
    bool confirm_exit;          /* F10 asks before quitting */
    bool auto_return;           /* back to the panels after a command */
    bool show_hidden;
    bool remember_dirs;         /* start where the panels were */
    bool preserve_attrs;        /* copies keep modes and times */
    bool internal_viewer;       /* F3: ttrc's viewer, or $PAGER */
    bool internal_editor;       /* F4: ttrc's editor, or $EDITOR */
    ttr::Str theme;             /* "" (automatic), or a theme_names() entry */
    ttr::Str tier;              /* "" (automatic), basic, ansi, 256, modern */
    ttr::Str images;            /* the viewer's: "" (automatic), off, blocks, kitty, sixel, iterm */
    /* Saved state */
    ttr::Str left_dir, right_dir;
    int left_mode, right_mode;  /* ListMode */
    int left_sort, right_sort;  /* SortMode */

    Config();
    void load();
    bool save() const;
};

/* $XDG_CONFIG_HOME/ttrc/<name>, or ~/.config/ttrc/<name>. */
ttr::Str config_file(const char* name);

} /* namespace ttrc */

#endif
