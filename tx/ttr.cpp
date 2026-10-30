/*
 * tx/ttr.cpp - ttr's own settings, for programs that follow them
 * (tx/ttr.h).
 */
#include "ttr.h"

#include "core/scale.h"
#include "core/settings.h"

#include <stdio.h>
#include <stdlib.h>

namespace tx {

int ttr_ui_scale()
{
    ttr::SettingsFile f;
    const char* v = 0;
    if (f.load(ttr::SettingsFile::default_path().c_str()))
        v = f.get("uiScale");
    int s = v ? atoi(v) : 100;
    return s < ttr::k_min_scale ? 100 : s > ttr::k_max_scale ? ttr::k_max_scale : s;
}

const char* ttr_theme()
{
    static char name[128];
    ttr::SettingsFile f;
    name[0] = 0;
    if (f.load(ttr::SettingsFile::default_path().c_str()) && f.get("theme"))
        snprintf(name, sizeof(name), "%s", f.get("theme"));
    return name;
}

} /* namespace tx */
