#include "archive.h"

#include "../third_party/zlib/zlib.h"
#define BZ_NO_STDIO
#include "../third_party/bzip2/bzlib.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

namespace tf {

/* ------------------------------------------------------------------ */
/* Sources and spools                                                  */

FdSource::FdSource(int fd) : fd_(fd), size_(0)
{
    struct stat sb;
    if (fstat(fd, &sb) == 0)
        size_ = (uint64_t)sb.st_size;
}

FdSource::~FdSource()
{
    if (fd_ >= 0)
        close(fd_);
}

long FdSource::read_at(uint64_t off, void* buf, size_t n)
{
    if (lseek(fd_, (off_t)off, SEEK_SET) == (off_t)-1)
        return -errno;
    for (;;) {
        ssize_t r = read(fd_, buf, n);
        if (r < 0 && errno == EINTR)
            continue;
        return r < 0 ? -errno : (long)r;
    }
}

namespace {

#ifndef ECANCELED
#define ECANCELED EINTR
#endif

ProgressFn g_progress = 0;
void* g_progress_ctx = 0;
const char* g_progress_what = "";

/* Report progress (at most every 100 ms); false: the user stopped it. */
bool report(uint64_t done, uint64_t total)
{
    static long last = 0;
    if (!g_progress)
        return true;
    struct timeval tv;
    gettimeofday(&tv, 0);
    long now = (long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    if (now - last < 100 && done < total)
        return true;
    last = now;
    return g_progress(g_progress_ctx, g_progress_what, done, total);
}

const char* tmp_dir()
{
    const char* dir = getenv("TMPDIR");
    if (!dir || !*dir || strlen(dir) > 900)
        dir = "/tmp";
    return dir;
}

/* What must stay free in the temporary directory: 64 MB, or a twentieth
 * of a small file system. */
uint64_t g_reserve = 0;      /* set_spool_reserve(); 0: the rule below */

uint64_t reserve_of(const Space& sp)
{
    if (g_reserve)
        return g_reserve;
    uint64_t r = (uint64_t)64 << 20;
    if (sp.total / 20 < r)
        r = sp.total / 20;
    return r;
}

/* An unlinked temporary file: gone when its descriptor closes. */
int make_spool(int* err)
{
    char path[1024];
    sprintf(path, "%s/ttrcXXXXXX", tmp_dir());
    int fd = mkstemp(path);
    if (fd < 0) {
        *err = errno;
        return -1;
    }
    unlink(path);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    return fd;
}

bool write_all(int fd, const void* data, size_t n, int* err)
{
    const char* p = (const char*)data;
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            *err = errno;
            return false;
        }
        p += w;
        n -= (size_t)w;
    }
    return true;
}

/* Unpacked data up to this size stays in memory. */
uint64_t g_mem_limit = (uint64_t)8 << 20;

/* Bytes kept in memory. */
class MemSource : public Source {
public:
    MemSource(char* data, size_t n) : data_(data), n_(n) {}
    ~MemSource() { free(data_); }
    long read_at(uint64_t off, void* buf, size_t n)
    {
        if (off >= n_)
            return 0;
        if (n > n_ - off)
            n = (size_t)(n_ - off);
        memcpy(buf, data_ + off, n);
        return (long)n;
    }
    uint64_t size() const { return n_; }

private:
    char* data_;
    size_t n_;
};

/* Where unpacked data goes: memory while it is small, then an unlinked
 * file in $TMPDIR, which is checked for free space as it grows. */
class Spool {
public:
    Spool() : mem_(0), len_(0), cap_(0), fd_(-1), written_(0), checked_(0) {}
    ~Spool()
    {
        free(mem_);
        if (fd_ >= 0)
            close(fd_);
    }
    /* Will 'expected' bytes fit? ENOSPC (and false) if not. */
    bool expect(uint64_t expected, int* err)
    {
        if (expected <= g_mem_limit)
            return true;
        Space sp;
        if (local_fs()->space(tmp_dir(), &sp) != 0)
            return true;            /* cannot tell: try */
        if (expected + reserve_of(sp) > sp.avail) {
            *err = ENOSPC;
            return false;
        }
        return true;
    }
    bool write(const void* data, size_t n, int* err)
    {
        if (fd_ < 0 && len_ + n <= g_mem_limit) {
            if (len_ + n > cap_) {
                size_t nc = cap_ ? cap_ * 2 : 65536;
                while (nc < len_ + n)
                    nc *= 2;
                char* m = (char*)realloc(mem_, nc);
                if (!m) {
                    *err = ENOMEM;
                    return false;
                }
                mem_ = m;
                cap_ = nc;
            }
            memcpy(mem_ + len_, data, n);
            len_ += n;
            return true;
        }
        if (fd_ < 0) {
            /* Too big for memory: on to a file, starting with what is
             * already here. */
            Space sp;
            if (local_fs()->space(tmp_dir(), &sp) == 0 && sp.avail < reserve_of(sp) + len_ + n) {
                *err = ENOSPC;
                return false;
            }
            fd_ = make_spool(err);
            if (fd_ < 0)
                return false;
            if (!write_all(fd_, mem_, len_, err))
                return false;
            written_ = len_;
            free(mem_);
            mem_ = 0;
            len_ = cap_ = 0;
        }
        /* Every 8 MB: is there still room? */
        if (written_ - checked_ >= ((uint64_t)8 << 20)) {
            checked_ = written_;
            Space sp;
            if (local_fs()->space(tmp_dir(), &sp) == 0 && sp.avail < reserve_of(sp) + n) {
                *err = ENOSPC;
                return false;
            }
        }
        if (!write_all(fd_, data, n, err))
            return false;
        written_ += n;
        return true;
    }
    /* The result; the spool is empty afterwards. */
    Source* finish()
    {
        if (fd_ >= 0) {
            Source* s = new FdSource(fd_);
            fd_ = -1;
            return s;
        }
        Source* s = new MemSource(mem_, len_);
        mem_ = 0;
        len_ = cap_ = 0;
        return s;
    }

private:
    char* mem_;
    size_t len_, cap_;
    int fd_;
    uint64_t written_, checked_;
};

const size_t k_buf = 65536;

/* gzip, possibly several members one after another. */
bool gunzip(Source* in, Spool& out, int* err)
{
    z_stream z;
    memset(&z, 0, sizeof(z));
    if (inflateInit2(&z, 15 + 32) != Z_OK) {
        *err = ENOMEM;
        return false;
    }
    unsigned char* ib = (unsigned char*)malloc(k_buf);
    unsigned char* ob = (unsigned char*)malloc(k_buf);
    uint64_t off = 0;
    bool ok = ib && ob;
    bool ended = false;
    while (ok) {
        if (z.avail_in == 0) {
            long n = in->read_at(off, ib, k_buf);
            if (n < 0) {
                *err = (int)-n;
                ok = false;
                break;
            }
            if (n == 0)
                break;
            off += (uint64_t)n;
            z.next_in = ib;
            z.avail_in = (uInt)n;
            if (!report(off, in->size())) {
                *err = ECANCELED;
                ok = false;
                break;
            }
        }
        z.next_out = ob;
        z.avail_out = (uInt)k_buf;
        int r = inflate(&z, Z_NO_FLUSH);
        size_t got = k_buf - z.avail_out;
        if (got && !out.write(ob, got, err)) {
            ok = false;
            break;
        }
        if (r == Z_STREAM_END) {
            ended = true;
            /* Another member may follow. */
            if (z.avail_in == 0) {
                long n = in->read_at(off, ib, k_buf);
                if (n <= 0)
                    break;
                off += (uint64_t)n;
                z.next_in = ib;
                z.avail_in = (uInt)n;
            }
            if (z.next_in[0] != 0x1f)
                break;          /* trailing garbage: ignored, as gzip does */
            inflateReset(&z);
            ended = false;
            continue;
        }
        if (r != Z_OK && r != Z_BUF_ERROR) {
            *err = EINVAL;
            ok = false;
            break;
        }
        if (r == Z_BUF_ERROR && z.avail_in != 0) {
            *err = EINVAL;
            ok = false;
            break;
        }
    }
    if (ok && !ended) {
        *err = EINVAL;      /* truncated */
        ok = false;
    }
    inflateEnd(&z);
    free(ib);
    free(ob);
    return ok;
}

bool bunzip(Source* in, Spool& out, int* err)
{
    bz_stream b;
    memset(&b, 0, sizeof(b));
    if (BZ2_bzDecompressInit(&b, 0, 0) != BZ_OK) {
        *err = ENOMEM;
        return false;
    }
    char* ib = (char*)malloc(k_buf);
    char* ob = (char*)malloc(k_buf);
    uint64_t off = 0;
    bool ok = ib && ob, ended = false;
    while (ok) {
        if (b.avail_in == 0) {
            long n = in->read_at(off, ib, k_buf);
            if (n < 0) {
                *err = (int)-n;
                ok = false;
                break;
            }
            if (n == 0)
                break;
            off += (uint64_t)n;
            b.next_in = ib;
            b.avail_in = (unsigned)n;
            if (!report(off, in->size())) {
                *err = ECANCELED;
                ok = false;
                break;
            }
        }
        b.next_out = ob;
        b.avail_out = (unsigned)k_buf;
        int r = BZ2_bzDecompress(&b);
        size_t got = k_buf - b.avail_out;
        if (got && !out.write(ob, got, err)) {
            ok = false;
            break;
        }
        if (r == BZ_STREAM_END) {
            ended = true;
            if (b.avail_in == 0) {
                long n = in->read_at(off, ib, k_buf);
                if (n <= 0)
                    break;
                off += (uint64_t)n;
                b.next_in = ib;
                b.avail_in = (unsigned)n;
            }
            if (b.next_in[0] != 'B')
                break;
            /* The next stream of a concatenated file. */
            char* keep_in = b.next_in;
            unsigned keep_n = b.avail_in;
            BZ2_bzDecompressEnd(&b);
            memset(&b, 0, sizeof(b));
            if (BZ2_bzDecompressInit(&b, 0, 0) != BZ_OK) {
                ok = false;
                break;
            }
            b.next_in = keep_in;
            b.avail_in = keep_n;
            ended = false;
            continue;
        }
        if (r != BZ_OK) {
            *err = EINVAL;
            ok = false;
            break;
        }
    }
    if (ok && !ended) {
        *err = EINVAL;
        ok = false;
    }
    BZ2_bzDecompressEnd(&b);
    free(ib);
    free(ob);
    return ok;
}

/* compress(1) data: LZW with codes of 9 to 16 bits, in the classic
 * layout (codes come in groups of n_bits bytes; a group is padded out
 * when the code size changes), as gzip's unlzw.c reads it. */
bool uncompress_lzw(Source* in, Spool& out, int* err)
{
    unsigned char hdr[3];
    if (in->read_at(0, hdr, 3) != 3 || hdr[0] != 0x1f || hdr[1] != 0x9d) {
        *err = EINVAL;
        return false;
    }
    int maxbits = hdr[2] & 0x1f;
    bool block_mode = (hdr[2] & 0x80) != 0;
    if (maxbits < 9 || maxbits > 16) {
        *err = EINVAL;
        return false;
    }
    long maxmaxcode = 1L << maxbits;
    const size_t inbuf_size = 65536;
    unsigned char* inbuf = (unsigned char*)malloc(inbuf_size + 64);
    unsigned short* prefix = (unsigned short*)malloc(sizeof(unsigned short) << 16);
    unsigned char* suffix = (unsigned char*)malloc(1 << 16);
    unsigned char* stack = (unsigned char*)malloc(1 << 16);
    unsigned char* outbuf = (unsigned char*)malloc(k_buf + 8);
    bool ok = inbuf && prefix && suffix && stack && outbuf;
    if (!ok) {
        *err = ENOMEM;
    } else {
        memset(inbuf, 0, inbuf_size + 64);
        for (int i = 0; i < 256; i++) {
            prefix[i] = 0;
            suffix[i] = (unsigned char)i;
        }
        int n_bits = 9;
        long maxcode = (1L << n_bits) - 1;
        long bitmask = maxcode;
        long oldcode = -1;
        int finchar = 0;
        long free_ent = block_mode ? 257 : 256;
        size_t insize = 0;
        uint64_t inoff = 3;
        long posbits = 0;
        size_t outpos = 0;
        long rsize;
        do {
        resetbuf:
            {
                size_t o = (size_t)(posbits >> 3);
                size_t e = o <= insize ? insize - o : 0;
                memmove(inbuf, inbuf + o, e);
                insize = e;
                posbits = 0;
            }
            rsize = 0;
            if (insize < inbuf_size) {
                rsize = in->read_at(inoff, inbuf + insize, inbuf_size - insize);
                if (rsize < 0) {
                    *err = (int)-rsize;
                    ok = false;
                    break;
                }
                inoff += (uint64_t)rsize;
                insize += (size_t)rsize;
                if (!report(inoff, in->size())) {
                    *err = ECANCELED;
                    ok = false;
                    break;
                }
                memset(inbuf + insize, 0, 64);
            }
            long inbits = rsize > 0 ? (long)(insize - insize % (size_t)n_bits) << 3
                                    : ((long)insize << 3) - (n_bits - 1);
            while (inbits > posbits) {
                if (free_ent > maxcode) {
                    posbits = (posbits - 1) +
                              ((n_bits << 3) - (posbits - 1 + (n_bits << 3)) % (n_bits << 3));
                    ++n_bits;
                    maxcode = n_bits == maxbits ? maxmaxcode : (1L << n_bits) - 1;
                    bitmask = (1L << n_bits) - 1;
                    goto resetbuf;
                }
                const unsigned char* p = inbuf + (posbits >> 3);
                long code = (((long)p[0] | ((long)p[1] << 8) | ((long)p[2] << 16)) >>
                             (posbits & 7)) & bitmask;
                posbits += n_bits;
                if (oldcode == -1) {
                    if (code >= 256) {
                        *err = EINVAL;
                        ok = false;
                        break;
                    }
                    outbuf[outpos++] = (unsigned char)(finchar = (int)(oldcode = code));
                    continue;
                }
                if (code == 256 && block_mode) {
                    for (int i = 0; i < 256; i++)
                        prefix[i] = 0;
                    free_ent = 256;
                    posbits = (posbits - 1) +
                              ((n_bits << 3) - (posbits - 1 + (n_bits << 3)) % (n_bits << 3));
                    n_bits = 9;
                    maxcode = (1L << n_bits) - 1;
                    bitmask = maxcode;
                    goto resetbuf;
                }
                long incode = code;
                unsigned char* sp = stack + (1 << 16);
                if (code >= free_ent) {
                    if (code > free_ent) {
                        *err = EINVAL;
                        ok = false;
                        break;
                    }
                    *--sp = (unsigned char)finchar;
                    code = oldcode;
                }
                while (code >= 256) {
                    if (sp <= stack) {
                        *err = EINVAL;
                        ok = false;
                        break;
                    }
                    *--sp = suffix[code];
                    code = prefix[code];
                }
                if (!ok)
                    break;
                *--sp = (unsigned char)(finchar = suffix[code]);
                size_t len = (size_t)(stack + (1 << 16) - sp);
                if (outpos + len > k_buf) {
                    if (!out.write(outbuf, outpos, err)) {
                        ok = false;
                        break;
                    }
                    outpos = 0;
                }
                if (len > k_buf) {
                    if (!out.write(sp, len, err)) {
                        ok = false;
                        break;
                    }
                } else {
                    memcpy(outbuf + outpos, sp, len);
                    outpos += len;
                }
                if ((code = free_ent) < maxmaxcode) {
                    prefix[code] = (unsigned short)oldcode;
                    suffix[code] = (unsigned char)finchar;
                    free_ent = code + 1;
                }
                oldcode = incode;
            }
        } while (ok && rsize != 0);
        if (ok && outpos && !out.write(outbuf, outpos, err))
            ok = false;
    }
    free(inbuf);
    free(prefix);
    free(suffix);
    free(stack);
    free(outbuf);
    return ok;
}

} /* namespace */

