/* Tests for tf: path normalization and the local file system. */
#include "../ops.h"
#include "../vfs.h"
#include "../path.h"
#include "../tf.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_checks, g_failures;

#define CHECK(c) check((c), __FILE__, __LINE__, #c)
#define CHECK_STR(a, b) check_str((a), (b), __FILE__, __LINE__)

static void check(bool ok, const char* file, int line, const char* what)
{
    g_checks++;
    if (!ok) {
        g_failures++;
        printf("%s:%d: failed: %s\n", file, line, what);
    }
}

static void check_str(const char* got, const char* want, const char* file, int line)
{
    g_checks++;
    if (!got || strcmp(got, want) != 0) {
        g_failures++;
        printf("%s:%d: got \"%s\", want \"%s\"\n", file, line, got ? got : "(null)", want);
    }
}

static void test_path()
{
    tf::Path base("/usr/share");
    CHECK_STR(tf::Path::resolve(base, "doc").c_str(), "/usr/share/doc");
    CHECK_STR(tf::Path::resolve(base, "..").c_str(), "/usr");
    CHECK_STR(tf::Path::resolve(base, "../../..").c_str(), "/");
    CHECK_STR(tf::Path::resolve(base, "/a//b/./c/../d/").c_str(), "/a/b/d");
    CHECK_STR(tf::Path::resolve(base, ".").c_str(), "/usr/share");
    CHECK_STR(base.child("x").c_str(), "/usr/share/x");
    CHECK_STR(tf::Path("/").child("x").c_str(), "/x");
    CHECK_STR(base.parent().c_str(), "/usr");
    CHECK_STR(tf::Path("/usr").parent().c_str(), "/");
    CHECK_STR(tf::Path("/").parent().c_str(), "/");
    CHECK_STR(base.name(), "share");
    CHECK(tf::Path("/").is_root());
    CHECK(!base.is_root());
    tf::Path copy(base);
    copy = copy.child("y");
    CHECK_STR(copy.c_str(), "/usr/share/y");
    CHECK_STR(base.c_str(), "/usr/share");
}

static void test_local()
{
    tf::Fs* fs = tf::local_fs();
    char tmpl[64];
    sprintf(tmpl, "/tmp/tftest%ld", (long)getpid());
    if (fs->mkdir(tmpl, 0700) != 0) {
        CHECK(!"mkdir of the test directory");
        return;
    }
    tf::Path dir(tmpl);
    tf::Path sub = dir.child("sub");
    tf::Path file = dir.child("file.txt");
    tf::Path link = dir.child("link");
    CHECK(fs->mkdir(sub.c_str(), 0755) == 0);
    CHECK(fs->mkdir(sub.c_str(), 0755) != 0);
    int err = 0;
    tf::Stream* s = fs->open(file.c_str(), tf::OPEN_WRITE | tf::OPEN_CREATE, 0644, &err);
    CHECK(s != 0 && err == 0);
    if (s) {
        CHECK(s->write("hello\n", 6) == 6);
        CHECK(s->close() == 0);
        delete s;
    }
    CHECK(fs->symlink("file.txt", link.c_str()) == 0);

    tf::Stat st;
    CHECK(fs->stat(file.c_str(), &st, false) == 0);
    CHECK(st.type == tf::FT_FILE && st.size == 6 && st.mode == 0644);
    CHECK(fs->stat(link.c_str(), &st, false) == 0 && st.type == tf::FT_LINK);
    CHECK(fs->stat(link.c_str(), &st, true) == 0 && st.type == tf::FT_FILE);
    CHECK(fs->stat(sub.c_str(), &st, false) == 0 && st.type == tf::FT_DIR);
    char target[64];
    CHECK(fs->readlink(link.c_str(), target, sizeof(target)) == 0);
    CHECK_STR(target, "file.txt");

    s = fs->open(file.c_str(), tf::OPEN_READ, 0, &err);
    if (s) {
        char buf[16];
        CHECK(s->read(buf, sizeof(buf)) == 6);
        CHECK(memcmp(buf, "hello\n", 6) == 0);
        CHECK(s->read(buf, sizeof(buf)) == 0);
        delete s;
    }

    tf::DirReader* d = fs->open_dir(dir.c_str(), &err);
    int seen = 0;
    if (d) {
        const char* n;
        while ((n = d->next()) != 0) {
            CHECK(strcmp(n, ".") != 0 && strcmp(n, "..") != 0);
            seen++;
        }
        delete d;
    }
    CHECK(seen == 3);
    CHECK(fs->open_dir(dir.child("nope").c_str(), &err) == 0 && err != 0);

    tf::Attrs a;
    a.mode = 0600;
    CHECK(fs->set_attrs(file.c_str(), a, tf::ATTR_MODE) == 0);
    CHECK(fs->stat(file.c_str(), &st, false) == 0 && st.mode == 0600);
    tf::Path moved = sub.child("moved.txt");
    CHECK(fs->rename(file.c_str(), moved.c_str()) == 0);
    CHECK(fs->remove(sub.c_str()) != 0);            /* not empty */
    CHECK(fs->remove(moved.c_str()) == 0);
    CHECK(fs->remove(link.c_str()) == 0);
    CHECK(fs->remove(sub.c_str()) == 0);
    tf::Space sp;
    CHECK(fs->space(dir.c_str(), &sp) == 0 && sp.total > 0 && sp.type[0]);
    CHECK(fs->remove(dir.c_str()) == 0);
}

