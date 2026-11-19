/*
 * ttrv's loader (ttrv/DESIGN.md section 4): one reader thread that owns
 * tf (folder listings, headers, file bytes) and decoder threads that
 * decode and scale with ti. The main thread says what it wants, in order
 * of importance, with set_wants() whenever the view changes; work that
 * fell off the list and has not started is dropped. Results come back
 * on the main thread through notify_fd() and dispatch().
 *
 * No X here: ttrv's main loop watches notify_fd() with XtAppAddInput,
 * and the tests poll it.
 */
#ifndef TTRV_LOADER_H
#define TTRV_LOADER_H

#include "../../tf/tf.h"
#include "../../ti/ti.h"
#include "core/str.h"
#include "core/vector.h"

namespace ttrv {

enum JobKind {
    JOB_LIST,       /* a folder's entries (archives are folders) */
    JOB_HEADER,     /* an image's format, size, depth: ti::probe */
    JOB_IMAGE,      /* decoded, fitted in w x h (0 x 0: full size) */
    JOB_THUMB       /* a thumbnail fitted in w x h, through the disk cache */
};

struct Entry {
    ttr::Str name;
    tf::Stat st;            /* of the target, for links */
    bool link;
    bool archive;           /* a file tf enters as a folder */
};

struct Want {
    JobKind kind;
    ttr::Str path;
    int w, h;
    Want() : kind(JOB_HEADER), w(0), h(0) {}
    Want(JobKind k, const char* p, int ww = 0, int hh = 0) : kind(k), path(p), w(ww), h(hh) {}
};

/* What a job produced; the client owns it once delivered. */
class Result {
public:
    Result();
    ~Result();

    JobKind kind;
    ttr::Str path;
    int w, h;               /* as wanted */
    int err;                /* errno from tf, or 0 */
    char msg[200];          /* why decoding failed, or "" */
    tf::Stat st;            /* the file's (or folder's) */
    ti::Info info;          /* JOB_HEADER and JOB_IMAGE; JOB_THUMB: the
                             * image's size, its format only if decoded */
    ti::Image image;        /* JOB_IMAGE: fitted (or the whole image) */
    ttr::Vector<Entry*> entries;    /* JOB_LIST, unsorted */

private:
    Result(const Result&);
    Result& operator=(const Result&);
};

class LoaderClient {
public:
    virtual ~LoaderClient() {}
    virtual void loaded(Result* r) = 0;
};

struct Job;

class Loader {
public:
    Loader();
    ~Loader();

    /* decoders: threads to decode with (0: the CPUs less one, at least
     * one). False if threads or the pipe could not be made. */
    bool start(LoaderClient* client, int decoders = 0);
    void stop();
    int decoders() const { return ndec_; }

    /* The work wanted, most important first. Replaces the last list. */
    void set_wants(const Want* w, int n);

    /* Readable when results are waiting; then dispatch() hands them to
     * the client. */
    int notify_fd() const { return pipe_[0]; }
    void dispatch();

    /* Thumbnails go to and come from the disk cache (thumbs.h), which
     * thumbs_init() made. Before start(). */
    void set_disk_thumbs(bool on) { disk_thumbs_ = on; }

    /* Bytes read for one image at most (bigger files fail). */
    static const unsigned long k_max_file = 256UL << 20;

    /* Thread bodies (public for the C trampolines). */
    void reader_loop();
    void decoder_loop();

private:
    Loader(const Loader&);
    Loader& operator=(const Loader&);

    void run_list(Job* j);
    void run_header(Job* j);
    bool run_read(Job* j);
    bool run_thumb_read(Job* j);
    void run_thumb_decode(Job* j);
    void run_decode(Job* j);
    void finish(Job* j);
    bool coming(const Want& w) const;

    LoaderClient* client_;
    int pipe_[2];
    int ndec_;
    bool started_, stopping_, disk_thumbs_;
    struct Sync* sync_;             /* the mutex, conditions and threads */
    ttr::Vector<Job*> pending_;     /* wanted, not started */
    ttr::Vector<Job*> read_;        /* read, waiting for a decoder */
    ttr::Vector<Job*> busy_;        /* being read or decoded */
    ttr::Vector<Job*> done_;        /* for dispatch() */
};

} /* namespace ttrv */

#endif
