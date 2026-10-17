#include "x11/keys.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace ttr {

static bool same_nocase(const char* a, const char* b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
        a++;
        b++;
    }
    return *a == *b;
}

static unsigned int modifier(const char* name)
{
    if (same_nocase(name, "Ctrl") || same_nocase(name, "Control"))
        return ControlMask;
    if (same_nocase(name, "Alt") || same_nocase(name, "Meta"))
        return Mod1Mask;
    if (same_nocase(name, "Shift"))
        return ShiftMask;
    if (same_nocase(name, "Super") || same_nocase(name, "Win") ||
        same_nocase(name, "Mod4"))
        return Mod4Mask;
    return 0;
}

bool parse_key_spec(const char* spec, KeySym* sym, unsigned int* mods)
{
    char buf[128];
    if (!spec || !*spec || strlen(spec) >= sizeof(buf))
        return false;
    strcpy(buf, spec);

    *mods = 0;
    *sym = NoSymbol;
    char* part = buf;
    for (;;) {
        while (*part == ' ')
            part++;
        char* plus = strchr(part, '+');
        /* "Ctrl++" means the plus key. */
        if (plus == part && plus[1] == 0)
            plus = 0;
        if (!plus)
            break;
        *plus = 0;
        char* end = plus - 1;
        while (end >= part && *end == ' ')
            *end-- = 0;
        unsigned int m = modifier(part);
        if (!m)
            return false;
        *mods |= m;
        part = plus + 1;
    }
    char* end = part + strlen(part) - 1;
    while (end >= part && *end == ' ')
        *end-- = 0;
    if (!*part)
        return false;

    if (part[1] == 0) {
        /* Single characters: letters by their lower-case keysym. */
        char c[2];
        c[0] = (char)tolower((unsigned char)part[0]);
        c[1] = 0;
        *sym = XStringToKeysym(c);
        if (*sym == NoSymbol && part[0] == '+')
            *sym = XStringToKeysym("plus");
    } else {
        *sym = XStringToKeysym(part);
        if (*sym == NoSymbol) {
            /* Accept "return" for "Return" and the like. */
            char fixed[128];
            strcpy(fixed, part);
            fixed[0] = (char)toupper((unsigned char)fixed[0]);
            *sym = XStringToKeysym(fixed);
        }
    }
    return *sym != NoSymbol;
}

void format_key_spec(KeySym sym, unsigned int mods, char* buf, int size)
{
    char out[128];
    out[0] = 0;
    if (mods & ControlMask) strcat(out, "Ctrl+");
    if (mods & Mod1Mask) strcat(out, "Alt+");
    if (mods & ShiftMask) strcat(out, "Shift+");
    if (mods & Mod4Mask) strcat(out, "Super+");
    const char* name = XKeysymToString(sym);
    if (!name)
        name = "?";
    if (name[0] && !name[1]) {
        char c[2];
        c[0] = (char)toupper((unsigned char)name[0]);
        c[1] = 0;
        strcat(out, c);
    } else if (strlen(out) + strlen(name) < sizeof(out)) {
        strcat(out, name);
    }
    strncpy(buf, out, size - 1);
    buf[size - 1] = 0;
}

}
