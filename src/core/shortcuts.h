/*
 * Program shortcuts: a name, a command, an optional icon and key, and
 * whether it appears in the quick launch bar. Stored in order in
 * ~/.config/ttr/shortcuts, one block per shortcut:
 *
 *   [shortcut]
 *   name: Terminal
 *   command: xterm
 *   icon: utilities-terminal
 *   key: Ctrl+Alt+T
 *   quicklaunch: true
 */
#ifndef TTR_CORE_SHORTCUTS_H
#define TTR_CORE_SHORTCUTS_H

#include "core/str.h"
#include "core/vector.h"

namespace ttr {

struct Shortcut {
    Str name;
    Str command;
    Str icon;       /* theme icon name or file path; may be empty */
    Str key;        /* e.g. "Ctrl+Alt+T"; may be empty */
    bool quicklaunch;

    Shortcut() : quicklaunch(true) {}
};

class ShortcutList {
public:
    ShortcutList();
    ~ShortcutList();

    bool load(const char* path);
    bool save(const char* path) const;

    int size() const { return items_.size(); }
    Shortcut* at(int i) { return items_[i]; }
    const Shortcut* at(int i) const { return items_[i]; }

    /* Takes ownership. */
    void add(Shortcut* s);
    void remove(int i);
    /* Swap entries i and j. */
    void swap(int i, int j);
    void clear();

    /* $XDG_CONFIG_HOME/ttr/shortcuts, or ~/.config/ttr/shortcuts. */
    static Str default_path();

private:
    ShortcutList(const ShortcutList&);
    ShortcutList& operator=(const ShortcutList&);

    Vector<Shortcut*> items_;
};

}

#endif
