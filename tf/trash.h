/*
 * tf/trash.h - the freedesktop.org trash (Trash specification 1.0), the
 * one GNOME, KDE, Xfce and every Linux file manager share: a file goes
 * into Trash/files under a free name, and Trash/info/<name>.trashinfo
 * records where it came from and when, so any of them can restore it.
 *
 * A file on the home trash's file system goes to $XDG_DATA_HOME/Trash
 * (~/.local/share/Trash); one elsewhere to its file system's own,
 * $topdir/.Trash/$uid (when the administrator made .Trash, sticky) or
 * $topdir/.Trash-$uid. Plain POSIX on local paths: it builds everywhere,
 * though only Linux desktops look there (ttrv/DESIGN.md section 2.3).
 */
#ifndef TF_TRASH_H
#define TF_TRASH_H

#include <stddef.h>

namespace tf {

/* Move path (absolute, local) to the trash. 0, or an errno value:
 * EXDEV when its file system has no trash we may use (the caller offers
 * to delete it instead). 'where', if given, gets the path it went to. */
int trash(const char* path, char* where = 0, size_t n = 0);

/* The home trash's directory ($XDG_DATA_HOME/Trash), not made. */
void trash_home(char* out, size_t n);

} /* namespace tf */

#endif
