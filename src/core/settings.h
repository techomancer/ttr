/*
 * The settings file, ~/.config/ttr/settings: one "name: value" per line,
 * with the same names as the X resources (Ttr*name). '#' and '!' start
 * comments. Saving keeps comments and unknown lines and only rewrites the
 * entries that are set.
 */
#ifndef TTR_CORE_SETTINGS_H
#define TTR_CORE_SETTINGS_H

#include "core/str.h"
#include "core/vector.h"

namespace ttr {

/* $XDG_CONFIG_HOME/ttr/<file>, or ~/.config/ttr/<file>. */
Str config_path(const char* file);

/* mkdir -p for the directory part of 'path'. */
void make_parent_dirs(const char* path);

class SettingsFile {
public:
    SettingsFile();
    ~SettingsFile();

    /* False if the file cannot be read (a missing file is not an error
     * for callers: they just get no entries). */
    bool load(const char* path);
    /* Writes atomically, creating the parent directory if needed. */
    bool save(const char* path) const;

    const char* get(const char* name) const;   /* 0 if absent */
    void set(const char* name, const char* value);

    int count() const;
    const char* name(int i) const;
    const char* value(int i) const;

    /* $XDG_CONFIG_HOME/ttr/settings, or ~/.config/ttr/settings. */
    static Str default_path();

private:
    struct Line {
        bool entry;
        Str text;   /* the raw line, for comments and unknown lines */
        Str name;
        Str value;
    };

    SettingsFile(const SettingsFile&);
    SettingsFile& operator=(const SettingsFile&);

    Line* find(const char* name) const;
    const Line* entry(int i) const;

    Vector<Line*> lines_;
    bool existed_;      /* loaded from a file; else save() adds a header */
};

}

#endif
