#include "core/settings.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

namespace ttr {

static const char* const k_header[] = {
    "# To The Roots settings.",
    "#",
    "# Edited by Start > Settings, or by hand. One \"name: value\" per line,",
    "# with the same names as the X resources (Ttr*name). Values here win",
    "# over ~/.Xresources; command line options win over this file.",
    "",
    0
};

SettingsFile::SettingsFile() : existed_(false) {}

SettingsFile::~SettingsFile()
{
    int i;
    for (i = 0; i < lines_.size(); i++)
        delete lines_[i];
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

bool SettingsFile::load(const char* path)
{
    FILE* f = fopen(path, "r");
    if (!f)
        return false;
    existed_ = true;
    char buf[2048];
    while (fgets(buf, sizeof(buf), f)) {
        int n = (int)strlen(buf);
        if (n > 0 && buf[n - 1] == '\n')
            buf[--n] = 0;

        Line* l = new Line;
        l->entry = false;
        l->text = buf;

        char work[2048];
        strcpy(work, buf);
        trim(work);
        char* colon = strchr(work, ':');
        if (work[0] && work[0] != '#' && work[0] != '!' && colon) {
            *colon = 0;
            char* name = work;
            char* value = colon + 1;
            trim(name);
            trim(value);
            if (*name) {
                l->entry = true;
                l->name = name;
                l->value = value;
            }
        }
        lines_.push_back(l);
    }
    fclose(f);
    return true;
}

void make_parent_dirs(const char* path)
{
    char buf[1024];
    if (strlen(path) >= sizeof(buf))
        return;
    strcpy(buf, path);
    char* p;
    for (p = buf + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = 0;
        if (mkdir(buf, 0755) != 0 && errno != EEXIST)
            return;
        *p = '/';
    }
}

bool SettingsFile::save(const char* path) const
{
    make_parent_dirs(path);
    char tmp[1100];
    if (strlen(path) > 1000)
        return false;
    sprintf(tmp, "%s.new", path);
    FILE* f = fopen(tmp, "w");
    if (!f)
        return false;

    int i;
    if (!existed_)
        for (i = 0; k_header[i]; i++)
            fprintf(f, "%s\n", k_header[i]);
    for (i = 0; i < lines_.size(); i++) {
        const Line* l = lines_[i];
        if (l->entry)
            fprintf(f, "%s: %s\n", l->name.c_str(), l->value.c_str());
        else
            fprintf(f, "%s\n", l->text.c_str());
    }
    bool ok = fflush(f) == 0;
    ok = fclose(f) == 0 && ok;
    if (!ok || rename(tmp, path) != 0) {
        remove(tmp);
        return false;
    }
    return true;
}

SettingsFile::Line* SettingsFile::find(const char* name) const
{
    int i;
    for (i = 0; i < lines_.size(); i++)
        if (lines_[i]->entry && strcmp(lines_[i]->name.c_str(), name) == 0)
            return lines_[i];
    return 0;
}

const char* SettingsFile::get(const char* name) const
{
    Line* l = find(name);
    return l ? l->value.c_str() : 0;
}

void SettingsFile::set(const char* name, const char* value)
{
    Line* l = find(name);
    if (!l) {
        l = new Line;
        l->entry = true;
        l->name = name;
        lines_.push_back(l);
    }
    l->value = value;
}

const SettingsFile::Line* SettingsFile::entry(int i) const
{
    int k, n = 0;
    for (k = 0; k < lines_.size(); k++)
        if (lines_[k]->entry && n++ == i)
            return lines_[k];
    return 0;
}

int SettingsFile::count() const
{
    int k, n = 0;
    for (k = 0; k < lines_.size(); k++)
        if (lines_[k]->entry)
            n++;
    return n;
}

const char* SettingsFile::name(int i) const
{
    const Line* l = entry(i);
    return l ? l->name.c_str() : "";
}

const char* SettingsFile::value(int i) const
{
    const Line* l = entry(i);
    return l ? l->value.c_str() : "";
}

Str config_path(const char* file)
{
    char buf[1100];
    const char* xdg = getenv("XDG_CONFIG_HOME");
    const char* home = getenv("HOME");
    if (strlen(file) > 60)
        file = "invalid";
    if (xdg && *xdg && strlen(xdg) < 1000)
        sprintf(buf, "%s/ttr/%s", xdg, file);
    else if (home && strlen(home) < 1000)
        sprintf(buf, "%s/.config/ttr/%s", home, file);
    else
        sprintf(buf, ".ttr-%s", file);
    return Str(buf);
}

Str SettingsFile::default_path()
{
    return config_path("settings");
}

}