static void write_file(tf::Fs* fs, const char* path, const char* text)
{
    int err = 0;
    tf::Stream* s = fs->open(path, tf::OPEN_WRITE | tf::OPEN_CREATE | tf::OPEN_TRUNCATE, 0644,
                             &err);
    if (s) {
        s->write(text, strlen(text));
        s->close();
        delete s;
    }
}

static bool read_is(tf::Fs* fs, const char* path, const char* text)
{
    int err = 0;
    tf::Stream* s = fs->open(path, tf::OPEN_READ, 0, &err);
    if (!s)
        return false;
    char buf[256];
    long n = s->read(buf, sizeof(buf) - 1);
    delete s;
    if (n < 0)
        return false;
    buf[n] = 0;
    return strcmp(buf, text) == 0;
}

static tf::Job::State run(tf::Job& j)
{
    tf::Job::State st;
    while ((st = j.step(50)) == tf::Job::WORKING) {
    }
    return st;
}

static void test_ops()
{
    tf::Fs* fs = tf::local_fs();
    char root[64];
    sprintf(root, "/tmp/tfops%ld", (long)getpid());
    tf::Path r(root);
    CHECK(fs->mkdir(root, 0755) == 0);
    tf::Path a = r.child("a"), b = r.child("b");
    fs->mkdir(a.c_str(), 0755);
    fs->mkdir(b.c_str(), 0755);
    fs->mkdir(a.child("sub").c_str(), 0750);
    write_file(fs, a.child("one.txt").c_str(), "one");
    write_file(fs, a.child("sub").child("two.txt").c_str(), "two");
    fs->symlink("one.txt", a.child("link").c_str());

    /* Copy the tree into b. */
    {
        tf::Job j(tf::OP_COPY, fs, fs);
        j.add(a.c_str());
        j.set_target(b.c_str(), true);
        CHECK(run(j) == tf::Job::DONE);
        CHECK(j.files_total() == 3 && j.files_done() == 3 && j.bytes_total() == 6);
    }
    tf::Path ba = b.child("a");
    CHECK(read_is(fs, ba.child("one.txt").c_str(), "one"));
    CHECK(read_is(fs, ba.child("sub").child("two.txt").c_str(), "two"));
    tf::Stat st;
    CHECK(fs->stat(ba.child("link").c_str(), &st, false) == 0 && st.type == tf::FT_LINK);
    CHECK(fs->stat(ba.child("sub").c_str(), &st, false) == 0 && st.mode == 0750);

    /* Again: overwrite questions. */
    write_file(fs, ba.child("one.txt").c_str(), "old");
    {
        tf::Job j(tf::OP_COPY, fs, fs);
        j.add(a.child("one.txt").c_str());
        j.set_target(ba.c_str(), true);
        CHECK(run(j) == tf::Job::ASK_OVERWRITE);
        CHECK(strcmp(j.target(), ba.child("one.txt").c_str()) == 0);
        j.answer(tf::ANS_NO);
        CHECK(run(j) == tf::Job::DONE);
    }
    CHECK(read_is(fs, ba.child("one.txt").c_str(), "old"));
    {
        tf::Job j(tf::OP_COPY, fs, fs);
        j.add(a.child("one.txt").c_str());
        j.set_target(ba.c_str(), true);
        CHECK(run(j) == tf::Job::ASK_OVERWRITE);
        j.answer(tf::ANS_YES);
        CHECK(run(j) == tf::Job::DONE);
    }
    CHECK(read_is(fs, ba.child("one.txt").c_str(), "one"));

    /* A copy under a new name, and into itself (refused). */
    {
        tf::Job j(tf::OP_COPY, fs, fs);
        j.add(a.child("one.txt").c_str());
        j.set_target(a.child("copy.txt").c_str(), false);
        CHECK(run(j) == tf::Job::DONE);
        CHECK(read_is(fs, a.child("copy.txt").c_str(), "one"));
    }
    {
        tf::Job j(tf::OP_COPY, fs, fs);
        j.add(a.c_str());
        j.set_target(a.child("sub").c_str(), true);
        CHECK(run(j) == tf::Job::ASK_ERROR);
        j.answer(tf::ANS_SKIP);
        CHECK(run(j) == tf::Job::DONE && j.failures() == 1);
    }

    /* Move (a rename) and delete. */
    {
        tf::Job j(tf::OP_MOVE, fs, fs);
        j.add(a.child("copy.txt").c_str());
        j.set_target(b.c_str(), true);
        CHECK(run(j) == tf::Job::DONE);
        CHECK(fs->stat(a.child("copy.txt").c_str(), &st, false) != 0);
        CHECK(read_is(fs, b.child("copy.txt").c_str(), "one"));
    }
    {
        tf::Job j(tf::OP_DELETE, fs, 0);
        j.add(a.c_str());
        j.add(b.c_str());
        CHECK(run(j) == tf::Job::DONE);
        CHECK(fs->stat(a.c_str(), &st, false) != 0 && fs->stat(b.c_str(), &st, false) != 0);
    }
    CHECK(fs->remove(root) == 0);

    CHECK(tf::match_pattern("*.txt", "a.txt", false));
    CHECK(!tf::match_pattern("*.txt", "a.txt.gz", false));
    CHECK(tf::match_pattern("*.TXT", "a.txt", true));
    CHECK(tf::match_pattern("a?c", "abc", false));
    CHECK(tf::match_pattern("[a-c]*", "build", false));
    CHECK(!tf::match_pattern("[!a-c]*", "build", false));
    CHECK(tf::match_pattern("*", "", false));
    CHECK(tf::match_pattern("*a*b*", "xxaxxbxx", false));
}

