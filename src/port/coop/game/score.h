#pragma once

// Who scores for the team, and where a life goes, when more than one human flies (#210).
//
// Decision 0008, 4b: gHitCount is a team total. A kill by any human player scores as one by player index 0 does, the
// thresholds stay, DMG_SRC_2 (what an enemy's hit leaves, and what upstream also gives player index 1) stays unscored,
// and a computer wingman scores no more than it does today. Decision 0008, 1a: Katina's Saucerer takes damage from
// every human player. Decision 0007, rules 1 and 2: the team has one pool of lives, gLifeCount[0], and anything that
// adds a life adds it there, whoever collected it.
//
// A human is told apart by the value CoopPlayers_DmgSource writes (players.h): player 0 keeps upstream's
// AI360_FOX + 1, players 1 to 3 get COOP_DMG_SRC_PLAYER + index in co-op, a value no game code writes or tests. A
// Landmaster's or a Blue Marine's shot carries the player index as an Arwing's does (Player_SetupTankShot,
// Player_SetupArwingShot in src/engine/fox_play.c), so they count the same way.
//
// Co-op off: player 0 only, and the slot the game names, so each function gives upstream's answer.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Nonzero when an actor's dmgSource is a human's kill: player 0's (AI360_FOX + 1), or in co-op that of a player 1 to 3
// of this level. Not DMG_SRC_2, not a computer wingman's (NPC_SHOT_ID + actor + 1), not a cutscene's.
int32_t CoopScore_HumanKill(int32_t dmgSource);

// Nonzero when a PlayerShot's sourceId is a human's: player 0 (TEAM_ID_FOX), or in co-op a player 1 to 3 of this level.
int32_t CoopScore_HumanShot(int32_t sourceId);

// The gLifeCount slot a life gained by player `slot` goes to: the pool, slot 0, in co-op; `slot` otherwise.
int32_t CoopScore_LifeSlot(int32_t slot);

#ifdef __cplusplus
}
#endif