void set_spool_memory(uint64_t bytes)
{
    g_mem_limit = bytes;
}

void set_spool_reserve(uint64_t bytes)
{
    g_reserve = bytes;
}

void set_progress(ProgressFn fn, void* ctx)
{
    g_progress = fn;
    g_progress_ctx = ctx;
}

Source* spool_stream(Stream* in, uint64_t size, int* err)
{
    Spool sp;
    if (!sp.expect(size, err))
        return 0;
    g_progress_what = "Reading";
    char* buf = (char*)malloc(k_buf);
    bool ok = buf != 0;
    uint64_t done = 0;
    while (ok) {
        long n = in->read(buf, k_buf);
        if (n < 0) {
            *err = (int)-n;
            ok = false;
            break;
        }
        if (n == 0)
            break;
        done += (uint64_t)n;
        ok = sp.write(buf, (size_t)n, err);
        if (ok && !report(done, size)) {
            *err = ECANCELED;
            ok = false;
        }
    }
    free(buf);
    return ok ? sp.finish() : 0;
}

/* How big gzip data is unpacked: the last member's size, modulo 4 GB,
 * from its last four bytes, made at least as big as the packed data. */
static uint64_t gzip_size(Source* src)
{
    uint64_t n = src->size();
    unsigned char t[4];
    if (n < 18 || src->read_at(n - 4, t, 4) != 4)
        return 0;
    uint64_t v = (uint64_t)t[0] | ((uint64_t)t[1] << 8) | ((uint64_t)t[2] << 16) |
                 ((uint64_t)t[3] << 24);
    while (v < n)
        v += (uint64_t)1 << 32;
    return v;
}

