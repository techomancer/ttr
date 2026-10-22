#include "ops.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

namespace tf {

namespace {

enum Phase {
    P_START,            /* copy: look at the source */
    P_DIR_READ,         /* copy: list a directory, queue its entries */
    P_DIR_DONE,         /* copy: the directory's own attributes */
    P_FILE_OPEN,
    P_FILE_DATA,
    P_DEL_START,
    P_DEL_RMDIR,
    P_MOVE_TOP,         /* move: try a rename */
    P_MOVE_DELETE       /* move across file systems: delete the source */
};

const size_t k_chunk = 65536;

char* dup(const char* s)
{
    size_t n = strlen(s);
    char* d = (char*)malloc(n + 1);
    if (d)
        memcpy(d, s, n + 1);
    return d;
}

char* join(const char* dir, const char* name)
{
    size_t a = strlen(dir), b = strlen(name);
    char* p = (char*)malloc(a + b + 2);
    if (!p)
        return 0;
    memcpy(p, dir, a);
    size_t o = a;
    if (o == 0 || p[o - 1] != '/')
        p[o++] = '/';
    memcpy(p + o, name, b + 1);
    return p;
}

const char* base_name(const char* p)
{
    const char* s = strrchr(p, '/');
    return s && s[1] ? s + 1 : p;
}

long now_ms()
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* b is a or inside a. */
bool inside(const char* a, const char* b)
{
    size_t n = strlen(a);
    return strncmp(a, b, n) == 0 && (b[n] == 0 || b[n] == '/' || (n == 1 && a[0] == '/'));
}

/* Directory entries, read at once (so the directory is not open while
 * its children are worked on). */
struct Names {
    char** v;
    int n, cap;
    Names() : v(0), n(0), cap(0) {}
    ~Names()
    {
        for (int i = 0; i < n; i++)
            free(v[i]);
        free(v);
    }
    void add(const char* s)
    {
        if (n == cap) {
            int nc = cap ? cap * 2 : 64;
            char** nv = (char**)realloc(v, (size_t)nc * sizeof(char*));
            if (!nv)
                return;
            v = nv;
            cap = nc;
        }
        v[n++] = dup(s);
    }
};

int read_names(Fs* fs, const char* dir, Names* out)
{
    int err = 0;
    DirReader* d = fs->open_dir(dir, &err);
    if (!d)
        return err;
    const char* n;
    while ((n = d->next()) != 0)
        out->add(n);
    delete d;
    return 0;
}

} /* namespace */

Job::Job(OpKind kind, Fs* src, Fs* dst)
    : preserve_attrs(true), kind_(kind), src_fs_(src), dst_fs_(dst ? dst : src), sources_(0),
      nsources_(0), next_source_(0), target_(0), into_dir_(true), stack_(0), depth_(0), cap_(0),
      state_(WORKING), counting_(true), count_stack_(0), count_depth_(0), count_cap_(0), in_(0),
      out_(0), buf_((char*)malloc(k_chunk)), overwrite_all_(false), skip_existing_(false),
      update_all_(false), skip_errors_(false), answered_overwrite_(false), cur_src_(""),
      cur_dst_(""), err_(0), what_(""), bytes_done_(0), bytes_total_(0), files_done_(0),
      files_total_(0), failures_(0)
{
}

Job::~Job()
{
    close_streams(state_ != DONE);
    while (depth_ > 0)
        pop();
    free(stack_);
    for (int i = 0; i < count_depth_; i++)
        free(count_stack_[i]);
    free(count_stack_);
    for (int i = 0; i < nsources_; i++)
        free(sources_[i]);
    free(sources_);
    free(target_);
    free(buf_);
}

void Job::add(const char* path)
{
    char** n = (char**)realloc(sources_, (size_t)(nsources_ + 1) * sizeof(char*));
    if (!n)
        return;
    sources_ = n;
    sources_[nsources_++] = dup(path);
    /* ... and for the counting pass. */
    char** c = (char**)realloc(count_stack_, (size_t)(count_depth_ + 1) * sizeof(char*));
    if (!c)
        return;
    count_stack_ = c;
    count_cap_ = count_depth_ + 1;
    count_stack_[count_depth_++] = dup(path);
}

void Job::set_target(const char* path, bool into_dir)
{
    free(target_);
    target_ = dup(path);
    into_dir_ = into_dir;
}

void Job::push(const char* src, const char* dst, int phase, int errors_before)
{
    if (depth_ == cap_) {
        int nc = cap_ ? cap_ * 2 : 32;
        Item* ns = (Item*)realloc(stack_, (size_t)nc * sizeof(Item));
        if (!ns)
            return;
        stack_ = ns;
        cap_ = nc;
    }
    Item& it = stack_[depth_++];
    it.src = dup(src);
    it.dst = dst ? dup(dst) : 0;
    it.phase = phase;
    it.errors_before = errors_before;
}

void Job::pop()
{
    if (depth_ == 0)
        return;
    Item& it = stack_[--depth_];
    free(it.src);
    free(it.dst);
    cur_src_ = cur_dst_ = "";
}

Job::Item* Job::top()
{
    return depth_ > 0 ? &stack_[depth_ - 1] : 0;
}

void Job::close_streams(bool remove_partial)
{
    if (in_) {
        delete in_;
        in_ = 0;
    }
    if (out_) {
        out_->close();
        delete out_;
        out_ = 0;
        Item* it = top();
        if (remove_partial && it && it->dst)
            dst_fs_->remove(it->dst);
    }
}

void Job::fail(int err, const char* what)
{
    Item* it = top();
    cur_src_ = it ? it->src : "";
    cur_dst_ = it && it->dst ? it->dst : "";
    if (skip_errors_) {
        failures_++;
        close_streams(true);
        pop();
        return;
    }
    err_ = err;
    what_ = what;
    state_ = ASK_ERROR;
}

void Job::answer(Answer a)
{
    if (a == ANS_ABORT) {
        close_streams(true);
        state_ = ABORTED;
        return;
    }
    if (state_ == ASK_OVERWRITE) {
        switch (a) {
        case ANS_YES: answered_overwrite_ = true; break;
        case ANS_ALL: overwrite_all_ = true; break;
        case ANS_UPDATE: update_all_ = true; break;
        case ANS_NONE: skip_existing_ = true; pop(); break;
        default: pop(); break;      /* ANS_NO */
        }
        state_ = WORKING;
        return;
    }
    if (state_ == ASK_ERROR) {
        if (a == ANS_SKIP || a == ANS_SKIP_ALL) {
            if (a == ANS_SKIP_ALL)
                skip_errors_ = true;
            failures_++;
            close_streams(true);
            pop();
        }
        /* ANS_RETRY: the same phase again */
        state_ = WORKING;
    }
}

/* Totals for the progress display. */
bool Job::count_step()
{
    if (count_depth_ == 0)
        return false;
    char* p = count_stack_[--count_depth_];
    Stat st;
    if (src_fs_->stat(p, &st, false) == 0) {
        if (st.type == FT_DIR) {
            Names names;
            if (read_names(src_fs_, p, &names) == 0)
                for (int i = 0; i < names.n; i++) {
                    if (count_depth_ == count_cap_) {
                        int nc = count_cap_ ? count_cap_ * 2 : 64;
                        char** c = (char**)realloc(count_stack_, (size_t)nc * sizeof(char*));
                        if (!c)
                            break;
                        count_stack_ = c;
                        count_cap_ = nc;
                    }
                    count_stack_[count_depth_++] = join(p, names.v[i]);
                }
        } else {
            files_total_++;
            if (st.type == FT_FILE)
                bytes_total_ += st.size;
        }
    }
    free(p);
    return true;
}

/* Is it all right to write over it->dst? False: wait for an answer or
 * the item was skipped. */
bool Job::open_target(Item& it)
{
    if (dst_fs_->stat(it.dst, &dst_st_, false) != 0)
        return true;
    if (dst_st_.type == FT_DIR && src_st_.type != FT_DIR) {
        fail(EISDIR, "replace a directory with");
        return false;
    }
    if (src_fs_ == dst_fs_ && dst_st_.dev == src_st_.dev && dst_st_.ino == src_st_.ino) {
        fail(EINVAL, "copy onto itself");
        return false;
    }
    if (answered_overwrite_ || overwrite_all_) {
        answered_overwrite_ = false;
        return true;
    }
    if (update_all_) {
        if (dst_st_.mtime >= src_st_.mtime) {
            pop();
            return false;
        }
        return true;
    }
    if (skip_existing_) {
        pop();
        return false;
    }
    cur_src_ = it.src;
    cur_dst_ = it.dst;
    state_ = ASK_OVERWRITE;
    return false;
}

bool Job::work_step()
{
    Item* itp = top();
    if (!itp) {
        if (next_source_ >= nsources_)
            return false;
        const char* src = sources_[next_source_++];
        char* dst = 0;
        if (kind_ != OP_DELETE && target_)
            dst = into_dir_ ? join(target_, base_name(src)) : dup(target_);
        int phase = kind_ == OP_DELETE ? P_DEL_START : kind_ == OP_MOVE ? P_MOVE_TOP : P_START;
        push(src, dst, phase);
        free(dst);
        return true;
    }
    Item& it = *itp;
    cur_src_ = it.src;
    cur_dst_ = it.dst ? it.dst : "";
    switch (it.phase) {
    case P_MOVE_TOP: {
        if (src_fs_->stat(it.src, &src_st_, false) != 0) {
            fail(ENOENT, "find");
            return true;
        }
        if (strcmp(it.src, it.dst) == 0) {
            pop();
            return true;
        }
        if (src_st_.type == FT_DIR && inside(it.src, it.dst)) {
            fail(EINVAL, "move a directory into itself");
            return true;
        }
        Stat d;
        bool exists = dst_fs_->stat(it.dst, &d, false) == 0;
        if (exists && d.type == FT_DIR && src_st_.type == FT_DIR) {
            /* Merge: copy into it, then delete the source. */
            it.phase = P_MOVE_DELETE;
            it.errors_before = failures_;
            char* s = dup(it.src);
            char* t = dup(it.dst);
            push(s, t, P_START);
            free(s);
            free(t);
            return true;
        }
        if (exists && !open_target(it))
            return true;
        if (src_fs_ == dst_fs_) {
            int r = src_fs_->rename(it.src, it.dst);
            if (r == 0) {
                files_done_++;
                pop();
                return true;
            }
            if (r != EXDEV) {
                fail(r, "move");
                return true;
            }
        }
        it.phase = P_MOVE_DELETE;
        it.errors_before = failures_;
        char* s = dup(it.src);
        char* t = dup(it.dst);
        push(s, t, P_START);
        free(s);
        free(t);
        return true;
    }
    case P_MOVE_DELETE:
        if (failures_ != it.errors_before) {
            pop();              /* keep the source: its copy is incomplete */
            return true;
        }
        it.phase = P_DEL_START;
        return true;
    case P_START: {
        if (src_fs_->stat(it.src, &src_st_, false) != 0) {
            fail(ENOENT, "read");
            return true;
        }
        if (src_st_.type == FT_DIR) {
            if (inside(it.src, it.dst)) {
                fail(EINVAL, "copy a directory into itself");
                return true;
            }
            int r = dst_fs_->mkdir(it.dst, 0700);
            if (r != 0) {
                Stat d;
                if (r != EEXIST || dst_fs_->stat(it.dst, &d, true) != 0 || d.type != FT_DIR) {
                    fail(r, "create the directory");
                    return true;
                }
            }
            it.phase = P_DIR_READ;
            return true;
        }
        if (src_st_.type == FT_LINK) {
            char target[4096];
            int r = src_fs_->readlink(it.src, target, sizeof(target));
            if (r != 0) {
                fail(r, "read the link");
                return true;
            }
            if (!open_target(it))
                return true;
            dst_fs_->remove(it.dst);
            r = dst_fs_->symlink(target, it.dst);
            if (r != 0) {
                fail(r, "create the link");
                return true;
            }
            files_done_++;
            pop();
            return true;
        }
        if (src_st_.type != FT_FILE) {
            fail(EINVAL, "copy (a device, pipe or socket)");
            return true;
        }
        it.phase = P_FILE_OPEN;
        return true;
    }
    case P_DIR_READ: {
        Names names;
        int r = read_names(src_fs_, it.src, &names);
        if (r != 0) {
            fail(r, "read the directory");
            return true;
        }
        it.phase = P_DIR_DONE;
        char* s = dup(it.src);
        char* d = dup(it.dst);
        for (int i = names.n - 1; i >= 0; i--) {
            char* cs = join(s, names.v[i]);
            char* cd = join(d, names.v[i]);
            push(cs, cd, P_START);
            free(cs);
            free(cd);
        }
        free(s);
        free(d);
        return true;
    }
    case P_DIR_DONE: {
        Stat st;
        if (preserve_attrs && src_fs_->stat(it.src, &st, false) == 0) {
            Attrs a;
            a.mode = st.mode;
            a.uid = st.uid;
            a.gid = st.gid;
            a.atime = st.atime;
            a.mtime = st.mtime;
            dst_fs_->set_attrs(it.dst, a, ATTR_MODE | ATTR_TIMES);
        }
        pop();
        return true;
    }
    case P_FILE_OPEN: {
        if (src_fs_->stat(it.src, &src_st_, false) != 0) {
            fail(ENOENT, "read");
            return true;
        }
        if (!open_target(it))
            return true;
        int err = 0;
        in_ = src_fs_->open(it.src, OPEN_READ, 0, &err);
        if (!in_) {
            fail(err, "open");
            return true;
        }
        out_ = dst_fs_->open(it.dst, OPEN_WRITE | OPEN_CREATE | OPEN_TRUNCATE, 0600, &err);
        if (!out_) {
            delete in_;
            in_ = 0;
            fail(err, "create");
            return true;
        }
        it.phase = P_FILE_DATA;
        return true;
    }
    case P_FILE_DATA: {
        long n = in_->read(buf_, k_chunk);
        if (n < 0) {
            fail((int)-n, "read");
            return true;
        }
        if (n > 0) {
            long w = out_->write(buf_, (size_t)n);
            if (w < 0) {
                fail((int)-w, "write");
                return true;
            }
            bytes_done_ += (uint64_t)n;
            return true;
        }
        delete in_;
        in_ = 0;
        int r = out_->close();
        delete out_;
        out_ = 0;
        if (r != 0) {
            fail(r, "write");
            return true;
        }
        Attrs a;
        a.mode = src_st_.mode;
        a.uid = src_st_.uid;
        a.gid = src_st_.gid;
        a.atime = src_st_.atime;
        a.mtime = src_st_.mtime;
        if (preserve_attrs) {
            dst_fs_->set_attrs(it.dst, a, ATTR_OWNER);    /* only works for root */
            dst_fs_->set_attrs(it.dst, a, ATTR_MODE | ATTR_TIMES);
        } else {
            dst_fs_->set_attrs(it.dst, a, ATTR_MODE);
        }
        files_done_++;
        pop();
        return true;
    }
    case P_DEL_START: {
        Stat st;
        if (src_fs_->stat(it.src, &st, false) != 0) {
            pop();              /* already gone */
            return true;
        }
        if (st.type == FT_DIR) {
            Names names;
            int r = read_names(src_fs_, it.src, &names);
            if (r != 0) {
                fail(r, "read the directory");
                return true;
            }
            it.phase = P_DEL_RMDIR;
            char* s = dup(it.src);
            for (int i = names.n - 1; i >= 0; i--) {
                char* c = join(s, names.v[i]);
                push(c, 0, P_DEL_START);
                free(c);
            }
            free(s);
            return true;
        }
        int r = src_fs_->remove(it.src);
        if (r != 0) {
            fail(r, "delete");
            return true;
        }
        files_done_++;
        pop();
        return true;
    }
    case P_DEL_RMDIR: {
        int r = src_fs_->remove(it.src);
        if (r != 0) {
            fail(r, "delete the directory");
            return true;
        }
        pop();
        return true;
    }
    default:
        pop();
        return true;
    }
}

Job::State Job::step(int budget_ms)
{
    if (state_ != WORKING)
        return state_;
    long end = now_ms() + budget_ms;
    if (counting_) {
        while (now_ms() < end)
            if (!count_step()) {
                counting_ = false;
                break;
            }
        if (counting_)
            return state_;
    }
    while (state_ == WORKING && now_ms() < end) {
        if (!work_step()) {
            state_ = DONE;
            break;
        }
    }
    return state_;
}

/* ------------------------------------------------------------------ */

static bool fold_eq(char a, char b, bool ic)
{
    return ic ? tolower((unsigned char)a) == tolower((unsigned char)b) : a == b;
}

bool match_pattern(const char* p, const char* s, bool ic)
{
    while (*p) {
        if (*p == '*') {
            while (*p == '*')
                p++;
            if (!*p)
                return true;
            for (const char* t = s; *t; t++)
                if (match_pattern(p, t, ic))
                    return true;
            return match_pattern(p, s + strlen(s), ic);
        }
        if (!*s)
            return false;
        if (*p == '?') {
            p++;
            s++;
            continue;
        }
        if (*p == '[') {
            const char* q = p + 1;
            bool neg = *q == '!' || *q == '^';
            if (neg)
                q++;
            bool hit = false;
            while (*q && *q != ']') {
                if (q[1] == '-' && q[2] && q[2] != ']') {
                    char lo = q[0], hi = q[2], c = *s;
                    if (ic) {
                        lo = (char)tolower((unsigned char)lo);
                        hi = (char)tolower((unsigned char)hi);
                        c = (char)tolower((unsigned char)c);
                    }
                    if (c >= lo && c <= hi)
                        hit = true;
                    q += 3;
                } else {
                    if (fold_eq(*q, *s, ic))
                        hit = true;
                    q++;
                }
            }
            if (*q != ']')
                return false;
            if (hit == neg)
                return false;
            p = q + 1;
            s++;
            continue;
        }
        if (*p == '\\' && p[1])
            p++;
        if (!fold_eq(*p, *s, ic))
            return false;
        p++;
        s++;
    }
    return !*s;
}

} /* namespace tf */
