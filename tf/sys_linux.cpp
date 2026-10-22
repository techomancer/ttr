/* tf/sys_linux.cpp - Linux: statvfs for sizes, statfs's magic number for
 * the file system type. */
#include "sys.h"

#include <errno.h>
#include <string.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>

namespace tf {

namespace {

struct Magic {
    unsigned long magic;
    const char* name;
};

const Magic k_magics[] = {
    { 0xef53, "ext4" },          /* ext2/3/4 share it */
    { 0x58465342, "xfs" },
    { 0x9123683e, "btrfs" },
    { 0x2fc12fc1, "zfs" },
    { 0x6969, "nfs" },
    { 0x01021994, "tmpfs" },
    { 0x4d44, "vfat" },
    { 0x5346544e, "ntfs" },
    { 0x7366746e, "ntfs3" },
    { 0x65735546, "fuse" },
    { 0xff534d42, "cifs" },
    { 0x9fa0, "proc" },
    { 0x62656572, "sysfs" },
    { 0x1373, "devfs" },
    { 0x794c7630, "overlay" },
    { 0x9660, "iso9660" },
    { 0xf2f52010, "f2fs" },
    { 0, 0 }
};

} /* namespace */

int sys_space(const char* path, Space* out)
{
    struct statvfs v;
    if (statvfs(path, &v) != 0)
        return errno;
    uint64_t unit = v.f_frsize ? (uint64_t)v.f_frsize : (uint64_t)v.f_bsize;
    out->total = (uint64_t)v.f_blocks * unit;
    out->free = (uint64_t)v.f_bfree * unit;
    out->avail = (uint64_t)v.f_bavail * unit;
    strcpy(out->type, "?");
    struct statfs f;
    if (statfs(path, &f) == 0) {
        for (int i = 0; k_magics[i].name; i++)
            if ((unsigned long)f.f_type == k_magics[i].magic) {
                strcpy(out->type, k_magics[i].name);
                break;
            }
    }
    return 0;
}

} /* namespace tf */
