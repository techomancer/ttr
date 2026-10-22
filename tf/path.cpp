#include "path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace tf {

Path::Path() : s_(0)
{
    set("/", 1);
}

Path::Path(const char* s) : s_(0)
{
    set(s, strlen(s));
}

Path::Path(const Path& o) : s_(0)
{
    set(o.c_str(), strlen(o.c_str()));
}

Path& Path::operator=(const Path& o)
{
    if (this != &o)
        set(o.c_str(), strlen(o.c_str()));
    return *this;
}

Path::~Path()
{
    free(s_);
}

void Path::set(const char* s, size_t n)
{
    char* c = (char*)malloc(n + 1);
    if (!c)
        return;
    memcpy(c, s, n);
    c[n] = 0;
    free(s_);
    s_ = c;
}

bool Path::is_root() const
{
    return strcmp(c_str(), "/") == 0;
}

/* A component like "utar:" that starts an archive (mc's notation). */
static bool is_marker(const char* c, size_t n)
{
    static const char* const schemes[] = { "utar:", "uzip:", "ugz:", "ubz2:", "uz:", 0 };
    for (int i = 0; schemes[i]; i++)
        if (strlen(schemes[i]) == n && strncmp(c, schemes[i], n) == 0)
            return true;
    return false;
}

/* Where the last "/xxx://" marker of p starts, or 0. */
static const char* last_marker(const char* p)
{
    const char* found = 0;
    for (const char* s = strstr(p, "://"); s; s = strstr(s + 3, "://")) {
        const char* b = s;
        while (b > p && b[-1] != '/')
            b--;
        if (b > p && is_marker(b, (size_t)(s - b + 1)))
            found = b - 1;
    }
    return found;
}

bool Path::is_archive_root() const
{
    const char* s = c_str();
    size_t n = strlen(s);
    return n > 3 && strcmp(s + n - 3, "://") == 0 && last_marker(s) != 0;
}

Path Path::child(const char* name) const
{
    size_t a = strlen(c_str()), b = strlen(name);
    char* t = (char*)malloc(a + b + 2);
    Path p;
    if (!t)
        return p;
    memcpy(t, c_str(), a);
    size_t o = a;
    if (o == 0 || t[o - 1] != '/')
        t[o++] = '/';
    memcpy(t + o, name, b);
    t[o + b] = 0;
    p.set(t, o + b);
    free(t);
    return p;
}

Path Path::parent() const
{
    const char* s = c_str();
    Path p;
    if (is_archive_root()) {
        /* Out of the archive: the directory holding the archive file. */
        const char* m = last_marker(s);
        Path file;
        file.set(s, (size_t)(m - s));
        return file.parent();
    }
    const char* slash = strrchr(s, '/');
    if (!slash || slash == s)
        return p;                       /* "/" */
    /* Just inside an archive: its root. */
    if (slash >= s + 2 && slash[-1] == '/' && slash[-2] == ':') {
        p.set(s, (size_t)(slash - s + 1));
        return p;
    }
    p.set(s, (size_t)(slash - s));
    return p;
}

const char* Path::name() const
{
    const char* s = c_str();
    if (is_archive_root()) {
        /* The archive file's own name. */
        const char* m = last_marker(s);
        const char* b = m;
        while (b > s && b[-1] != '/')
            b--;
        static char buf[1024];
        size_t n = (size_t)(m - b);
        if (n >= sizeof(buf))
            n = sizeof(buf) - 1;
        memcpy(buf, b, n);
        buf[n] = 0;
        return buf;
    }
    const char* slash = strrchr(s, '/');
    return slash ? slash + 1 : s;
}

Path Path::resolve(const Path& base, const char* p)
{
    /* Build the raw path, then normalize component by component; an
     * archive marker ("utar:") is followed by "//" in the result, and
     * ".." right after one leaves the archive. */
    const char* home = getenv("HOME");
    size_t pl = strlen(p);
    size_t cap = strlen(base.c_str()) + pl + (home ? strlen(home) : 0) + 8;
    char* raw = (char*)malloc(cap);
    char* out = (char*)malloc(cap);
    size_t* starts = (size_t*)malloc(cap * sizeof(size_t));
    bool* marks = (bool*)malloc(cap * sizeof(bool));
    Path result;
    if (!raw || !out || !starts || !marks) {
        free(raw);
        free(out);
        free(starts);
        free(marks);
        return result;
    }
    if (p[0] == '/')
        strcpy(raw, p);
    else if (p[0] == '~' && (p[1] == 0 || p[1] == '/') && home)
        sprintf(raw, "%s%s", home, p + 1);
    else
        sprintf(raw, "%s/%s", base.c_str(), p);

    size_t o = 0;
    int depth = 0;                  /* components in out */
    const char* q = raw;
    while (*q) {
        while (*q == '/')
            q++;
        if (!*q)
            break;
        const char* e = strchr(q, '/');
        size_t n = e ? (size_t)(e - q) : strlen(q);
        if (n == 1 && q[0] == '.') {
            /* nothing */
        } else if (n == 2 && q[0] == '.' && q[1] == '.') {
            if (depth > 0) {
                depth--;
                o = starts[depth];
                /* At an archive's root: out of it, past its file too. */
                if (marks[depth] && depth > 0) {
                    depth--;
                    o = starts[depth];
                }
            }
        } else {
            starts[depth] = o;
            bool m = is_marker(q, n);
            marks[depth] = m;
            depth++;
            out[o++] = '/';
            memcpy(out + o, q, n);
            o += n;
            if (m) {
                out[o++] = '/';
                out[o++] = '/';
            }
        }
        q += n;
    }
    if (o == 0)
        out[o++] = '/';
    out[o] = 0;
    /* "/x/utar:///y" -> "/x/utar://y": a marker's slashes are its own. */
    char* fixed = (char*)malloc(o + 1);
    size_t f = 0;
    for (size_t i = 0; i < o; i++) {
        if (out[i] == '/' && i >= 3 && out[i - 1] == '/' && out[i - 2] == '/' && out[i - 3] == ':')
            continue;
        fixed[f++] = out[i];
    }
    fixed[f] = 0;
    result.set(fixed, f);
    free(fixed);
    free(raw);
    free(out);
    free(starts);
    free(marks);
    return result;
}

Path Path::cwd()
{
    char buf[4096];
    if (getcwd(buf, sizeof(buf)))
        return Path(buf);
    const char* pwd = getenv("PWD");
    return Path(pwd && pwd[0] == '/' ? pwd : "/");
}

} /* namespace tf */
