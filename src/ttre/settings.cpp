#include "settings.h"

#include "core/settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ttre {

namespace {

const char* text(const ttr::SettingsFile& f, const char* name, const char* dflt)
{
    const char* v = f.get(name);
    return v ? v : dflt;
}

bool flag(const ttr::SettingsFile& f, const char* name, bool dflt)
{
    const char* v = f.get(name);
    if (!v)
        return dflt;
    return strcmp(v, "true") == 0 || strcmp(v, "yes") == 0 || strcmp(v, "1") == 0;
}

int number(const ttr::SettingsFile& f, const char* name, int dflt, int lo, int hi)
{
    const char* v = f.get(name);
    int n = v && *v ? atoi(v) : dflt;
    return n < lo ? lo : n > hi ? hi : n;
}

} /* namespace */

Settings::Settings()
    : theme(""), tier(""), scheme(""), tab_width(8), indent_width(4), insert_spaces(true),
      auto_indent(true), line_numbers(false), highlight_line(true), show_whitespace(false),
      ruler(80), cursor_shape("default"), cursor_blink(true), cursor_color("")
{
}

ttr::Str Settings::path()
{
    const char* xdg = getenv("XDG_CONFIG_HOME");
    const char* home = getenv("HOME");
    char p[1100];
    if (xdg && *xdg && strlen(xdg) < 1000)
        sprintf(p, "%s/ttre/settings", xdg);
    else
        sprintf(p, "%.1000s/.config/ttre/settings", home ? home : ".");
    return ttr::Str(p);
}

void Settings::load()
{
    ttr::SettingsFile f;
    if (!f.load(path().c_str()))
        return;
    theme = text(f, "theme", "");
    tier = text(f, "terminal", "");
    scheme = text(f, "colorScheme", "");
    tab_width = number(f, "tabWidth", 8, 1, 32);
    indent_width = number(f, "indentWidth", 4, 0, 32);
    insert_spaces = flag(f, "insertSpaces", true);
    auto_indent = flag(f, "autoIndent", true);
    line_numbers = flag(f, "lineNumbers", false);
    highlight_line = flag(f, "highlightLine", true);
    show_whitespace = flag(f, "showWhitespace", false);
    ruler = number(f, "rulerColumn", 80, 0, 1000);
    cursor_shape = text(f, "cursorShape", "default");
    cursor_blink = flag(f, "cursorBlink", true);
    cursor_color = text(f, "cursorColor", "");
}

bool Settings::save() const
{
    ttr::SettingsFile f;
    static const char* const header[] = {
        "# ttre's settings (F9 > Options > Editor writes them).", 0
    };
    f.set_header(header);
    ttr::Str p = path();
    f.load(p.c_str());
    char v[32];
    f.set("theme", theme.c_str());
    f.set("terminal", tier.c_str());
    f.set("colorScheme", scheme.c_str());
    sprintf(v, "%d", tab_width);
    f.set("tabWidth", v);
    sprintf(v, "%d", indent_width);
    f.set("indentWidth", v);
    f.set("insertSpaces", insert_spaces ? "true" : "false");
    f.set("autoIndent", auto_indent ? "true" : "false");
    f.set("lineNumbers", line_numbers ? "true" : "false");
    f.set("highlightLine", highlight_line ? "true" : "false");
    f.set("showWhitespace", show_whitespace ? "true" : "false");
    sprintf(v, "%d", ruler);
    f.set("rulerColumn", v);
    f.set("cursorShape", cursor_shape.c_str());
    f.set("cursorBlink", cursor_blink ? "true" : "false");
    f.set("cursorColor", cursor_color.c_str());
    ttr::make_parent_dirs(p.c_str());
    return f.save(p.c_str());
}

} /* namespace ttre */
