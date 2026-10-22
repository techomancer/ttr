#include "theme.h"

#include <string.h>

namespace ttrc {

using tu::Style;
using tu::COLOR_DEFAULT;

namespace {

const char* const k_names[] = { "classic", "midnight", "dark", "mono", 0 };

tu::DialogStyle dialog_style(tu::Color body_fg, tu::Color body_bg, tu::Color frame_fg,
                             tu::Color title_fg, tu::Color input_fg, tu::Color input_bg,
                             tu::Color focus_fg, tu::Color focus_bg, tu::Color hot,
                             tu::FrameStyle fs)
{
    tu::DialogStyle d;
    d.body = Style(body_fg, body_bg);
    d.frame = Style(frame_fg, body_bg);
    d.title = Style(title_fg, body_bg, tu::ATTR_BOLD);
    d.input = Style(input_fg, input_bg);
    d.input_focus = Style(input_fg, input_bg);
    d.button = Style(body_fg, body_bg);
    d.button_focus = Style(focus_fg, focus_bg);
    d.hotkey = Style(hot, body_bg, tu::ATTR_BOLD);
    d.list = Style(input_fg, input_bg);
    d.list_cursor = Style(focus_fg, focus_bg);
    d.list_cursor_unfocused = Style(body_fg, body_bg, tu::ATTR_REVERSE);
    d.progress = Style(focus_fg, focus_bg);
    d.frame_style = fs;
    return d;
}

Theme mono()
{
    Theme t;
    Style plain;
    Style rev(COLOR_DEFAULT, COLOR_DEFAULT, tu::ATTR_REVERSE);
    Style bold(COLOR_DEFAULT, COLOR_DEFAULT, tu::ATTR_BOLD);
    t.panel = plain;
    t.frame = plain;
    t.frame_title = plain;
    t.frame_active_title = rev;
    t.header = bold;
    t.dir = bold;
    t.exec = plain;
    t.link = plain;
    t.special = plain;
    t.marked = Style(COLOR_DEFAULT, COLOR_DEFAULT, tu::ATTR_UNDERLINE);
    t.cursor = rev;
    t.cursor_marked = Style(COLOR_DEFAULT, COLOR_DEFAULT, tu::ATTR_REVERSE | tu::ATTR_UNDERLINE);
    t.status = plain;
    t.menu_bar = rev;
    t.menu_hotkey = Style(COLOR_DEFAULT, COLOR_DEFAULT, tu::ATTR_REVERSE | tu::ATTR_BOLD);
    t.key_num = plain;
    t.key_label = rev;
    t.command = plain;
    t.error = Style(COLOR_DEFAULT, COLOR_DEFAULT, tu::ATTR_BOLD | tu::ATTR_REVERSE);
    t.frame_style = tu::FRAME_DOUBLE;
    t.text = plain;
    t.text_status = rev;
    t.text_select = rev;
    t.text_ctrl = bold;
    tu::DialogStyle d;
    d.body = plain;
    d.frame = plain;
    d.title = bold;
    d.input = Style(COLOR_DEFAULT, COLOR_DEFAULT, tu::ATTR_UNDERLINE);
    d.input_focus = Style(COLOR_DEFAULT, COLOR_DEFAULT, tu::ATTR_UNDERLINE | tu::ATTR_BOLD);
    d.button = plain;
    d.button_focus = rev;
    d.hotkey = bold;
    d.list = plain;
    d.list_cursor = rev;
    d.list_cursor_unfocused = Style(COLOR_DEFAULT, COLOR_DEFAULT, tu::ATTR_UNDERLINE);
    d.progress = rev;
    d.frame_style = tu::FRAME_SINGLE;
    t.dialog = d;
    t.alarm = d;
    t.menu = d;
    return t;
}

/* The Norton Commander look: cyan on blue, grey dialogs. */
Theme classic()
{
    Theme t;
    const tu::Color blue = 0x0000a8, cyan = 0x55ffff, white = 0xffffff, yellow = 0xffff55,
                    black = 0x000000, teal = 0x00a8a8, green = 0x55ff55, grey = 0xc0c0c0,
                    magenta = 0xff55ff, red = 0xa80000;
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
    /* Red: yellow on cyan all but vanishes in xwsh's palette. */
    t.cursor_marked = Style(red, teal, tu::ATTR_BOLD);
    t.status = Style(cyan, blue);
    t.menu_bar = Style(black, teal);
    t.menu_hotkey = Style(red, teal);
    t.key_num = Style(white, black);
    t.key_label = Style(black, teal);
    t.command = Style();
    t.error = Style(white, red, tu::ATTR_BOLD);
    t.frame_style = tu::FRAME_DOUBLE;
    t.text = Style(white, blue);
    t.text_status = Style(black, teal);
    t.text_select = Style(black, teal);
    t.text_ctrl = Style(yellow, blue);
    t.dialog = dialog_style(black, grey, white, blue, black, teal, black, teal, red,
                            tu::FRAME_DOUBLE);
    t.alarm = dialog_style(white, red, white, yellow, black, grey, black, grey, yellow,
                           tu::FRAME_DOUBLE);
    t.menu = dialog_style(black, teal, white, black, black, teal, white, black, yellow,
                          tu::FRAME_SINGLE);
    return t;
}

/* Deeper blues with softer text, for true colour terminals. */
Theme midnight()
{
    Theme t = classic();
    const tu::Color bg = 0x0b2a78;
    t.panel = Style(0x7fe8ff, bg);
    t.frame = Style(0x5ab4e6, bg);
    t.frame_title = Style(0x9fd8ff, bg);
    t.frame_active_title = Style(0x0b1530, 0x6fd3e8, tu::ATTR_BOLD);
    t.header = Style(0xffe27a, bg, tu::ATTR_BOLD);
    t.dir = Style(0xffffff, bg, tu::ATTR_BOLD);
    t.exec = Style(0x8cff8c, bg, tu::ATTR_BOLD);
    t.link = Style(0xc8d4e8, bg, tu::ATTR_ITALIC);
    t.special = Style(0xff9cf0, bg);
    t.marked = Style(0xffe27a, bg, tu::ATTR_BOLD);
    t.cursor = Style(0x0b1530, 0x6fd3e8);
    t.cursor_marked = Style(0x7a2a00, 0x6fd3e8, tu::ATTR_BOLD);
    t.status = Style(0x9fd8ff, bg);
    t.menu_bar = Style(0x0b1530, 0x6fd3e8);
    t.menu_hotkey = Style(0x8a1c00, 0x6fd3e8, tu::ATTR_BOLD);
    t.key_label = Style(0x0b1530, 0x6fd3e8);
    t.key_num = Style(0xffffff, 0x101010);
    t.text = Style(0xe8f0ff, bg);
    t.text_status = Style(0x0b1530, 0x6fd3e8);
    t.text_select = Style(0x0b1530, 0xffe27a);
    t.text_ctrl = Style(0xffe27a, bg);
    t.dialog = dialog_style(0x101828, 0xd4dae6, 0x506078, 0x0b2a78, 0x101828, 0x9fd8ff,
                            0x0b1530, 0x6fd3e8, 0xa02000, tu::FRAME_ROUNDED);
    t.alarm = dialog_style(0xffffff, 0xa82828, 0xffd0d0, 0xffe27a, 0x101828, 0xe0e0e0,
                           0x101828, 0xffe27a, 0xffe27a, tu::FRAME_ROUNDED);
    t.menu = dialog_style(0x0b1530, 0x9ad9ea, 0x0b2a78, 0x0b1530, 0x0b1530, 0x9ad9ea,
                          0xffffff, 0x0b2a78, 0x8a1c00, tu::FRAME_SINGLE);
    return t;
}

/* Charcoal and amber. */
Theme dark()
{
    Theme t = classic();
    const tu::Color bg = 0x1c1c1c, fg = 0xc8c8c8, amber = 0xffb000;
    t.panel = Style(fg, bg);
    t.frame = Style(0x5a5a5a, bg);
    t.frame_title = Style(0x9a9a9a, bg);
    t.frame_active_title = Style(0x1c1c1c, amber, tu::ATTR_BOLD);
    t.header = Style(amber, bg, tu::ATTR_BOLD);
    t.dir = Style(0xffffff, bg, tu::ATTR_BOLD);
    t.exec = Style(0x87d787, bg, tu::ATTR_BOLD);
    t.link = Style(0x87afd7, bg);
    t.special = Style(0xd787d7, bg);
    t.marked = Style(amber, bg, tu::ATTR_BOLD);
    t.cursor = Style(0x1c1c1c, 0x8a8a8a);
    t.cursor_marked = Style(0x5f0000, 0x8a8a8a, tu::ATTR_BOLD);
    t.status = Style(0x9a9a9a, bg);
    t.menu_bar = Style(0x1c1c1c, 0x8a8a8a);
    t.menu_hotkey = Style(0x5f0000, 0x8a8a8a, tu::ATTR_BOLD);
    t.key_label = Style(0x1c1c1c, 0x8a8a8a);
    t.key_num = Style(amber, 0x000000);
    t.frame_style = tu::FRAME_SINGLE;
    t.text = Style(0xd0d0d0, bg);
    t.text_status = Style(0x1c1c1c, 0x8a8a8a);
    t.text_select = Style(0x1c1c1c, amber);
    t.text_ctrl = Style(amber, bg);
    t.dialog = dialog_style(0xd0d0d0, 0x303030, 0x707070, amber, 0xffffff, 0x4a4a4a,
                            0x1c1c1c, amber, amber, tu::FRAME_ROUNDED);
    t.alarm = dialog_style(0xffffff, 0x5f0000, 0xd78787, amber, 0xffffff, 0x870000,
                           0x1c1c1c, amber, amber, tu::FRAME_ROUNDED);
    t.menu = dialog_style(0xd0d0d0, 0x303030, 0x707070, amber, 0xffffff, 0x303030,
                          0x1c1c1c, amber, amber, tu::FRAME_SINGLE);
    return t;
}

} /* namespace */

const char* const* theme_names()
{
    return k_names;
}

Theme theme_for(tu::Tier tier, const char* name)
{
    if (tier == tu::TIER_BASIC || (name && strcmp(name, "mono") == 0))
        return mono();
    if (name && strcmp(name, "dark") == 0)
        return dark();
    if (name && strcmp(name, "midnight") == 0 && tier >= tu::TIER_256)
        return midnight();
    if (!name || !*name)
        return tier == tu::TIER_MODERN ? midnight() : classic();
    return classic();
}

} /* namespace ttrc */
