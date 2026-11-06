#pragma once

// Per-frame log of every player's state and position, for unattended tests (WP-11).
//
// Off unless the environment variable SF64_COOP_TRACE names a file. Then, after each game frame in GSTATE_PLAY, one
// line is appended:
//
//   frame=<gGameFrameCount> level=<gCurrentLevel> mode=<gLevelMode> players=<n>
//   path=<gPathProgress> pathvel=<gPathVelZ> scroll=<gPathTexScroll>
//   p0=<state>,<csState>,<x>,<y>,<z>,<parked>,<draw>,<zPath>,<camDist>,<xPath>,<yPath>,<pathWidth>,<shields>,
//      <somersault>,<form> ... radio=<gRadioState>,<gRadioMsgId> lasers=<gLaserStrength per player>
//   [hits=<actor>:<aiType>:<dmgType>:<dmgSource>:<shooter>;...] [marks=<n>]
//
// (all on one line) where x and y are pos.x and pos.y, z is trueZpos, parked is CoopPlayers_IsParked and draw is the
// player's draw flag. tools read it to check what each player did without looking at the screen.
//
// The last two fields appear only on frames with something to say (#86). hits lists wingmen at a boss with a hit they
// have not read yet. shooter is the sourceId of a shot SF64_COOP_TRACE_STEER carried to that wingman on this frame, -1
// for any other hit. A wingman reads a hit only with work_048 at 0, so one hit can show on many frames, and the line it
// starts shows on the frame after the last. marks counts the laser marks new on this frame.
//
// SF64_COOP_TRACE_STEER=<frames> (test only, needs SF64_COOP_TRACE) is for replays, which cannot aim: at most once in
// that many frames, while the radio is free, a flying shot of a player other than player 0 is carried to a wingman at
// a boss. Everything from there on is the game's own collision and damage code.

#ifdef __cplusplus
extern "C" {
#endif

void CoopTrace_Init(void);

#ifdef __cplusplus
}
#endif
