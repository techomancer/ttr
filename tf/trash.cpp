/*
 * tf/trash.cpp - the freedesktop.org trash (trash.h).
 */
#include "trash.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

namespace tf {

namespace {

/* mkdir -p, the last component with 'mode'. */
bool make_dirs(const char* path, unsigned mode)
{
    char p[4096];
    snprintf(p, sizeof(p), "%s", path);
    for (char* c = p + 1; *c; c++) {
        if (*c != '/')
            continue;
        *c = 0;
        mkdir(p, 0755);
        *c = '/';
    }
    struct stat st;
    if (mkdir(p, (mode_t)mode) != 0 && errno != EEXIST)
        return false;
    return lstat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

/* A trash directory with files/ and info/ under it, made if 'make'. */
bool trash_dirs(const char* dir, bool make)
{
    char f[4200], i[4200];
    snprintf(f, sizeof(f), "%s/files", dir);
    snprintf(i, sizeof(i), "%s/info", dir);
    if (make)
        return make_dirs(dir, 0700) && make_dirs(f, 0700) && make_dirs(i, 0700);
    struct stat st;
    return lstat(f, &st) == 0 && S_ISDIR(st.st_mode) && lstat(i, &st) == 0 &&
           S_ISDIR(st.st_mode);
}

/* The mount point of the file system holding path: the highest
 * directory above it on the same device. */
void top_dir(const char* path, dev_t dev, char* out, size_t n)
{
    char p[4096];
    snprintf(p, sizeof(p), "%s", path);
    char* slash = strrchr(p, '/');
    if (slash)
        *slash = 0;
    while (p[0]) {
        char up[4096];
        snprintf(up, sizeof(up), "%s", p);
        char* s = strrchr(up, '/');
        if (!s)
            break;
        if (s == up)
            s[1] = 0;
        else
            *s = 0;
        struct stat st;
        if (stat(up, &st) != 0 || st.st_dev != dev)
            break;
        if (strcmp(up, "/") == 0) {
            p[0] = 0;
            break;
        }
        snprintf(p, sizeof(p), "%s", up);
    }
    snprintf(out, n, "%s", p[0] ? p : "/");
}

/* The trash for a file on another file system than the home trash's:
 * $topdir/.Trash/$uid if the administrator made .Trash (a directory,
 * sticky, not a link), else $topdir/.Trash-$uid. */
bool top_trash(const char* top, char* out, size_t n)
{
    char t[4200];
    snprintf(t, sizeof(t), "%s/.Trash", strcmp(top, "/") == 0 ? "" : top);
    struct stat st;
    if (lstat(t, &st) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & S_ISVTX)) {
        snprintf(out, n, "%s/%ld", t, (long)getuid());
        if (trash_dirs(out, true))
            return true;
    }
    snprintf(out, n, "%s/.Trash-%ld", strcmp(top, "/") == 0 ? "" : top, (long)getuid());
    if (lstat(out, &st) == 0 && (!S_ISDIR(st.st_mode) || st.st_uid != getuid()))
        return false;
    return trash_dirs(out, true);
}

/* RFC 2396 escapes, '/' kept, as the spec's Path= wants. */
void escape(const char* s, char* out, size_t n)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (; *s && o + 4 < n; s++) {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            strchr("/-_.!~*'()", c)) {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }
    out[o] = 0;
}

} /* namespace */

void trash_home(char* out, size_t n)
{
    const char* data = getenv("XDG_DATA_HOME");
    const char* home = getenv("HOME");
    if (data && data[0] == '/')
        snprintf(out, n, "%s/Trash", data);
    else
        snprintf(out, n, "%s/.local/share/Trash", home && *home ? home : "");
}

int trash(const char* path, char* where, size_t wn)
{
    struct stat st;
    if (!path || path[0] != '/')
        return EINVAL;
    if (lstat(path, &st) != 0)
        return errno;

    /* Which trash: the home one if the file is on its file system. */
    char dir[4200];
    trash_home(dir, sizeof(dir));
    if (!dir[0] || dir[0] != '/')
        return EXDEV;
    /* Never the trash itself, or a folder holding it. */
    size_t pl = strlen(path);
    if (strncmp(dir, path, pl) == 0 && (dir[pl] == 0 || dir[pl] == '/'))
        return EINVAL;
    const char* rel = path;     /* Path= in the info file */
    struct stat ts;
    bool home_ok = trash_dirs(dir, true) && stat(dir, &ts) == 0 && ts.st_dev == st.st_dev;
    char top[4096];
    if (!home_ok) {
        top_dir(path, st.st_dev, top, sizeof(top));
        if (!top_trash(top, dir, sizeof(dir)))
            return EXDEV;
        if (stat(dir, &ts) != 0 || ts.st_dev != st.st_dev)
            return EXDEV;
        /* Relative to the top, so the medium can move. */
        size_t tl = strcmp(top, "/") == 0 ? 0 : strlen(top);
        if (strncmp(path, top, tl) == 0 && path[tl] == '/')
            rel = path + tl + 1;
    }

    /* A free name: the info file is made first (O_EXCL), which claims
     * it; "a.jpg", then "a.2.jpg", "a.3.jpg"... */
    const char* base = strrchr(path, '/') + 1;
    if (!*base)
        return EINVAL;
    const char* ext = strrchr(base, '.');
    if (!ext || ext == base)
        ext = base + strlen(base);
    char name[1100], info[5400], dest[5400];
    int fd = -1;
    for (int i = 1; i < 10000 && fd < 0; i++) {
        if (i == 1)
            snprintf(name, sizeof(name), "%.1000s", base);
        else
            snprintf(name, sizeof(name), "%.*s.%d%.40s", (int)(ext - base > 900 ? 900 : ext - base),
                     base, i, ext);
        snprintf(info, sizeof(info), "%s/info/%s.trashinfo", dir, name);
        snprintf(dest, sizeof(dest), "%s/files/%s", dir, name);
        struct stat x;
        if (lstat(dest, &x) == 0)
            continue;
        fd = open(info, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd < 0 && errno != EEXIST)
            return errno;
    }
    if (fd < 0)
        return EEXIST;

    char esc[13000], text[13200], date[40];
    escape(rel, esc, sizeof(esc));
    time_t now = time(0);
    struct tm* tm = localtime(&now);
    strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", tm);
    int len = snprintf(text, sizeof(text), "[Trash Info]\nPath=%s\nDeletionDate=%s\n", esc, date);
    bool wrote = write(fd, text, (size_t)len) == len;
    if (close(fd) != 0)
        wrote = false;
    if (!wrote) {
        int e = errno ? errno : EIO;
        unlink(info);
        return e;
    }
    if (rename(path, dest) != 0) {
        int e = errno;
        unlink(info);
        return e == EXDEV ? EXDEV : e;
    }
    if (where)
        snprintf(where, wn, "%s", dest);
    return 0;
}

} /* namespace tf */
