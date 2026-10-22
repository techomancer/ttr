#include "config.h"

#include "core/settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ttrc {

namespace {

const char* const k_header[] = {
    "# ttrc settings.",
    "#",
    "# Edited by Options > Configuration in ttrc, or by hand. One",
    "# \"name: value\" per line.",
    "",
    0
};

bool flag(ttr::SettingsFile& f, const char* name, bool dflt)
{
    const char* v = f.get(name);
    if (!v)
        return dflt;
    return strcmp(v, "true") == 0 || strcmp(v, "yes") == 0 || strcmp(v, "1") == 0;
}

int number(ttr::SettingsFile& f, const char* name, int dflt)
{
    const char* v = f.get(name);
    return v ? atoi(v) : dflt;
}

ttr::Str text(ttr::SettingsFile& f, const char* name, const char* dflt)
{
    const char* v = f.get(name);
    return ttr::Str(v ? v : dflt);
}

} /* namespace */

ttr::Str config_file(const char* name)
{
    const char* xdg = getenv("XDG_CONFIG_HOME");
    const char* home = getenv("HOME");
    char path[1200];
    if (xdg && *xdg && strlen(xdg) < 1000)
        sprintf(path, "%s/ttrc/%.100s", xdg, name);
    else
        sprintf(path, "%.1000s/.config/ttrc/%.100s", home ? home : ".", name);
    return ttr::Str(path);
}

Config::Config()
    : confirm_delete(true), confirm_overwrite(true), auto_return(true), show_hidden(true),
      remember_dirs(true), preserve_attrs(true), internal_viewer(true), internal_editor(true),
      left_mode(0), right_mode(0), left_sort(0),
      right_sort(0)
{
}

void Config::load()
{
    ttr::SettingsFile f;
    if (!f.load(config_file("settings").c_str()))
        return;
    confirm_delete = flag(f, "confirmDelete", true);
    confirm_overwrite = flag(f, "confirmOverwrite", true);
    auto_return = flag(f, "returnAfterCommand", true);
    show_hidden = flag(f, "showHidden", true);
    remember_dirs = flag(f, "rememberDirectories", true);
    preserve_attrs = flag(f, "preserveAttributes", true);
    internal_viewer = flag(f, "internalViewer", true);
    internal_editor = flag(f, "internalEditor", true);
    theme = text(f, "theme", "");
    tier = text(f, "terminal", "");
    left_dir = text(f, "leftDirectory", "");
    right_dir = text(f, "rightDirectory", "");
    left_mode = number(f, "leftListing", 0);
    right_mode = number(f, "rightListing", 0);
    left_sort = number(f, "leftSort", 0);
    right_sort = number(f, "rightSort", 0);
}

bool Config::save() const
{
    ttr::SettingsFile f;
    f.set_header(k_header);
    ttr::Str path = config_file("settings");
    f.load(path.c_str());
    f.set("confirmDelete", confirm_delete ? "true" : "false");
    f.set("confirmOverwrite", confirm_overwrite ? "true" : "false");
    f.set("returnAfterCommand", auto_return ? "true" : "false");
    f.set("showHidden", show_hidden ? "true" : "false");
    f.set("rememberDirectories", remember_dirs ? "true" : "false");
    f.set("preserveAttributes", preserve_attrs ? "true" : "false");
    f.set("internalViewer", internal_viewer ? "true" : "false");
    f.set("internalEditor", internal_editor ? "true" : "false");
    f.set("theme", theme.c_str());
    f.set("terminal", tier.c_str());
    if (remember_dirs) {
        f.set("leftDirectory", left_dir.c_str());
        f.set("rightDirectory", right_dir.c_str());
    } else {
        f.remove("leftDirectory");
        f.remove("rightDirectory");
    }
    char b[16];
    sprintf(b, "%d", left_mode);
    f.set("leftListing", b);
    sprintf(b, "%d", right_mode);
    f.set("rightListing", b);
    sprintf(b, "%d", left_sort);
    f.set("leftSort", b);
    sprintf(b, "%d", right_sort);
    f.set("rightSort", b);
    return f.save(path.c_str());
}

} /* namespace ttrc */
