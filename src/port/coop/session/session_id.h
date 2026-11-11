#pragma once

// A session id: a random 64-bit number the host makes for each session it opens, never 0. It comes from the
// operating system's random source and a clock, outside the simulation: nothing of the game's seeds goes in, and
// nothing of it reaches the game. See CoopSession_SetId (session.h) for what it is for.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint64_t CoopSessionId_New(void);

#ifdef __cplusplus
}
#endif
