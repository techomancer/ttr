/* Tests for tf: path normalization and the local file system. */
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

int main()
{
    test_path();
    test_local();
    printf("tf tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
