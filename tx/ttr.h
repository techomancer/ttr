/*
 * tx/ttr.h - what ttr itself has set, for the programs whose settings
 * follow ttr's unless they have their own ("Same as ttr"): its UI scale
 * and UI theme, read from its settings file. The programs do not need ttr
 * running, or installed.
 */
#ifndef TX_TTR_H
#define TX_TTR_H

namespace tx {

/* ttr's UI scale (uiScale) in percent; 100 if not set. */
int ttr_ui_scale();

/* ttr's UI theme (theme), or "" for th's default. */
const char* ttr_theme();

} /* namespace tx */

#endif
