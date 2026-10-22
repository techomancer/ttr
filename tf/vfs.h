/*
 * tf/vfs.h - the file system ttrc uses for everything: local paths, and
 * paths into archives in mc's notation (ttrc/DESIGN.md section 5.1),
 *
 *   /home/me/src/ttr-1.0.tar.gz/utar://ttr/README.md
 *   /data/old.zip/uzip://backups/home.tar/utar://me/.profile
 *
 * Archives are opened when a path first goes into them and kept in a
 * small cache, checked against the archive file's size and time.
 */
#ifndef TF_VFS_H
#define TF_VFS_H

#include "tf.h"

namespace tf {

Fs* vfs();

/* The scheme ttrc enters a file with, from its name ("utar" for
 * x.tar.gz, "uzip", "ugz", "ubz2", "uz"), or 0. */
const char* archive_scheme(const char* name);
/* Not inside an archive. */
bool is_local_path(const char* path);

} /* namespace tf */

#endif
