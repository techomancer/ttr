/*
 * The wrapper through which tf compiles libbzip2 (third_party/bzip2,
 * pinned at 1.0.8). bzlib.c needs the compressor's entry points too, so
 * all of the library is here; tf only decompresses.
 */
#define BZ_NO_STDIO
#include "../../third_party/bzip2/blocksort.c"
#include "../../third_party/bzip2/huffman.c"
#include "../../third_party/bzip2/crctable.c"
#include "../../third_party/bzip2/randtable.c"
#include "../../third_party/bzip2/compress.c"
#include "../../third_party/bzip2/decompress.c"
#include "../../third_party/bzip2/bzlib.c"

/* With BZ_NO_STDIO the library leaves this to its user. It is called only
 * when bzip2's own consistency checks fail (a bug, not bad input); the
 * library cannot continue then. */
#include <stdlib.h>
void bz_internal_error(int errcode)
{
    (void)errcode;
    abort();
}
