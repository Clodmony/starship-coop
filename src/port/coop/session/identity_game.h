#pragma once

// The identity of the running game, for the handshake. Call after the game has loaded its archives.

#include "identity.h"

#ifdef __cplusplus
extern "C" {
#endif

// The git commit this game was built from; every use of the id goes through here.
const char* CoopIdentity_BuildId(void);

void CoopIdentity_FromGame(CoopIdentity* out);

#ifdef __cplusplus
}
#endif
