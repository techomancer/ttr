/* tf/local.cpp - the local file system, through POSIX calls. The few
 * things that differ per system are in sys_*.cpp (sys.h). */
#include "tf.h"
#include "sys.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>

namespace tf {

namespace {

FileType type_of(mode_t m)
{
    if (S_ISREG(m))
        return FT_FILE;
    if (S_ISDIR(m))
        return FT_DIR;
    if (S_ISLNK(m))
        return FT_LINK;
    if (S_ISCHR(m))
        return FT_CHAR;
    if (S_ISBLK(m))
        return FT_BLOCK;
    if (S_ISFIFO(m))
        return FT_FIFO;
#if defined(S_ISSOCK)
    if (S_ISSOCK(m))
        return FT_SOCKET;
#endif
    return FT_OTHER;
}

class LocalDir : public DirReader {
public:
    explicit LocalDir(DIR* d) : d_(d) {}
    ~LocalDir() { closedir(d_); }
    const char* next()
    {
        for (;;) {
            struct dirent* e = readdir(d_);
            if (!e)
                return 0;
            const char* n = e->d_name;
            if (n[0] == '.' && (n[1] == 0 || (n[1] == '.' && n[2] == 0)))
                continue;
            return n;
        }
    }

private:
    DIR* d_;
};

class LocalStream : public Stream {
public:
    explicit LocalStream(int fd) : fd_(fd) {}
    ~LocalStream()
    {
        if (fd_ >= 0)
            ::close(fd_);
    }
    long read(void* buf, size_t n)
    {
        for (;;) {
            ssize_t r = ::read(fd_, buf, n);
            if (r < 0 && errno == EINTR)
                continue;
            return r < 0 ? -errno : (long)r;
        }
    }
    long write(const void* buf, size_t n)
    {
        size_t off = 0;
        while (off < n) {
            ssize_t w = ::write(fd_, (const char*)buf + off, n - off);
            if (w < 0) {
                if (errno == EINTR)
                    continue;
                return -errno;
            }
            off += (size_t)w;
        }
        return (long)n;
    }
    int seek(uint64_t off)
    {
        return lseek(fd_, (off_t)off, SEEK_SET) == (off_t)-1 ? errno : 0;
    }
    int close()
    {
        int r = ::close(fd_) == 0 ? 0 : errno;
        fd_ = -1;
        return r;
    }

private:
    int fd_;
};

class LocalFs : public Fs {
public:
    int caps() const { return CAP_WRITE | CAP_SEEK | CAP_SYMLINKS | CAP_OWNERS; }

    int stat(const char* path, Stat* st, bool follow)
    {
        struct stat sb;
        int r = follow ? ::stat(path, &sb) : ::lstat(path, &sb);
        if (r != 0)
            return errno;
        st->type = type_of(sb.st_mode);
        st->mode = (unsigned)(sb.st_mode & 07777);
        st->size = (uint64_t)sb.st_size;
        st->mtime = (long)sb.st_mtime;
        st->atime = (long)sb.st_atime;
        st->ctime = (long)sb.st_ctime;
        st->uid = (long)sb.st_uid;
        st->gid = (long)sb.st_gid;
        st->nlink = (unsigned long)sb.st_nlink;
        st->dev = (uint64_t)sb.st_dev;
        st->ino = (uint64_t)sb.st_ino;
        st->rdev = (uint64_t)sb.st_rdev;
        return 0;
    }

    DirReader* open_dir(const char* path, int* err)
    {
        DIR* d = opendir(path);
        if (!d) {
            *err = errno;
            return 0;
        }
        *err = 0;
        return new LocalDir(d);
    }

    Stream* open(const char* path, int flags, unsigned mode, int* err)
    {
        int f = (flags & OPEN_WRITE) ? ((flags & OPEN_READ) ? O_RDWR : O_WRONLY) : O_RDONLY;
        if (flags & OPEN_CREATE)
            f |= O_CREAT;
        if (flags & OPEN_TRUNCATE)
            f |= O_TRUNC;
        if (flags & OPEN_EXCLUSIVE)
            f |= O_EXCL;
        int fd = ::open(path, f, (mode_t)mode);
        if (fd < 0) {
            *err = errno;
            return 0;
        }
        fcntl(fd, F_SETFD, FD_CLOEXEC);
        *err = 0;
        return new LocalStream(fd);
    }

    int mkdir(const char* path, unsigned mode)
    {
        return ::mkdir(path, (mode_t)mode) == 0 ? 0 : errno;
    }

    int remove(const char* path)
    {
        struct stat sb;
        if (::lstat(path, &sb) != 0)
            return errno;
        int r = S_ISDIR(sb.st_mode) ? ::rmdir(path) : ::unlink(path);
        return r == 0 ? 0 : errno;
    }

    int rename(const char* from, const char* to)
    {
        return ::rename(from, to) == 0 ? 0 : errno;
    }

    int symlink(const char* target, const char* path)
    {
        return ::symlink(target, path) == 0 ? 0 : errno;
    }

    int readlink(const char* path, char* buf, size_t n)
    {
        if (n == 0)
            return EINVAL;
        ssize_t r = ::readlink(path, buf, n - 1);
        if (r < 0) {
            buf[0] = 0;
            return errno;
        }
        buf[r] = 0;
        return 0;
    }

    int set_attrs(const char* path, const Attrs& a, int which)
    {
        if ((which & ATTR_OWNER) && lchown(path, (uid_t)a.uid, (gid_t)a.gid) != 0)
            return errno;
        if ((which & ATTR_MODE) && chmod(path, (mode_t)a.mode) != 0)
            return errno;
        if (which & ATTR_TIMES) {
            struct utimbuf ut;
            ut.actime = (time_t)a.atime;
            ut.modtime = (time_t)a.mtime;
            if (utime(path, &ut) != 0)
                return errno;
        }
        return 0;
    }

    int space(const char* path, Space* out)
    {
        return sys_space(path, out);
    }
};

} /* namespace */

Fs* local_fs()
{
    static LocalFs fs;
    return &fs;
}

} /* namespace tf */
