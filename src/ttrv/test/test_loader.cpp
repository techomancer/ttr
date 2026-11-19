/* Tests for ttrv's loader: folders (and a zip as one), headers, images
 * fitted to a size, wants replaced while work is running. Run from the
 * source tree's top. */
#include "../loader.h"
#include "../thumbs.h"

#include <errno.h>
#include <poll.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_checks, g_failures;

#define CHECK(c) check((c), __FILE__, __LINE__, #c)

static void check(bool ok, const char* file, int line, const char* what)
{
    g_checks++;
    if (!ok) {
        g_failures++;
        printf("%s:%d: failed: %s\n", file, line, what);
    }
}

struct Collect : public ttrv::LoaderClient {
    ttr::Vector<ttrv::Result*> got;
    void loaded(ttrv::Result* r) { got.push_back(r); }
    ~Collect()
    {
        for (int i = 0; i < got.size(); i++)
            delete got[i];
    }
    ttrv::Result* find(ttrv::JobKind k, const char* path)
    {
        for (int i = 0; i < got.size(); i++)
            if (got[i]->kind == k && strcmp(got[i]->path.c_str(), path) == 0)
                return got[i];
        return 0;
    }
};

/* Dispatch until n results have come, or 20 seconds. */
static void wait_for(ttrv::Loader& l, Collect& c, int n)
{
    for (int i = 0; i < 200 && c.got.size() < n; i++) {
        struct pollfd p;
        p.fd = l.notify_fd();
        p.events = POLLIN;
        p.revents = 0;
        poll(&p, 1, 100);
        l.dispatch();
    }
}

static bool has_entry(const ttrv::Result* r, const char* name, bool dir)
{
    for (int i = 0; r && i < r->entries.size(); i++)
        if (strcmp(r->entries[i]->name.c_str(), name) == 0)
            return (r->entries[i]->st.type == tf::FT_DIR) == dir;
    return false;
}

