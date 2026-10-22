/*
 * tf/path.h - path strings for ttrc's panels. A Path owns a copy of its
 * text. Local paths are absolute and kept normalized ("/a/b", never
 * "/a/./b/" or "/a/x/../b"). Paths into archives use mc's notation,
 * "file.tar.gz/utar://inner/path" (ttrc/DESIGN.md section 5.1); this
 * milestone only has local ones.
 */
#ifndef TF_PATH_H
#define TF_PATH_H

#include <stddef.h>

namespace tf {

class Path {
public:
    Path();
    explicit Path(const char* s);
    Path(const Path& o);
    Path& operator=(const Path& o);
    ~Path();

    const char* c_str() const { return s_ ? s_ : ""; }
    bool empty() const { return !s_ || !*s_; }
    bool is_root() const;
    /* "file.tar.gz/utar://": the top of an archive. */
    bool is_archive_root() const;

    /* This path with name appended ("/a" + "b" = "/a/b"). */
    Path child(const char* name) const;
    /* The directory part ("/a/b" -> "/a", "/" -> "/"). */
    Path parent() const;
    /* The last component ("/a/b" -> "b", "/" -> ""). */
    const char* name() const;

    /* An absolute, normalized path from 'p' relative to 'base' (when p
     * is not absolute); "~" and "~/x" are the home directory. */
    static Path resolve(const Path& base, const char* p);
    /* The process's working directory. */
    static Path cwd();

private:
    void set(const char* s, size_t n);
    char* s_;
};

} /* namespace tf */

#endif
