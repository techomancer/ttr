#include "core/xdgautostart.h"
#include "core/apps.h"
#include "core/audio.h"     /* in_path() */
#include "core/launch.h"
#include "core/settings.h"  /* make_parent_dirs() */

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace ttr {

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

static Str config_home()
{
    char buf[1100];
    const char* xdg = getenv("XDG_CONFIG_HOME");
    const char* home = getenv("HOME");
    if (xdg && *xdg && strlen(xdg) < 1000)
        sprintf(buf, "%s", xdg);
    else if (home && strlen(home) < 1000)
        sprintf(buf, "%s/.config", home);
    else
        strcpy(buf, ".config");
    return Str(buf);
}

Str xdg_autostart_user_dir()
{
    Str home = config_home();
    char buf[1200];
    sprintf(buf, "%.1100s/autostart", home.c_str());
    return Str(buf);
}

/* Directories in order of precedence. */
static void autostart_dirs(Vector<Str>* out)
{
    out->push_back(xdg_autostart_user_dir());
    const char* dirs = getenv("XDG_CONFIG_DIRS");
    if (!dirs || !*dirs)
        dirs = "/etc/xdg";
    const char* p = dirs;
    while (*p) {
        const char* e = strchr(p, ':');
        int n = e ? (int)(e - p) : (int)strlen(p);
        if (n > 0 && n < 1000) {
            char buf[1100];
            sprintf(buf, "%.*s/autostart", n, p);
            out->push_back(Str(buf));
        }
        if (!e)
            break;
        p = e + 1;
    }
}

static bool same_nocase(const char* a, const char* b, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return false;
    return true;
}

/* Does the ';' list 'list' name one of our desktops? */
static bool names_us(const char* list)
{
    const char* current = getenv("XDG_CURRENT_DESKTOP");
    char ours[512];
    sprintf(ours, "TTR:%.500s", current ? current : "");
    char buf[1024];
    strncpy(buf, list, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    char* tok = strtok(buf, ";");
    while (tok) {
        /* Match tok against each ':'-separated name in 'ours'. */
        const char* p = ours;
        int tl = (int)strlen(tok);
        while (*p) {
            const char* e = strchr(p, ':');
            int n = e ? (int)(e - p) : (int)strlen(p);
            if (n == tl && n > 0 && same_nocase(p, tok, n))
                return true;
            if (!e)
                break;
            p = e + 1;
        }
        tok = strtok(0, ";");
    }
    return false;
}

static bool program_exists(const char* program)
{
    if (strchr(program, '/'))
        return access(program, X_OK) == 0;
    return in_path(program);
}

/* "if-exists FILE" / "unless-exists FILE"; anything else is false. */
static bool condition_holds(const char* cond)
{
    const char* sp = strchr(cond, ' ');
    if (!sp)
        return false;
    int kind = 0;
    if (strncmp(cond, "if-exists ", 10) == 0)
        kind = 1;
    else if (strncmp(cond, "unless-exists ", 14) == 0)
        kind = 2;
    if (!kind)
        return false;
    while (*sp == ' ')
        sp++;
    Str home = config_home();
    char buf[1200];
    if (sp[0] == '/')
        sprintf(buf, "%.1100s", sp);
    else
        sprintf(buf, "%.500s/%.600s", home.c_str(), sp);
    bool exists = access(buf, F_OK) == 0;
    return kind == 1 ? exists : !exists;
}

static XdgAutostartEntry* parse(const char* path, const char* id)
{
    FILE* f = fopen(path, "r");
    if (!f)
        return 0;
    XdgAutostartEntry* e = new XdgAutostartEntry;
    e->id = id;
    e->path = path;
    e->terminal = false;
    e->status = XdgAutostartEntry::START;

    Str exec, type, only, not_in, try_exec, condition;
    bool hidden = false, in_entry = false, under_systemd = false;
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (!line[0] || line[0] == '#')
            continue;
        if (line[0] == '[') {
            in_entry = strcmp(line, "[Desktop Entry]") == 0;
            continue;
        }
        char* eq = strchr(line, '=');
        if (!in_entry || !eq)
            continue;
        *eq = 0;
        char* key = line;
        char* val = eq + 1;
        trim(key);
        trim(val);
        desktop_unescape(val);
        if (strcmp(key, "Name") == 0) e->name = val;
        else if (strcmp(key, "Exec") == 0) exec = val;
        else if (strcmp(key, "Type") == 0) type = val;
        else if (strcmp(key, "TryExec") == 0) try_exec = val;
        else if (strcmp(key, "OnlyShowIn") == 0) only = val;
        else if (strcmp(key, "NotShowIn") == 0) not_in = val;
        else if (strcmp(key, "AutostartCondition") == 0) condition = val;
        else if (strcmp(key, "Terminal") == 0)
            e->terminal = strcmp(val, "true") == 0;
        else if (strcmp(key, "Hidden") == 0)
            hidden = hidden || strcmp(val, "true") == 0;
        else if (strcmp(key, "X-GNOME-Autostart-enabled") == 0)
            hidden = hidden || strcmp(val, "false") == 0;
        else if (strcmp(key, "X-GNOME-HiddenUnderSystemd") == 0 ||
                 strcmp(key, "X-KDE-HiddenUnderSystemd") == 0)
            under_systemd = under_systemd || strcmp(val, "true") == 0;
    }
    fclose(f);

    e->command = strip_exec_codes(exec.c_str());
    if (e->name.empty())
        e->name = id;

    char detail[1200];
    detail[0] = 0;
    if (hidden) {
        e->status = XdgAutostartEntry::OFF;
    } else if (e->command.empty() ||
               (!type.empty() && strcmp(type.c_str(), "Application") != 0)) {
        e->status = XdgAutostartEntry::BROKEN;
        strcpy(detail, "Not a program");
    } else if (under_systemd && access("/run/systemd/system", F_OK) == 0) {
        /* systemd user units start these (PipeWire, for one). */
        e->status = XdgAutostartEntry::OTHER_DESKTOP;
        strcpy(detail, "Started by systemd");
    } else if (!only.empty() && !names_us(only.c_str())) {
        e->status = XdgAutostartEntry::OTHER_DESKTOP;
        sprintf(detail, "Only for %.500s", only.c_str());
    } else if (!not_in.empty() && names_us(not_in.c_str())) {
        e->status = XdgAutostartEntry::OTHER_DESKTOP;
        sprintf(detail, "Not for %.500s", not_in.c_str());
    } else if (!try_exec.empty() && !program_exists(try_exec.c_str())) {
        e->status = XdgAutostartEntry::MISSING;
        sprintf(detail, "Not installed: %.500s", try_exec.c_str());
    } else if (!condition.empty() && !condition_holds(condition.c_str())) {
        e->status = XdgAutostartEntry::CONDITION;
        sprintf(detail, "Condition: %.500s", condition.c_str());
    }
    /* Desktop lists read "GNOME, Unity", not "GNOME;Unity;". */
    char shown[1400];
    int i, o = 0;
    for (i = 0; detail[i] && o < (int)sizeof(shown) - 3; i++) {
        if (detail[i] != ';') {
            shown[o++] = detail[i];
        } else if (detail[i + 1]) {
            shown[o++] = ',';
            shown[o++] = ' ';
        }
    }
    shown[o] = 0;
    e->detail = shown;
    return e;
}

static int compare_nocase(const char* a, const char* b)
{
    while (*a && *b) {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb)
            return ca - cb;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

void xdg_autostart_scan(Vector<XdgAutostartEntry*>* out)
{
    Vector<Str> dirs;
    autostart_dirs(&dirs);
    Vector<Str> seen;
    int d;
    for (d = 0; d < dirs.size(); d++) {
        DIR* dir = opendir(dirs[d].c_str());
        if (!dir)
            continue;
        struct dirent* de;
        while ((de = readdir(dir)) != 0) {
            const char* nm = de->d_name;
            int len = (int)strlen(nm);
            if (len < 9 || strcmp(nm + len - 8, ".desktop") != 0)
                continue;
            int k;
            for (k = 0; k < seen.size(); k++)
                if (strcmp(seen[k].c_str(), nm) == 0)
                    break;
            if (k < seen.size())
                continue;       /* hidden by an earlier directory */
            seen.push_back(Str(nm));
            char path[1400];
            if (dirs[d].length() + len > 1300)
                continue;
            sprintf(path, "%s/%s", dirs[d].c_str(), nm);
            XdgAutostartEntry* e = parse(path, nm);
            if (!e)
                continue;
            /* Insert sorted by name. */
            int at = out->size(), i;
            for (i = 0; i < out->size(); i++)
                if (compare_nocase(e->name.c_str(),
                                   (*out)[i]->name.c_str()) < 0) {
                    at = i;
                    break;
                }
            out->push_back(e);
            for (i = out->size() - 1; i > at; i--)
                (*out)[i] = (*out)[i - 1];
            (*out)[at] = e;
        }
        closedir(dir);
    }
}

void xdg_autostart_free(Vector<XdgAutostartEntry*>* list)
{
    int i;
    for (i = 0; i < list->size(); i++)
        delete (*list)[i];
    list->clear();
}

bool xdg_autostart_set_enabled(const XdgAutostartEntry& e, bool on)
{
    Str dir = xdg_autostart_user_dir();
    char target[1400], tmp[1500];
    if (dir.length() + e.id.length() > 1300)
        return false;
    sprintf(target, "%s/%s", dir.c_str(), e.id.c_str());
    sprintf(tmp, "%s.new", target);

    /* Copy the file that counts (the user's own, or the system one),
     * dropping the keys that turn it off, then add Hidden=true if off. */
    FILE* in = fopen(e.path.c_str(), "r");
    if (!in)
        return false;
    make_parent_dirs(target);
    FILE* out = fopen(tmp, "w");
    if (!out) {
        fclose(in);
        return false;
    }
    char line[2048], key[2048];
    bool in_entry = false;
    while (fgets(line, sizeof(line), in)) {
        strcpy(key, line);
        trim(key);
        if (key[0] == '[') {
            in_entry = strcmp(key, "[Desktop Entry]") == 0;
            fputs(line, out);
            if (in_entry && !on)
                fputs("Hidden=true\n", out);
            continue;
        }
        char* eq = strchr(key, '=');
        if (in_entry && eq) {
            *eq = 0;
            trim(key);
            if (strcmp(key, "Hidden") == 0 ||
                strcmp(key, "X-GNOME-Autostart-enabled") == 0)
                continue;
        }
        fputs(line, out);
    }
    fclose(in);
    bool ok = fflush(out) == 0;
    ok = fclose(out) == 0 && ok;
    if (!ok || rename(tmp, target) != 0) {
        remove(tmp);
        return false;
    }
    return true;
}

void run_xdg_autostart(const char* terminal)
{
    Vector<XdgAutostartEntry*> list;
    xdg_autostart_scan(&list);
    int i;
    for (i = 0; i < list.size(); i++) {
        const XdgAutostartEntry* e = list[i];
        if (e->status != XdgAutostartEntry::START)
            continue;
        if (e->terminal && terminal && *terminal) {
            char* cmd = new char[strlen(terminal) + e->command.length() + 2];
            sprintf(cmd, "%s %s", terminal, e->command.c_str());
            spawn(cmd);
            delete [] cmd;
        } else {
            spawn(e->command.c_str());
        }
    }
    xdg_autostart_free(&list);
}

}
