/*
 * tu: the terminal UI library (ttrc/DESIGN.md section 3). Common types.
 *
 * tu draws full-screen interfaces on a character terminal: it reads the
 * terminal's terminfo entry itself (no curses), keeps a screen of cells
 * and sends only what changed, and decodes keys and mouse events. It
 * builds with MIPSpro 7.4 and g++ -std=c++98 -pedantic, includes nothing
 * from src/ and nothing from X, and uses tl/ only for character widths.
 */
#ifndef TU_TU_H
#define TU_TU_H

#include "../tl/port.h"

namespace tu {

/* Colours are 24-bit RGB (0xRRGGBB); tu maps them to what the terminal
 * has. COLOR_DEFAULT is the terminal's own foreground or background. */
typedef uint32_t Color;
const Color COLOR_DEFAULT = 0xff000000u;
/* A colour of the terminal's own palette (0-255), sent as that index:
 * what a program in the shell asked for, redrawn exactly. */
const Color COLOR_INDEXED = 0x01000000u;
inline Color color_index(int i) { return COLOR_INDEXED | (Color)(i & 255); }
inline bool is_indexed(Color c) { return (c & 0xff000000u) == COLOR_INDEXED; }

enum {
    ATTR_BOLD = 1,
    ATTR_DIM = 2,
    ATTR_ITALIC = 4,
    ATTR_UNDERLINE = 8,
    ATTR_REVERSE = 16,
    ATTR_BLINK = 32
};

struct Style {
    Color fg;
    Color bg;
    unsigned attrs;

    Style() : fg(COLOR_DEFAULT), bg(COLOR_DEFAULT), attrs(0) {}
    Style(Color f, Color b, unsigned a = 0) : fg(f), bg(b), attrs(a) {}
    bool operator==(const Style& o) const
    {
        return fg == o.fg && bg == o.bg && attrs == o.attrs;
    }
    bool operator!=(const Style& o) const { return !(*this == o); }
};

/* What the terminal can do, best last (DESIGN.md section 3.3). */
enum Tier {
    TIER_BASIC = 0,     /* no colour: vt100, serial consoles */
    TIER_ANSI,          /* 8 or 16 colours: iris-ansi, Linux console */
    TIER_256,           /* 256 colours */
    TIER_MODERN         /* 24-bit colour, Unicode, ttrt and friends */
};

} /* namespace tu */

#endif
