#pragma once

#include <thread>
#include <utility>

// devkitA64's pthread_detach is a stub that returns ENOSYS unconditionally.
// Leak the thread handle instead. The OS thread keeps running and nothing ever joins
// or destroys the object.
inline void port_detachThread(std::thread&& worker) {
    if (!worker.joinable()) {
        return;
    }
#ifdef __SWITCH__
    new std::thread(std::move(worker));
#else
    worker.detach();
#endif
}
