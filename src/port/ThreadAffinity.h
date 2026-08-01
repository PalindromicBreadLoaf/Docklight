#ifndef PORT_THREAD_AFFINITY_H
#define PORT_THREAD_AFFINITY_H

// Core placement for the port's long-lived threads.

typedef enum PortThreadRole {
    // Window thread
    PORT_ROLE_RENDER = 0,
    // push_frame: game logic, and building the display lists the other two
    // stages carry.
    PORT_ROLE_TICK,
    // thread5, audio manager, vimgr, pfsmanager, rumble, watchdog.
    PORT_ROLE_AUX,
    // The 60Hz retrace source.
    PORT_ROLE_TIMER,
} PortThreadRole;

#ifdef __cplusplus
extern "C" {
#endif

// Called by a thread on itself, once, before it enters its loop.
void port_pinCurrentThread(PortThreadRole role);

// Which core the calling thread is on right now, or -1 where the platform has
// no answer.
int port_currentCore(void);

#ifdef __cplusplus
}
#endif

#endif // PORT_THREAD_AFFINITY_H