Source* decompress_source(Source* src, int* err)
{
    unsigned char m[3] = { 0, 0, 0 };
    src->read_at(0, m, 3);
    bool gz = m[0] == 0x1f && m[1] == 0x8b;
    bool bz = m[0] == 'B' && m[1] == 'Z' && m[2] == 'h';
    bool lzw = m[0] == 0x1f && m[1] == 0x9d;
    if (!gz && !bz && !lzw)
        return src;
    Spool sp;
    /* gzip says how big it gets; bzip2 and compress do not, so their
     * space is checked as they are written. */
    if (gz && !sp.expect(gzip_size(src), err)) {
        delete src;
        return 0;
    }
    g_progress_what = "Unpacking";
    bool ok = gz ? gunzip(src, sp, err) : bz ? bunzip(src, sp, err) : uncompress_lzw(src, sp, err);
    delete src;
    return ok ? sp.finish() : 0;
}

/* ------------------------------------------------------------------ */
/* The tree of entries                                                 */

namespace {

struct Node {
    char* path;             /* "dir/file", no leading or trailing slash; "" the root */
    Stat st;
    uint64_t off;           /* data in the source */
    uint64_t csize;         /* stored (compressed) size */
    int method;             /* 0 stored, 8 deflate */
    bool encrypted;
    char* link;             /* symlink target, or hard link's member path */
    bool hardlink;
    int parent;
    int first_child, next_sibling;
};

/* Names in archives can be anything: "./a", "/abs", "a//b", "a/../../b".
 * Clean them; false for names that would leave the archive. */
bool clean_name(const char* in, char* out, size_t n)
{
    size_t o = 0;
    const char* p = in;
    while (*p) {
        while (*p == '/')
            p++;
        if (!*p)
            break;
        const char* e = strchr(p, '/');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len == 1 && p[0] == '.') {
            /* skip */
        } else if (len == 2 && p[0] == '.' && p[1] == '.') {
            return false;
        } else {
            if (o + len + 2 >= n)
                return false;
            if (o)
                out[o++] = '/';
            memcpy(out + o, p, len);
            o += len;
        }
        p += len;
    }
    out[o] = 0;
    return true;
}

