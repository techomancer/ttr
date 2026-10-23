#include "internal.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(TTR_DATADIR)
#define TTR_DATADIR "/usr/share/ttr"
#endif

namespace th {

namespace {

int hex_digit(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    c = tolower(c);
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

char g_program_dir[1024];

/* Motif's CalculateColorsRGB (lib/Xm/Visual.c), in 16-bit channels: the
 * background classed dark, medium or light by brightness, then each
 * channel scaled by a percentage. */
const long MAXC = 65535;
const int DARK_THRESHOLD = 20, LIGHT_THRESHOLD = 93, FG_THRESHOLD = 70;
const int LITE_SEL = 15, LITE_BS = 45, LITE_TS = 50;
const int LO_SEL = 15, LO_BS = 60, LO_TS = 50;
const int HI_SEL = 15, HI_BS = 40, HI_TS = 60;
const int DARK_SEL = 15, DARK_BS = 30, DARK_TS = 50;

struct C16 {
    long r, g, b;
};

C16 to16(Color c)
{
    C16 v;
    v.r = (long)((c >> 16) & 0xff) * 257;
    v.g = (long)((c >> 8) & 0xff) * 257;
    v.b = (long)(c & 0xff) * 257;
    return v;
}

Color to8(const C16& v)
{
    return ((Color)(v.r >> 8) << 16) | ((Color)(v.g >> 8) << 8) | (Color)(v.b >> 8);
}

long bright16(const C16& c)
{
    long intensity = (c.r + c.g + c.b) / 3;
    long luminosity = (c.r * 30 + c.g * 59 + c.b * 11) / 100;
    return (intensity * 75 + luminosity * 25) / 100;
}

Color darker(Color c, long pct)
{
    C16 v = to16(c);
    v.r -= v.r * pct / 100;
    v.g -= v.g * pct / 100;
    v.b -= v.b * pct / 100;
    return to8(v);
}

Color lighter(Color c, long pct)
{
    C16 v = to16(c);
    v.r += (MAXC - v.r) * pct / 100;
    v.g += (MAXC - v.g) * pct / 100;
    v.b += (MAXC - v.b) * pct / 100;
    return to8(v);
}

/* The display name of a file: its "name:" line, or the file name
 * without its extension. */
void file_name(const char* path, const char* ext, char* out, int size)
{
    const char* b = strrchr(path, '/');
    b = b ? b + 1 : path;
    copy_str(out, b, size);
    int n = (int)strlen(out), el = (int)strlen(ext);
    if (n > el && strcmp(out + n - el, ext) == 0)
        out[n - el] = 0;
    FILE* f = fopen(path, "r");
    if (!f)
        return;
    char line[512];
    int lines = 0;
    while (lines++ < 20 && fgets(line, sizeof(line), f)) {
        if (strncmp(line, "name:", 5) == 0) {
            char* v = line + 5;
            trim(v);
            if (*v)
                copy_str(out, v, size);
            break;
        }
    }
    fclose(f);
}

void scan_dir(const char* dir, const char* ext, const char* origin, List* out)
{
    DIR* d = opendir(dir);
    if (!d)
        return;
    List found;
    struct dirent* de;
    int el = (int)strlen(ext);
    while ((de = readdir(d)) != 0) {
        int n = (int)strlen(de->d_name);
        if (n <= el || strcmp(de->d_name + n - el, ext) != 0 || n > 200)
            continue;
        Entry e;
        snprintf(e.path, sizeof(e.path), "%.800s/%.200s", dir, de->d_name);
        file_name(e.path, ext, e.name, sizeof(e.name));
        copy_str(e.origin, origin, sizeof(e.origin));
        if (!out->has(e.name) && !found.has(e.name))
            found.add(e);
    }
    closedir(d);
    /* By name: a few dozen at most, so a simple insertion order. */
    bool* used = (bool*)calloc((size_t)found.count() + 1, sizeof(bool));
    for (int k = 0; used && k < found.count(); k++) {
        int best = -1;
        for (int i = 0; i < found.count(); i++)
            if (!used[i] && (best < 0 || compare_names(found[i].name, found[best].name) < 0))
                best = i;
        used[best] = true;
        out->add(found[best]);
    }
    free(used);
}

} /* namespace */

bool parse_hex(const char* s, Color* out)
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
        *out = ((Color)(v[0] * 16 + v[1]) << 16) | ((Color)(v[2] * 16 + v[3]) << 8) |
               (Color)(v[4] * 16 + v[5]);
        return true;
    }
    if (n == 3) {
        *out = ((Color)(v[0] * 17) << 16) | ((Color)(v[1] * 17) << 8) | (Color)(v[2] * 17);
        return true;
    }
    return false;
}

