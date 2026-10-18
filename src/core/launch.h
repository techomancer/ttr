#ifndef TTR_CORE_LAUNCH_H
#define TTR_CORE_LAUNCH_H

#include "core/str.h"

namespace ttr {

/* Run a shell command, detached: it is not our child and has its own
 * session, so it outlives us and leaves no zombies. */
void spawn(const char* command);

/* 's' in single quotes, for a shell command line. */
Str shell_quote(const char* s);

}

#endif
