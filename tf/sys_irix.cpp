/* tf/sys_irix.cpp - IRIX: statvfs names the file system type itself. */
#include "sys.h"

#include <errno.h>
#include <string.h>
#include <sys/statvfs.h>

namespace tf {

int sys_space(const char* path, Space* out)
{
    struct statvfs v;
    if (statvfs(path, &v) != 0)
        return errno;
    uint64_t unit = v.f_frsize ? (uint64_t)v.f_frsize : (uint64_t)v.f_bsize;
    out->total = (uint64_t)v.f_blocks * unit;
    out->free = (uint64_t)v.f_bfree * unit;
    out->avail = (uint64_t)v.f_bavail * unit;
    strncpy(out->type, v.f_basetype, sizeof(out->type) - 1);
    out->type[sizeof(out->type) - 1] = 0;
    return 0;
}

} /* namespace tf */
