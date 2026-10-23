/*
 * th/motif.h - UI themes on Motif widgets. Needs Xt and Motif 2.1.
 */
#ifndef TH_MOTIF_H
#define TH_MOTIF_H

#include "theme.h"

#include <Xm/Xm.h>

namespace th {

/* The theme as resources ("*background", "*XmList.background", ...),
 * for widgets made from now on: what the theme sets, nothing else, so
 * Motif derives the rest as usual. */
void apply_theme(XrmDatabase* db, const Theme& t);

/* Recolour existing widgets: 'root', its children and its popups, with
 * Motif's derived shadows (XmChangeColor). Widgets of class 'skip' and
 * their children keep their colours (a program's own drawing areas). */
void recolor(Widget root, const Theme& t, WidgetClass skip);

/* An X pixel for a colour, allocated in the widget's colour map. */
Pixel pixel(Widget w, Color c);

} /* namespace th */

#endif
