/*
 * ti's XPM 3 reader. The file is C source: comments, and strings that
 * hold the values line ("w h ncolors cpp"), the colours ("chars key value
 * ...", the visual keys c, g, g4 and m) and the rows of pixels. Colours
 * are #hex, None or X colour names, which come from the system's rgb.txt
 * (read once) so no X server is needed: this runs in decoder threads,
 * and on files from inside archives.
 */
#include "internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ti {
namespace internal {

namespace {

/* The strings of the file, in order. */
struct Strings {
    const unsigned char* d;
    size_t n, at;

    bool next(const char** s, size_t* len)
    {
        while (at < n) {
            unsigned char ch = d[at];
            if (ch == '/' && at + 1 < n && d[at + 1] == '*') {
                at += 2;
                while (at + 1 < n && !(d[at] == '*' && d[at + 1] == '/'))
                    at++;
                at += 2;
                continue;
            }
            if (ch == '"') {
                size_t start = ++at;
                while (at < n && d[at] != '"') {
                    if (d[at] == '\\' && at + 1 < n)
                        at++;
                    at++;
                }
                if (at >= n)
                    return false;
                *s = (const char*)d + start;
                *len = at - start;
                at++;
                return true;
            }
            at++;
        }
        return false;
    }
};

bool space(char c)
{
    return c == ' ' || c == '\t';
}

int hexval(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* 0xRRGGBB; -2 for None; -1 if not understood. */
long parse_color(const char* v, size_t len)
{
    if (len == 4 && (v[0] == 'N' || v[0] == 'n') && strncmp(v + 1, "one", 3) == 0)
        return -2;
    if (len == 4 && strncmp(v, "none", 4) == 0)
        return -2;
    if (len > 1 && v[0] == '#') {
        size_t digits = len - 1;
        if (digits % 3 || digits < 3 || digits > 12)
            return -1;
        size_t k = digits / 3;
        long rgb = 0;
        for (int c = 0; c < 3; c++) {
            unsigned long x = 0;
            for (size_t i = 0; i < k; i++) {
                int h = hexval(v[1 + c * k + i]);
                if (h < 0)
                    return -1;
                x = x * 16 + (unsigned long)h;
            }
            if (k == 1)
                x *= 17;
            else if (k == 3)
                x >>= 4;
            else if (k == 4)
                x >>= 8;
            rgb = (rgb << 8) | (long)x;
        }
        return rgb;
    }
    return xpm_color_name(v, len);
}

/* One colour line: the value of the best visual key (c, then g, g4, m). */
long color_of(const char* s, size_t len)
{
    static const char* const keys[4] = { "c", "g", "g4", "m" };
    long best = -1;
    int best_rank = 99;
    size_t i = 0;
    while (i < len) {
        while (i < len && space(s[i]))
            i++;
        size_t ks = i;
        while (i < len && !space(s[i]))
            i++;
        size_t kl = i - ks;
        int rank = -1;
        for (int k = 0; k < 4; k++)
            if (kl == strlen(keys[k]) && strncmp(s + ks, keys[k], kl) == 0)
                rank = k;
        bool sym = kl == 1 && s[ks] == 's';
        if (rank < 0 && !sym)
            continue;
        /* The value: words up to the next key. */
        size_t vs = i, ve = i;
        for (;;) {
            while (i < len && space(s[i]))
                i++;
            if (i >= len)
                break;
            size_t ws = i;
            while (i < len && !space(s[i]))
                i++;
            size_t wl = i - ws;
            bool key = (wl == 1 && (s[ws] == 'c' || s[ws] == 'g' || s[ws] == 'm' || s[ws] == 's')) ||
                       (wl == 2 && s[ws] == 'g' && s[ws + 1] == '4');
            if (key && ve > vs) {
                i = ws;
                break;
            }
            if (ve == vs)
                vs = ws;
            ve = i;
        }
        if (rank >= 0 && rank < best_rank && ve > vs) {
            long c = parse_color(s + vs, ve - vs);
            if (c != -1) {
                best = c;
                best_rank = rank;
            }
        }
    }
    return best;
}

unsigned hash_chars(const char* s, int cpp)
{
    unsigned h = 2166136261u;
    for (int i = 0; i < cpp; i++)
        h = (h ^ (unsigned char)s[i]) * 16777619u;
    return h;
}

} /* namespace */

bool xpm_decode(Ctx& c)
{
    Strings st;
    st.d = c.d;
    st.n = c.n;
    st.at = 0;
    const char* s;
    size_t len;
    if (!st.next(&s, &len))
        return fail(c, "XPM: no values line.");
    long w = 0, h = 0, ncolors = 0, cpp = 0;
    {
        char buf[64];
        size_t k = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
        memcpy(buf, s, k);
        buf[k] = 0;
        if (sscanf(buf, "%ld %ld %ld %ld", &w, &h, &ncolors, &cpp) != 4)
            return fail(c, "XPM: the values line is damaged.");
    }
    if (cpp < 1 || cpp > 8 || ncolors < 1 || ncolors > (1L << 20))
        return fail(c, "XPM: %ld colours of %ld characters is not an image.", ncolors, cpp);
    if (!check_size(c, w, h))
        return false;

    /* The colours, and a table from their characters to them. */
    unsigned long* rgba = (unsigned long*)malloc((size_t)ncolors * sizeof(unsigned long));
    const char** key = (const char**)malloc((size_t)ncolors * sizeof(const char*));
    size_t slots = 16;
    while (slots < (size_t)ncolors * 2)
        slots *= 2;
    int* table = (int*)malloc(slots * sizeof(int));
    bool ok = rgba && key && table;
    if (!ok)
        fail(c, "Out of memory.");
    if (table)
        for (size_t i = 0; i < slots; i++)
            table[i] = -1;
    for (long i = 0; ok && i < ncolors; i++) {
        if (!st.next(&s, &len) || len < (size_t)cpp) {
            ok = fail(c, "XPM: the colours end early.");
            break;
        }
        key[i] = s;
        long col = color_of(s + cpp, len - (size_t)cpp);
        if (col == -2)
            rgba[i] = 0;                            /* transparent */
        else if (col < 0)
            rgba[i] = 0x000000ffUL;                 /* unknown: black */
        else
            rgba[i] = ((unsigned long)col << 8) | 0xff;
        size_t at = hash_chars(s, (int)cpp) & (slots - 1);
        while (table[at] >= 0 && memcmp(key[table[at]], s, (size_t)cpp) != 0)
            at = (at + 1) & (slots - 1);
        if (table[at] < 0)
            table[at] = (int)i;
    }

    if (ok && !c.out->alloc((int)w, (int)h))
        ok = fail(c, "Out of memory.");
    for (long y = 0; ok && y < h; y++) {
        if (!st.next(&s, &len) || len < (size_t)(w * cpp)) {
            ok = fail(c, "XPM: row %ld is short or missing.", y + 1);
            break;
        }
        unsigned char* o = c.out->row((int)y);
        for (long x = 0; x < w; x++, s += cpp, o += 4) {
            size_t at = hash_chars(s, (int)cpp) & (slots - 1);
            while (table[at] >= 0 && memcmp(key[table[at]], s, (size_t)cpp) != 0)
                at = (at + 1) & (slots - 1);
            unsigned long v = table[at] >= 0 ? rgba[table[at]] : 0x000000ffUL;
            o[0] = (unsigned char)(v >> 24);
            o[1] = (unsigned char)(v >> 16);
            o[2] = (unsigned char)(v >> 8);
            o[3] = (unsigned char)v;
        }
    }
    free(rgba);
    free(key);
    free(table);
    return ok;
}

/* ------------------------------------------------------------------ */
/* X colour names                                                      */

namespace {

struct Named {
    char name[32];      /* lower case, no spaces */
    long rgb;
};

Named* g_names = 0;
int g_count = 0;
bool g_loaded = false;

/* "Light Gray" -> "lightgray"; false if too long. */
bool normalize(const char* s, size_t len, char* out, size_t cap)
{
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        if (space(s[i]))
            continue;
        if (o + 1 >= cap)
            return false;
        out[o++] = (char)tolower((unsigned char)s[i]);
    }
    out[o] = 0;
    return o > 0;
}

void add_name(const char* name, size_t len, long rgb, int* cap)
{
    if (g_count == *cap) {
        int nc = *cap ? *cap * 2 : 256;
        Named* n = (Named*)realloc(g_names, (size_t)nc * sizeof(Named));
        if (!n)
            return;
        g_names = n;
        *cap = nc;
    }
    if (normalize(name, len, g_names[g_count].name, sizeof(g_names[g_count].name))) {
        g_names[g_count].rgb = rgb;
        g_count++;
    }
}

void load_names()
{
    static const char* const paths[] = {
        "/usr/share/X11/rgb.txt", "/usr/lib/X11/rgb.txt", "/etc/X11/rgb.txt",
        "/usr/X11R6/lib/X11/rgb.txt", "/usr/openwin/lib/X11/rgb.txt", 0
    };
    int cap = 0;
    for (int i = 0; paths[i] && !g_count; i++) {
        FILE* f = fopen(paths[i], "r");
        if (!f)
            continue;
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            int r, g, b, at = 0;
            if (line[0] == '!' || sscanf(line, "%d %d %d %n", &r, &g, &b, &at) != 3 || !at)
                continue;
            size_t len = strlen(line + at);
            while (len && (line[at + len - 1] == '\n' || line[at + len - 1] == '\r'))
                len--;
            add_name(line + at, len, ((long)(r & 255) << 16) | ((g & 255) << 8) | (b & 255), &cap);
        }
        fclose(f);
    }
    if (!g_count) {
        /* No rgb.txt: the names XPMs use most. grayN is computed. */
        static const struct { const char* n; long rgb; } basic[] = {
            { "black", 0x000000 }, { "white", 0xffffff }, { "red", 0xff0000 },
            { "green", 0x00ff00 }, { "blue", 0x0000ff }, { "yellow", 0xffff00 },
            { "cyan", 0x00ffff }, { "magenta", 0xff00ff }, { "gray", 0xbebebe },
            { "grey", 0xbebebe }, { "darkgray", 0xa9a9a9 }, { "lightgray", 0xd3d3d3 },
            { "orange", 0xffa500 }, { "brown", 0xa52a2a }, { "navy", 0x000080 },
            { "maroon", 0xb03060 }, { "purple", 0xa020f0 }, { "pink", 0xffc0cb },
            { 0, 0 }
        };
        for (int i = 0; basic[i].n; i++)
            add_name(basic[i].n, strlen(basic[i].n), basic[i].rgb, &cap);
    }
}

} /* namespace */

long xpm_color_name(const char* name, size_t len)
{
    char want[32];
    if (!normalize(name, len, want, sizeof(want)))
        return -1;
    lock();
    if (!g_loaded) {
        load_names();
        g_loaded = true;
    }
    unlock();
    for (int i = 0; i < g_count; i++)
        if (strcmp(g_names[i].name, want) == 0)
            return g_names[i].rgb;
    /* gray0 .. gray100, grey0 .. grey100, as X computes them. */
    if ((strncmp(want, "gray", 4) == 0 || strncmp(want, "grey", 4) == 0) && want[4]) {
        char* end = 0;
        long pct = strtol(want + 4, &end, 10);
        if (end && !*end && pct >= 0 && pct <= 100) {
            long v = (pct * 255 + 50) / 100;
            return (v << 16) | (v << 8) | v;
        }
    }
    return -1;
}

} /* namespace internal */
} /* namespace ti */
