#include "scheme.h"
#include "internal.h"

#include <stdio.h>
#include <string.h>

namespace th {

namespace {

const char* const k_keys[SC_ANSI0] = {
    "foreground", "background", "cursor", "selectionForeground", "selectionBackground",
    "gutterForeground", "gutterBackground", "findBackground", "control", "comment",
    "keyword", "string", "number", "preprocessor", "currentLine", "ruler", "whitespace"
};

/* xterm's 16 colours. */
const Color k_xterm[16] = {
    0x000000, 0xcd0000, 0x00cd00, 0xcdcd00, 0x0000ee, 0xcd00cd, 0x00cdcd, 0xe5e5e5,
    0x7f7f7f, 0xff0000, 0x00ff00, 0xffff00, 0x5c5cff, 0xff00ff, 0x00ffff, 0xffffff
};

} /* namespace */

const char* scheme_key(SchemeColor c)
{
    static char ansi[16][10];
    if (c >= 0 && c < SC_ANSI0)
        return k_keys[c];
    if (c >= SC_ANSI0 && c < SC_COUNT) {
        int i = c - SC_ANSI0;
        sprintf(ansi[i], "color%d", i);
        return ansi[i];
    }
    return "";
}

Scheme::Scheme() : dark(false)
{
    strcpy(name, "Light");
    path[0] = 0;
    for (int i = 0; i < SC_COUNT; i++) {
        color[i] = 0;
        set[i] = false;
    }
}

Scheme Scheme::light()
{
    Scheme s;
    s.color[SC_FOREGROUND] = 0x000000;
    s.color[SC_BACKGROUND] = 0xffffff;
    s.color[SC_SELECTION_FG] = 0xffffff;
    s.color[SC_SELECTION_BG] = 0x000080;
    s.color[SC_GUTTER_FG] = 0x808080;
    s.color[SC_GUTTER_BG] = 0xf0f0f0;
    for (int i = SC_FOREGROUND; i <= SC_GUTTER_BG; i++)
        s.set[i] = i != SC_CURSOR;
    return s;
}

Scheme Scheme::dark_default()
{
    Scheme s;
    strcpy(s.name, "Dark");
    s.dark = true;
    s.color[SC_FOREGROUND] = 0xd4d4d4;
    s.color[SC_BACKGROUND] = 0x1e1e1e;
    s.color[SC_CURSOR] = 0xaeafad;
    s.color[SC_SELECTION_FG] = 0xffffff;
    s.color[SC_SELECTION_BG] = 0x264f78;
    s.color[SC_GUTTER_FG] = 0x858585;
    s.color[SC_GUTTER_BG] = 0x252526;
    for (int i = SC_FOREGROUND; i <= SC_GUTTER_BG; i++)
        s.set[i] = true;
    return s;
}

Color Scheme::get(SchemeColor c) const
{
    if (c < 0 || c >= SC_COUNT)
        return 0;
    if (set[c])
        return color[c];
    switch (c) {
    case SC_BACKGROUND:
        return dark ? 0x1e1e1e : 0xffffff;
    case SC_FOREGROUND:
        return brightness(get(SC_BACKGROUND)) >= 128 ? 0x000000 : 0xd4d4d4;
    default:
        break;
    }
    Color bg = get(SC_BACKGROUND), fg = get(SC_FOREGROUND);
    switch (c) {
    case SC_CURSOR:
        return fg;
    case SC_SELECTION_BG:
        return dark ? 0x264f78 : 0x000080;
    case SC_SELECTION_FG:
        return brightness(get(SC_SELECTION_BG)) >= 128 ? 0x000000 : 0xffffff;
    case SC_GUTTER_FG:
        return mix(bg, fg, 45);
    case SC_GUTTER_BG:
        return mix(bg, fg, 6);
    case SC_FIND_BG:
        return dark ? 0x613214 : 0xffff80;
    case SC_CONTROL:
        return dark ? 0xd16969 : 0x800000;
    case SC_COMMENT:
        return dark ? 0x6a9955 : 0x008000;
    case SC_KEYWORD:
        return dark ? 0x569cd6 : 0x0000ff;
    case SC_STRING:
        return dark ? 0xce9178 : 0xa31515;
    case SC_NUMBER:
        return dark ? 0xb5cea8 : 0x098658;
    case SC_PREPROCESSOR:
        return dark ? 0xc586c0 : 0x0000ff;
    case SC_CURRENT_LINE:
        return mix(bg, fg, dark ? 7 : 5);
    case SC_RULER:
        return mix(bg, fg, 20);
    case SC_WHITESPACE:
        return mix(bg, fg, 35);
    default:
        return k_xterm[c - SC_ANSI0];
    }
}

bool Scheme::load(const char* file)
{
    FILE* f = fopen(file, "r");
    if (!f)
        return false;
    *this = Scheme();
    const char* b = strrchr(file, '/');
    copy_str(name, b ? b + 1 : file, sizeof(name));
    int nl = (int)strlen(name);
    if (nl > 7 && strcmp(name + nl - 7, ".scheme") == 0)
        name[nl - 7] = 0;
    copy_str(path, file, sizeof(path));
    bool dark_given = false;
    int found = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (!line[0] || line[0] == '!' || line[0] == '#')
            continue;
        char* colon = strchr(line, ':');
        if (!colon)
            continue;
        *colon = 0;
        char* key = line;
        char* value = colon + 1;
        trim(key);
        trim(value);
        if (strcmp(key, "name") == 0) {
            if (*value)
                copy_str(name, value, sizeof(name));
            continue;
        }
        if (strcmp(key, "isDark") == 0) {
            dark = strcmp(value, "true") == 0;
            dark_given = true;
            continue;
        }
        for (int i = 0; i < SC_COUNT; i++) {
            Color c;
            if (strcmp(key, scheme_key((SchemeColor)i)) == 0 && parse_hex(value, &c)) {
                color[i] = c;
                set[i] = true;
                found++;
                break;
            }
        }
    }
    fclose(f);
    if (!dark_given && set[SC_BACKGROUND])
        dark = brightness(color[SC_BACKGROUND]) < 128;
    return found > 0;
}

void list_schemes(List* out)
{
    Entry e;
    e.path[0] = 0;
    strcpy(e.origin, "built-in");
    strcpy(e.name, "Light");
    out->add(e);
    strcpy(e.name, "Dark");
    out->add(e);
    scan("schemes", out);
}

bool find_scheme(const char* name, Scheme* out)
{
    *out = Scheme::light();
    if (!name || !*name || compare_names(name, "Light") == 0)
        return true;
    if (compare_names(name, "Dark") == 0) {
        *out = Scheme::dark_default();
        return true;
    }
    List list;
    list_schemes(&list);
    for (int i = 0; i < list.count(); i++) {
        if (!list[i].path[0] || compare_names(list[i].name, name) != 0)
            continue;
        Scheme s;
        if (s.load(list[i].path)) {
            copy_str(s.name, list[i].name, sizeof(s.name));
            *out = s;
            return true;
        }
    }
    return false;
}

} /* namespace th */
