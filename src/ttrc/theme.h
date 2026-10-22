/*
 * ttrc's colours (ttrc/DESIGN.md section 3.4). One set of styles per
 * theme; the colours are 24-bit and tu maps them to what the terminal
 * has. On a terminal without colours every theme becomes "mono": bold,
 * underline and reverse.
 */
#ifndef TTRC_THEME_H
#define TTRC_THEME_H

#include "../../tu/screen.h"
#include "../../tu/widgets.h"

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
    /* The viewer and the editor */
    tu::Style text;
    tu::Style text_status;
    tu::Style text_select;      /* a selection, a search hit */
    tu::Style text_ctrl;        /* control characters, hex offsets */
    tu::DialogStyle dialog;
    tu::DialogStyle alarm;      /* errors and dangerous questions */
    tu::DialogStyle menu;       /* pull-down menus */
};

/* The themes, by name; theme_names()[i] ends with a null pointer. */
const char* const* theme_names();
Theme theme_for(tu::Tier tier, const char* name);

} /* namespace ttrc */

#endif
