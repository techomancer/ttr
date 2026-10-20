#include "tltest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int tl_test_failures = 0;
int tl_test_checks = 0;
const char* tl_test_data_dir = "tl/test/data";

void tl_test_fail(const char* file, int line, const char* what)
{
    tl_test_failures++;
    fprintf(stderr, "%s:%d: FAIL: %s\n", file, line, what);
}

bool tl_check_str(const char* file, int line, const char* got, const char* want)
{
    if (strcmp(got, want) == 0)
        return true;
    tl_test_failures++;
    fprintf(stderr, "%s:%d: FAIL:\n     got: %s\n    want: %s\n", file, line, got, want);
    return false;
}

int main(int argc, char** argv)
{
    const char* env = getenv("TL_TEST_DATA");
    if (env)
        tl_test_data_dir = env;
    env = getenv("TL_TEST_VTERM");
    if (env)
        tl_test_vterm_dir = env;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 && i + 1 < argc)
            tl_test_data_dir = argv[++i];
        else if (strcmp(argv[i], "-v") == 0 && i + 1 < argc)
            tl_test_vterm_dir = argv[++i];
    }

    tl_test_parser();
    tl_test_unicode();
    tl_test_terminal();
    tl_test_view();
    tl_test_vterm();

    printf("tl tests: %d checks, %d failures\n", tl_test_checks, tl_test_failures);
    return tl_test_failures ? 1 : 0;
}
