/*
 * tx/appicon.h - a program's own icon on its top-level window, drawn from
 * its SVG (data/icons/) when it starts: _NET_WM_ICON at 16, 32 and 48
 * pixels for ttr's taskbar and other EWMH window managers, and a 48 pixel
 * icon pixmap with a mask (WM_HINTS) for mwm and 4Dwm. The SVG is built
 * into the program (the Makefile makes a header of it), so nothing has to
 * be installed. Needs ttr's nanosvg wrapper (x11/svg.cpp) linked in.
 */
#ifndef TX_APPICON_H
#define TX_APPICON_H

#include <X11/Intrinsic.h>

namespace tx {

/* 'shell' must be realized; 'svg' is the icon's SVG text. */
void set_app_icon(Widget shell, const char* svg);

} /* namespace tx */

#endif