class Tree {
public:
    Tree() : nodes_(0), n_(0), cap_(0), hash_(0), hcap_(0) {}
    ~Tree()
    {
        for (int i = 0; i < n_; i++) {
            free(nodes_[i].path);
            free(nodes_[i].link);
        }
        free(nodes_);
        free(hash_);
    }

    /* Adds or replaces (a later entry of the same name wins, as tar
     * extraction does). Creates missing parent directories. */
    Node* add(const char* path, long dir_mtime)
    {
        int at = find(path);
        if (at >= 0) {
            Node& n = nodes_[at];
            free(n.link);
            n.link = 0;
            n.hardlink = false;
            return &n;
        }
        int parent = -1;
        if (*path) {
            const char* slash = strrchr(path, '/');
            char pp[4096];
            size_t pl = slash ? (size_t)(slash - path) : 0;
            if (pl >= sizeof(pp))
                pl = sizeof(pp) - 1;
            memcpy(pp, path, pl);
            pp[pl] = 0;
            parent = find(pp);
            if (parent < 0) {
                Node* d = add(pp, dir_mtime);
                if (!d)
                    return 0;
                d->st.type = FT_DIR;
                d->st.mode = 0755;
                d->st.mtime = d->st.atime = d->st.ctime = dir_mtime;
                parent = find(pp);
            }
        }
        if (n_ == cap_) {
            int nc = cap_ ? cap_ * 2 : 256;
            Node* nn = (Node*)realloc(nodes_, (size_t)nc * sizeof(Node));
            if (!nn)
                return 0;
            nodes_ = nn;
            cap_ = nc;
        }
        Node& n = nodes_[n_];
        n.st = Stat();
        n.off = n.csize = 0;
        n.method = 0;
        n.encrypted = false;
        n.link = 0;
        n.hardlink = false;
        n.path = strdup(path);
        n.parent = parent;
        n.first_child = -1;
        n.next_sibling = -1;
        if (parent >= 0) {
            n.next_sibling = nodes_[parent].first_child;
            nodes_[parent].first_child = n_;
        }
        n_++;
        rehash_add(n_ - 1);
        return &nodes_[n_ - 1];
    }

