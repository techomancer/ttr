/*
 * th: ttr's colours, for every ttr program (th/DESIGN.md).
 *
 * UI themes (th/theme.h) colour Motif widgets and window frames: ttr's
 * own themes and the 30 IRIX desktop schemes. Text schemes
 * (th/scheme.h) colour text views: ttrcode's editor, ttrt's terminal.
 * Both are plain files found in the same places; the user's own go in
 * ~/.config/ttr/themes and ~/.config/ttr/schemes.
 *
 * The library needs nothing but the C library, except th/motif.h, which
 * applies a theme to Motif widgets. C++98, no STL, no exceptions.
 */
#ifndef TH_TH_H
#define TH_TH_H

namespace th {

/* 0xRRGGBB */
typedef unsigned long Color;

/* "#rrggbb" or "#rgb"; false if 's' is not one. */
bool parse_hex(const char* s, Color* out);
/* "#rrggbb" into buf (8 bytes). */
void format_hex(Color c, char* buf);

/* A background and the colours Motif derives from it (XmGetColors). */
struct ColorSet {
    Color bg, fg, top, bottom, select;
};
ColorSet motif_colors(Color bg);
/* Perceived brightness, 0 (black) to 255 (white). */
int brightness(Color c);
/* a and b mixed, pct percent of b. */
Color mix(Color a, Color b, int pct);

/* argv[0], so that data/ is found next to an uninstalled build. */
void set_program_path(const char* argv0);

/* A theme or scheme file found on disk. */
struct Entry {
    char name[128];
    char path[1024];
    char origin[16];        /* "yours", "ttr", "IRIX" or "built-in" */
};

/* A list of entries, sorted by name within each origin. */
class List {
public:
    List() : items_(0), count_(0), cap_(0) {}
    ~List();
    int count() const { return count_; }
    const Entry& operator[](int i) const { return items_[i]; }
    void add(const Entry& e);
    void clear() { count_ = 0; }
    bool has(const char* name) const;

private:
    Entry* items_;
    int count_, cap_;
    List(const List&);
    List& operator=(const List&);
};

/* Case-insensitive comparison, for names. */
int compare_names(const char* a, const char* b);

} /* namespace th */

#endif
