#ifndef TTR_SHELL_LAUNCH_H
#define TTR_SHELL_LAUNCH_H

namespace ttr {

/* Run a shell command, detached: it is not our child and has its own
 * session, so it outlives us and leaves no zombies. */
void spawn(const char* command);

}

#endif
