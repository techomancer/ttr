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
    const char* slash = strrchr(s, '/');
    Path p;
    if (!slash || slash == s)
        return p;                       /* "/" */
    p.set(s, (size_t)(slash - s));
    return p;
}

const char* Path::name() const
{
    const char* s = c_str();
    const char* slash = strrchr(s, '/');
    return slash ? slash + 1 : s;
}

Path Path::resolve(const Path& base, const char* p)
{
    /* Build the raw path, then normalize component by component. */
    const char* home = getenv("HOME");
    size_t pl = strlen(p);
    size_t cap = strlen(base.c_str()) + pl + (home ? strlen(home) : 0) + 4;
    char* raw = (char*)malloc(cap);
    char* out = (char*)malloc(cap);
    Path result;
    if (!raw || !out) {
        free(raw);
        free(out);
        return result;
    }
    if (p[0] == '/')
        strcpy(raw, p);
    else if (p[0] == '~' && (p[1] == 0 || p[1] == '/') && home)
        sprintf(raw, "%s%s", home, p + 1);
    else
        sprintf(raw, "%s/%s", base.c_str(), p);

    size_t o = 0;
    out[o++] = '/';
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
            while (o > 1 && out[o - 1] != '/')
                o--;
            if (o > 1)
                o--;                    /* the slash before it */
            if (o == 0)
                o = 1;
        } else {
            if (o > 1)
                out[o++] = '/';
            memcpy(out + o, q, n);
            o += n;
        }
        q += n;
    }
    out[o] = 0;
    result.set(out, o);
    free(raw);
    free(out);
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
