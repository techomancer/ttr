/*
 * ttre, the To The Roots editor (ttre/DESIGN.md).
 *
 *   ttre [+LINE] [file]
 *
 * One file in a terminal; a file that does not exist yet is created on
 * the first save. Usable as $EDITOR and $VISUAL; ttrc's F4 runs it.
 */
#include "editor.h"

#include "../../th/th.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace {

void usage(FILE* f)
{
    fprintf(f, "usage: ttre [+LINE] [file]\n");
}

} /* namespace */

int main(int argc, char** argv)
{
    const char* file = 0;
    long line = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        } else if (argv[i][0] == '+' && argv[i][1] >= '0' && argv[i][1] <= '9') {
            line = atol(argv[i] + 1);
        } else if (strcmp(argv[i], "--") == 0 && i + 1 < argc && !file) {
            file = argv[++i];
        } else if (argv[i][0] == '-' && argv[i][1]) {
            usage(stderr);
            return 2;
        } else if (!file) {
            file = argv[i];
        } else {
            usage(stderr);
            return 2;
        }
    }
    if (!isatty(0) || !isatty(1)) {
        fprintf(stderr, "ttre: needs a terminal\n");
        return 1;
    }
    th::set_program_path(argv[0]);
    tu::Term term;
    if (!term.open(0, 1)) {
        fprintf(stderr, "ttre: no terminal description for TERM\n");
        return 1;
    }
    ttre::Editor ed(term, file, line);
    int err = ed.load();
    if (err) {
        fprintf(stderr, "ttre: %s: %s\n", file, strerror(err));
        return 1;
    }
    return ed.run();
}
