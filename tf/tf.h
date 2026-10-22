/*
 * tf: file systems for ttrc (ttrc/DESIGN.md section 5). Every file
 * access in ttrc goes through an Fs: the local file system now, archives
 * and compressed files later, all behind the same interface. Errors are
 * errno values, never exceptions. Builds with MIPSpro 7.4 and
 * g++ -std=c++98 -pedantic; includes nothing from src/ or X.
 */
#ifndef TF_TF_H
#define TF_TF_H

#include "../tl/port.h"

#include <stddef.h>

namespace tf {

enum FileType {
    FT_FILE,
    FT_DIR,
    FT_LINK,
    FT_CHAR,
    FT_BLOCK,
    FT_FIFO,
    FT_SOCKET,
    FT_OTHER
};

struct Stat {
    FileType type;
    unsigned mode;          /* permission bits, with setuid, setgid, sticky */
    uint64_t size;
    long mtime, atime, ctime;   /* seconds since the epoch */
    long uid, gid;
    unsigned long nlink;
    uint64_t dev, ino;
    uint64_t rdev;          /* devices */

    Stat() : type(FT_OTHER), mode(0), size(0), mtime(0), atime(0), ctime(0), uid(0), gid(0),
             nlink(0), dev(0), ino(0), rdev(0) {}
};

struct Space {
    uint64_t total, free, avail;    /* bytes */
    char type[32];                  /* "xfs", "efs", "ext4", ... */
};

/* Capabilities of a file system. */
enum {
    CAP_WRITE = 1,
    CAP_SEEK = 2,
    CAP_SYMLINKS = 4,
    CAP_OWNERS = 8
};

/* Which attributes set_attrs() changes. */
enum {
    ATTR_MODE = 1,
    ATTR_OWNER = 2,
    ATTR_TIMES = 4
};

struct Attrs {
    unsigned mode;
    long uid, gid;
    long atime, mtime;
};

/* Open flags for Fs::open. */
enum {
    OPEN_READ = 1,
    OPEN_WRITE = 2,
    OPEN_CREATE = 4,
    OPEN_TRUNCATE = 8,
    OPEN_EXCLUSIVE = 16
};

class DirReader {
public:
    virtual ~DirReader() {}
    /* The next name ("." and ".." are skipped), or 0 at the end. The
     * pointer is valid until the next call. */
    virtual const char* next() = 0;
};

class Stream {
public:
    virtual ~Stream() {}
    virtual long read(void* buf, size_t n) = 0;         /* -errno on error */
    virtual long write(const void* buf, size_t n) = 0;
    virtual int seek(uint64_t off) = 0;                 /* where CAP_SEEK */
    virtual int close() = 0;                            /* reports write errors */
};

class Fs {
public:
    virtual ~Fs() {}
    virtual int caps() const = 0;
    /* 0 or an errno value. follow: stat the target of a symlink. */
    virtual int stat(const char* path, Stat* st, bool follow) = 0;
    virtual DirReader* open_dir(const char* path, int* err) = 0;
    virtual Stream* open(const char* path, int flags, unsigned mode, int* err) = 0;
    virtual int mkdir(const char* path, unsigned mode) = 0;
    /* A file, link or empty directory. */
    virtual int remove(const char* path) = 0;
    virtual int rename(const char* from, const char* to) = 0;
    virtual int symlink(const char* target, const char* path) = 0;
    /* The link's target, NUL-terminated, truncated to n - 1. */
    virtual int readlink(const char* path, char* buf, size_t n) = 0;
    virtual int set_attrs(const char* path, const Attrs& a, int which) = 0;
    virtual int space(const char* path, Space* out) = 0;
};

/* The local file system (local.cpp). One instance serves all paths. */
Fs* local_fs();

} /* namespace tf */

#endif
