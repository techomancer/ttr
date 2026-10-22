/*
 * The wrappers through which tf compiles zlib (third_party/zlib, pinned
 * at 1.3.1; AGENTS.md: third-party code is never edited, only compiled
 * through wrapper files). Only inflating is needed: gzip files and
 * deflated zip entries are read, never written.
 *
 * zlib's inflate sources cannot share one translation unit (inftrees.h
 * has no include guard), so inflate.c, inffast.c and inftrees.c each have
 * their own wrapper: zlib_inflate.c, zlib_inffast.c, zlib_inftrees.c.
 */
#include "../../third_party/zlib/adler32.c"
#include "../../third_party/zlib/crc32.c"
#include "../../third_party/zlib/zutil.c"
