#include "port/ThreadAffinity.h"

#ifdef __SWITCH__

#include <spdlog/spdlog.h>
#include <switch.h>

namespace {

struct Placement {
    int preferred; // core the thread runs on when it has a choice
    u32 mask;      // cores it may run on
    const char* name;
};

// Indexed by PortThreadRole.
constexpr Placement kPlacements[] = {
    { 0, 1u << 0, "render" },
    { 1, 1u << 1, "tick" },
    { 2, 1u << 2, "aux" },
    { 2, 0b111u, "timer" },
};

static_assert(sizeof(kPlacements) / sizeof(kPlacements[0]) == PORT_ROLE_TIMER + 1,
              "every PortThreadRole needs a placement");

// The cores this process actually has.
u32 AllowedCores() {
    static const u32 allowed = [] {
        u64 mask = 0;
        Result rc = svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
        if (R_FAILED(rc) || mask == 0) {
            SPDLOG_WARN("Could not read the process core mask (rc {:#x}).", rc);
            return 0u;
        }
        SPDLOG_INFO("Process core mask {:#b} ({} cores), main thread starts on core {}", (u32)mask,
                    __builtin_popcountll(mask), svcGetCurrentProcessorNumber());
        return (u32)mask;
    }();
    return allowed;
}

} // namespace

extern "C" void port_pinCurrentThread(PortThreadRole role) {
    const u32 allowed = AllowedCores();
    if (allowed == 0) {
        return;
    }

    const Placement& want = kPlacements[role];
    u32 mask = want.mask & allowed;
    if (mask == 0) {
        // None of the cores this role asked for are ours.
        SPDLOG_WARN("{} thread wanted core mask {:#b} but the process only has {:#b}", want.name,
                    want.mask, allowed);
        mask = allowed;
    }
    const int preferred = ((mask >> want.preferred) & 1u) != 0 ? want.preferred : __builtin_ctz(mask);

    Result rc = svcSetThreadCoreMask(CUR_THREAD_HANDLE, preferred, mask);
    if (R_FAILED(rc)) {
        SPDLOG_WARN("{} thread: svcSetThreadCoreMask(core {}, mask {:#b}) failed, rc {:#x}", want.name, preferred, mask,
                    rc);
        return;
    }
    // Read back rather than repeat the request.
    SPDLOG_INFO("{} thread -> core {} (mask {:#b}), running on core {}", want.name, preferred, mask,
                svcGetCurrentProcessorNumber());
}

extern "C" int port_currentCore(void) {
    return (int)svcGetCurrentProcessorNumber();
}

#else

extern "C" void port_pinCurrentThread(PortThreadRole role) {
    (void)role;
}

extern "C" int port_currentCore(void) {
    return -1;
}

#endif
