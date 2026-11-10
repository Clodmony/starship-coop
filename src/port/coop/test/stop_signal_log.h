#pragma once

// Where libultraship writes its log (logs/<name>.log in the app directory), for the crash report of stop_signal.c
// (#158). Writes an empty string when there is no context yet.

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void CoopStopSignal_LogPath(char* out, size_t size);

#ifdef __cplusplus
}
#endif
