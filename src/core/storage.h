/*
 * Removable storage (USB sticks, card readers): finding the volumes and
 * the commands to mount, unmount and eject them without root.
 *
 * ttr never runs as root, so it uses whatever unprivileged way the system
 * offers, per volume, in this order: an /etc/fstab entry with the user or
 * users option (mount is setuid), udisks2 (polkit lets the active session
 * mount), pmount, udevil. With none of these the volume cannot be mounted
 * from the desktop and the menu says what to install.
 *
 * Linux: /sys/block, the udev database (/run/udev/data) for filesystem and
 * label, /proc/self/mounts. IRIX gets its own storage_snapshot().
 */
#ifndef TTR_CORE_STORAGE_H
#define TTR_CORE_STORAGE_H

#include "core/str.h"
#include "core/vector.h"

namespace ttr {

enum MountMethod {
    MOUNT_NONE,
    MOUNT_FSTAB,    /* mount/umount DEV, a user entry in /etc/fstab */
    MOUNT_UDISKS,   /* udisksctl */
    MOUNT_PMOUNT,   /* pmount/pumount */
    MOUNT_UDEVIL    /* udevil */
};

struct StorageVolume {
    Str device;         /* /dev/sdf1 */
    Str disk;           /* /dev/sdf, for eject */
    Str model;          /* "SanDisk SDDR-C531 Reader" */
    Str label;          /* filesystem label, may be empty */
    Str fstype;         /* exfat, vfat, ext4, ...; may be empty */
    double bytes;
    Str mount_point;    /* empty if not mounted */
    MountMethod method;

    StorageVolume() : bytes(0), method(MOUNT_NONE) {}
};

/* Volumes with a filesystem on removable or USB disks. Returns false if
 * this system has no implementation. The caller owns the entries. */
bool storage_snapshot(Vector<StorageVolume*>* out);
void storage_free(Vector<StorageVolume*>* list);

/* "MYSTICK (sdf1, 119.1 GB)" or "SanDisk SDDR-C531 Reader (sdf1, ...)" */
Str storage_title(const StorageVolume& v);

/* Shell commands; empty if the volume's method cannot do it. */
Str storage_mount_command(const StorageVolume& v);
Str storage_unmount_command(const StorageVolume& v);
/* Unmount every mounted volume of v's disk, then power it off or eject
 * it. 'disk_volumes' are all volumes of the same disk. */
Str storage_eject_command(const StorageVolume& v,
                          const Vector<StorageVolume*>& disk_volumes);

/* Open a folder: xdg-open, else a known file manager. Empty if none. */
Str open_folder_command(const char* path);

}

#endif
