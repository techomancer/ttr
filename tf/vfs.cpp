#include "vfs.h"
#include "archive.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace tf {

namespace {

struct Scheme {
    const char* name;
    ArchiveKind kind;
};

const Scheme k_schemes[] = {
    { "utar", ARCHIVE_TAR },
    { "uzip", ARCHIVE_ZIP },
    { "ugz", ARCHIVE_SINGLE },
    { "ubz2", ARCHIVE_SINGLE },
    { "uz", ARCHIVE_SINGLE },
    { 0, ARCHIVE_TAR }
};

bool ends_with(const char* s, const char* suffix, bool icase)
{
    size_t a = strlen(s), b = strlen(suffix);
    if (a <= b)
        return false;
    const char* t = s + a - b;
    for (size_t i = 0; i < b; i++) {
        char x = t[i], y = suffix[i];
        if (icase) {
            if (x >= 'A' && x <= 'Z')
                x = (char)(x - 'A' + 'a');
            if (y >= 'A' && y <= 'Z')
                y = (char)(y - 'A' + 'a');
        }
        if (x != y)
            return false;
    }
    return true;
}

/* Split "outer/utar://inner" at its last archive marker. */
bool split(const char* path, char* outer, size_t on, const Scheme** scheme, const char** inner)
{
    const char* best = 0;
    const Scheme* bs = 0;
    for (const char* s = strstr(path, "://"); s; s = strstr(s + 3, "://")) {
        const char* b = s;
        while (b > path && b[-1] != '/')
            b--;
        for (int i = 0; k_schemes[i].name; i++) {
            size_t l = strlen(k_schemes[i].name);
            if ((size_t)(s - b) == l && strncmp(b, k_schemes[i].name, l) == 0 && b > path) {
                best = b;
                bs = &k_schemes[i];
            }
        }
    }
    if (!best)
        return false;
    size_t ol = (size_t)(best - 1 - path);
    if (ol >= on)
        return false;
    memcpy(outer, path, ol);
    outer[ol] = 0;
    *scheme = bs;
    *inner = best + strlen(bs->name) + 3;
    return true;
}

struct Mount {
    char* key;              /* outer path + "\n" + scheme */
    Fs* fs;
    uint64_t size;
    long mtime;
    long used;
};

const int k_mounts = 8;

class Vfs : public Fs {
public:
    Vfs() : clock_(0)
    {
        memset(mounts_, 0, sizeof(mounts_));
    }
    ~Vfs()
    {
        for (int i = 0; i < k_mounts; i++) {
            free(mounts_[i].key);
            delete mounts_[i].fs;
        }
    }

    /* The file system and the path inside it. */
    Fs* route(const char* path, const char** inner, int* err)
    {
        char outer[4096];
        const Scheme* sc;
        if (!split(path, outer, sizeof(outer), &sc, inner)) {
            *inner = path;
            return local_fs();
        }
        return mount(outer, sc, err);
    }

    Fs* mount(const char* outer, const Scheme* sc, int* err)
    {
        Stat st;
        int r = stat(outer, &st, true);
        if (r != 0) {
            *err = r;
            return 0;
        }
        if (st.type != FT_FILE) {
            *err = ENOTDIR;
            return 0;
        }
        char key[4200];
        snprintf(key, sizeof(key), "%s\n%s", outer, sc->name);
        int free_slot = -1, oldest = 0;
        for (int i = 0; i < k_mounts; i++) {
            Mount& m = mounts_[i];
            if (m.key && strcmp(m.key, key) == 0) {
                if (m.size == st.size && m.mtime == st.mtime) {
                    m.used = ++clock_;
                    return m.fs;
                }
                /* The archive changed: read it again. */
                free(m.key);
                delete m.fs;
                m.key = 0;
                m.fs = 0;
            }
            if (!m.key && free_slot < 0)
                free_slot = i;
            if (mounts_[i].used < mounts_[oldest].used)
                oldest = i;
        }
        int slot = free_slot >= 0 ? free_slot : oldest;
        Mount& m = mounts_[slot];
        free(m.key);
        delete m.fs;
        m.key = 0;
        m.fs = 0;

        /* The archive's bytes: the file itself when it is local, a spool
         * of it otherwise. */
        Source* src = 0;
        if (is_local_path(outer)) {
            int fd = ::open(outer, O_RDONLY);
            if (fd < 0) {
                *err = errno;
                return 0;
            }
            fcntl(fd, F_SETFD, FD_CLOEXEC);
            src = new FdSource(fd);
        } else {
            Stream* in = open(outer, OPEN_READ, 0, err);
            if (!in)
                return 0;
            src = spool_stream(in, err);
            delete in;
            if (!src)
                return 0;
        }
        const char* base = strrchr(outer, '/');
        base = base ? base + 1 : outer;
        Fs* fs = open_archive(sc->kind, src, base, st.mtime, err);
        if (!fs) {
            if (!*err)
                *err = EINVAL;
            return 0;
        }
        m.key = strdup(key);
        m.fs = fs;
        m.size = st.size;
        m.mtime = st.mtime;
        m.used = ++clock_;
        return fs;
    }