    int find(const char* path) const
    {
        if (!hcap_)
            return -1;
        unsigned long h = hash(path) & (unsigned long)(hcap_ - 1);
        while (hash_[h] >= 0) {
            if (strcmp(nodes_[hash_[h]].path, path) == 0)
                return hash_[h];
            h = (h + 1) & (unsigned long)(hcap_ - 1);
        }
        return -1;
    }
    Node& node(int i) { return nodes_[i]; }
    int size() const { return n_; }

private:
    static unsigned long hash(const char* s)
    {
        unsigned long h = 2166136261u;
        while (*s)
            h = (h ^ (unsigned char)*s++) * 16777619u;
        return h;
    }
    void rehash_add(int idx)
    {
        if ((n_ + 1) * 2 > hcap_) {
            int nc = hcap_ ? hcap_ * 2 : 512;
            while (nc < (n_ + 1) * 2)
                nc *= 2;
            int* nh = (int*)malloc((size_t)nc * sizeof(int));
            if (!nh)
                return;
            for (int i = 0; i < nc; i++)
                nh[i] = -1;
            free(hash_);
            hash_ = nh;
            hcap_ = nc;
            for (int i = 0; i < n_; i++)
                insert(i);
            return;
        }
        insert(idx);
    }
    void insert(int i)
    {
        unsigned long h = hash(nodes_[i].path) & (unsigned long)(hcap_ - 1);
        while (hash_[h] >= 0)
            h = (h + 1) & (unsigned long)(hcap_ - 1);
        hash_[h] = i;
    }

    Node* nodes_;
    int n_, cap_;
    int* hash_;
    int hcap_;
};

/* ------------------------------------------------------------------ */
/* Streams over members                                                */

class RangeStream : public Stream {
public:
    RangeStream(Source* s, uint64_t off, uint64_t size) : s_(s), off_(off), size_(size), pos_(0) {}
    long read(void* buf, size_t n)
    {
        if (pos_ >= size_)
            return 0;
        if (n > size_ - pos_)
            n = (size_t)(size_ - pos_);
        long r = s_->read_at(off_ + pos_, buf, n);
        if (r > 0)
            pos_ += (uint64_t)r;
        return r == 0 && pos_ < size_ ? -EIO : r;    /* the archive was cut short */
    }
    long write(const void*, size_t) { return -EROFS; }
    int seek(uint64_t off)
    {
        pos_ = off;
        return 0;
    }
    int close() { return 0; }

private:
    Source* s_;
    uint64_t off_, size_, pos_;
};

class InflateStream : public Stream {
public:
    InflateStream(Source* s, uint64_t off, uint64_t csize)
        : s_(s), off_(off), csize_(csize), pos_(0), ok_(true), done_(false)
    {
        memset(&z_, 0, sizeof(z_));
        if (inflateInit2(&z_, -15) != Z_OK)
            ok_ = false;
        in_ = (unsigned char*)malloc(k_buf);
    }
    ~InflateStream()
    {
        inflateEnd(&z_);
        free(in_);
    }
    long read(void* buf, size_t n)
    {
        if (!ok_ || !in_)
            return -EIO;
        if (done_)
            return 0;
        z_.next_out = (Bytef*)buf;
        z_.avail_out = (uInt)n;
        while (z_.avail_out > 0) {
            if (z_.avail_in == 0 && pos_ < csize_) {
                size_t want = csize_ - pos_ < k_buf ? (size_t)(csize_ - pos_) : k_buf;
                long r = s_->read_at(off_ + pos_, in_, want);
                if (r <= 0)
                    return -EIO;
                pos_ += (uint64_t)r;
                z_.next_in = in_;
                z_.avail_in = (uInt)r;
            }
            int r = inflate(&z_, Z_NO_FLUSH);
            if (r == Z_STREAM_END) {
                done_ = true;
                break;
            }
            if (r != Z_OK) {
                ok_ = false;
                return -EIO;
            }
            if (z_.avail_in == 0 && pos_ >= csize_)
                break;
        }
        return (long)(n - z_.avail_out);
    }
    long write(const void*, size_t) { return -EROFS; }
    int seek(uint64_t) { return ESPIPE; }
    int close() { return 0; }

private:
    Source* s_;
    uint64_t off_, csize_, pos_;
    z_stream z_;
    unsigned char* in_;
    bool ok_, done_;
};

class ListDir : public DirReader {
public:
    ListDir(Tree& t, int first) : t_(t), i_(first) {}
    const char* next()
    {
        if (i_ < 0)
            return 0;
        Node& n = t_.node(i_);
        i_ = n.next_sibling;
        const char* s = strrchr(n.path, '/');
        return s ? s + 1 : n.path;
    }

private:
    Tree& t_;
    int i_;
};

/* ------------------------------------------------------------------ */
/* The archive as a file system                                        */

class ArchiveFs : public Fs {
public:
    ArchiveFs(Source* src, long mtime) : src_(src), mtime_(mtime)
    {
        Node* root = tree_.add("", mtime);
        root->st.type = FT_DIR;
        root->st.mode = 0755;
        root->st.mtime = root->st.atime = root->st.ctime = mtime;
    }
    ~ArchiveFs() { delete src_; }
    Tree& tree() { return tree_; }
    Source* source() { return src_; }
    long mtime() const { return mtime_; }

    int caps() const { return CAP_SEEK | CAP_SYMLINKS; }

    int lookup(const char* path) const
    {
        char clean[4096];
        if (!clean_name(path, clean, sizeof(clean)))
            return -1;
        return tree_.find(clean);
    }

    int stat(const char* path, Stat* st, bool follow)
    {
        int i = lookup(path);
        if (i < 0)
            return ENOENT;
        Node* n = &tree_.node(i);
        if (n->hardlink) {
            int t = lookup(n->link);
            if (t >= 0) {
                *st = tree_.node(t).st;
                st->mtime = n->st.mtime;
                return 0;
            }
        }
        if (follow && n->st.type == FT_LINK && n->link) {
            /* Relative to the link's directory, inside the archive. */
            char target[4096];
            if (n->link[0] == '/') {
                snprintf(target, sizeof(target), "%s", n->link);
            } else {
                const char* slash = strrchr(n->path, '/');
                int dl = slash ? (int)(slash - n->path) : 0;
                snprintf(target, sizeof(target), "%.*s/%s", dl, n->path, n->link);
            }
            int t = lookup(target);
            if (t >= 0 && tree_.node(t).st.type != FT_LINK) {
                *st = tree_.node(t).st;
                return 0;
            }
            return ENOENT;
        }
        *st = n->st;
        return 0;
    }

