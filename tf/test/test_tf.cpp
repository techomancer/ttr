/* Tests for tf: path normalization and the local file system. */
#include "../ops.h"
#include "../path.h"
#include "../tf.h"

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

int main()
{
    test_path();
    test_local();
    test_ops();
    printf("tf tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
