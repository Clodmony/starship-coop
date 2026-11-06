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
// section starts: once player 0 is active on rails in an Arwing (WP-12) or in all-range mode (WP-11), as a copy of
// player 0 placed beside it. It parks the player again when player 0 stops flying, for example in the cut from rails
// to all-range, and brings it back when player 0 flies again.
int32_t CoopPlayers_SkipUpdate(int32_t index);

struct Player;

// Call at the start of Player_UpdatePath. Nonzero means `player` is a co-op player other than player 0: it took player
// 0's place on the path and Player_UpdatePath must return, because the rest would write the path globals and move the
// path by this player's speed. The player keeps its own x and y inside the flight box and its own camDist, so boost
// and brake move it ahead of and behind player 0 without changing the speed along the path.
int32_t CoopPlayers_FollowPath(struct Player* player);

// Points gInputHold, gInputPress and gControllerRumble at player `index`, in co-op only: its pad slot, which holds that
// player's input for this frame whoever steers it (input.h), and its rumble flag.
void CoopPlayers_SelectInput(int32_t index);

// The pad slot to read for something that belongs to player `index`, such as a shot of that player: its own slot in
// co-op, gMainController otherwise, as upstream.
int32_t CoopPlayers_PadOf(int32_t index);

struct Actor;

// Call in Actor_Update where the campaign runs down its lock-on. Nonzero means co-op, and the lock-ons of every player
// on `actor` were handled here: each player's runs down on its own A. Upstream only looks at player 0's.
int32_t CoopPlayers_ReleaseLockOns(struct Actor* actor);

// Call at the start of Player_Down. Nonzero means a co-op player other than player 0 went down: it is taken out of
// the level and Player_Down must return, because it also stops the music, plays the "player down" theme and starts
// the fade to a level restart for everyone. Respawn rules are WP-16.
int32_t CoopPlayers_OnPlayerDown(struct Player* player);

#define COOP_PLAYERS_DEBUG_CVAR "gCoopDebugPlayerCount"

// The slot of player `index` as a plain number, for the state hash: a number has the same size on every compiler, an
// enum need not. Which number means what is private to players.c.
int32_t CoopPlayers_SlotState(int32_t index);

// Test only: sets what CoopPlayers_SlotState returns. The state hash probe changes a slot, hashes and puts it back.
void CoopPlayers_SetSlotState(int32_t index, int32_t state);

#ifdef __cplusplus
}
#endif
