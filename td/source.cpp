#include "source.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace td {

void Sources::add_dir(const char* dir)
{
    int i;
    for (i = 0; i < dirs_.size(); i++)
        if (!strcmp(dirs_[i].c_str(), dir))
            return;
    dirs_.push_back(ttr::Str(dir));
}

bool Sources::find(const char* recorded, ttr::Str* path) const
{
    if (!recorded || !*recorded)
        return false;
    if (access(recorded, R_OK) == 0) {
        *path = recorded;
        return true;
    }
    int i;
    for (i = 0; i < dirs_.size(); i++) {
        const char* tail = recorded;
        while (*tail == '/')
            tail++;
        while (*tail) {
            char p[4096];
            snprintf(p, sizeof(p), "%s/%s", dirs_[i].c_str(), tail);
            if (access(p, R_OK) == 0) {
                *path = p;
                return true;
            }
            const char* slash = strchr(tail, '/');
            if (!slash)
                break;
            tail = slash + 1;
        }
    }
    return false;
}

bool Sources::line(const char* recorded, int n, char* buf, size_t size) const
{
    ttr::Str path;
    if (!find(recorded, &path))
        return false;
    FILE* f = fopen(path.c_str(), "r");
    if (!f)
        return false;
    int at = 1;
    bool ok = false;
    while (fgets(buf, (int)size, f)) {
        if (at == n) {
            size_t l = strlen(buf);
            while (l && (buf[l - 1] == '\n' || buf[l - 1] == '\r'))
                buf[--l] = 0;
            ok = true;
            break;
        }
        if (strchr(buf, '\n'))
            at++;
    }
    fclose(f);
    return ok;
}

} /* namespace td */
