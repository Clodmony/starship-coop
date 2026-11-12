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

// Hits per player, for display only (#252, decision 0008 4d on top of 4b). In co-op a human's kill also adds its hits
// to gPlayerScores[that player], next to the team's gHitCount, where the game knows the shooter: an actor's dmgSource
// or a PlayerShot's sourceId. Hits with no shooter (chain blasts, scripted kills, bosses) count for the team only, so
// the per-player numbers add up to at most gHitCount. Versus and co-op off: nothing.
void CoopScore_CreditKill(int32_t dmgSource, int32_t hits);
void CoopScore_CreditShot(int32_t sourceId, int32_t hits);

// Call where the game saves gHitCount at a checkpoint: the per-player hits are saved with it.
void CoopScore_Checkpoint(void);

// Call after the level start zeroes gPlayerScores: a restart from a checkpoint (gSavedHitCount above 0) gives back the
// hits saved there, as the game gives back gHitCount; a new level keeps 0 and forgets the saved ones.
void CoopScore_LevelInit(void);

// Each player's own hits as saved at the last checkpoint, for a level start that begins at a checkpoint
// (level_start.h): the host's go to every game with the checkpoint, and CoopScore_LevelInit gives them back there.
int32_t CoopScore_SavedHits(int32_t player);
void CoopScore_SetSavedHits(int32_t player, int32_t hits);

// The status screen (pause and level clear): a window at the top right with a title and one row per human, the
// character's face, name and that player's own hits. On the level clear screen also "OK" for each human who has
// pressed A, and "PRESS A" while the scene holds. Nothing outside co-op or with one player.
void CoopScore_DrawStatus(void);

// The results hold: in a co-op session the level clear status screen stays until every human has pressed A or B, 30 s
// at most (see score.c for the frames). Registers a PlayUpdateEvent listener that holds the scene; call once at boot.
void CoopScore_Init(void);

// Nonzero while the scene holds for the results. Simulation state: the same frames on every game of a session.
int32_t CoopScore_ResultsHolding(void);

// Nonzero once player `index` has pressed A or B on the level clear screen shown now.
int32_t CoopScore_ResultsReady(int32_t index);

#ifdef __cplusplus
}
#endif
