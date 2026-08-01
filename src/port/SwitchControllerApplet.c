// Suppresses SDL's Switch controller-support applet.

#ifdef __SWITCH__

#include <switch.h>

Result __wrap_hidLaShowControllerSupportForSystem(HidLaControllerSupportResultInfo* result_info,
                                                  const HidLaControllerSupportArg* arg, bool flag) {
    (void)arg;
    (void)flag;
    if (result_info != NULL) {
        HidLaControllerSupportResultInfo empty = { 0 };
        *result_info = empty;
    }
    // SDL discards this.
    return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
}

#endif
