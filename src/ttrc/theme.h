/*
 * ttrc's colours (ttrc/DESIGN.md section 3.4). One set of styles per
 * look; the colours are 24-bit and tu maps them to what the terminal
 * has. On a terminal without colours the theme uses bold, underline and
 * reverse instead.
 */
#ifndef TTRC_THEME_H
#define TTRC_THEME_H

#include "../../tu/screen.h"

namespace ttrc {

struct Theme {
    tu::Style panel;            /* panel background and plain files */
    tu::Style frame;
    tu::Style frame_active_title;
    tu::Style frame_title;
    tu::Style header;           /* column names */
    tu::Style dir;
    tu::Style exec;
    tu::Style link;
    tu::Style special;          /* devices, fifos, sockets */
    tu::Style marked;
    tu::Style cursor;           /* the cursor bar */
    tu::Style cursor_marked;
    tu::Style status;           /* the line under the listing */
    tu::Style menu_bar;
    tu::Style menu_hotkey;
    tu::Style key_num;          /* the F-key bar: numbers */
    tu::Style key_label;
    tu::Style command;          /* the command line */
    tu::Style error;
    tu::FrameStyle frame_style;
};

/* The theme for this terminal. */
Theme theme_for(tu::Tier tier);

} /* namespace ttrc */

#endif
