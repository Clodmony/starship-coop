#pragma once

// Per-frame log of every player's state and position, for unattended tests (WP-11).
//
// Off unless the environment variable SF64_COOP_TRACE names a file. Then, after each game frame in GSTATE_PLAY, one
// line is appended:
//
//   frame=<gGameFrameCount> level=<gCurrentLevel> mode=<gLevelMode> players=<n>
//   path=<gPathProgress> pathvel=<gPathVelZ> scroll=<gPathTexScroll>
//   lowmsg=<gTeamLowHealthMsgTimer> radio=<gRadioState>,<gRadioMsgId>
//   p0=<state>,<csState>,<x>,<y>,<z>,<parked>,<draw>,<zPath>,<camDist>,<xPath>,<yPath>,<pathWidth>,<shields>,
//      <somersault>,<form> ...
//
// (all on one line) where x and y are pos.x and pos.y, z is trueZpos, parked is CoopPlayers_IsParked and draw is the
// player's draw flag. tools read it to check what each player did without looking at the screen.
//
// SF64_COOP_TRACE_SHIELDS=<player 0>,<player 1>,... holds those players' shields at those values while they update, so
// a test can have one player low and not the other. 0 or nothing leaves a player alone. It changes the simulation, so
// only a test sets it, and it only acts together with SF64_COOP_TRACE.

#ifdef __cplusplus
extern "C" {
#endif

void CoopTrace_Init(void);

#ifdef __cplusplus
}
#endif
