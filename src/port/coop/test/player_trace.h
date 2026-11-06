#pragma once

// Per-frame log of every player's state and position, for unattended tests (WP-11).
//
// Off unless the environment variable SF64_COOP_TRACE names a file. Then, after each game frame in GSTATE_PLAY, one
// line is appended:
//
//   frame=<gGameFrameCount> level=<gCurrentLevel> mode=<gLevelMode> players=<n>
//   path=<gPathProgress> pathvel=<gPathVelZ> scroll=<gPathTexScroll>
//   p0=<state>,<csState>,<x>,<y>,<z>,<parked>,<draw>,<zPath>,<camDist>,<xPath>,<yPath>,<pathWidth>,<shields>,
//      <somersault>,<form> ...
//   tframe=<test pad frame> pad0=<hold button>,<hold x>,<hold y>,<press button>,<press x>,<press y> ... pad3=...
//   shots=<id>:<owner>:<timer>:<exploded>;...  locks=<actors locked by player 0>,...,<by player 3>
//
// (all on one line) where x and y are pos.x and pos.y, z is trueZpos, parked is CoopPlayers_IsParked and draw is the
// player's draw flag. tools read it to check what each player did without looking at the screen. The pads are
// gControllerHold and gControllerPress as game code found them at the start of this frame; tframe is
// CoopTestPad_Frame (test_pad.h), -1 without a test pad. shots lists the bombs and lock-search shots in flight, - for
// none: those are the shots that answer to a button after they left the ship.

#ifdef __cplusplus
extern "C" {
#endif

void CoopTrace_Init(void);

#ifdef __cplusplus
}
#endif
