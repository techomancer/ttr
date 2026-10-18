#include "core/shortcuts.h"
#include "core/settings.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace ttr {

ShortcutList::ShortcutList() {}

ShortcutList::~ShortcutList()
{
    clear();
}

void ShortcutList::clear()
{
    int i;
    for (i = 0; i < items_.size(); i++)
        delete items_[i];
    items_.clear();
}

void ShortcutList::add(Shortcut* s)
{
    items_.push_back(s);
}

void ShortcutList::remove(int i)
{
    if (i < 0 || i >= items_.size())
        return;
    delete items_[i];
    items_.remove_at(i);
}

void ShortcutList::swap(int i, int j)
{
    if (i < 0 || j < 0 || i >= items_.size() || j >= items_.size())
        return;
    Shortcut* t = items_[i];
    items_[i] = items_[j];
    items_[j] = t;
}

static void trim(char* s)
{
    int n = (int)strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = 0;
    int i = 0;
    while (s[i] && isspace((unsigned char)s[i]))
        i++;
    if (i > 0)
        memmove(s, s + i, n - i + 1);
}

bool ShortcutList::load(const char* path)
{
    FILE* f = fopen(path, "r");
    if (!f)
        return false;
    clear();
    char line[2048];
    Shortcut* cur = 0;
    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (!line[0] || line[0] == '#' || line[0] == '!')
            continue;
        if (strcmp(line, "[shortcut]") == 0) {
            cur = new Shortcut;
            items_.push_back(cur);
            continue;
        }
        char* colon = strchr(line, ':');
        if (!cur || !colon)
            continue;
        *colon = 0;
        char* key = line;
        char* value = colon + 1;
        trim(key);
        trim(value);
        if (strcmp(key, "name") == 0) cur->name = value;
        else if (strcmp(key, "command") == 0) cur->command = value;
        else if (strcmp(key, "icon") == 0) cur->icon = value;
        else if (strcmp(key, "key") == 0) cur->key = value;
        else if (strcmp(key, "quicklaunch") == 0)
            cur->quicklaunch = strcmp(value, "false") != 0;
    }
    fclose(f);

    /* A shortcut without a command does nothing. */
    int i;
    for (i = 0; i < items_.size(); ) {
        if (items_[i]->command.empty())
            remove(i);
        else
            i++;
    }
    return true;
}

bool ShortcutList::save(const char* path) const
{
    make_parent_dirs(path);
    char tmp[1100];
    if (strlen(path) > 1000)
        return false;
    sprintf(tmp, "%s.new", path);

    FILE* f = fopen(tmp, "w");
    if (!f)
        return false;
    fprintf(f, "# To The Roots shortcuts, in order. Edited by Start > Settings >\n"
               "# Edit Shortcuts..., or by hand. Keys look like Ctrl+Alt+T\n"
               "# (Ctrl, Alt, Shift, Super, then a key name such as F5 or\n"
               "# Return). quicklaunch: true shows the icon next to Start.\n");
    int i;
    for (i = 0; i < items_.size(); i++) {
        const Shortcut* s = items_[i];
        fprintf(f, "\n[shortcut]\n");
        fprintf(f, "name: %s\n", s->name.c_str());
        fprintf(f, "command: %s\n", s->command.c_str());
        if (!s->icon.empty())
            fprintf(f, "icon: %s\n", s->icon.c_str());
        if (!s->key.empty())
            fprintf(f, "key: %s\n", s->key.c_str());
        fprintf(f, "quicklaunch: %s\n", s->quicklaunch ? "true" : "false");
    }
    bool ok = fflush(f) == 0;
    ok = fclose(f) == 0 && ok;
    if (!ok || rename(tmp, path) != 0) {
        ::remove(tmp);
        return false;
    }
    return true;
}

Str ShortcutList::default_path()
{
    return config_path("shortcuts");
}

}
