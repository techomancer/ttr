/*
 * tl/input.h - keys, paste and focus, encoded for the application in the
 * terminal's current modes. Hosts translate their native key events to a
 * tl::Key plus modifiers, or hand over the text a key produced.
 *
 * The encoding is xterm's (legacy, "PC-style" function keys with
 * modifier parameters). The kitty keyboard protocol comes later.
 */
#ifndef TL_INPUT_H
#define TL_INPUT_H

namespace tl {

enum Key {
    KEY_NONE = 0,
    KEY_ENTER,
    KEY_TAB,
    KEY_BACKSPACE,
    KEY_ESCAPE,
    KEY_UP,
    KEY_DOWN,
    KEY_RIGHT,
    KEY_LEFT,
    KEY_HOME,
    KEY_END,
    KEY_INSERT,
    KEY_DELETE,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN,
    KEY_BEGIN,                  /* keypad 5 without NumLock */
    KEY_F1,                     /* KEY_F1 + n - 1 for Fn, up to F20 */
    KEY_F20 = KEY_F1 + 19,
    KEY_KP_0,                   /* KEY_KP_0 + n for keypad digit n */
    KEY_KP_9 = KEY_KP_0 + 9,
    KEY_KP_DECIMAL,
    KEY_KP_DIVIDE,
    KEY_KP_MULTIPLY,
    KEY_KP_SUBTRACT,
    KEY_KP_ADD,
    KEY_KP_ENTER,
    KEY_KP_EQUAL,
    KEY_KP_SEPARATOR
};

enum Mod {
    MOD_SHIFT = 1,
    MOD_ALT = 2,
    MOD_CTRL = 4,
    MOD_META = 8
};

} /* namespace tl */

#endif
