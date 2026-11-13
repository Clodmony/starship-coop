#pragma once

// What every player's game starts a level from (WP-24). The host's game fills one in, the session carries it to the
// clients, and every game, the host's too, enters the level from it. So nobody's own history decides: not the seeds a
// game got from the clock at boot, not the team values no level start clears. See docs/coop/PROTOCOL.md, Level start.
//
// The session reads this header and nothing else from the game: to it the values are numbers, so a new one is a new row
// in the table in level_start.c.

#include <stdbool.h>
#include <stdint.h>

#include "players.h"
#include "save.h"

#ifdef __cplusplus
extern "C" {
#endif

// How many values the table in level_start.c holds.
#define COOP_LEVEL_START_VALUES 182
// Levels below this can be started: LEVEL_VERSUS and up (include/sf64level.h) are not co-op levels.
#define COOP_LEVEL_START_LEVELS 20

typedef struct {
    uint8_t level;                    // LevelId
    uint8_t phase;                    // gLevelPhase
    // Which level start of its session this is: 1 for the first, one more each time the session starts a level again
    // after a lost player or a desync. CoopSession_StartLevel and the session's restart fill it in; 0 outside a
    // session.
    uint8_t run;
    uint8_t roster[COOP_PLAYERS_MAX]; // who flies what, as CoopRoster; CoopSession_StartLevel fills this in
    int32_t values[COOP_LEVEL_START_VALUES];
    // The host's save as its game held it (CoopSave_FromGame). It travels in SESSION_SAVE, ahead of the level start,
    // and a game in a session takes it with CoopSave_Enter; CoopLevelStart_Enter does not read it.
    uint8_t save[COOP_SAVE_SIZE];
} CoopLevelStart;

// Host: level and phase as given, values and the save from this game as they are now, for a level started from
// outside one (the lobby, a test): its hit count is 0.
void CoopLevelStart_FromGame(CoopLevelStart* out, uint8_t level, uint8_t phase);

// Host: the level this game is about to enter by itself (gNextLevel and gNextLevelPhase, with gNextGameState at
// GSTATE_PLAY), with everything as it is now. Sent again later, it starts that level again from its start for
// everyone: what the session keeps as its restart point (session.h, "Recovery").
void CoopLevelStart_Entering(CoopLevelStart* out);

// The same for a level this game has just entered and not set up yet (GSTATE_PLAY, PLAY_STANDBY): the map enters a
// level that way, without gNextGameState (Map_PlayLevel, src/overlays/ovl_menu/fox_map.c:4290).
void CoopLevelStart_Entered(CoopLevelStart* out);

// Host: the level this game is in, from the checkpoint it reached there, with everything as it is now: what the
// session starts again when the games part, or a player is lost, after a checkpoint (the owner's rule of 2026-11-11).
// False, and nothing is written, when this game is not in a level or no checkpoint was reached in it: then the level
// starts again from its start (CoopLevelStart_Entering, taken when the level was entered). A checkpoint is what the
// game itself restarts from after a death: on rails the saved path progress, object index, ground and searchlight
// (the checkpoint item, src/engine/fox_enmy.c:2512), in all-range gAllRangeCheckpoint (Fortuna, Katina, Bolse,
// Andross), with the hits and each player's own hits saved there.
bool CoopLevelStart_AtCheckpoint(CoopLevelStart* out);

// Call first thing of every frame. A level start that begins at a checkpoint puts the checkpoint in place here, one
// frame after CoopLevelStart_Enter: entering a level clears it (Play_Setup, src/engine/fox_play.c:530), and the level's
// first setup, a few frames later, reads it. Nothing outside that one frame.
void CoopLevelStart_AfterEntry(void);

// Puts the values in place and sends the game to the level on this frame, from whatever it is doing. Call it from a
// GamePreUpdateEvent listener or anywhere else outside Game_Update, so that nothing runs between the values and the
// level. The players of the roster stay in the level until CoopPlayers_SetRoster(NULL). The audio heap is reset on the
// way in, also when the level's audio spec is the one loaded, so every game's sound thread starts over there (#230).
// Before that the sound is played ahead on this frame until no audio reset is running and 16 frames more have passed:
// the game takes a new reset up only when the running one is done, and one that just ended still has commands on
// their way, so the voice player would come back a number of frames after the level start that follows what the
// sound was doing, which differs between two games (#289).
void CoopLevelStart_Enter(const CoopLevelStart* start);

// Where a level start holds the first value of the game variable at `var` (one of the table in level_start.c), -1 if
// it holds none. For the test switches, which set a value by its variable and not by a place that moves.
int32_t CoopLevelStart_ValueIndex(const void* var);

// Test only, SF64_COOP_TEST_AUDIO_RESET_RUNS_ON (src/port/coop/test/switches.h): CoopLevelStart_Enter does not play
// the sound ahead, so a run can show what that is there for (#289).
void CoopLevelStart_SetResetRunsOn(bool runsOn);

#ifdef __cplusplus
}
#endif
