/*
 * libdwarf's dwarf_line.c, compiled through this wrapper (AGENTS.md: third-party
 * code is never edited). On IRIX, an `#ifdef __sgi` block in
 * dwarf_line_table_reader_common.h sets a field (li_offset) the line
 * structure no longer has, so __sgi is taken away for this file after
 * the system headers it uses are in (td/DESIGN.md 5.4). Nothing else in
 * the file depends on __sgi.
 */
#include <config.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#undef __sgi
#include "../../third_party/libdwarf/src/lib/libdwarf/dwarf_line.c"
