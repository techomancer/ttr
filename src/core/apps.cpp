#include "core/apps.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

namespace ttr {

/* ------------------------------------------------------------------ */
/* Tree                                                                */

AppNode::AppNode(Kind k) : kind(k), folder(0) {}

AppNode::~AppNode() { delete folder; }

AppFolder::AppFolder(const char* n) : name(n) {}

AppFolder::~AppFolder()
{
    int i;
    for (i = 0; i < items.size(); i++)
        delete items[i];
}

AppFolder* AppFolder::subfolder(const char* n)
{
    int i;
    for (i = 0; i < items.size(); i++)
        if (items[i]->kind == AppNode::FOLDER &&
            strcmp(items[i]->name.c_str(), n) == 0)
            return items[i]->folder;
    AppNode* node = new AppNode(AppNode::FOLDER);
    node->name = n;
    node->folder = new AppFolder(n);
    items.push_back(node);
    return node->folder;
}

void AppFolder::add_app(const char* n, const char* exec, const char* icon)
{
    AppNode* node = new AppNode(AppNode::APP);
    node->name = n;
    node->exec = exec;
    node->icon = icon;
    items.push_back(node);
}

void AppFolder::add_separator()
{
    items.push_back(new AppNode(AppNode::SEPARATOR));
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

static bool node_before(const AppNode* a, const AppNode* b)
{
    if (a->kind != b->kind)
        return a->kind == AppNode::FOLDER;
    return compare_nocase(a->name.c_str(), b->name.c_str()) < 0;
}

void AppFolder::sort_recursive()
{
    int i, j;
    for (i = 0; i < items.size(); ) {
        if (items[i]->kind == AppNode::SEPARATOR) {
            delete items[i];
            items.remove_at(i);
        } else {
            i++;
        }
    }
    /* Insertion sort: menus are small and this keeps it simple. */
    for (i = 1; i < items.size(); i++) {
        AppNode* n = items[i];
        for (j = i; j > 0 && node_before(n, items[j - 1]); j--)
            items[j] = items[j - 1];
        items[j] = n;
    }
    for (i = 0; i < items.size(); i++)
        if (items[i]->kind == AppNode::FOLDER)
            items[i]->folder->sort_recursive();
}

void AppFolder::prune()
{
    int i;
    for (i = 0; i < items.size(); ) {
        AppNode* n = items[i];
        if (n->kind == AppNode::FOLDER) {
            n->folder->prune();
            bool empty = true;
            int j;
            for (j = 0; j < n->folder->items.size(); j++)
                if (n->folder->items[j]->kind != AppNode::SEPARATOR)
                    empty = false;
            if (empty) {
                delete n;
                items.remove_at(i);
                continue;
            }
        }
        i++;
    }
}

Str strip_exec_codes(const char* exec)
{
    int len = (int)strlen(exec);
    char* out = new char[len + 1];
    int i, o = 0;
    for (i = 0; i < len; i++) {
        if (exec[i] == '%' && i + 1 < len) {
            if (exec[i + 1] == '%')
                out[o++] = '%';
            i++;
            continue;
        }
        out[o++] = exec[i];
    }
    /* Drop trailing blanks left by removed codes. */
    while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '\t'))
        o--;
    out[o] = 0;
    Str s(out);
    delete [] out;
    return s;
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

/* ------------------------------------------------------------------ */
/* XDG desktop entries                                                 */

/* Desktop entry string escapes: \s \n \t \r \\, in place. */
static void unescape(char* s)
{
    char* o = s;
    while (*s) {
        if (*s == '\\' && s[1]) {
            s++;
            switch (*s) {
            case 's': *o++ = ' '; break;
            case 'n': *o++ = '\n'; break;
            case 't': *o++ = '\t'; break;
            case 'r': *o++ = '\r'; break;
            default: *o++ = *s; break;
            }
            s++;
        } else {
            *o++ = *s++;
        }
    }
    *o = 0;
}

DesktopAppSource::DesktopAppSource(const char* terminal)
    : terminal_(terminal)
{
}

DesktopAppSource::~DesktopAppSource() {}

void DesktopAppSource::add_dir(const char* path)
{
    dirs_.push_back(Str(path));
}

static void add_xdg_dir_list(DesktopAppSource* src, const char* list)
{
    char buf[1024];
    const char* p = list;
    while (*p) {
        const char* e = strchr(p, ':');
        int n = e ? (int)(e - p) : (int)strlen(p);
        if (n > 0 && n < (int)sizeof(buf) - 16) {
            memcpy(buf, p, n);
            strcpy(buf + n, "/applications");
            src->add_dir(buf);
        }
        if (!e)
            break;
        p = e + 1;
    }
}

void DesktopAppSource::add_xdg_dirs()
{
    const char* home = getenv("XDG_DATA_HOME");
    if (home && *home) {
        add_xdg_dir_list(this, home);
    } else if (getenv("HOME")) {
        char buf[1024];
        sprintf(buf, "%.900s/.local/share", getenv("HOME"));
        add_xdg_dir_list(this, buf);
    }
    const char* dirs = getenv("XDG_DATA_DIRS");
    add_xdg_dir_list(this, dirs && *dirs ? dirs : "/usr/local/share:/usr/share");
}

bool DesktopAppSource::seen(const char* id) const
{
    int i;
    for (i = 0; i < seen_.size(); i++)
        if (strcmp(seen_[i].c_str(), id) == 0)
            return true;
    return false;
}

void DesktopAppSource::load(AppFolder* root)
{
    int i;
    for (i = 0; i < dirs_.size(); i++)
        scan(dirs_[i].c_str(), "", root);
}

/* Desktop file ids are paths relative to the applications directory with
 * '/' replaced by '-'. */
void DesktopAppSource::scan(const char* dir, const char* id_prefix,
                            AppFolder* root)
{
    DIR* d = opendir(dir);
    if (!d)
        return;
    struct dirent* e;
    while ((e = readdir(d)) != 0) {
        if (e->d_name[0] == '.')
            continue;
        char path[1024], id[512];
        if (strlen(dir) + strlen(e->d_name) + 2 > sizeof(path) ||
            strlen(id_prefix) + strlen(e->d_name) + 2 > sizeof(id))
            continue;
        strcpy(path, dir);
        strcat(path, "/");
        strcat(path, e->d_name);
        strcpy(id, id_prefix);
        strcat(id, e->d_name);

        struct stat st;
        if (stat(path, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            strcat(id, "-");
            scan(path, id, root);
            continue;
        }
        int n = (int)strlen(e->d_name);
        if (n < 9 || strcmp(e->d_name + n - 8, ".desktop") != 0)
            continue;
        if (seen(id))
            continue;
        seen_.push_back(Str(id));
        parse(path, root);
    }
    closedir(d);
}

/* Main XDG categories mapped to Windows-like folder names. */
static const char* folder_for(const char* categories)
{
    static const char* const map[][2] = {
        { "AudioVideo", "Multimedia" }, { "Audio", "Multimedia" },
        { "Video", "Multimedia" }, { "Development", "Development" },
        { "Education", "Education" }, { "Game", "Games" },
        { "Graphics", "Graphics" }, { "Network", "Internet" },
        { "Office", "Office" }, { "Science", "Science" },
        { "Settings", "Settings" }, { "System", "System Tools" },
        { "Utility", "Accessories" }
    };
    const int n = (int)(sizeof(map) / sizeof(map[0]));
    char buf[512];
    strncpy(buf, categories, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    char* tok = strtok(buf, ";");
    while (tok) {
        int i;
        for (i = 0; i < n; i++)
            if (strcmp(tok, map[i][0]) == 0)
                return map[i][1];
        tok = strtok(0, ";");
    }
    return "Other";
}

void DesktopAppSource::parse(const char* path, AppFolder* root)
{
    FILE* f = fopen(path, "r");
    if (!f)
        return;

    char line[2048];
    bool in_entry = false;
    Str name, exec, icon, categories, type;
    bool hidden = false, terminal = false, only_show_in = false;

    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (!line[0] || line[0] == '#')
            continue;
        if (line[0] == '[') {
            in_entry = strcmp(line, "[Desktop Entry]") == 0;
            continue;
        }
        if (!in_entry)
            continue;
        char* eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        char* key = line;
        char* val = eq + 1;
        trim(key);
        trim(val);
        unescape(val);
        if (strcmp(key, "Name") == 0) name = val;
        else if (strcmp(key, "Exec") == 0) exec = val;
        else if (strcmp(key, "Icon") == 0) icon = val;
        else if (strcmp(key, "Categories") == 0) categories = val;
        else if (strcmp(key, "Type") == 0) type = val;
        else if (strcmp(key, "NoDisplay") == 0 || strcmp(key, "Hidden") == 0)
            hidden = hidden || strcmp(val, "true") == 0;
        else if (strcmp(key, "Terminal") == 0)
            terminal = strcmp(val, "true") == 0;
        else if (strcmp(key, "OnlyShowIn") == 0)
            only_show_in = true;    /* meant for some other desktop */
    }
    fclose(f);

    if (hidden || only_show_in || name.empty() || exec.empty() ||
        strcmp(type.c_str(), "Application") != 0)
        return;

    Str cmd = strip_exec_codes(exec.c_str());
    if (terminal && !terminal_.empty()) {
        int len = terminal_.length() + cmd.length() + 2;
        char* buf = new char[len];
        sprintf(buf, "%s %s", terminal_.c_str(), cmd.c_str());
        cmd = buf;
        delete [] buf;
    }
    root->subfolder(folder_for(categories.c_str()))
        ->add_app(name.c_str(), cmd.c_str(), icon.c_str());
}

/* ------------------------------------------------------------------ */
/* 4Dwm toolchest                                                      */

ToolchestAppSource::Menu::~Menu()
{
    int i;
    for (i = 0; i < lines.size(); i++)
        delete lines[i];
}

ToolchestAppSource::ToolchestAppSource() {}

ToolchestAppSource::~ToolchestAppSource()
{
    int i;
    for (i = 0; i < menus_.size(); i++)
        delete menus_[i];
}

void ToolchestAppSource::add_file(const char* path)
{
    files_.push_back(Str(path));
}

/* Next token from *p: a "quoted string" (with \" and \\ escapes) or a run
 * of non-blank characters. Returns false at end of line. */
static bool next_token(const char** p, char* out, int size)
{
    const char* s = *p;
    while (*s && isspace((unsigned char)*s))
        s++;
    if (!*s)
        return false;
    int o = 0;
    if (*s == '"') {
        s++;
        while (*s && *s != '"') {
            if (*s == '\\' && s[1])
                s++;
            if (o < size - 1)
                out[o++] = *s;
            s++;
        }
        if (*s == '"')
            s++;
    } else {
        while (*s && !isspace((unsigned char)*s)) {
            if (o < size - 1)
                out[o++] = *s;
            s++;
        }
    }
    out[o] = 0;
    *p = s;
    return true;
}

void ToolchestAppSource::parse_file(const char* path)
{
    FILE* f = fopen(path, "r");
    if (!f)
        return;

    char line[2048], tok[1024];
    Menu* cur = 0;          /* menu being read */
    bool in_body = false;

    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (!line[0] || line[0] == '#' || line[0] == '!')
            continue;

        const char* p = line;
        if (!cur) {
            if (!next_token(&p, tok, sizeof(tok)) || strcmp(tok, "Menu") != 0)
                continue;
            if (!next_token(&p, tok, sizeof(tok)))
                continue;
            cur = new Menu;
            cur->name = tok;
            in_body = false;
            if (next_token(&p, tok, sizeof(tok)) && strcmp(tok, "{") == 0)
                in_body = true;
            continue;
        }
        if (!in_body) {
            if (strcmp(line, "{") == 0)
                in_body = true;
            continue;
        }
        if (strcmp(line, "}") == 0) {
            if (find_menu(cur->name.c_str()))
                delete cur;     /* first definition wins */
            else
                menus_.push_back(cur);
            cur = 0;
            continue;
        }

        /* label [mnemonic] [accelerator] function [arguments] */
        Line* l = new Line;
        bool first = true, got_func = false;
        while (next_token(&p, tok, sizeof(tok))) {
            if (first) {
                if (strcmp(tok, "no-label") != 0)
                    l->label = tok;
                first = false;
            } else if (!got_func) {
                if (strncmp(tok, "f.", 2) == 0) {
                    l->func = tok;
                    got_func = true;
                }
            } else {
                /* The rest of the line is the argument. */
                while (*p && isspace((unsigned char)*p))
                    p++;
                if (*p) {
                    char rest[1024];
                    sprintf(rest, "%.500s %.500s", tok, p);
                    l->arg = rest;
                } else {
                    l->arg = tok;
                }
                break;
            }
        }
        if (got_func)
            cur->lines.push_back(l);
        else
            delete l;
    }
    delete cur;     /* unterminated menu */
    fclose(f);
}

ToolchestAppSource::Menu* ToolchestAppSource::find_menu(const char* name)
{
    int i;
    for (i = 0; i < menus_.size(); i++)
        if (strcmp(menus_[i]->name.c_str(), name) == 0)
            return menus_[i];
    return 0;
}

void ToolchestAppSource::build(AppFolder* folder, Menu* m, int depth)
{
    if (depth > 16)     /* menus that include themselves */
        return;
    int i;
    for (i = 0; i < m->lines.size(); i++) {
        Line* l = m->lines[i];
        const char* fn = l->func.c_str();
        if (strcmp(fn, "f.separator") == 0) {
            folder->add_separator();
        } else if (strcmp(fn, "f.menu") == 0) {
            Menu* sub = find_menu(l->arg.c_str());
            if (sub && !l->label.empty()) {
                /* Not subfolder(): toolchests may repeat labels. */
                AppNode* node = new AppNode(AppNode::FOLDER);
                node->name = l->label;
                node->folder = new AppFolder(l->label.c_str());
                folder->items.push_back(node);
                build(node->folder, sub, depth + 1);
            }
        } else if ((strncmp(fn, "f.exec", 6) == 0 ||
                    strncmp(fn, "f.checkexec", 11) == 0) &&
                   !l->label.empty() && !l->arg.empty()) {
            folder->add_app(l->label.c_str(), l->arg.c_str(), "");
        }
        /* f.title and window manager functions are not applications. */
    }
}

void ToolchestAppSource::load(AppFolder* root)
{
    int i;
    for (i = 0; i < files_.size(); i++)
        parse_file(files_[i].c_str());
    Menu* top = find_menu("ToolChest");
    if (top)
        build(root, top, 0);
}

}
