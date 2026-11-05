#pragma once

// The identity of the running game, for the handshake. Call after the game has loaded its archives.

#include "identity.h"

#ifdef __cplusplus
extern "C" {
#endif

void CoopIdentity_FromGame(CoopIdentity* out);

#ifdef __cplusplus
}
#endif
