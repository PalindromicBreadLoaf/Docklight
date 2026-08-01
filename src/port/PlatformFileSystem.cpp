#include "port/PlatformFileSystem.h"

#include <cerrno>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#if defined(__SWITCH__)
#include <switch.h>
#endif

extern "C" bool port_syncFile(const char* path) {
#if defined(_WIN32)
    const int fd = _open(path, _O_WRONLY | _O_BINARY);
    if (fd < 0) {
        return false;
    }
    const bool synced = _commit(fd) == 0;
    const bool closed = _close(fd) == 0;
#else
    const int fd = open(path, O_WRONLY);
    if (fd < 0) {
        return false;
    }

    int syncResult;
    do {
        syncResult = fsync(fd);
    } while (syncResult != 0 && errno == EINTR);

    const bool synced = syncResult == 0;
    const bool closed = close(fd) == 0;
#endif
    return synced && closed;
}

extern "C" bool port_commitStorage(void) {
#if defined(__SWITCH__)
    return R_SUCCEEDED(fsdevCommitDevice("sdmc"));
#else
    return true;
#endif
}
