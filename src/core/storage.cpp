#include "core/storage.h"
#include "core/audio.h"     /* in_path() */
#include "core/net.h"       /* format_bytes() */

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ttr {

void storage_free(Vector<StorageVolume*>* list)
{
    int i;
    for (i = 0; i < list->size(); i++)
        delete (*list)[i];
    list->clear();
}

Str shell_quote(const char* s)
{
    int n = (int)strlen(s), extra = 0, i;
    for (i = 0; i < n; i++)
        if (s[i] == '\'')
            extra += 3;
    char* out = new char[n + extra + 3];
    int o = 0;
    out[o++] = '\'';
    for (i = 0; i < n; i++) {
        if (s[i] == '\'') {
            out[o++] = '\'';
            out[o++] = '\\';
            out[o++] = '\'';
            out[o++] = '\'';
        } else {
            out[o++] = s[i];
        }
    }
    out[o++] = '\'';
    out[o] = 0;
    Str r(out);
    delete [] out;
    return r;
}

Str storage_title(const StorageVolume& v)
{
    const char* dev = strrchr(v.device.c_str(), '/');
    dev = dev ? dev + 1 : v.device.c_str();
    Str size = format_bytes(v.bytes);
    char buf[512];
    const char* name = !v.label.empty() ? v.label.c_str()
                     : !v.model.empty() ? v.model.c_str() : "Removable disk";
    sprintf(buf, "%.200s (%.32s, %.32s)", name, dev, size.c_str());
    return Str(buf);
}

#if defined(__linux__)

static bool read_line(const char* path, char* buf, int size)
{
    FILE* f = fopen(path, "r");
    if (!f)
        return false;
    bool ok = fgets(buf, size, f) != 0;
    fclose(f);
    if (!ok)
        return false;
    int n = (int)strlen(buf);
    while (n > 0 && isspace((unsigned char)buf[n - 1]))
        buf[--n] = 0;
    return true;
}

/* Values from udev's database for a block device: ID_FS_TYPE, ...
 * The file is keyed by major:minor, from /sys/class/block/X/dev. */
static void udev_props(const char* name, Str* fstype, Str* label)
{
    char path[256], dev[32];
    sprintf(path, "/sys/class/block/%.64s/dev", name);
    if (!read_line(path, dev, sizeof(dev)))
        return;
    sprintf(path, "/run/udev/data/b%.30s", dev);
    FILE* f = fopen(path, "r");
    if (!f)
        return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        int n = (int)strlen(line);
        while (n > 0 && isspace((unsigned char)line[n - 1]))
            line[--n] = 0;
        if (strncmp(line, "E:ID_FS_TYPE=", 13) == 0)
            *fstype = line + 13;
        else if (strncmp(line, "E:ID_FS_LABEL=", 14) == 0)
            *label = line + 14;
    }
    fclose(f);
}

/* /proc/self/mounts escapes blanks as \040 and so on. */
static void unescape_mount(char* s)
{
    char* o = s;
    while (*s) {
        if (s[0] == '\\' && isdigit((unsigned char)s[1]) &&
            isdigit((unsigned char)s[2]) && isdigit((unsigned char)s[3])) {
            *o++ = (char)((s[1] - '0') * 64 + (s[2] - '0') * 8 + (s[3] - '0'));
            s += 4;
        } else {
            *o++ = *s++;
        }
    }
    *o = 0;
}

static Str mount_point_of(const char* device)
{
    FILE* f = fopen("/proc/self/mounts", "r");
    if (!f)
        return Str("");
    char dev[512], dir[1024];
    Str result("");
    while (fscanf(f, "%511s %1023s %*[^\n]", dev, dir) == 2) {
        unescape_mount(dev);
        if (strcmp(dev, device) == 0) {
            unescape_mount(dir);
            result = dir;
            break;
        }
    }
    fclose(f);
    return result;
}

/* An /etc/fstab line for the device (by path, LABEL= or UUID=, without
 * resolving symlinks) whose options include user or users. */
