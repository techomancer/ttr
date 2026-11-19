/*
 * ttrv's software rendering of the viewer (ttrv/DESIGN.md section 5.2):
 * an image at any zoom and position into a window-sized frame, and
 * rotation by quarter turns. Only the window's pixels are computed, so a
 * zoomed 12-megapixel image costs what the window costs.
 */
#ifndef TTRV_RENDER_H
#define TTRV_RENDER_H

#include "../../ti/ti.h"

namespace ttrv {

/* out (w x h, allocated here) shows src at 'zoom' window pixels per
 * source pixel, the window's top left at source position (x0, y0); the
 * rest is bg (0xRRGGBB). Shrinking averages the pixels each window pixel
 * covers; growing is bilinear up to 2x and the nearest pixel beyond, so
 * pixels stay sharp when looked at closely. */
bool render_view(const ti::Image& src, double zoom, double x0, double y0, int w, int h,
                 unsigned long bg, ti::Image* out);

/* src turned clockwise by quarter turns (0 to 3) into out. */
bool rotate(const ti::Image& src, int quarters, ti::Image* out);

} /* namespace ttrv */

#endif
