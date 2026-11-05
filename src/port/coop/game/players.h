#pragma once

// How many Player structs the game simulates, apart from how many cameras it renders (WP-10).
//
// Upstream ties both to gCamCount: 1 in the campaign, 4 in versus. gCamCount is also read as "is this versus?" in
// about 25 places (gCamCount != 1, gCamCount == 4), so co-op keeps gCamCount at 1 and counts players here instead.
// Loops that run once per player use CoopPlayers_Count(); loops and checks that mean "per camera" or "versus" keep
// gCamCount. See docs/coop/NOTES.md (WP-10) for the list of sites.
//
// Co-op off: CoopPlayers_Count() == gCamCount, so the game behaves as upstream.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COOP_PLAYERS_MAX 4 // the per-player arrays in sf64context.h have 4 slots

// Fixes the player count for the level being started. Call it in Play_Init, after gCamCount is set and before
// gPlayer is allocated. Until the co-op session exists (WP-21, WP-24), the count comes from the debug CVar
// COOP_PLAYERS_DEBUG_CVAR (default 1, clamped to 1..COOP_PLAYERS_MAX); outside the campaign (gCamCount != 1) it is
// always gCamCount.
void CoopPlayers_LevelInit(void);

// Number of Player structs in gPlayer for the current level.
int32_t CoopPlayers_Count(void);

// Nonzero when more players are simulated than cameras rendered, which only co-op does.
int32_t CoopPlayers_IsCoop(void);

// Index of the player this client controls and looks through. 0 for now; WP-28 makes it per client.
int32_t CoopPlayers_LocalIndex(void);

// Nonzero while player `index` is parked: allocated, but not updated, not given a camera update and not drawn.
// In co-op, players 1 and up start parked and join when the level reaches a section that supports them.
int32_t CoopPlayers_IsParked(int32_t index);

// Call before Player_Update for player `index`; nonzero means skip it this frame. Brings a parked player in when its
// section starts: in all-range mode, once player 0 is active (after the all-range cutscene), as a copy of player 0
// placed beside it (WP-11). Players on rails stay parked (WP-12).
int32_t CoopPlayers_SkipUpdate(int32_t index);

#define COOP_PLAYERS_DEBUG_CVAR "gCoopDebugPlayerCount"

#ifdef __cplusplus
}
#endif
