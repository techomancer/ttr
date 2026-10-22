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

/* Copy a stream into a new spool (an unlinked temporary file). */
Source* spool_stream(Stream* in, int* err);
/* If 'src' is gzip, bzip2 or compress data, a new spool holding it
 * decompressed (src is deleted); otherwise src itself. */
Source* decompress_source(Source* src, int* err);

} /* namespace tf */

#endif
