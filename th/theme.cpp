#include "theme.h"
#include "internal.h"

#include <stdio.h>
#include <string.h>

namespace th {

namespace {

const char* const k_role_names[ROLE_COUNT] = {
    "BasicBackground", "TextForeground", "ButtonBackground",
    "TextFieldBackground", "ScrolledListBackground", "ScrollBarTroughColor",
    "ScrollBarControlBackground", "SelectFillColor", "WMBackground",
    "WMForeground", "WMActiveBackground", "WMActiveForeground"
};

/* The gamma the colours are meant for, as the prefix of the scheme
 * files' keys. On IRIX the display hardware applies the system gamma
 * (1.7 by default), and SGI's schemes have values for it; elsewhere the
 * screen shows colours as they are. */
const char* gamma_prefix()
{
#if defined(__sgi)
    static char prefix[16] = "";
    if (!prefix[0]) {
        double g = 1.7;
        FILE* f = fopen("/etc/config/system.glGammaVal", "r");
        if (f) {
            if (fscanf(f, "%lf", &g) != 1)
                g = 1.7;
            fclose(f);
        }
        strcpy(prefix, g < 1.35 ? "gamma1_0." : g < 2.05 ? "gamma1_7." : "gamma2_4.");
    }
    return prefix;
#else
    return "gamma1_0.";
#endif
}

} /* namespace */

const char* role_name(Role role)
{
    return role >= 0 && role < ROLE_COUNT ? k_role_names[role] : "";
}

Theme::Theme() : dark(false)
{
    strcpy(name, "Classic");
    path[0] = 0;
    for (int i = 0; i < ROLE_COUNT; i++) {
        color[i] = 0;
        set[i] = false;
    }
}

Theme Theme::classic()
{
    Theme t;
    t.put(ROLE_BACKGROUND, 0xb8b8b8);
    t.put(ROLE_SELECT_FILL, 0x3c5a8c);
    t.put(ROLE_INACTIVE, 0xb8b8b8);
    t.put(ROLE_ACTIVE, 0x6080a8);
    return t;
}

void Theme::put(Role role, Color c)
{
    color[role] = c;
    set[role] = true;
    if (role == ROLE_BACKGROUND) {
        set[ROLE_BUTTON] = set[ROLE_TEXT_FIELD] = set[ROLE_LIST] = false;
        set[ROLE_TROUGH] = set[ROLE_SCROLL_BAR] = false;
    }
}

Color Theme::get(Role role) const
{
    if (set[role])
        return color[role];
    switch (role) {
    case ROLE_BACKGROUND:
        return 0xb8b8b8;
    case ROLE_FOREGROUND:
        return motif_colors(get(ROLE_BACKGROUND)).fg;
    case ROLE_INACTIVE:
        return get(ROLE_BACKGROUND);
    case ROLE_ACTIVE:
        return motif_colors(get(ROLE_INACTIVE)).select;
    case ROLE_INACTIVE_TEXT:
        return motif_colors(get(ROLE_INACTIVE)).fg;
    case ROLE_ACTIVE_TEXT:
        return motif_colors(get(ROLE_ACTIVE)).fg;
    case ROLE_SELECT_FILL:
        return motif_colors(get(ROLE_BACKGROUND)).select;
    default:
        return get(ROLE_BACKGROUND);    /* the specific backgrounds */
    }
}

bool Theme::load(const char* file)
{
    FILE* f = fopen(file, "r");
    if (!f)
        return false;
    *this = Theme();
    const char* b = strrchr(file, '/');
    copy_str(name, b ? b + 1 : file, sizeof(name));
    int nl = (int)strlen(name);
    if (nl > 3 && strcmp(name + nl - 3, ".ad") == 0)
        name[nl - 3] = 0;
    copy_str(path, file, sizeof(path));
    const char* prefix = gamma_prefix();
    int plen = (int)strlen(prefix);
    int found = 0;
    /* A value for our gamma beats a bare one, whatever the order. */
    bool exact[ROLE_COUNT];
    for (int i = 0; i < ROLE_COUNT; i++)
        exact[i] = false;
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
        if (strcmp(key, "isDark") == 0) {
            dark = strcmp(value, "true") == 0;
            continue;
        }
        if (strcmp(key, "name") == 0) {
            if (*value)
                copy_str(name, value, sizeof(name));
            continue;
        }
        bool prefixed = false;
        if (strncmp(key, "gamma", 5) == 0) {
            if (strncmp(key, prefix, (size_t)plen) != 0)
                continue;       /* another gamma's value */
            key += plen;
            prefixed = true;
        }
        for (int i = 0; i < ROLE_COUNT; i++) {
            if (strcmp(key, k_role_names[i]) != 0)
                continue;
            Color c;
            if (exact[i] && !prefixed)
                break;
            if (parse_hex(value, &c)) {
                color[i] = c;   /* not put(): the order must not matter */
                set[i] = true;
                exact[i] = prefixed;
                found++;
            }
            break;
        }
    }
    fclose(f);
    return found > 0;
}

void list_themes(List* out)
{
    Entry classic;
    strcpy(classic.name, "Classic");
    classic.path[0] = 0;
    strcpy(classic.origin, "built-in");
    out->add(classic);
    scan("themes", out);
}

bool find_theme(const char* name, Theme* out)
{
    *out = Theme::classic();
    if (!name || !*name || compare_names(name, "Classic") == 0)
        return true;
    List list;
    list_themes(&list);
    for (int i = 0; i < list.count(); i++) {
        if (!list[i].path[0] || compare_names(list[i].name, name) != 0)
            continue;
        Theme t;
        if (t.load(list[i].path)) {
            copy_str(t.name, list[i].name, sizeof(t.name));
            *out = t;
            return true;
        }
    }
    return false;
}

} /* namespace th */
