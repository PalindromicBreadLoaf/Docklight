#ifndef PORT_PLATFORM_FILE_SYSTEM_H
#define PORT_PLATFORM_FILE_SYSTEM_H

#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Flushes a closed file's contents to its backing storage.
bool port_syncFile(const char* path);

// Commits pending filesystem metadata where the platform requires it.
bool port_commitStorage(void);

#ifdef __cplusplus
}
#endif

#endif // PORT_PLATFORM_FILE_SYSTEM_H
