#ifndef TTR_CORE_LAUNCH_H
#define TTR_CORE_LAUNCH_H

namespace ttr {

/* Run a shell command, detached: it is not our child and has its own
 * session, so it outlives us and leaves no zombies. */
void spawn(const char* command);

}

#endif
