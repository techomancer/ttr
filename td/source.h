/*
 * td/source.h - finding the source files the line tables name. A file is
 * read where the compiler recorded it, or else under the directories the
 * user adds, trying its path's trailing parts ("x/y/z.c": then "y/z.c",
 * then "z.c") so a source tree moved elsewhere is still found.
 */
#ifndef TD_SOURCE_H
#define TD_SOURCE_H

#include "core/str.h"
#include "core/vector.h"

#include <stddef.h>

namespace td {

class Sources {
public:
    void add_dir(const char* dir);
    int dir_count() const { return dirs_.size(); }
    const char* dir(int i) const { return dirs_[i].c_str(); }

    /* The file to read for a recorded name; false if none is readable. */
    bool find(const char* recorded, ttr::Str* path) const;
    /* One line of it, without the newline. */
    bool line(const char* recorded, int n, char* buf, size_t size) const;

private:
    ttr::Vector<ttr::Str> dirs_;
};

} /* namespace td */

#endif
