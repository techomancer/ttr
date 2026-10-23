#include "file.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace te {

int load_file(Document& doc, const char* path, time_t* mtime)
{
    FILE* f = fopen(path, "rb");
    if (!f) {
        int e = errno;
        doc.set_text("", 0);
        if (mtime)
            *mtime = 0;
        return e;
    }
    size_t cap = 65536, len = 0;
    char* buf = (char*)malloc(cap);
    int err = 0;
    while (buf) {
        if (len == cap) {
            char* nb = (char*)realloc(buf, cap * 2);
            if (!nb) {
                err = ENOMEM;
                break;
            }
            buf = nb;
            cap *= 2;
        }
        size_t r = fread(buf + len, 1, cap - len, f);
        if (r == 0) {
            if (ferror(f))
                err = errno ? errno : EIO;
            break;
        }
        len += r;
    }
    fclose(f);
    if (!buf)
        err = ENOMEM;
    doc.set_text(buf ? buf : "", buf ? len : 0);
    free(buf);
    struct stat st;
    if (mtime)
        *mtime = stat(path, &st) == 0 ? st.st_mtime : 0;
    return err;
}

int save_file(Document& doc, const char* path, time_t* mtime)
{
    size_t n;
    char* t = doc.text(&n);
    if (!t)
        return ENOMEM;
    struct stat st, lst;
    bool exists = stat(path, &st) == 0;
    bool link = lstat(path, &lst) == 0 && S_ISLNK(lst.st_mode);
    char tmp[2100];
    bool direct = link || strlen(path) > 2000;
    if (!direct)
        sprintf(tmp, "%s.te-%ld", path, (long)getpid());
    FILE* f = fopen(direct ? path : tmp, "wb");
    if (!f && !direct) {
        /* No right to make files in the directory: write in place. */
        direct = true;
        f = fopen(path, "wb");
    }
    if (!f) {
        int e = errno;
        free(t);
        return e;
    }
    int err = 0;
    if (n && fwrite(t, 1, n, f) != n)
        err = errno ? errno : EIO;
    if (fflush(f) != 0 && !err)
        err = errno ? errno : EIO;
    if (fclose(f) != 0 && !err)
        err = errno ? errno : EIO;
    free(t);
    if (!direct) {
        if (!err && exists)
            chmod(tmp, st.st_mode & 07777);
        if (!err && rename(tmp, path) != 0)
            err = errno;
        if (err)
            unlink(tmp);
    }
    if (!err) {
        if (mtime)
            *mtime = stat(path, &st) == 0 ? st.st_mtime : 0;
        doc.mark_saved();
    }
    return err;
}

} /* namespace te */
