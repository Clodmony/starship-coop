#pragma once

// What every player's game starts a level from (WP-24). The host's game fills one in, the session carries it to the
// clients, and every game, the host's too, enters the level from it. So nobody's own history decides: not the seeds a
// game got from the clock at boot, not the team values no level start clears. See docs/coop/PROTOCOL.md, Level start.
//
// The session reads this header and nothing else from the game: to it the values are numbers, so a new one is a new row
// in the table in level_start.c.

#include <stdint.h>

#include "players.h"

#ifdef __cplusplus
extern "C" {
#endif

// How many values the table in level_start.c holds.
#define COOP_LEVEL_START_VALUES 87
// Levels below this can be started: LEVEL_VERSUS and up (include/sf64level.h) are not co-op levels.
#define COOP_LEVEL_START_LEVELS 20

typedef struct {
    uint8_t level;                    // LevelId
    uint8_t phase;                    // gLevelPhase
    uint8_t roster[COOP_PLAYERS_MAX]; // who flies what, as CoopRoster; CoopSession_StartLevel fills this in
    int32_t values[COOP_LEVEL_START_VALUES];
    uint8_t cleared; // 1 when the host's save has the level cleared (normalClear), so Start may skip its intro (#191)
} CoopLevelStart;

// Host: level and phase as given, values and the cleared flag from this game as they are now.
void CoopLevelStart_FromGame(CoopLevelStart* out, uint8_t level, uint8_t phase);

// Puts the values in place and sends the game to the level on this frame, from whatever it is doing. Call it from a
// GamePreUpdateEvent listener or anywhere else outside Game_Update, so that nothing runs between the values and the
// level. The players of the roster stay in the level until CoopPlayers_SetRoster(NULL). The audio heap is reset on the
// way in, also when the level's audio spec is the one loaded, so every game's sound thread starts over there (#230).
void CoopLevelStart_Enter(const CoopLevelStart* start);

// Nonzero when the last level start this game entered said the host's save has `level` cleared: what the intro skip
// reads in a session instead of this game's own save (#191). Only the level of that start: a later level of the
// session is not in it, so 0, as is a game that entered none.
int32_t CoopLevelStart_Cleared(int32_t level);

#ifdef __cplusplus
}
#endif