int main()
{
    char cwd[2048], dir[2100], zip[2100], png[2200], inzip[2300], gif[2400], xpm[2400];
    if (!getcwd(cwd, sizeof(cwd)))
        return 1;
    sprintf(dir, "%s/ti/test/data", cwd);
    sprintf(zip, "%s/src/ttrv/test/data/pics.zip", cwd);
    sprintf(png, "%s/rgb.png", dir);
    sprintf(inzip, "%s/uzip://", zip);
    sprintf(gif, "%strip/pal.gif", inzip);
    sprintf(xpm, "%strip/pal.xpm", inzip);

    /* Thumbnails in a cache of the test's own. */
    char cache[300];
    sprintf(cache, "/tmp/ttrv-test-cache.%ld", (long)getpid());
    static char env[400], home[400];
    sprintf(env, "XDG_CACHE_HOME=%s", cache);
    sprintf(home, "HOME=%s", cache);
    putenv(env);
    putenv(home);
    mkdir(cache, 0700);
    CHECK(ttrv::thumbs_init());
    /* The spec's file names: MD5 of the URI. */
    char md5[33];
    ttrv::md5_hex((const unsigned char*)"", 0, md5);
    CHECK(strcmp(md5, "d41d8cd98f00b204e9800998ecf8427e") == 0);
    ttrv::md5_hex((const unsigned char*)"file:///home/jens/photos/me.png", 31, md5);
    CHECK(strcmp(md5, "c6ee772d9e49320e97ec29a7eb5b1697") == 0);
    char uri[300];
    ttrv::file_uri("/a b/c%.png", uri, sizeof(uri));
    CHECK(strcmp(uri, "file:///a%20b/c%25.png") == 0);

    Collect c;
    ttrv::Loader l;
    l.set_disk_thumbs(true);
    CHECK(l.start(&c, 2));
    CHECK(l.decoders() == 2);

    ttrv::Want w[6];
    w[0] = ttrv::Want(ttrv::JOB_LIST, dir);
    w[1] = ttrv::Want(ttrv::JOB_HEADER, png);
    w[2] = ttrv::Want(ttrv::JOB_IMAGE, png, 3, 3);
    w[3] = ttrv::Want(ttrv::JOB_LIST, inzip);
    w[4] = ttrv::Want(ttrv::JOB_IMAGE, gif);
    w[5] = ttrv::Want(ttrv::JOB_HEADER, xpm);
    l.set_wants(w, 6);
    /* The same list again: nothing twice. */
    l.set_wants(w, 6);
    wait_for(l, c, 6);
    CHECK(c.got.size() == 6);

    ttrv::Result* r = c.find(ttrv::JOB_LIST, dir);
    CHECK(r && r->err == 0 && r->entries.size() > 10);
    CHECK(has_entry(r, "rgb.png", false));
    r = c.find(ttrv::JOB_HEADER, png);
    CHECK(r && r->err == 0 && r->info.format == ti::FMT_PNG && r->info.width == 7 &&
          r->info.bits == 24 && r->st.size > 0);
    r = c.find(ttrv::JOB_IMAGE, png);
    /* 7 x 5 fitted in 3 x 3. */
    CHECK(r && r->msg[0] == 0 && r->image.w == 3 && r->image.h == 2 && r->info.width == 7);
    r = c.find(ttrv::JOB_LIST, inzip);
    CHECK(r && r->err == 0);
    CHECK(has_entry(r, "rgb.png", false) && has_entry(r, "trip", true));
    r = c.find(ttrv::JOB_IMAGE, gif);
    CHECK(r && r->msg[0] == 0 && r->image.w == 7 && r->image.h == 5);
    r = c.find(ttrv::JOB_HEADER, xpm);
    CHECK(r && r->info.format == ti::FMT_XPM && r->info.colors == 35);

    /* A thumbnail: made and saved, then read back from the disk cache
     * (no format: not decoded from the file). */
    ttrv::Want t(ttrv::JOB_THUMB, png, 4, 4);
    l.set_wants(&t, 1);
    wait_for(l, c, 7);
    r = c.find(ttrv::JOB_THUMB, png);
    CHECK(r && r->msg[0] == 0 && r->image.w == 4 && r->image.h == 3 &&
          r->info.format == ti::FMT_PNG && r->info.width == 7);
    char tp[1300];
    struct stat ts;
    CHECK(ttrv::thumb_path(png, 128, tp, sizeof(tp)) && stat(tp, &ts) == 0);
    if (r)
        r->kind = ttrv::JOB_LIST;   /* so find() sees the next one */
    l.set_wants(&t, 1);
    wait_for(l, c, 8);
    r = c.find(ttrv::JOB_THUMB, png);
    CHECK(r && r->image.w == 4 && r->info.format == ti::FMT_UNKNOWN && r->info.width == 7 &&
          r->info.height == 5);
    /* Inside a zip: made, never saved. */
    ttrv::Want tz(ttrv::JOB_THUMB, gif, 4, 4);
    l.set_wants(&tz, 1);
    wait_for(l, c, 9);
    r = c.find(ttrv::JOB_THUMB, gif);
    CHECK(r && r->image.w == 4);
    CHECK(ttrv::thumb_path(gif, 128, tp, sizeof(tp)) && stat(tp, &ts) != 0);

    /* EXIF orientation 6: the header's size and the image upright. */
    char ex[2200];
    sprintf(ex, "%s/exif.jpg", dir);
    ttrv::Want o[2];
    o[0] = ttrv::Want(ttrv::JOB_HEADER, ex);
    o[1] = ttrv::Want(ttrv::JOB_IMAGE, ex);
    l.set_wants(o, 2);
    int had = c.got.size();
    wait_for(l, c, had + 2);
    r = c.find(ttrv::JOB_HEADER, ex);
    CHECK(r && r->exif.orientation == 6 && r->info.width == 5 && r->info.height == 7 &&
          strcmp(r->exif.make, "TestCam") == 0);
    r = c.find(ttrv::JOB_IMAGE, ex);
    CHECK(r && r->image.w == 5 && r->image.h == 7 && r->info.width == 5);

    /* Errors come back as results. */
    char missing[2200];
    sprintf(missing, "%s/no-such-file.png", dir);
    char broken[2200];
    sprintf(broken, "%s/short.png", dir);
    ttrv::Want e[2];
    e[0] = ttrv::Want(ttrv::JOB_IMAGE, missing);
    e[1] = ttrv::Want(ttrv::JOB_IMAGE, broken);
    l.set_wants(e, 2);
    wait_for(l, c, 13);
    r = c.find(ttrv::JOB_IMAGE, missing);
    CHECK(r && r->err != 0);
    r = c.find(ttrv::JOB_IMAGE, broken);
    CHECK(r && r->err == 0 && r->msg[0] && r->image.empty());

    /* Many wants, replaced at once by one: that one arrives, and the
     * dropped ones mostly never start. */
    int before = c.got.size();
    ttrv::Want many[40];
    for (int i = 0; i < 40; i++)
        many[i] = ttrv::Want(ttrv::JOB_IMAGE, png, 100 + i, 100 + i);
    l.set_wants(many, 40);
    ttrv::Want one(ttrv::JOB_IMAGE, png, 50, 50);
    l.set_wants(&one, 1);
    bool arrived = false;
    for (int i = 0; i < 200 && !arrived; i++) {
        wait_for(l, c, c.got.size() + 1);
        for (int k = before; k < c.got.size(); k++)
            if (c.got[k]->w == 50)
                arrived = true;
    }
    CHECK(arrived);
    CHECK(c.got.size() - before < 41);
    l.stop();
    char cmd[400];
    sprintf(cmd, "rm -rf %s", cache);
    if (system(cmd) != 0)
        CHECK(false);

    printf("ttrv loader: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
