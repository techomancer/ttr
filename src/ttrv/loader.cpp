/* ttrv's loader (loader.h, ttrv/DESIGN.md section 4). */
#include "loader.h"

#include "thumbs.h"

#include "../../tf/vfs.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace ttrv {

struct Job {
    Want want;
    Result* r;
    unsigned char* data;    /* JOB_IMAGE: the file, between reader and decoder */
    size_t len;
    bool cached_thumb;      /* JOB_THUMB: data is the disk cache's thumbnail */
    int image_w, image_h;   /* its image's size, as the thumbnail says */
    Job(const Want& w)
        : want(w), r(0), data(0), len(0), cached_thumb(false), image_w(0), image_h(0)
    {
    }
    ~Job()
    {
        free(data);
        delete r;
    }
};

struct Sync {
    pthread_mutex_t m;
    pthread_cond_t reader, decoder;
    pthread_t reader_thread;
    pthread_t* decoder_threads;
    int ndecoder_threads;
};

namespace {

bool same(const Want& a, const Want& b)
{
    return a.kind == b.kind && a.w == b.w && a.h == b.h && strcmp(a.path.c_str(), b.path.c_str()) == 0;
}

int find_job(const ttr::Vector<Job*>& v, const Want& w)
{
    for (int i = 0; i < v.size(); i++)
        if (same(v[i]->want, w))
            return i;
    return -1;
}

void ti_lock(void* m)
{
    pthread_mutex_lock((pthread_mutex_t*)m);
}

void ti_unlock(void* m)
{
    pthread_mutex_unlock((pthread_mutex_t*)m);
}

pthread_mutex_t g_ti_mutex = PTHREAD_MUTEX_INITIALIZER;

int cpus()
{
#if defined(_SC_NPROCESSORS_ONLN)
    long n = sysconf(_SC_NPROCESSORS_ONLN);
#elif defined(_SC_NPROC_ONLN)
    long n = sysconf(_SC_NPROC_ONLN);       /* IRIX */
#else
    long n = 1;
#endif
    return n > 0 ? (int)n : 1;
}

} /* namespace */

} /* namespace ttrv */

extern "C" {
static void* ttrv_reader_main(void* p)
{
    ((ttrv::Loader*)p)->reader_loop();
    return 0;
}
static void* ttrv_decoder_main(void* p)
{
    ((ttrv::Loader*)p)->decoder_loop();
    return 0;
}
}

