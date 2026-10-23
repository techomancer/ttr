/*
 * ttre's settings: its own file, $XDG_CONFIG_HOME/ttre/settings (or
 * ~/.config/ttre/settings), "name: value" lines. F9 > Options > Editor
 * writes it; ttre needs neither ttrc nor ttr.
 */
#ifndef TTRE_SETTINGS_H
#define TTRE_SETTINGS_H

#include "core/str.h"

namespace ttre {

struct Settings {
    ttr::Str theme;             /* tu's themes: classic, midnight, dark, mono; "": by terminal */
    ttr::Str tier;              /* basic, ansi, 256, modern; "": what TERM says */
    ttr::Str scheme;            /* a th text scheme for the text; "": the theme's colours */
    int tab_width;
    int indent_width;
    bool insert_spaces;
    bool auto_indent;
    bool line_numbers;
    bool highlight_line;
    bool show_whitespace;
    int ruler;                  /* column marker; 0: none */
    ttr::Str cursor_shape;      /* default, bar, block, underline */
    bool cursor_blink;
    ttr::Str cursor_color;      /* #rrggbb; "": the terminal's */

    Settings();
    void load();
    bool save() const;
    static ttr::Str path();
};

} /* namespace ttre */

#endif