static bool fstab_user_entry(const char* device, const char* name)
{
    FILE* f = fopen("/etc/fstab", "r");
    if (!f)
        return false;
    char spec[512], dir[512], type[64], opts[512];
    bool found = false;
    char line[2048];
    while (!found && fgets(line, sizeof(line), f)) {
        if (line[0] == '#' ||
            sscanf(line, "%511s %511s %63s %511s", spec, dir, type, opts) != 4)
            continue;
        bool match = strcmp(spec, device) == 0;
        char link[600];
        link[0] = 0;
        if (strncmp(spec, "/dev/disk/", 10) == 0)
            strcpy(link, spec);
        else if (strncmp(spec, "UUID=", 5) == 0)
            sprintf(link, "/dev/disk/by-uuid/%.500s", spec + 5);
        else if (strncmp(spec, "LABEL=", 6) == 0)
            sprintf(link, "/dev/disk/by-label/%.500s", spec + 6);
        if (!match && link[0]) {
            char target[512];
            ssize_t n = readlink(link, target, sizeof(target) - 1);
            if (n > 0) {
                target[n] = 0;
                const char* base = strrchr(target, '/');
                match = base && strcmp(base + 1, name) == 0;
            }
        }
        if (!match)
            continue;
        char* opt = strtok(opts, ",");
        while (opt) {
            if (strcmp(opt, "user") == 0 || strcmp(opt, "users") == 0)
                found = true;
            opt = strtok(0, ",");
        }
    }
    fclose(f);
    return found;
}

static MountMethod choose_method(const char* device, const char* name)
{
    if (fstab_user_entry(device, name))
        return MOUNT_FSTAB;
    if (in_path("udisksctl"))
        return MOUNT_UDISKS;
    if (in_path("pmount"))
        return MOUNT_PMOUNT;
    if (in_path("udevil"))
        return MOUNT_UDEVIL;
    return MOUNT_NONE;
}

static double sectors_bytes(const char* name)
{
    char path[256], buf[64];
    sprintf(path, "/sys/class/block/%.64s/size", name);
    return read_line(path, buf, sizeof(buf)) ? atof(buf) * 512.0 : 0;
}

static void add_volume(Vector<StorageVolume*>* out, const char* disk,
                       const char* part, const Str& model)
{
    Str fstype, label;
    udev_props(part, &fstype, &label);
    /* Only things with a filesystem we could mount; skip swap, LVM, ... */
    if (fstype.empty() || strcmp(fstype.c_str(), "swap") == 0 ||
        strstr(fstype.c_str(), "_member") || strcmp(fstype.c_str(), "LVM2") == 0)
        return;
    double bytes = sectors_bytes(part);
    if (bytes <= 0)
        return;
    StorageVolume* v = new StorageVolume;
    char buf[128];
    sprintf(buf, "/dev/%.64s", part);
    v->device = buf;
    sprintf(buf, "/dev/%.64s", disk);
    v->disk = buf;
    v->model = model;
    v->label = label;
    v->fstype = fstype;
    v->bytes = bytes;
    v->mount_point = mount_point_of(v->device.c_str());
    v->method = choose_method(v->device.c_str(), part);
    out->push_back(v);
}

bool storage_snapshot(Vector<StorageVolume*>* out)
{
    DIR* d = opendir("/sys/block");
    if (!d)
        return true;
    struct dirent* e;
    while ((e = readdir(d)) != 0) {
        const char* disk = e->d_name;
        if (disk[0] == '.' || strncmp(disk, "loop", 4) == 0 ||
            strncmp(disk, "ram", 3) == 0)
            continue;
        char path[512], buf[256];
        sprintf(path, "/sys/block/%.64s/removable", disk);
        bool removable = read_line(path, buf, sizeof(buf)) &&
                         strcmp(buf, "1") == 0;
        /* USB disks often say they are not removable; their path tells. */
        sprintf(path, "/sys/block/%.64s", disk);
        char target[1024];
        ssize_t n = readlink(path, target, sizeof(target) - 1);
        bool usb = false;
        if (n > 0) {
            target[n] = 0;
            usb = strstr(target, "/usb") != 0;
        }
        if (!removable && !usb)
            continue;

        char vendor[128] = "", model[128] = "";
        sprintf(path, "/sys/block/%.64s/device/vendor", disk);
        read_line(path, vendor, sizeof(vendor));
        sprintf(path, "/sys/block/%.64s/device/model", disk);
        read_line(path, model, sizeof(model));
        char name[300];
        sprintf(name, "%s%s%s", vendor, vendor[0] && model[0] ? " " : "",
                model);

        /* Partitions are subdirectories named after the disk: sdf1. */
        bool any = false;
        sprintf(path, "/sys/block/%.64s", disk);
        DIR* pd = opendir(path);
        if (pd) {
            struct dirent* pe;
            int len = (int)strlen(disk);
            while ((pe = readdir(pd)) != 0) {
                if (strncmp(pe->d_name, disk, len) != 0 ||
                    !pe->d_name[len])
                    continue;
                add_volume(out, disk, pe->d_name, Str(name));
                any = true;
            }
            closedir(pd);
        }
        if (!any)   /* a filesystem on the whole disk */
            add_volume(out, disk, disk, Str(name));
    }
    closedir(d);
    return true;
}

