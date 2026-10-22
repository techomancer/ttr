/*
 * tu/terminfo.h - a terminal's terminfo entry, read without curses
 * (ttrc/DESIGN.md section 3.1).
 *
 * Compiled entries are found in the usual directories and parsed: the
 * legacy format (magic 0432, IRIX and older ncurses) and ncurses' 32-bit
 * format (magic 01036), both little-endian on every system, including
 * the extended section with user capabilities such as RGB, Tc and Smulx.
 * Without an entry, a few built-in descriptions written in terminfo
 * source syntax are used instead.
 */
#ifndef TU_TERMINFO_H
#define TU_TERMINFO_H

#include "tu.h"

#include <stddef.h>

namespace tu {

class Terminfo {
public:
    Terminfo();
    ~Terminfo();

    /* Find and load the entry for 'term': $TERMINFO, ~/.terminfo,
     * $TERMINFO_DIRS, then the system directories; then the built-in
     * entries. False if there is no such terminal anywhere. */
    bool load(const char* term);
    /* A compiled entry from a file. */
    bool load_file(const char* path);
    /* An entry from memory: compiled format. */
    bool load_compiled(const unsigned char* data, size_t n);
    /* An entry in terminfo source syntax ("name|desc, am, cols#80,
     * cup=\E[%i%p1%d;%p2%dH, ..."); the built-ins are written this way. */
    bool load_source(const char* text);
    /* A built-in entry, if there is one by this name. */
    bool load_builtin(const char* term);
    /* Capabilities in source syntax ("colors#8, setaf=\E[3%p1%dm,")
     * added where the entry does not have them already. */
    void supplement(const char* caps);

    /* Standard and extended capabilities by their short names. */
    bool flag(const char* name) const;
    int num(const char* name) const;            /* -1 if absent */
    const char* str(const char* name) const;    /* 0 if absent */

    /* The first name of the entry ("xterm-256color"). */
    const char* name() const { return name_; }
    /* Where it came from: a path, or "built-in". */
    const char* source() const { return source_; }

private:
    struct Cap {
        const char* name;
        char type;          /* 'b', 'n', 's' */
        int num;            /* flag (1) or number */
        const char* str;
    };

    struct Block {
        Block* next;
        size_t used, size;
    };

    void clear();
    void add(const char* name, char type, int num, const char* str);
    const Cap* find(const char* name, char type) const;
    /* A copy of s[0..n) with a NUL, in storage that never moves. */
    char* keep(const char* s, size_t n);
    void parse_fields(const char* text, bool first_is_name);

    Cap* caps_;
    int ncaps_, capcap_;
    Block* blocks_;
    char name_[128];
    char source_[512];

    Terminfo(const Terminfo&);
    Terminfo& operator=(const Terminfo&);
};

} /* namespace tu */

#endif