static const char* g_data = "tf/test/data";

/* Read a whole file through the VFS. */
static int slurp(const char* path, char* buf, size_t cap, long* total)
{
    int err = 0;
    tf::Stream* s = tf::vfs()->open(path, tf::OPEN_READ, 0, &err);
    *total = 0;
    if (!s)
        return err;
    for (;;) {
        long n = s->read(buf + *total, cap - 1 - (size_t)*total);
        if (n < 0) {
            delete s;
            return (int)-n;
        }
        if (n == 0)
            break;
        *total += n;
        if ((size_t)*total >= cap - 1)
            break;
    }
    buf[*total] = 0;
    delete s;
    return 0;
}

static bool lists(const char* dir, const char* name)
{
    int err = 0;
    tf::DirReader* d = tf::vfs()->open_dir(dir, &err);
    if (!d)
        return false;
    bool found = false;
    const char* n;
    while ((n = d->next()) != 0)
        if (strcmp(n, name) == 0)
            found = true;
    delete d;
    return found;
}

static void test_archive_paths()
{
    tf::Path b("/a");
    CHECK_STR(tf::Path::resolve(b, "b.tar/utar://x/y").c_str(), "/a/b.tar/utar://x/y");
    CHECK_STR(tf::Path::resolve(b, "/a/b.tar/utar://x/..").c_str(), "/a/b.tar/utar://");
    CHECK_STR(tf::Path::resolve(b, "/a/b.tar/utar://x/../..").c_str(), "/a");
    CHECK_STR(tf::Path("/a/b.tar/utar://").parent().c_str(), "/a");
    CHECK_STR(tf::Path("/a/b.tar/utar://x").parent().c_str(), "/a/b.tar/utar://");
    CHECK_STR(tf::Path("/a/b.tar/utar://x/y").parent().c_str(), "/a/b.tar/utar://x");
    CHECK_STR(tf::Path("/a/b.tar/utar://").name(), "b.tar");
    CHECK(tf::Path("/a/b.tar/utar://").is_archive_root());
    CHECK_STR(tf::Path("/a/b.tar/utar://").child("x").c_str(), "/a/b.tar/utar://x");
    CHECK(!tf::is_local_path("/a/b.tar/utar://x"));
    CHECK(tf::is_local_path("/a/b.tar"));
    CHECK_STR(tf::archive_scheme("x.TAR.GZ"), "utar");
    CHECK_STR(tf::archive_scheme("x.tgz"), "utar");
    CHECK_STR(tf::archive_scheme("x.zip"), "uzip");
    CHECK_STR(tf::archive_scheme("x.txt.gz"), "ugz");
    CHECK_STR(tf::archive_scheme("x.Z"), "uz");
    CHECK(tf::archive_scheme("x.txt") == 0);
}

