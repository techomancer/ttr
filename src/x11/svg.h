/*
 * SVG icons through nanosvg (third_party/nanosvg, zlib license). Only
 * svg.cpp includes nanosvg; it is built with relaxed warnings because the
 * vendored code is not ours to fix.
 */
#ifndef TTR_X11_SVG_H
#define TTR_X11_SVG_H

#include "core/port.h"

namespace ttr {

/* Render the SVG file at 'path' into size x size non-premultiplied ARGB
 * pixels (0xAARRGGBB), centered and scaled to fit. False on failure. */
bool svg_render(const char* path, int size, uint32_t* argb);

}

#endif