    int caps() const { return CAP_WRITE | CAP_SEEK | CAP_SYMLINKS | CAP_OWNERS; }

    int stat(const char* path, Stat* st, bool follow)
    {
        const char* in;
        int err = 0;
        Fs* fs = route(path, &in, &err);
        return fs ? fs->stat(*in ? in : "/", st, follow) : err;
    }
    DirReader* open_dir(const char* path, int* err)
    {
        const char* in;
        *err = 0;
        Fs* fs = route(path, &in, err);
        return fs ? fs->open_dir(*in ? in : "/", err) : 0;
    }
    Stream* open(const char* path, int flags, unsigned mode, int* err)
    {
        const char* in;
        *err = 0;
        Fs* fs = route(path, &in, err);
        return fs ? fs->open(in, flags, mode, err) : 0;
    }
    int mkdir(const char* path, unsigned mode)
    {
        const char* in;
        int err = 0;
        Fs* fs = route(path, &in, &err);
        return fs ? fs->mkdir(in, mode) : err;
    }
    int remove(const char* path)
    {
        const char* in;
        int err = 0;
        Fs* fs = route(path, &in, &err);
        return fs ? fs->remove(in) : err;
    }
    int rename(const char* from, const char* to)
    {
        const char *a, *b;
        int err = 0;
        Fs* fa = route(from, &a, &err);
        if (!fa)
            return err;
        Fs* fb = route(to, &b, &err);
        if (!fb)
            return err;
        if (fa != fb)
            return EXDEV;
        return fa->rename(a, b);
    }
    int symlink(const char* target, const char* path)
    {
        const char* in;
        int err = 0;
        Fs* fs = route(path, &in, &err);
        return fs ? fs->symlink(target, in) : err;
    }
    int readlink(const char* path, char* buf, size_t n)
    {
        const char* in;
        int err = 0;
        Fs* fs = route(path, &in, &err);
        return fs ? fs->readlink(in, buf, n) : err;
    }
    int set_attrs(const char* path, const Attrs& a, int which)
    {
        const char* in;
        int err = 0;
        Fs* fs = route(path, &in, &err);
        return fs ? fs->set_attrs(in, a, which) : err;
    }
    int space(const char* path, Space* out)
    {
        const char* in;
        int err = 0;
        Fs* fs = route(path, &in, &err);
        return fs ? fs->space(*in ? in : "/", out) : err;
    }

private:
    Mount mounts_[k_mounts];
    long clock_;
};

} /* namespace */

Fs* vfs()
{
    static Vfs v;
    return &v;
}

bool is_local_path(const char* path)
{
    const Scheme* sc;
    const char* in;
    char* outer = (char*)malloc(strlen(path) + 1);
    bool local = true;
    if (outer) {
        local = !split(path, outer, strlen(path) + 1, &sc, &in);
        free(outer);
    }
    return local;
}

const char* archive_scheme(const char* name)
{
    static const struct {
        const char* suffix;
        const char* scheme;
    } k[] = {
        { ".tar.gz", "utar" }, { ".tgz", "utar" }, { ".tar.bz2", "utar" }, { ".tbz", "utar" },
        { ".tbz2", "utar" }, { ".tar.z", "utar" }, { ".taz", "utar" }, { ".tar", "utar" },
        { ".zip", "uzip" }, { ".jar", "uzip" }, { ".war", "uzip" }, { ".apk", "uzip" },
        { ".gz", "ugz" }, { ".bz2", "ubz2" }, { ".z", "uz" },
        { 0, 0 }
    };
    for (int i = 0; k[i].suffix; i++)
        if (ends_with(name, k[i].suffix, true))
            return k[i].scheme;
    return 0;
}

} /* namespace tf */
