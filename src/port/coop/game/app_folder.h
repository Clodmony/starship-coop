#pragma once

// The game's own folder, where its config, saves and logs are: Ship::Context::GetAppDirectoryPath, which follows
// SHIP_HOME where the port reads it. For files a player has to find, such as a desync report (WP-23).

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Without a trailing separator. Empty if it is not known.
void CoopAppFolder_Path(char* out, size_t cap);

#ifdef __cplusplus
}
#endif
