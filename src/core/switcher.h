/*
 * Windows-style Alt+Tab panel: a grid of window icons with the selected
 * window's title in a sunken box underneath.
 */
#ifndef TTR_CORE_SWITCHER_H
#define TTR_CORE_SWITCHER_H

#include "core/color.h"

namespace ttr {

class Icon;
class Painter;

struct SwitchItem {
    const Icon* icon;
    const char* title;
};

void switcher_size(Painter& p, int count, int* w, int* h);

void paint_switcher(Painter& p, const ColorSet& cs, const SwitchItem* items,
                    int count, int selected, int w, int h);

}

#endif