    DirReader* open_dir(const char* path, int* err)
    {
        int i = lookup(path);
        if (i < 0) {
            *err = ENOENT;
            return 0;
        }
        if (tree_.node(i).st.type != FT_DIR) {
            *err = ENOTDIR;
            return 0;
        }
        *err = 0;
        return new ListDir(tree_, tree_.node(i).first_child);
    }

    Stream* open(const char* path, int flags, unsigned, int* err)
    {
        if (flags & (OPEN_WRITE | OPEN_CREATE)) {
            *err = EROFS;
            return 0;
        }
        int i = lookup(path);
        if (i >= 0 && tree_.node(i).hardlink)
            i = lookup(tree_.node(i).link);
        if (i < 0) {
            *err = ENOENT;
            return 0;
        }
        Node& n = tree_.node(i);
        if (n.st.type == FT_DIR) {
            *err = EISDIR;
            return 0;
        }
        if (n.encrypted) {
            *err = EACCES;
            return 0;
        }
        *err = 0;
        if (n.method == 8)
            return new InflateStream(src_, n.off, n.csize);
        if (n.method != 0) {
            *err = EINVAL;      /* a compression method we do not have */
            return 0;
        }
        return new RangeStream(src_, n.off, n.st.size);
    }

    int readlink(const char* path, char* buf, size_t n)
    {
        int i = lookup(path);
        if (i < 0)
            return ENOENT;
        Node& nd = tree_.node(i);
        if (nd.st.type != FT_LINK || !nd.link)
            return EINVAL;
        snprintf(buf, n, "%s", nd.link);
        return 0;
    }

    int mkdir(const char*, unsigned) { return EROFS; }
    int remove(const char*) { return EROFS; }
    int rename(const char*, const char*) { return EROFS; }
    int symlink(const char*, const char*) { return EROFS; }
    int set_attrs(const char*, const Attrs&, int) { return EROFS; }
    int space(const char*, Space* out)
    {
        out->total = src_->size();
        out->free = out->avail = 0;
        strcpy(out->type, "archive");
        return 0;
    }

private:
    Tree tree_;
    Source* src_;
    long mtime_;
};

/* ---- tar ---- */

uint64_t tar_number(const char* p, size_t n)
{
    /* Base-256 for big values (GNU), else octal. */
    if ((unsigned char)p[0] & 0x80) {
        uint64_t v = (unsigned char)p[0] & 0x7f;
        for (size_t i = 1; i < n; i++)
            v = (v << 8) | (unsigned char)p[i];
        return v;
    }
    uint64_t v = 0;
    size_t i = 0;
    while (i < n && (p[i] == ' ' || p[i] == 0))
        i++;
    for (; i < n && p[i] >= '0' && p[i] <= '7'; i++)
        v = v * 8 + (uint64_t)(p[i] - '0');
    return v;
}

bool tar_checksum_ok(const unsigned char* h)
{
    unsigned long sum = 0;
    for (int i = 0; i < 512; i++)
        sum += (i >= 148 && i < 156) ? ' ' : h[i];
    return sum == (unsigned long)tar_number((const char*)h + 148, 8);
}

/* pax extended header: "len key=value\n" records. */
void pax_value(const char* data, size_t n, const char* key, char* out, size_t outn)
{
    size_t kl = strlen(key);
    size_t i = 0;
    while (i < n) {
        size_t len = 0, j = i;
        while (j < n && data[j] >= '0' && data[j] <= '9')
            len = len * 10 + (size_t)(data[j++] - '0');
        if (len == 0 || i + len > n)
            break;
        const char* rec = data + j + 1;
        size_t rl = len - (j + 1 - i);
        if (rl > kl + 1 && strncmp(rec, key, kl) == 0 && rec[kl] == '=') {
            size_t vl = rl - kl - 2;   /* minus '=' and '\n' */
            if (vl >= outn)
                vl = outn - 1;
            memcpy(out, rec + kl + 1, vl);
            out[vl] = 0;
        }
        i += len;
    }
}

