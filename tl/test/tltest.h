/* Minimal test support for tl: a failure counter and check macros. */
#ifndef TL_TEST_TLTEST_H
#define TL_TEST_TLTEST_H

#include "../port.h"

extern int tl_test_failures;
extern int tl_test_checks;

void tl_test_fail(const char* file, int line, const char* what);
bool tl_check_str(const char* file, int line, const char* got, const char* want);

#define TL_CHECK(cond) \
    do { tl_test_checks++; if (!(cond)) tl_test_fail(__FILE__, __LINE__, #cond); } while (0)
#define TL_CHECK_STR(got, want) \
    do { tl_test_checks++; tl_check_str(__FILE__, __LINE__, (got), (want)); } while (0)

/* Each test file exports one entry point. */
void tl_test_parser();
void tl_test_unicode();
void tl_test_terminal();
void tl_test_vterm();

/* Directory with test data files; from -d or TL_TEST_DATA. */
extern const char* tl_test_data_dir;
/* libvterm's t/ directory; from -v or TL_TEST_VTERM. */
extern const char* tl_test_vterm_dir;

#endif