#else

bool storage_snapshot(Vector<StorageVolume*>* out)
{
    (void)out;
    return false;
}

#endif

Str storage_mount_command(const StorageVolume& v)
{
    Str dev = shell_quote(v.device.c_str());
    char buf[600];
    switch (v.method) {
    case MOUNT_FSTAB:
        sprintf(buf, "mount %.500s", dev.c_str());
        break;
    case MOUNT_UDISKS:
        sprintf(buf, "udisksctl mount --no-user-interaction -b %.500s",
                dev.c_str());
        break;
    case MOUNT_PMOUNT:
        sprintf(buf, "pmount %.500s", dev.c_str());
        break;
    case MOUNT_UDEVIL:
        sprintf(buf, "udevil mount %.500s", dev.c_str());
        break;
    case MOUNT_NONE:
        return Str("");
    }
    return Str(buf);
}

Str storage_unmount_command(const StorageVolume& v)
{
    Str dev = shell_quote(v.device.c_str());
    char buf[600];
    switch (v.method) {
    case MOUNT_FSTAB:
        sprintf(buf, "umount %.500s", dev.c_str());
        break;
    case MOUNT_UDISKS:
        sprintf(buf, "udisksctl unmount --no-user-interaction -b %.500s",
                dev.c_str());
        break;
    case MOUNT_PMOUNT:
        sprintf(buf, "pumount %.500s", dev.c_str());
        break;
    case MOUNT_UDEVIL:
        sprintf(buf, "udevil umount %.500s", dev.c_str());
        break;
    case MOUNT_NONE:
        return Str("");
    }
    return Str(buf);
}

Str storage_eject_command(const StorageVolume& v,
                          const Vector<StorageVolume*>& disk_volumes)
{
    Str cmd("");
    int i;
    char buf[4096];
    buf[0] = 0;
    for (i = 0; i < disk_volumes.size(); i++) {
        const StorageVolume* p = disk_volumes[i];
        if (p->mount_point.empty())
            continue;
        Str u = storage_unmount_command(*p);
        if (u.empty() || strlen(buf) + u.length() + 8 > sizeof(buf) - 200)
            continue;
        strcat(buf, u.c_str());
        strcat(buf, " && ");
    }
    Str disk = shell_quote(v.disk.c_str());
    if (v.method == MOUNT_UDISKS)
        sprintf(buf + strlen(buf),
                "udisksctl power-off --no-user-interaction -b %.150s",
                disk.c_str());
    else if (in_path("eject"))
        sprintf(buf + strlen(buf), "eject %.150s", disk.c_str());
    else
        strcat(buf, "true");    /* unmounted; safe to pull out */
    cmd = buf;
    return cmd;
}

Str open_folder_command(const char* path)
{
    static const char* const managers[] = {
        "xdg-open", "thunar", "pcmanfm", "pcmanfm-qt", "nautilus", "dolphin",
        "caja", "nemo", "spacefm", "rox", 0
    };
    Str q = shell_quote(path);
    int i;
    for (i = 0; managers[i]; i++) {
        if (in_path(managers[i])) {
            char buf[1200];
            sprintf(buf, "%s %.1100s", managers[i], q.c_str());
            return Str(buf);
        }
    }
    return Str("");
}

}