void format_hex(Color c, char* buf)
{
    sprintf(buf, "#%06lx", c & 0xffffffUL);
}

ColorSet motif_colors(Color bg)
{
    ColorSet cs;
    long br = bright16(to16(bg));
    cs.bg = bg;
    if (br < DARK_THRESHOLD * MAXC / 100) {
        cs.select = lighter(bg, DARK_SEL);
        cs.bottom = lighter(bg, DARK_BS);
        cs.top = lighter(bg, DARK_TS);
    } else if (br > LIGHT_THRESHOLD * MAXC / 100) {
        cs.select = darker(bg, LITE_SEL);
        cs.bottom = darker(bg, LITE_BS);
        cs.top = lighter(bg, LITE_TS);
    } else {
        cs.select = darker(bg, LO_SEL + br * (HI_SEL - LO_SEL) / MAXC);
        cs.bottom = darker(bg, LO_BS + br * (HI_BS - LO_BS) / MAXC);
        cs.top = lighter(bg, LO_TS + br * (HI_TS - LO_TS) / MAXC);
    }
    cs.fg = br > FG_THRESHOLD * MAXC / 100 ? 0x000000UL : 0xffffffUL;
    return cs;
}

int brightness(Color c)
{
    return (int)(bright16(to16(c)) >> 8);
}

Color mix(Color a, Color b, int pct)
{
    Color out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        long x = (long)((a >> sh) & 0xff), y = (long)((b >> sh) & 0xff);
        out |= (Color)(x + (y - x) * pct / 100) << sh;
    }
    return out;
}

void set_program_path(const char* argv0)
{
    const char* slash = argv0 ? strrchr(argv0, '/') : 0;
    if (!slash || slash - argv0 > 900)
        return;     /* found on $PATH: an installed copy */
    memcpy(g_program_dir, argv0, (size_t)(slash - argv0));
    g_program_dir[slash - argv0] = 0;
}

int compare_names(const char* a, const char* b)
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

List::~List()
{
    free(items_);
}

void List::add(const Entry& e)
{
    if (count_ == cap_) {
        int nc = cap_ ? cap_ * 2 : 16;
        Entry* n = (Entry*)realloc(items_, sizeof(Entry) * (size_t)nc);
        if (!n)
            return;
        items_ = n;
        cap_ = nc;
    }
    items_[count_++] = e;
}

bool List::has(const char* name) const
{
    for (int i = 0; i < count_; i++)
        if (compare_names(items_[i].name, name) == 0)
            return true;
    return false;
}

void trim(char* s)
{
    int n = (int)strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = 0;
    int i = 0;
    while (s[i] && isspace((unsigned char)s[i]))
        i++;
    if (i > 0)
        memmove(s, s + i, (size_t)(n - i + 1));
}

void copy_str(char* dst, const char* src, int size)
{
    int n = (int)strlen(src);
    if (n > size - 1)
        n = size - 1;
    memcpy(dst, src, (size_t)n);
    dst[n] = 0;
}

void scan(const char* kind, List* out)
{
    bool themes = strcmp(kind, "themes") == 0;
    const char* ext = themes ? ".ad" : ".scheme";
    char dir[1100];
    const char* xdg = getenv("XDG_CONFIG_HOME");
    const char* home = getenv("HOME");
    if (xdg && *xdg && strlen(xdg) < 900)
        sprintf(dir, "%s/ttr/%s", xdg, kind);
    else if (home && strlen(home) < 900)
        sprintf(dir, "%s/.config/ttr/%s", home, kind);
    else
        dir[0] = 0;
    if (dir[0])
        scan_dir(dir, ext, "yours", out);
    static const char* const theme_subs[] = { "ttr", "irix", 0 };
    static const char* const theme_origins[] = { "ttr", "IRIX", 0 };
    static const char* const scheme_subs[] = { "text", 0 };
    static const char* const scheme_origins[] = { "ttr", 0 };
    const char* const* subs = themes ? theme_subs : scheme_subs;
    const char* const* origins = themes ? theme_origins : scheme_origins;
    for (int i = 0; subs[i]; i++) {
        /* build/prog in a source tree: data/ is next to build/. */
        if (g_program_dir[0]) {
            snprintf(dir, sizeof(dir), "%s/../data/schemes/%s", g_program_dir, subs[i]);
            scan_dir(dir, ext, origins[i], out);
        }
        snprintf(dir, sizeof(dir), "%s/schemes/%s", TTR_DATADIR, subs[i]);
        scan_dir(dir, ext, origins[i], out);
    }
}

} /* namespace th */
