/*
 * ttrc, To The Roots Commander (ttrc/DESIGN.md).
 *
 *   ttrc [-P file] [left-dir [right-dir]]
 *
 * -P writes the active panel's directory to file at exit, for the `tc`
 * shell function (section 4.8).
 */
#include "app.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char** argv)
{
    const char* pwd_file = 0;
    const char* dirs[2] = { 0, 0 };
    int nd = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-P") == 0 && i + 1 < argc) {
            pwd_file = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("usage: ttrc [-P file] [left-dir [right-dir]]\n");
            return 0;
        } else if (nd < 2) {
            dirs[nd++] = argv[i];
        } else {
            fprintf(stderr, "usage: ttrc [-P file] [left-dir [right-dir]]\n");
            return 2;
        }
    }
    if (!isatty(0) || !isatty(1)) {
        fprintf(stderr, "ttrc: needs a terminal\n");
        return 1;
    }
    ttrc::set_program_path(argv[0]);
    tu::Term term;
    if (!term.open(0, 1)) {
        fprintf(stderr, "ttrc: no terminal description for TERM\n");
        return 1;
    }
    tf::Path cwd = tf::Path::cwd();
    tf::Path left = dirs[0] ? tf::Path::resolve(cwd, dirs[0]) : cwd;
    tf::Path right = dirs[1] ? tf::Path::resolve(cwd, dirs[1]) : left;
    int rc;
    {
        ttrc::App app(term, left, right, nd > 0);
        rc = app.run();
        if (pwd_file) {
            FILE* f = fopen(pwd_file, "w");
            if (f) {
                fprintf(f, "%s\n", app.active_path().c_str());
                fclose(f);
            }
        }
    }
    return rc;
}
