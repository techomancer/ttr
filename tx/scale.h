/*
 * tx/scale.h - the UI scale (core/scale.h) on Motif: fonts at a percent
 * of their size, and the pixel sizes Motif does not take from its fonts
 * (shadows, focus highlights, scroll bars). ttr scales itself with these;
 * ttrt and ttrcode have a UI scale setting of their own that follows
 * ttr's unless it is set.
 */
#ifndef TX_SCALE_H
#define TX_SCALE_H

#include "core/str.h"

#include <X11/Intrinsic.h>

namespace tx {

/* An XLFD at 'percent' of its size: the nearest size the server has, in
 * the family asked for or a similar one. Names that are not XLFDs, or
 * have no size, are returned as they are. With 'who' (the program's
 * name), a missing size or family is reported on stderr. */
ttr::Str scale_font(Display* dpy, const char* name, int percent, const char* who = 0);

/* Shadows, focus highlights and combo box arrows at ttr::ui_scale(), for
 * widgets made from now on. */
void scale_motif_resources(XrmDatabase* db);

/* The scales the Settings dialogs offer, in percent. */
extern const char* const k_ui_scales[];
extern const int k_ui_scale_count;

/* A program's uiScale setting as a percent: empty (or not a number)
 * follows ttr's (tx/ttr.h); numbers are kept within 100..300. */
int ui_scale_of(const char* setting);

/* The UI scale setting in a Settings dialog: "Same as ttr", then the
 * scales as "150%" ('setting' is a uiScale value; one not on the list is
 * added). The value back: "" for ttr's, else the percent. */
Widget scale_drop_down(Widget parent, const char* setting);
ttr::Str scale_drop_down_value(Widget drop_down);

/* A Motif resource and the font it has at 100%. */
struct ScaledFont {
    const char* spec;       /* "*fontList", "*menuBar*fontList" */
    const char* xlfd;
};

/* Before any widget but the top-level shell is made: the scale for
 * ttr::scaled(), the fonts at it, thicker scroll bars, and Motif's pixel
 * sizes. At 100% only ttr::set_ui_scale(), so X resources keep their say. */
void apply_ui_scale(Widget top, int percent, const ScaledFont* fonts, int count);

} /* namespace tx */

#endif