namespace ttrv {

Result::Result() : kind(JOB_HEADER), w(0), h(0), err(0)
{
    msg[0] = 0;
}

Result::~Result()
{
    for (int i = 0; i < entries.size(); i++)
        delete entries[i];
}

Loader::Loader()
    : client_(0), ndec_(0), started_(false), stopping_(false), disk_thumbs_(false), sync_(0)
{
    pipe_[0] = pipe_[1] = -1;
}

Loader::~Loader()
{
    stop();
}

bool Loader::start(LoaderClient* client, int decoders)
{
    if (started_)
        return true;
    client_ = client;
    if (pipe(pipe_) != 0)
        return false;
    fcntl(pipe_[0], F_SETFL, fcntl(pipe_[0], F_GETFL) | O_NONBLOCK);
    fcntl(pipe_[1], F_SETFL, fcntl(pipe_[1], F_GETFL) | O_NONBLOCK);
    fcntl(pipe_[0], F_SETFD, FD_CLOEXEC);
    fcntl(pipe_[1], F_SETFD, FD_CLOEXEC);
    ti::set_lock(ti_lock, ti_unlock, &g_ti_mutex);
    ndec_ = decoders > 0 ? decoders : cpus() - 1;
    if (ndec_ < 1)
        ndec_ = 1;
    sync_ = new Sync;
    pthread_mutex_init(&sync_->m, 0);
    pthread_cond_init(&sync_->reader, 0);
    pthread_cond_init(&sync_->decoder, 0);
    sync_->decoder_threads = new pthread_t[ndec_];
    sync_->ndecoder_threads = 0;
    stopping_ = false;
    if (pthread_create(&sync_->reader_thread, 0, ttrv_reader_main, this) != 0) {
        delete [] sync_->decoder_threads;
        delete sync_;
        sync_ = 0;
        return false;
    }
    started_ = true;
    for (int i = 0; i < ndec_; i++)
        if (pthread_create(&sync_->decoder_threads[i], 0, ttrv_decoder_main, this) == 0)
            sync_->ndecoder_threads++;
    if (!sync_->ndecoder_threads) {
        stop();
        return false;
    }
    ndec_ = sync_->ndecoder_threads;
    return true;
}

void Loader::stop()
{
    if (started_) {
        pthread_mutex_lock(&sync_->m);
        stopping_ = true;
        pthread_cond_broadcast(&sync_->reader);
        pthread_cond_broadcast(&sync_->decoder);
        pthread_mutex_unlock(&sync_->m);
        pthread_join(sync_->reader_thread, 0);
        for (int i = 0; i < sync_->ndecoder_threads; i++)
            pthread_join(sync_->decoder_threads[i], 0);
        started_ = false;
    }
    if (sync_) {
        pthread_mutex_destroy(&sync_->m);
        pthread_cond_destroy(&sync_->reader);
        pthread_cond_destroy(&sync_->decoder);
        delete [] sync_->decoder_threads;
        delete sync_;
        sync_ = 0;
    }
    ttr::Vector<Job*>* lists[4] = { &pending_, &read_, &busy_, &done_ };
    for (int l = 0; l < 4; l++) {
        for (int i = 0; i < lists[l]->size(); i++)
            delete (*lists[l])[i];
        lists[l]->clear();
    }
    for (int i = 0; i < 2; i++)
        if (pipe_[i] >= 0) {
            close(pipe_[i]);
            pipe_[i] = -1;
        }
}

/* Already read, being worked on or done: no new job for it. */
bool Loader::coming(const Want& w) const
{
    return find_job(read_, w) >= 0 || find_job(busy_, w) >= 0 || find_job(done_, w) >= 0;
}

void Loader::set_wants(const Want* w, int n)
{
    if (!started_)
        return;
    pthread_mutex_lock(&sync_->m);
    ttr::Vector<Job*> next;
    for (int i = 0; i < n; i++) {
        if (coming(w[i]) || find_job(next, w[i]) >= 0)
            continue;
        int at = find_job(pending_, w[i]);
        if (at >= 0) {
            next.push_back(pending_[at]);
            pending_.remove_at(at);
        } else {
            next.push_back(new Job(w[i]));
        }
    }
    /* What is no longer wanted, and not started, goes; so do files read
     * but not decoded yet. */
    for (int i = 0; i < pending_.size(); i++)
        delete pending_[i];
    pending_.clear();
    for (int i = 0; i < next.size(); i++)
        pending_.push_back(next[i]);
    for (int i = read_.size() - 1; i >= 0; i--) {
        bool wanted = false;
        for (int k = 0; k < n && !wanted; k++)
            wanted = same(read_[i]->want, w[k]);
        if (!wanted) {
            delete read_[i];
            read_.remove_at(i);
        }
    }
    pthread_cond_signal(&sync_->reader);
    pthread_mutex_unlock(&sync_->m);
}

void Loader::dispatch()
{
    char buf[64];
    while (read(pipe_[0], buf, sizeof(buf)) > 0) {
    }
    if (!started_)
        return;
    pthread_mutex_lock(&sync_->m);
    ttr::Vector<Job*> ready;
    for (int i = 0; i < done_.size(); i++)
        ready.push_back(done_[i]);
    done_.clear();
    pthread_mutex_unlock(&sync_->m);
    for (int i = 0; i < ready.size(); i++) {
        Result* r = ready[i]->r;
        ready[i]->r = 0;
        delete ready[i];
        if (client_)
            client_->loaded(r);
        else
            delete r;
    }
}

/* The job's result is ready: hand it to the main thread. Called with
 * the lock held. */
void Loader::finish(Job* j)
{
    int at = busy_.find(j);
    if (at >= 0)
        busy_.remove_at(at);
    done_.push_back(j);
    char c = 1;
    if (write(pipe_[1], &c, 1) < 0) {
        /* Full: a wakeup is already waiting. */
    }
}

void Loader::reader_loop()
{
    pthread_mutex_lock(&sync_->m);
    for (;;) {
        /* The next wanted job; images only while the decoders keep up,
         * so reading does not run far ahead of them. */
        Job* j = 0;
        while (!stopping_) {
            for (int i = 0; i < pending_.size(); i++) {
                JobKind k = pending_[i]->want.kind;
                if ((k == JOB_IMAGE || k == JOB_THUMB) && read_.size() > ndec_)
                    continue;
                j = pending_[i];
                pending_.remove_at(i);
                break;
            }
            if (j)
                break;
            pthread_cond_wait(&sync_->reader, &sync_->m);
        }
        if (stopping_)
            break;
        busy_.push_back(j);
        j->r = new Result;
        j->r->kind = j->want.kind;
        j->r->path = j->want.path;
        j->r->w = j->want.w;
        j->r->h = j->want.h;
        pthread_mutex_unlock(&sync_->m);

        bool to_decode = false;
        if (j->want.kind == JOB_LIST)
            run_list(j);
        else if (j->want.kind == JOB_HEADER)
            run_header(j);
        else if (j->want.kind == JOB_THUMB)
            to_decode = run_thumb_read(j);
        else
            to_decode = run_read(j);

        pthread_mutex_lock(&sync_->m);
        if (to_decode) {
            int at = busy_.find(j);
            if (at >= 0)
                busy_.remove_at(at);
            read_.push_back(j);
            pthread_cond_signal(&sync_->decoder);
        } else {
            finish(j);
        }
    }
    pthread_mutex_unlock(&sync_->m);
}

void Loader::decoder_loop()
{
    pthread_mutex_lock(&sync_->m);
    for (;;) {
        while (!stopping_ && read_.empty())
            pthread_cond_wait(&sync_->decoder, &sync_->m);
        if (stopping_)
            break;
        /* The most wanted first: read_ keeps the order files were read
         * in, which is the wanted order. */
        Job* j = read_[0];
        read_.remove_at(0);
        busy_.push_back(j);
        /* A decoder freed: the reader may read the next image. */
        pthread_cond_signal(&sync_->reader);
        pthread_mutex_unlock(&sync_->m);
        if (j->want.kind == JOB_THUMB)
            run_thumb_decode(j);
        else
            run_decode(j);
        pthread_mutex_lock(&sync_->m);
        finish(j);
    }
    pthread_mutex_unlock(&sync_->m);
}

void Loader::run_list(Job* j)
{
    tf::Fs* fs = tf::vfs();
    Result* r = j->r;
    const char* dir = j->want.path.c_str();
    r->err = fs->stat(dir, &r->st, true);
    if (r->err)
        return;
    int err = 0;
    tf::DirReader* d = fs->open_dir(dir, &err);
    if (!d) {
        r->err = err ? err : ENOTDIR;
        return;
    }
    size_t dl = strlen(dir);
    bool slash = dl > 0 && dir[dl - 1] == '/';
    const char* name;
    char path[4200];
    while ((name = d->next()) != 0) {
        snprintf(path, sizeof(path), "%s%s%s", dir, slash ? "" : "/", name);
        Entry* e = new Entry;
        e->name = name;
        tf::Stat lst;
        e->link = fs->stat(path, &lst, false) == 0 && lst.type == tf::FT_LINK;
        if (fs->stat(path, &e->st, true) != 0)
            e->st = lst;
        e->archive = e->st.type == tf::FT_FILE && tf::archive_scheme(name) != 0;
        r->entries.push_back(e);
    }
    delete d;
}

void Loader::run_header(Job* j)
{
    tf::Fs* fs = tf::vfs();
    Result* r = j->r;
    const char* p = j->want.path.c_str();
    r->err = fs->stat(p, &r->st, true);
    if (r->err)
        return;
    int err = 0;
    tf::Stream* s = fs->open(p, tf::OPEN_READ, 0, &err);
    if (!s) {
        r->err = err;
        return;
    }
    /* 64 KB is enough for nearly every header; a JPEG with a big preview
     * before its frame, or a TIFF with its directory at the end, gets
     * more. */
    size_t cap = 65536, len = 0;
    unsigned char* buf = (unsigned char*)malloc(cap);
    for (int round = 0; buf && round < 2; round++) {
        while (len < cap) {
            long got = s->read(buf + len, cap - len);
            if (got <= 0)
                break;
            len += (size_t)got;
        }
        if (ti::probe(buf, len, &r->info) || len < cap || r->info.format == ti::FMT_UNKNOWN)
            break;
        size_t more = r->st.size < (uint64_t)(16UL << 20) ? (size_t)r->st.size : (size_t)16 << 20;
        if (more <= cap)
            break;
        unsigned char* nb = (unsigned char*)realloc(buf, more);
        if (!nb)
            break;
        buf = nb;
        cap = more;
    }
    free(buf);
    delete s;
}

bool Loader::run_read(Job* j)
{
    tf::Fs* fs = tf::vfs();
    Result* r = j->r;
    const char* p = j->want.path.c_str();
    r->err = fs->stat(p, &r->st, true);
    if (r->err)
        return false;
    if (r->st.size > (uint64_t)k_max_file) {
        snprintf(r->msg, sizeof(r->msg), "The file is over %lu MB.", k_max_file >> 20);
        return false;
    }
    int err = 0;
    tf::Stream* s = fs->open(p, tf::OPEN_READ, 0, &err);
    if (!s) {
        r->err = err;
        return false;
    }
    /* The size tf gave, or growing when it does not know (some archive
     * members). */
    size_t cap = r->st.size ? (size_t)r->st.size + 1 : 1 << 20;
    j->data = (unsigned char*)malloc(cap);
    j->len = 0;
    while (j->data) {
        if (j->len == cap) {
            if (cap >= k_max_file) {
                snprintf(r->msg, sizeof(r->msg), "The file is over %lu MB.", k_max_file >> 20);
                break;
            }
            unsigned char* n = (unsigned char*)realloc(j->data, cap * 2);
            if (!n)
                break;
            j->data = n;
            cap *= 2;
        }
        long got = s->read(j->data + j->len, cap - j->len);
        if (got < 0) {
            r->err = (int)-got;
            break;
        }
        if (got == 0)
            break;
        j->len += (size_t)got;
    }
    delete s;
    if (!j->data && !r->err)
        snprintf(r->msg, sizeof(r->msg), "Out of memory.");
    return j->data && !r->err && !r->msg[0];
}

/* A thumbnail: the disk cache's when it is there and fresh, else the
 * file itself. */
bool Loader::run_thumb_read(Job* j)
{
    Result* r = j->r;
    const char* p = j->want.path.c_str();
    if (disk_thumbs_ && tf::is_local_path(p)) {
        r->err = tf::vfs()->stat(p, &r->st, true);
        if (r->err)
            return false;
        int cls = thumb_class(j->want.w > j->want.h ? j->want.w : j->want.h);
        j->data = thumb_read(p, r->st.mtime, cls, &j->len, &j->image_w, &j->image_h);
        if (j->data) {
            j->cached_thumb = true;
            return true;
        }
    }
    return run_read(j);
}

void Loader::run_thumb_decode(Job* j)
{
    Result* r = j->r;
    int w = j->want.w, h = j->want.h;
    int cls = thumb_class(w > h ? w : h);
    ti::DecodeOptions o;
    o.reduce_to = j->cached_thumb ? 0 : cls;
    ti::Image im;
    ti::Info info;
    bool ok = ti::decode(j->data, j->len, &im, &info, r->msg, sizeof(r->msg), o);
    free(j->data);
    j->data = 0;
    if (!ok)
        return;
    if (j->cached_thumb) {
        /* The image's size from the thumbnail; its format is unknown. */
        r->info = ti::Info();
        r->info.width = j->image_w;
        r->info.height = j->image_h;
    } else {
        r->info = info;
        /* The size class's thumbnail, saved for next time. */
        int tw = im.w, th = im.h;
        ti::fit(im.w, im.h, cls, cls, 1.0, false, &tw, &th);
        ti::Image t;
        if (tw != im.w || th != im.h) {
            if (ti::scale(im, tw, th, &t))
                im.take(t);
        }
        const char* p = j->want.path.c_str();
        if (disk_thumbs_ && tf::is_local_path(p))
            thumb_write(p, r->st.mtime, cls, im, info.width, info.height);
    }
    int fw = im.w, fh = im.h;
    ti::fit(im.w, im.h, w, h, 1.0, false, &fw, &fh);
    if (fw == im.w && fh == im.h)
        r->image.take(im);
    else if (!ti::scale(im, fw, fh, &r->image))
        snprintf(r->msg, sizeof(r->msg), "Not enough memory to scale the image.");
}

void Loader::run_decode(Job* j)
{
    Result* r = j->r;
    ti::DecodeOptions o;
    int w = j->want.w, h = j->want.h;
    o.reduce_to = w > h ? w : h;
    ti::Image im;
    bool ok = ti::decode(j->data, j->len, &im, &r->info, r->msg, sizeof(r->msg), o);
    free(j->data);
    j->data = 0;
    if (!ok)
        return;
    int fw = im.w, fh = im.h;
    if (w > 0 && h > 0)
        ti::fit(im.w, im.h, w, h, 1.0, false, &fw, &fh);
    if (fw == im.w && fh == im.h) {
        r->image.take(im);
    } else if (!ti::scale(im, fw, fh, &r->image)) {
        snprintf(r->msg, sizeof(r->msg), "Not enough memory to scale the image.");
    }
}

} /* namespace ttrv */
