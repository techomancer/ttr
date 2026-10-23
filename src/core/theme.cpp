/* ttr's themes are th's (th/theme.h): conversions between the two. */
#include "core/theme.h"

#include <string.h>

namespace ttr {

namespace {

th::Color to_color(const Rgb& c)
{
    return ((th::Color)(c.r >> 8) << 16) | ((th::Color)(c.g >> 8) << 8) | (th::Color)(c.b >> 8);
}

} /* namespace */

const char* theme_role_name(ThemeRole role)
{
    return th::role_name((th::Role)role);
}

bool parse_hex_color(const char* s, Rgb* out)
{
    th::Color c;
    if (!th::parse_hex(s, &c))
        return false;
    *out = Rgb::hex(c);
    return true;
}

void format_hex_color(const Rgb& c, char* buf)
{
    th::format_hex(to_color(c), buf);
}

Theme::Theme() : name("Classic"), dark(false)
{
    int i;
    for (i = 0; i < TR_COUNT; i++)
        set[i] = false;
}

th::Theme Theme::to_th() const
{
    th::Theme t;
    strncpy(t.name, name.c_str(), sizeof(t.name) - 1);
    t.name[sizeof(t.name) - 1] = 0;
    strncpy(t.path, path.c_str(), sizeof(t.path) - 1);
    t.path[sizeof(t.path) - 1] = 0;
    t.dark = dark;
    for (int i = 0; i < TR_COUNT; i++) {
        t.color[i] = to_color(color[i]);
        t.set[i] = set[i];
    }
    return t;
}

Theme Theme::from_th(const th::Theme& t)
{
    Theme r;
    r.name = t.name;
    r.path = t.path;
    r.dark = t.dark;
    for (int i = 0; i < TR_COUNT; i++) {
        r.color[i] = Rgb::hex(t.color[i]);
        r.set[i] = t.set[i];
    }
    return r;
}

Theme Theme::classic()
{
    return from_th(th::Theme::classic());
}

void Theme::put(ThemeRole role, const Rgb& c)
{
    th::Theme t = to_th();
    t.put((th::Role)role, to_color(c));
    *this = from_th(t);
}

Rgb Theme::get(ThemeRole role) const
{
    return Rgb::hex(to_th().get((th::Role)role));
}

bool Theme::load(const char* file)
{
    th::Theme t;
    if (!t.load(file))
        return false;
    *this = from_th(t);
    return true;
}

void set_program_path(const char* argv0)
{
    th::set_program_path(argv0);
}

void list_themes(Vector<ThemeInfo*>* out)
{
    th::List list;
    th::list_themes(&list);
    for (int i = 0; i < list.count(); i++) {
        if (!list[i].path[0])
            continue;       /* Classic: ttr lists it itself */
        ThemeInfo* t = new ThemeInfo;
        t->name = list[i].name;
        t->path = list[i].path;
        t->origin = list[i].origin;
        out->push_back(t);
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
    th::Theme t;
    bool ok = th::find_theme(name, &t);
    *out = Theme::from_th(t);
    return ok;
}

}
