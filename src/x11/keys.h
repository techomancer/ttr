/*
 * Key specs as written in settings: "Ctrl+Alt+T", "Super+Return", "F5".
 * Modifiers are Ctrl, Alt, Shift and Super (also Win, Meta); the last part
 * is an X keysym name, matched without regard to case for single letters.
 */
#ifndef TTR_X11_KEYS_H
#define TTR_X11_KEYS_H

#include <X11/Xlib.h>

namespace ttr {

/* False if the spec is empty or names no key. */
bool parse_key_spec(const char* spec, KeySym* sym, unsigned int* mods);

/* The canonical spelling of a key, e.g. "Ctrl+Alt+T", into buf. */
void format_key_spec(KeySym sym, unsigned int mods, char* buf, int size);

}

#endif
