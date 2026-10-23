/*
 * te/file.h - documents to and from files, for the editors that share te
 * (ttrcode, ttre). Saving writes a new file next to the old one and
 * renames it over it, so a full disk never leaves half a file; the old
 * file's permissions are kept, and a symbolic link is written through.
 */
#ifndef TE_FILE_H
#define TE_FILE_H

#include "document.h"

#include <time.h>

namespace te {

/* The file into the document (empty when it does not exist: ENOENT is
 * returned and the document is empty). 0 or an errno value; *mtime, if
 * given, gets the file's modification time. */
int load_file(Document& doc, const char* path, time_t* mtime);
/* 0 or an errno value; on success the document is marked saved. */
int save_file(Document& doc, const char* path, time_t* mtime);

} /* namespace te */

#endif
