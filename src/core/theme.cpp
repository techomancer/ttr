#include "core/theme.h"
#include "core/settings.h"  /* config_path() */

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(TTR_DATADIR)
#define TTR_DATADIR "/usr/share/ttr"
#endif

namespace ttr {

static const char* const k_role_names[TR_COUNT] = {
    "BasicBackground", "TextForeground", "ButtonBackground",
    "TextFieldBackground", "ScrolledListBackground", "ScrollBarTroughColor",
    "ScrollBarControlBackground", "SelectFillColor", "WMBackground",
    "WMForeground", "WMActiveBackground", "WMActiveForeground"
};

const char* theme_role_name(ThemeRole role)
{
    return role >= 0 && role < TR_COUNT ? k_role_names[role] : "";
}

static int hex_digit(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    c = tolower(c);
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

bool parse_hex_color(const char* s, Rgb* out)
{
    while (*s == ' ' || *s == '\t')
        s++;
    if (*s != '#')
        return false;
    s++;
    int n = 0, v[6];
    while (n < 6 && s[n] && hex_digit((unsigned char)s[n]) >= 0) {
        v[n] = hex_digit((unsigned char)s[n]);
        n++;
    }
    if (s[n] && !isspace((unsigned char)s[n]))
        return false;
    if (n == 6) {
        *out = Rgb((unsigned short)((v[0] * 16 + v[1]) * 257),
                   (unsigned short)((v[2] * 16 + v[3]) * 257),
                   (unsigned short)((v[4] * 16 + v[5]) * 257));
        return true;
    }
    if (n == 3) {
        *out = Rgb((unsigned short)(v[0] * 0x1111),
                   (unsigned short)(v[1] * 0x1111),
                   (unsigned short)(v[2] * 0x1111));
        return true;
    }
    return false;
}

void format_hex_color(const Rgb& c, char* buf)
{
    sprintf(buf, "#%02x%02x%02x", c.r >> 8, c.g >> 8, c.b >> 8);
}

Theme::Theme() : name("Classic"), dark(false)
{
    int i;
    for (i = 0; i < TR_COUNT; i++)
        set[i] = false;
}

Theme Theme::classic()
{
    Theme t;
    t.put(TR_BACKGROUND, Rgb::hex(0xb8b8b8));
    t.put(TR_SELECT_FILL, Rgb::hex(0x3c5a8c));
    t.put(TR_INACTIVE, Rgb::hex(0xb8b8b8));
    t.put(TR_ACTIVE, Rgb::hex(0x6080a8));
    return t;
}

void Theme::put(ThemeRole role, const Rgb& c)
{
    color[role] = c;
    set[role] = true;
    if (role == TR_BACKGROUND) {
        set[TR_BUTTON] = set[TR_TEXT_FIELD] = set[TR_LIST] = false;
        set[TR_TROUGH] = set[TR_SCROLL_BAR] = false;
    }
}

Rgb Theme::get(ThemeRole role) const
{
    if (set[role])
        return color[role];
    switch (role) {
    case TR_BACKGROUND:
        return Rgb::hex(0xb8b8b8);
    case TR_FOREGROUND:
        return motif_colors(get(TR_BACKGROUND)).fg;
    case TR_INACTIVE:
        return get(TR_BACKGROUND);
    case TR_ACTIVE:
        return motif_colors(get(TR_INACTIVE)).select;
    case TR_INACTIVE_TEXT:
        return motif_colors(get(TR_INACTIVE)).fg;
    case TR_ACTIVE_TEXT:
        return motif_colors(get(TR_ACTIVE)).fg;
    case TR_SELECT_FILL:
        return motif_colors(get(TR_BACKGROUND)).select;
    default:
        return get(TR_BACKGROUND);  /* the specific backgrounds */
    }
}

/* The gamma the colors are meant for, as the prefix of the scheme files'
 * keys. On IRIX the display hardware applies the system gamma (1.7 by
 * default), and SGI's schemes have values for it; elsewhere the screen
 * shows colors as they are. */
static const char* gamma_prefix()
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
        strcpy(prefix, g < 1.35 ? "gamma1_0." : g < 2.05 ? "gamma1_7."
                                                         : "gamma2_4.");
    }
    return prefix;
#else
    return "gamma1_0.";
#endif
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

/* The file's base name without ".ad". */
static Str base_name(const char* path)
{
    const char* b = strrchr(path, '/');
    b = b ? b + 1 : path;
    char buf[256];
    int n = (int)strlen(b);
    if (n > 3 && strcmp(b + n - 3, ".ad") == 0)
        n -= 3;
    if (n > (int)sizeof(buf) - 1)
        n = sizeof(buf) - 1;
    memcpy(buf, b, n);
    buf[n] = 0;
    return Str(buf);
}

bool Theme::load(const char* file)
{
    FILE* f = fopen(file, "r");
    if (!f)
        return false;
    *this = Theme();
    name = base_name(file);
    path = file;
    const char* prefix = gamma_prefix();
    int plen = (int)strlen(prefix);
    int found = 0;
    /* A value for our gamma beats a bare one, whatever the order. */
    bool exact[TR_COUNT];
    int i;
    for (i = 0; i < TR_COUNT; i++)
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
                name = value;
            continue;
        }
        bool prefixed = false;
        if (strncmp(key, "gamma", 5) == 0) {
            if (strncmp(key, prefix, plen) != 0)
                continue;       /* another gamma's value */
            key += plen;
            prefixed = true;
        }
        Rgb c;
        for (i = 0; i < TR_COUNT; i++) {
            if (strcmp(key, k_role_names[i]) != 0)
                continue;
            if (exact[i] && !prefixed)
                break;
            if (parse_hex_color(value, &c)) {
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

/* ------------------------------------------------------------------ */
/* Finding themes                                                      */

static Str g_program_dir;

void set_program_path(const char* argv0)
{
    const char* slash = argv0 ? strrchr(argv0, '/') : 0;
    if (!slash)
        return;     /* found on $PATH: an installed copy */
    char buf[1100];
    int n = (int)(slash - argv0);
    if (n > 1000)
        return;
    memcpy(buf, argv0, n);
    buf[n] = 0;
    g_program_dir = buf;
}

/* Folders to look in, with the origin they show in the theme list. */
static void theme_dirs(Vector<Str>* dirs, Vector<Str>* origins)
{
    dirs->push_back(config_path("themes"));
    origins->push_back(Str("yours"));
    char buf[1200];
    if (!g_program_dir.empty()) {
        /* build/ttr in a source tree: data/ is next to build/. */
        sprintf(buf, "%.1000s/../data/schemes/ttr", g_program_dir.c_str());
        dirs->push_back(Str(buf));
        origins->push_back(Str("ttr"));
        sprintf(buf, "%.1000s/../data/schemes/irix", g_program_dir.c_str());
        dirs->push_back(Str(buf));
        origins->push_back(Str("IRIX"));
    }
    dirs->push_back(Str(TTR_DATADIR "/schemes/ttr"));
    origins->push_back(Str("ttr"));
    dirs->push_back(Str(TTR_DATADIR "/schemes/irix"));
    origins->push_back(Str("IRIX"));
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

/* The display name of a theme file: its "name:" line, or its file name. */
static Str theme_name(const char* path)
{
    FILE* f = fopen(path, "r");
    Str name = base_name(path);
    if (!f)
        return name;
    char line[512];
    int lines = 0;
    while (lines++ < 20 && fgets(line, sizeof(line), f)) {
        if (strncmp(line, "name:", 5) == 0) {
            char* v = line + 5;
            trim(v);
            if (*v)
                name = v;
            break;
        }
    }
    fclose(f);
    return name;
}

void list_themes(Vector<ThemeInfo*>* out)
{
    Vector<Str> dirs, origins;
    theme_dirs(&dirs, &origins);
    int d, i, j;
    for (d = 0; d < dirs.size(); d++) {
        DIR* dir = opendir(dirs[d].c_str());
        if (!dir)
            continue;
        int first = out->size();
        struct dirent* de;
        while ((de = readdir(dir)) != 0) {
            int n = (int)strlen(de->d_name);
            if (n < 4 || strcmp(de->d_name + n - 3, ".ad") != 0 || n > 200)
                continue;
            char path[1500];
            sprintf(path, "%.1200s/%s", dirs[d].c_str(), de->d_name);
            Str name = theme_name(path);
            bool dup = compare_nocase(name.c_str(), "Classic") == 0;
            for (i = 0; i < out->size() && !dup; i++)
                dup = compare_nocase((*out)[i]->name.c_str(),
                                     name.c_str()) == 0;
            if (dup)
                continue;
            ThemeInfo* t = new ThemeInfo;
            t->name = name;
            t->path = path;
            t->origin = origins[d];
            out->push_back(t);
            /* By name within this folder. */
            for (j = out->size() - 1; j > first &&
                 compare_nocase((*out)[j - 1]->name.c_str(),
                                (*out)[j]->name.c_str()) > 0; j--) {
                ThemeInfo* tmp = (*out)[j];
                (*out)[j] = (*out)[j - 1];
                (*out)[j - 1] = tmp;
            }
        }
        closedir(dir);
    }
}

void free_themes(Vector<ThemeInfo*>* list)
{
    int i;
    for (i = 0; i < list->size(); i++)
        delete (*list)[i];
    list->clear();
}

bool find_theme(const char* name, Theme* out)
{
    *out = Theme::classic();
    if (!name || !*name || compare_nocase(name, "Classic") == 0)
        return true;
    Vector<ThemeInfo*> list;
    list_themes(&list);
    bool ok = false;
    int i;
    for (i = 0; i < list.size() && !ok; i++) {
        if (compare_nocase(list[i]->name.c_str(), name) != 0)
            continue;
        Theme t;
        if (t.load(list[i]->path.c_str())) {
            t.name = list[i]->name;
            *out = t;
            ok = true;
        }
    }
    free_themes(&list);
    return ok;
}

}
