/*
 * th/scheme.h - text colour schemes, for the text views of ttr's
 * programs: ttrcode's editor and terminal pane, ttrt's terminal. A
 * scheme file is lines of "key: #rrggbb":
 *
 *   name: Dark
 *   foreground: #d4d4d4
 *   background: #1e1e1e
 *   color1: #cd3131
 *
 * Keys a scheme leaves out are derived from the ones it has: the gutter
 * from the background, the selection from the foreground, the terminal
 * palette (color0 to color15) from xterm's, adjusted to light or dark.
 * "Light" and "Dark" are built in.
 */
#ifndef TH_SCHEME_H
#define TH_SCHEME_H

#include "th.h"

namespace th {

enum SchemeColor {
    SC_FOREGROUND,
    SC_BACKGROUND,
    SC_CURSOR,
    SC_SELECTION_FG,
    SC_SELECTION_BG,
    SC_GUTTER_FG,           /* line numbers */
    SC_GUTTER_BG,
    SC_FIND_BG,             /* the last search hit */
    SC_CONTROL,             /* control characters shown as ^X */
    SC_COMMENT,             /* syntax colours (ttrcode, later) */
    SC_KEYWORD,
    SC_STRING,
    SC_NUMBER,
    SC_PREPROCESSOR,
    SC_CURRENT_LINE,        /* the background of the caret's line */
    SC_RULER,               /* the column marker */
    SC_WHITESPACE,          /* visible spaces and tabs */
    SC_ANSI0,               /* the terminal's 16 colours: black, red, ... */
    SC_COUNT = SC_ANSI0 + 16
};

/* The key in scheme files ("foreground", "color3", ...). */
const char* scheme_key(SchemeColor c);

struct Scheme {
    char name[128];
    char path[1024];        /* empty for a built-in scheme */
    bool dark;
    Color color[SC_COUNT];
    bool set[SC_COUNT];

    Scheme();
    static Scheme light();  /* black on white, VS6's */
    static Scheme dark_default();
    bool load(const char* path);
    /* Set or derived. */
    Color get(SchemeColor c) const;
    /* The terminal's colour i (0-15). */
    Color ansi(int i) const { return get((SchemeColor)(SC_ANSI0 + (i & 15))); }
};

/* "Light" and "Dark", then the user's (~/.config/ttr/schemes), then
 * ttr's (data/schemes/text). */
void list_schemes(List* out);
/* False, with Light in *out, if there is no such scheme. */
bool find_scheme(const char* name, Scheme* out);

} /* namespace th */

#endif
