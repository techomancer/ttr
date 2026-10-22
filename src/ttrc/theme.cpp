#include "theme.h"

namespace ttrc {

using tu::Style;
using tu::COLOR_DEFAULT;

Theme theme_for(tu::Tier tier)
{
    Theme t;
    if (tier == tu::TIER_BASIC) {
        /* No colours: what a VT220 can show. */
        Style plain;
        t.panel = plain;
        t.frame = plain;
        t.frame_title = plain;
        t.frame_active_title = Style(COLOR_DEFAULT, COLOR_DEFAULT, tu::ATTR_REVERSE);
        t.header = Style(COLOR_DEFAULT, COLOR_DEFAULT, tu::ATTR_BOLD);
        t.dir = Style(COLOR_DEFAULT, COLOR_DEFAULT, tu::ATTR_BOLD);
        t.exec = plain;
        t.link = plain;
        t.special = plain;
        t.marked = Style(COLOR_DEFAULT, COLOR_DEFAULT, tu::ATTR_UNDERLINE);
        t.cursor = Style(COLOR_DEFAULT, COLOR_DEFAULT, tu::ATTR_REVERSE);
        t.cursor_marked = Style(COLOR_DEFAULT, COLOR_DEFAULT,
                                tu::ATTR_REVERSE | tu::ATTR_UNDERLINE);
        t.status = plain;
        t.menu_bar = Style(COLOR_DEFAULT, COLOR_DEFAULT, tu::ATTR_REVERSE);
        t.menu_hotkey = Style(COLOR_DEFAULT, COLOR_DEFAULT, tu::ATTR_REVERSE | tu::ATTR_BOLD);
        t.key_num = plain;
        t.key_label = Style(COLOR_DEFAULT, COLOR_DEFAULT, tu::ATTR_REVERSE);
        t.command = plain;
        t.error = Style(COLOR_DEFAULT, COLOR_DEFAULT, tu::ATTR_BOLD | tu::ATTR_REVERSE);
        t.frame_style = tu::FRAME_DOUBLE;
        return t;
    }

    /* The Norton Commander look: cyan on blue. */
    const tu::Color blue = 0x0000a8, cyan = 0x55ffff, white = 0xffffff, yellow = 0xffff55,
                    black = 0x000000, teal = 0x00a8a8, green = 0x55ff55, grey = 0xc0c0c0,
                    magenta = 0xff55ff;
    t.panel = Style(cyan, blue);
    t.frame = Style(cyan, blue);
    t.frame_title = Style(cyan, blue);
    t.frame_active_title = Style(black, teal);
    t.header = Style(yellow, blue, tu::ATTR_BOLD);
    t.dir = Style(white, blue, tu::ATTR_BOLD);
    t.exec = Style(green, blue, tu::ATTR_BOLD);
    t.link = Style(grey, blue);
    t.special = Style(magenta, blue, tu::ATTR_BOLD);
    t.marked = Style(yellow, blue, tu::ATTR_BOLD);
    t.cursor = Style(black, teal);
    t.cursor_marked = Style(0xa80000, teal, tu::ATTR_BOLD);
    t.status = Style(cyan, blue);
    t.menu_bar = Style(black, teal);
    /* Red: yellow on cyan all but vanishes in xwsh's palette. */
    t.menu_hotkey = Style(0xa80000, teal);
    t.key_num = Style(white, black);
    t.key_label = Style(black, teal);
    t.command = Style();
    t.error = Style(white, 0xa80000, tu::ATTR_BOLD);
    t.frame_style = tu::FRAME_DOUBLE;
    if (tier == tu::TIER_MODERN) {
        /* A little depth where the terminal can show it. */
        t.panel = Style(0x7fe8ff, 0x0b2a78);
        t.frame = Style(0x5ab4e6, 0x0b2a78);
        t.frame_title = Style(0x9fd8ff, 0x0b2a78);
        t.frame_active_title = Style(0x0b1530, 0x6fd3e8, tu::ATTR_BOLD);
        t.header = Style(0xffe27a, 0x0b2a78, tu::ATTR_BOLD);
        t.dir = Style(0xffffff, 0x0b2a78, tu::ATTR_BOLD);
        t.exec = Style(0x8cff8c, 0x0b2a78, tu::ATTR_BOLD);
        t.link = Style(0xc8d4e8, 0x0b2a78, tu::ATTR_ITALIC);
        t.special = Style(0xff9cf0, 0x0b2a78);
        t.marked = Style(0xffe27a, 0x0b2a78, tu::ATTR_BOLD);
        t.cursor = Style(0x0b1530, 0x6fd3e8);
        t.cursor_marked = Style(0x7a4a00, 0x6fd3e8, tu::ATTR_BOLD);
        t.status = Style(0x9fd8ff, 0x0b2a78);
        t.menu_bar = Style(0x0b1530, 0x6fd3e8);
        t.menu_hotkey = Style(0x8a1c00, 0x6fd3e8, tu::ATTR_BOLD);
        t.key_label = Style(0x0b1530, 0x6fd3e8);
        t.key_num = Style(0xffffff, 0x101010);
    }
    return t;
}

} /* namespace ttrc */