bool index_tar(ArchiveFs* fs, int* err)
{
    Source* s = fs->source();
    uint64_t off = 0;
    unsigned char h[512];
    char longname[4096], longlink[4096];
    longname[0] = longlink[0] = 0;
    char pax_path[4096], pax_link[4096];
    pax_path[0] = pax_link[0] = 0;
    uint64_t pax_size = 0;
    bool have_pax_size = false;
    int entries = 0;
    for (;;) {
        long r = s->read_at(off, h, 512);
        if (r < 512)
            break;          /* end, or cut short: keep what was read */
        bool zero = true;
        for (int i = 0; i < 512; i++)
            if (h[i]) {
                zero = false;
                break;
            }
        if (zero)
            break;
        if (!tar_checksum_ok(h)) {
            if (entries == 0) {
                *err = EINVAL;
                return false;
            }
            break;
        }
        entries++;
        uint64_t size = tar_number((const char*)h + 124, 12);
        char type = (char)h[156];
        uint64_t data = off + 512;
        uint64_t next = data + ((size + 511) / 512) * 512;
        if (type == 'L' || type == 'K' || type == 'x') {
            char* buf = (char*)malloc((size_t)size + 1);
            if (buf && size < (1 << 20) && s->read_at(data, buf, (size_t)size) == (long)size) {
                buf[size] = 0;
                if (type == 'L')
                    snprintf(longname, sizeof(longname), "%s", buf);
                else if (type == 'K')
                    snprintf(longlink, sizeof(longlink), "%s", buf);
                else {
                    pax_value(buf, (size_t)size, "path", pax_path, sizeof(pax_path));
                    pax_value(buf, (size_t)size, "linkpath", pax_link, sizeof(pax_link));
                    char sz[32] = "";
                    pax_value(buf, (size_t)size, "size", sz, sizeof(sz));
                    if (sz[0]) {
                        pax_size = (uint64_t)strtod(sz, 0);
                        have_pax_size = true;
                    }
                }
            }
            free(buf);
            off = next;
            continue;
        }
        if (type == 'g') {          /* global pax header: ignored */
            off = next;
            continue;
        }
        if (have_pax_size) {
            size = pax_size;
            next = data + ((size + 511) / 512) * 512;
        }
        char name[4096];
        if (pax_path[0])
            snprintf(name, sizeof(name), "%s", pax_path);
        else if (longname[0])
            snprintf(name, sizeof(name), "%s", longname);
        else if (memcmp(h + 257, "ustar", 5) == 0 && h[345])
            snprintf(name, sizeof(name), "%.155s/%.100s", (const char*)h + 345, (const char*)h);
        else
            snprintf(name, sizeof(name), "%.100s", (const char*)h);
        char link[4096];
        if (pax_link[0])
            snprintf(link, sizeof(link), "%s", pax_link);
        else if (longlink[0])
            snprintf(link, sizeof(link), "%s", longlink);
        else
            snprintf(link, sizeof(link), "%.100s", (const char*)h + 157);
        longname[0] = longlink[0] = pax_path[0] = pax_link[0] = 0;
        have_pax_size = false;

        char clean[4096];
        if (clean_name(name, clean, sizeof(clean)) && clean[0]) {
            Node* n = fs->tree().add(clean, fs->mtime());
            if (n) {
                n->st.mode = (unsigned)tar_number((const char*)h + 100, 8) & 07777;
                n->st.uid = (long)tar_number((const char*)h + 108, 8);
                n->st.gid = (long)tar_number((const char*)h + 116, 8);
                n->st.mtime = n->st.atime = n->st.ctime = (long)tar_number((const char*)h + 136, 12);
                n->st.nlink = 1;
                n->st.size = 0;
                n->off = data;
                n->method = 0;
                switch (type) {
                case '5':
                    n->st.type = FT_DIR;
                    break;
                case '2':
                    n->st.type = FT_LINK;
                    n->link = strdup(link);
                    n->st.size = strlen(link);
                    break;
                case '1': {
                    char lc[4096];
                    n->st.type = FT_FILE;
                    if (clean_name(link, lc, sizeof(lc))) {
                        n->hardlink = true;
                        n->link = strdup(lc);
                    }
                    break;
                }
                case '3': n->st.type = FT_CHAR; break;
                case '4': n->st.type = FT_BLOCK; break;
                case '6': n->st.type = FT_FIFO; break;
                default:
                    n->st.type = FT_FILE;
                    n->st.size = size;
                    n->csize = size;
                    break;
                }
            }
        }
        off = next;
    }
    if (entries == 0) {
        *err = EINVAL;
        return false;
    }
    return true;
}

/* ---- zip ---- */

unsigned le16(const unsigned char* p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
unsigned long le32(const unsigned char* p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) |
           ((unsigned long)p[3] << 24);
}
uint64_t le64(const unsigned char* p)
{
    return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32);
}

long dos_time(unsigned date, unsigned tim)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = (int)((date >> 9) & 0x7f) + 80;
    t.tm_mon = (int)((date >> 5) & 0x0f) - 1;
    t.tm_mday = (int)(date & 0x1f);
    t.tm_hour = (int)((tim >> 11) & 0x1f);
    t.tm_min = (int)((tim >> 5) & 0x3f);
    t.tm_sec = (int)((tim & 0x1f) * 2);
    t.tm_isdst = -1;
    return (long)mktime(&t);
}

