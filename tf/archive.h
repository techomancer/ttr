/*
 * tf/archive.h - archives and compressed files as read-only file systems
 * (ttrc/DESIGN.md section 5.3): tar (plain, gzip, bzip2, compress), zip
 * (stored and deflated, Zip64), and single gzip, bzip2 and compress files
 * shown as a directory holding the uncompressed file.
 *
 * An archive is read from a Source: the file itself when it is a plain
 * local file, otherwise a spool (an unlinked temporary file) holding its
 * bytes, decompressed where needed, so members can be read at any offset.
 */
#ifndef TF_ARCHIVE_H
#define TF_ARCHIVE_H

#include "tf.h"

namespace tf {

/* Bytes at an offset. */
class Source {
public:
    virtual ~Source() {}
    /* Bytes read (0 at the end), or -errno. */
    virtual long read_at(uint64_t off, void* buf, size_t n) = 0;
    virtual uint64_t size() const = 0;
};

/* A local file, or an unlinked temporary file (owns the descriptor). */
class FdSource : public Source {
public:
    explicit FdSource(int fd);
    ~FdSource();
    long read_at(uint64_t off, void* buf, size_t n);
    uint64_t size() const { return size_; }

private:
    int fd_;
    uint64_t size_;
};

enum ArchiveKind {
    ARCHIVE_TAR,        /* utar: tar, possibly compressed */
    ARCHIVE_ZIP,        /* uzip */
    ARCHIVE_SINGLE      /* ugz, ubz2, uz: one compressed file */
};

/* Open an archive read from 'data' (taken over). 'name' is the archive
 * file's name, for naming the single file of ugz/ubz2/uz; mtime is used
 * for directories the archive does not list. 0 and *err on failure. */
Fs* open_archive(ArchiveKind kind, Source* data, const char* name, long mtime, int* err);

/* Copy a stream of about 'size' bytes into a new spool: memory up to
 * 8 MB, beyond that an unlinked file in $TMPDIR (or /tmp). ENOSPC when
 * $TMPDIR has not room for it and a reserve (64 MB, or a twentieth of a
 * small file system), checked first when the size is known and every
 * 8 MB while writing. */
Source* spool_stream(Stream* in, uint64_t size, int* err);
/* If 'src' is gzip, bzip2 or compress data, a new spool holding it
 * decompressed (src is deleted); otherwise src itself. gzip records its
 * size, so its space is checked before starting. */
Source* decompress_source(Source* src, int* err);

/* Long work (reading or unpacking an archive) reports its progress here:
 * bytes done of total (0: unknown). Returning false stops it; the
 * opening then fails with ECANCELED. One hook for the program. */
typedef bool (*ProgressFn)(void* ctx, const char* what, uint64_t done, uint64_t total);
void set_progress(ProgressFn fn, void* ctx);
/* How much unpacked data stays in memory before going to a file. */
void set_spool_memory(uint64_t bytes);
/* What must stay free in $TMPDIR (0: 64 MB, or a twentieth of a small
 * file system). For tests. */
void set_spool_reserve(uint64_t bytes);

} /* namespace tf */

#endif