static void test_archives()
{
    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd)))
        return;
    tf::Path data = tf::Path::resolve(tf::Path(cwd), g_data);
    static const char* const tars[] = {
        "t.tar/utar://", "t_pax.tar/utar://", "t.tar.gz/utar://", "t.tar.bz2/utar://",
        "t.tar.Z/utar://", "t.zip/uzip://", "t_stored.zip/uzip://", 0
    };
    static char first_big[80000];
    long first_len = -1;
    for (int i = 0; tars[i]; i++) {
        tf::Path root = data.child(tars[i]);
        char path[2048], buf[80000];
        long n = 0;
        tf::Stat st;
        printf("  %s\n", tars[i]);
        CHECK(tf::vfs()->stat(root.c_str(), &st, true) == 0 && st.type == tf::FT_DIR);
        CHECK(lists(root.c_str(), "t"));
        sprintf(path, "%s/t", root.c_str());
        CHECK(lists(path, "hello.txt"));
        CHECK(lists(path, "dir"));
        CHECK(lists(path, "a_name_that_is_longer_than_the_hundred_characters_a_plain_tar_header_can_hold_so_it_needs_an_extension.txt"));
        sprintf(path, "%s/t/hello.txt", root.c_str());
        CHECK(slurp(path, buf, sizeof(buf), &n) == 0 && strcmp(buf, "hello\n") == 0);
        CHECK(tf::vfs()->stat(path, &st, false) == 0 && st.type == tf::FT_FILE && st.size == 6);
        sprintf(path, "%s/t/dir/sub/deep.txt", root.c_str());
        CHECK(slurp(path, buf, sizeof(buf), &n) == 0 && strcmp(buf, "deep\n") == 0);
        sprintf(path, "%s/t/link", root.c_str());
        char target[256] = "";
        CHECK(tf::vfs()->stat(path, &st, false) == 0 && st.type == tf::FT_LINK);
        CHECK(tf::vfs()->readlink(path, target, sizeof(target)) == 0 &&
              strcmp(target, "hello.txt") == 0);
        CHECK(tf::vfs()->stat(path, &st, true) == 0 && st.type == tf::FT_FILE);
        sprintf(path, "%s/t/big.bin", root.c_str());
        CHECK(slurp(path, buf, sizeof(buf), &n) == 0 && n == 70000);
        if (first_len < 0) {
            memcpy(first_big, buf, (size_t)n);
            first_len = n;
        } else {
            CHECK(n == first_len && memcmp(first_big, buf, (size_t)n) == 0);
        }
        /* Archives are read-only. */
        sprintf(path, "%s/t/new", root.c_str());
        CHECK(tf::vfs()->mkdir(path, 0755) == EROFS);
    }
    /* Single compressed files. */
    static const char* const singles[] = {
        "hello.txt.gz/ugz://hello.txt", "hello.txt.bz2/ubz2://hello.txt",
        "hello.txt.Z/uz://hello.txt", 0
    };
    for (int i = 0; singles[i]; i++) {
        char buf[64];
        long n = 0;
        CHECK(slurp(data.child(singles[i]).c_str(), buf, sizeof(buf), &n) == 0 &&
              strcmp(buf, "hello\n") == 0);
    }
    /* Names that would leave the archive are left out. */
    tf::Path evil = data.child("evil.tar/utar://");
    CHECK(lists(evil.c_str(), "fine.txt"));
    CHECK(lists(evil.c_str(), "abs.txt"));
    CHECK(!lists(evil.c_str(), "escape.txt"));
    CHECK(!lists(evil.c_str(), "up.txt"));
    /* A cut-off archive does not open. */
    tf::Stat st;
    CHECK(tf::vfs()->stat(data.child("truncated.tar.gz/utar://").c_str(), &st, true) != 0);

    /* Copying out of an archive; moving out fails on the delete. */
    char out[64];
    sprintf(out, "/tmp/tfarc%ld", (long)getpid());
    tf::local_fs()->mkdir(out, 0755);
    {
        tf::Job j(tf::OP_COPY, tf::vfs(), tf::vfs());
        j.add(data.child("t.tar.gz/utar://t/dir").c_str());
        j.set_target(out, true);
        CHECK(run(j) == tf::Job::DONE);
        char p[200], buf[64];
        long n = 0;
        sprintf(p, "%s/dir/sub/deep.txt", out);
        CHECK(slurp(p, buf, sizeof(buf), &n) == 0 && strcmp(buf, "deep\n") == 0);
    }
    {
        tf::Job j(tf::OP_MOVE, tf::vfs(), tf::vfs());
        j.add(data.child("t.zip/uzip://t/hello.txt").c_str());
        j.set_target(out, true);
        CHECK(run(j) == tf::Job::ASK_ERROR && j.error() == EROFS);
        j.answer(tf::ANS_SKIP);
        CHECK(run(j) == tf::Job::DONE);
        char p[200];
        sprintf(p, "%s/hello.txt", out);
        CHECK(tf::local_fs()->stat(p, &st, false) == 0);
    }
    tf::Job cleanup(tf::OP_DELETE, tf::local_fs(), 0);
    cleanup.add(out);
    run(cleanup);
}

int main(int argc, char** argv)
{
    if (argc > 1)
        g_data = argv[1];
    test_path();
    test_local();
    test_ops();
    test_archive_paths();
    test_archives();
    printf("tf tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
