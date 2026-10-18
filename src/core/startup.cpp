#include "core/startup.h"
#include "core/launch.h"
#include "core/settings.h"
#include "core/xdgautostart.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ttr {

StartupList::StartupList() {}

StartupList::~StartupList()
{
    clear();
}

void StartupList::clear()
{
    int i;
    for (i = 0; i < items_.size(); i++)
        delete items_[i];
    items_.clear();
}

void StartupList::add(StartupItem* s)
{
    items_.push_back(s);
}

void StartupList::remove(int i)
{
    if (i < 0 || i >= items_.size())
        return;
    delete items_[i];
    items_.remove_at(i);
}

void StartupList::swap(int i, int j)
{
    if (i < 0 || j < 0 || i >= items_.size() || j >= items_.size())
        return;
    StartupItem* t = items_[i];
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

bool StartupList::load(const char* path)
{
    FILE* f = fopen(path, "r");
    if (!f)
        return false;
    clear();
    char line[2048];
    StartupItem* cur = 0;
    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (!line[0] || line[0] == '#' || line[0] == '!')
            continue;
        if (strcmp(line, "[program]") == 0) {
            cur = new StartupItem;
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
        if (strcmp(key, "command") == 0)
            cur->command = value;
        else if (strcmp(key, "enabled") == 0)
            cur->enabled = strcmp(value, "false") != 0;
    }
    fclose(f);

    int i;
    for (i = 0; i < items_.size(); ) {
        if (items_[i]->command.empty())
            remove(i);
        else
            i++;
    }
    return true;
}

bool StartupList::save(const char* path) const
{
    make_parent_dirs(path);
    char tmp[1100];
    if (strlen(path) > 1000)
        return false;
    sprintf(tmp, "%s.new", path);

    FILE* f = fopen(tmp, "w");
    if (!f)
        return false;
    fprintf(f, "# To The Roots startup programs, started in order when ttr\n"
               "# starts (not when it restarts). Edited by Start > Settings >\n"
               "# Startup, or by hand. enabled: false keeps an entry without\n"
               "# starting it.\n");
    int i;
    for (i = 0; i < items_.size(); i++) {
        const StartupItem* s = items_[i];
        fprintf(f, "\n[program]\n");
        fprintf(f, "command: %s\n", s->command.c_str());
        fprintf(f, "enabled: %s\n", s->enabled ? "true" : "false");
    }
    bool ok = fflush(f) == 0;
    ok = fclose(f) == 0 && ok;
    if (!ok || rename(tmp, path) != 0) {
        ::remove(tmp);
        return false;
    }
    return true;
}

Str StartupList::default_path()
{
    return config_path("startup");
}

Str autostart_script_path()
{
    return config_path("autostart.sh");
}

bool create_autostart_script(const char* path)
{
    if (access(path, F_OK) == 0)
        return true;
    make_parent_dirs(path);
    FILE* f = fopen(path, "w");
    if (!f)
        return false;
    fprintf(f, "#!/bin/sh\n"
               "# To The Roots runs this with sh when it starts (not when it\n"
               "# restarts), after the programs on Settings > Startup.\n"
               "# End programs that keep running with &, or the rest of the\n"
               "# script waits for them.\n"
               "#\n"
               "# xset r rate 250 30\n"
               "# xsetroot -solid '#4a6a8a'\n"
               "# xscreensaver -no-splash &\n");
    bool ok = fclose(f) == 0;
    if (ok)
        chmod(path, 0755);
    return ok;
}

void run_startup(bool xdg, bool script, const char* terminal)
{
    if (xdg)
        run_xdg_autostart(terminal);
    StartupList list;
    Str path = StartupList::default_path();
    if (list.load(path.c_str())) {
        int i;
        for (i = 0; i < list.size(); i++)
            if (list.at(i)->enabled)
                spawn(list.at(i)->command.c_str());
    }
    if (!script)
        return;
    Str sh = autostart_script_path();
    if (access(sh.c_str(), R_OK) != 0)
        return;
    Str quoted = shell_quote(sh.c_str());
    char* cmd = new char[quoted.length() + 4];
    sprintf(cmd, "sh %s", quoted.c_str());
    spawn(cmd);
    delete [] cmd;
}

}
