// libnx part of the updater.

#ifdef ENABLE_UPDATER

#include <string>

#include <switch.h>

namespace Updater {

// Asks the loader to run `nroPath` instead of returning to the homebrew menu when this process
// exits.
bool QueueNextLoad(const std::string& nroPath) {
    if (!envHasNextLoad()) {
        return false;
    }
    return R_SUCCEEDED(envSetNextLoad(nroPath.c_str(), nroPath.c_str()));
}

} // namespace Updater

#endif // ENABLE_UPDATER