bool index_zip(ArchiveFs* fs, int* err)
{
    Source* s = fs->source();
    uint64_t size = s->size();
    if (size < 22) {
        *err = EINVAL;
        return false;
    }
    /* The end of central directory record, within the last 64 KB. */
    size_t tail = size < 65557 ? (size_t)size : 65557;
    unsigned char* buf = (unsigned char*)malloc(tail);
    if (!buf) {
        *err = ENOMEM;
        return false;
    }
    if (s->read_at(size - tail, buf, tail) != (long)tail) {
        free(buf);
        *err = EIO;
        return false;
    }
    long eocd = -1;
    for (long i = (long)tail - 22; i >= 0; i--)
        if (le32(buf + i) == 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd < 0) {
        free(buf);
        *err = EINVAL;
        return false;
    }
    uint64_t count = le16(buf + eocd + 10);
    uint64_t cd_size = le32(buf + eocd + 12);
    uint64_t cd_off = le32(buf + eocd + 16);
    /* Zip64: the locator sits right before the end record. */
    if ((cd_off == 0xffffffffu || count == 0xffff) && eocd >= 20 &&
        le32(buf + eocd - 20) == 0x07064b50) {
        uint64_t z64 = le64(buf + eocd - 20 + 8);
        unsigned char r[56];
        if (s->read_at(z64, r, 56) == 56 && le32(r) == 0x06064b50) {
            count = le64(r + 32);
            cd_size = le64(r + 40);
            cd_off = le64(r + 48);
        }
    }
    free(buf);
    if (cd_size > (uint64_t)1 << 30 || cd_off + cd_size > size) {
        *err = EINVAL;
        return false;
    }
    unsigned char* cd = (unsigned char*)malloc((size_t)cd_size + 1);
    if (!cd || s->read_at(cd_off, cd, (size_t)cd_size) != (long)cd_size) {
        free(cd);
        *err = EIO;
        return false;
    }
    size_t p = 0;
    for (uint64_t k = 0; k < count && p + 46 <= cd_size; k++) {
        const unsigned char* e = cd + p;
        if (le32(e) != 0x02014b50)
            break;
        unsigned made_by = le16(e + 4);
        unsigned flags = le16(e + 8);
        unsigned method = le16(e + 10);
        unsigned tim = le16(e + 12), date = le16(e + 14);
        uint64_t csize = le32(e + 20), usize = le32(e + 24);
        unsigned nl = le16(e + 28), xl = le16(e + 30), cl = le16(e + 32);
        unsigned long ext = le32(e + 38);
        uint64_t lho = le32(e + 42);
        if (p + 46 + nl + xl + cl > cd_size)
            break;
        char name[4096];
        size_t n = nl < sizeof(name) - 1 ? nl : sizeof(name) - 1;
        memcpy(name, e + 46, n);
        name[n] = 0;
        /* Zip64 sizes and offset, in the extra field. */
        const unsigned char* x = e + 46 + nl;
        for (unsigned xo = 0; xo + 4 <= xl;) {
            unsigned id = le16(x + xo), len = le16(x + xo + 2);
            if (id == 1) {
                const unsigned char* v = x + xo + 4;
                unsigned used = 0;
                if (usize == 0xffffffffu && used + 8 <= len) {
                    usize = le64(v + used);
                    used += 8;
                }
                if (csize == 0xffffffffu && used + 8 <= len) {
                    csize = le64(v + used);
                    used += 8;
                }
                if (lho == 0xffffffffu && used + 8 <= len)
                    lho = le64(v + used);
            }
            xo += 4 + len;
        }
        p += 46 + nl + xl + cl;
        bool dir = n > 0 && name[n - 1] == '/';
        char clean[4096];
        if (!clean_name(name, clean, sizeof(clean)) || !clean[0])
            continue;
        /* Where the data starts: after the local header. */
        unsigned char lh[30];
        if (s->read_at(lho, lh, 30) != 30 || le32(lh) != 0x04034b50)
            continue;
        uint64_t data = lho + 30 + le16(lh + 26) + le16(lh + 28);
        Node* nd = fs->tree().add(clean, fs->mtime());
        if (!nd)
            continue;
        unsigned unix_mode = (made_by >> 8) == 3 ? (unsigned)(ext >> 16) : 0;
        nd->st.mtime = nd->st.atime = nd->st.ctime = dos_time(date, tim);
        nd->st.nlink = 1;
        nd->off = data;
        nd->csize = csize;
        nd->method = (int)method;
        nd->encrypted = (flags & 1) != 0;
        if (dir || (unix_mode & 0170000) == 0040000) {
            nd->st.type = FT_DIR;
            nd->st.mode = unix_mode ? unix_mode & 07777 : 0755;
        } else if ((unix_mode & 0170000) == 0120000 && method == 0 && csize < 4096) {
            nd->st.type = FT_LINK;
            nd->st.mode = 0777;
            char t[4096];
            long r = s->read_at(data, t, (size_t)csize);
            t[r > 0 ? r : 0] = 0;
            nd->link = strdup(t);
            nd->st.size = strlen(t);
        } else {
            nd->st.type = FT_FILE;
            nd->st.mode = unix_mode ? unix_mode & 07777 : 0644;
            nd->st.size = usize;
        }
    }
    free(cd);
    return true;
}

} /* namespace */

Fs* open_archive(ArchiveKind kind, Source* data, const char* name, long mtime, int* err)
{
    *err = 0;
    if (kind == ARCHIVE_TAR || kind == ARCHIVE_SINGLE) {
        data = decompress_source(data, err);
        if (!data)
            return 0;
    }
    ArchiveFs* fs = new ArchiveFs(data, mtime);
    bool ok = true;
    if (kind == ARCHIVE_TAR) {
        ok = index_tar(fs, err);
    } else if (kind == ARCHIVE_ZIP) {
        ok = index_zip(fs, err);
    } else {
        /* One file: the name without its compression suffix. */
        char n[1024];
        snprintf(n, sizeof(n), "%s", name);
        size_t l = strlen(n);
        if (l > 4 && strcmp(n + l - 4, ".tgz") == 0)
            strcpy(n + l - 4, ".tar");
        else if (l > 3 && strcmp(n + l - 3, ".gz") == 0)
            n[l - 3] = 0;
        else if (l > 4 && strcmp(n + l - 4, ".bz2") == 0)
            n[l - 4] = 0;
        else if (l > 2 && strcmp(n + l - 2, ".Z") == 0)
            n[l - 2] = 0;
        else
            strcat(n, ".out");
        char clean[1024];
        if (!clean_name(n, clean, sizeof(clean)) || !clean[0] || strchr(clean, '/'))
            strcpy(clean, "data");
        Node* nd = fs->tree().add(clean, mtime);
        if (nd) {
            nd->st.type = FT_FILE;
            nd->st.mode = 0644;
            nd->st.size = data->size();
            nd->st.mtime = nd->st.atime = nd->st.ctime = mtime;
            nd->off = 0;
            nd->csize = data->size();
            nd->method = 0;
        }
    }
    if (!ok) {
        delete fs;
        return 0;
    }
    return fs;
}

} /* namespace tf */
