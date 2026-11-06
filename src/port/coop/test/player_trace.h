#pragma once

// Per-frame log of every player's state and position, for unattended tests (WP-11).
//
// Off unless the environment variable SF64_COOP_TRACE names a file. Then, after each game frame in GSTATE_PLAY, one
// line is appended:
//
//   frame=<gGameFrameCount> level=<gCurrentLevel> mode=<gLevelMode> players=<n>
//   p0=<state>,<csState>,<x>,<y>,<z>,<parked>,<draw> ...
//
// (all on one line) where x and y are pos.x and pos.y, z is trueZpos, parked is CoopPlayers_IsParked and draw is the
// player's draw flag. tools read it to check what each player did without looking at the screen.

#ifdef __cplusplus
extern "C" {
#endif

void CoopTrace_Init(void);

#ifdef __cplusplus
}
#endif
