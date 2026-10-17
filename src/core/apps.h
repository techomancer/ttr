/*
 * The application catalog behind the start menu: a tree of folders,
 * applications and separators, filled by an AppSource. Linux uses XDG
 * .desktop files; IRIX uses the 4Dwm toolchest files.
 */
#ifndef TTR_CORE_APPS_H
#define TTR_CORE_APPS_H

#include "core/str.h"
#include "core/vector.h"

namespace ttr {

struct AppFolder;

struct AppNode {
    enum Kind { APP, FOLDER, SEPARATOR };

    Kind kind;
    Str name;
    Str exec;           /* shell command, for APP */
    Str icon;           /* icon name or path, may be empty */
    AppFolder* folder;  /* owned, for FOLDER */

    explicit AppNode(Kind k);
    ~AppNode();

private:
    AppNode(const AppNode&);
    AppNode& operator=(const AppNode&);
};

struct AppFolder {
    Str name;
    Vector<AppNode*> items;

    explicit AppFolder(const char* n);
    ~AppFolder();

    /* Find a direct subfolder by name, creating it at the end if absent. */
    AppFolder* subfolder(const char* name);
    void add_app(const char* name, const char* exec, const char* icon);
    void add_separator();

    /* Folders first, then applications, each by name ignoring case.
     * Separators are dropped. */
    void sort_recursive();

    /* Remove empty subfolders, recursively. */
    void prune();

private:
    AppFolder(const AppFolder&);
    AppFolder& operator=(const AppFolder&);
};

class AppSource {
public:
    virtual ~AppSource() {}
    /* Add this source's applications to 'root'. */
    virtual void load(AppFolder* root) = 0;
};

/* XDG desktop entries from the given directories (earlier directories
 * override later ones), grouped into Windows-like folders by category.
 * 'terminal' prefixes Terminal=true entries, e.g. "xterm -e". */
class DesktopAppSource : public AppSource {
public:
    explicit DesktopAppSource(const char* terminal);
    ~DesktopAppSource();
    /* Add the XDG data directories ($XDG_DATA_HOME, $XDG_DATA_DIRS). */
    void add_xdg_dirs();
    void add_dir(const char* path);
    void load(AppFolder* root);

private:
    void scan(const char* dir, const char* id_prefix, AppFolder* root);
    void parse(const char* path, AppFolder* root);

    bool seen(const char* id) const;

    Str terminal_;
    Vector<Str> dirs_;
    Vector<Str> seen_;      /* desktop file ids already loaded */
};

/* 4Dwm toolchest menu files (mwm menu syntax). The menu named "ToolChest"
 * becomes the root; f.menu entries become folders. Files are read in
 * order; a menu defined twice keeps its first definition. */
class ToolchestAppSource : public AppSource {
public:
    ToolchestAppSource();
    ~ToolchestAppSource();
    void add_file(const char* path);
    void load(AppFolder* root);

private:
    struct Line {
        Str label;      /* empty for no-label */
        Str func;       /* f.exec, f.menu, f.separator, ... */
        Str arg;
    };
    struct Menu {
        Str name;
        Vector<Line*> lines;
        ~Menu();
    };

    void parse_file(const char* path);
    Menu* find_menu(const char* name);
    void build(AppFolder* folder, Menu* m, int depth);

    Vector<Str> files_;
    Vector<Menu*> menus_;
};

/* Remove XDG field codes (%f, %U, ...) from an Exec value. */
Str strip_exec_codes(const char* exec);

}

#endif
