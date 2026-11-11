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
//   tframe=<test pad frame> pad0=<hold button>,<hold x>,<hold y>,<press button>,<press x>,<press y> ... pad3=...
//   shots=<id>:<owner>:<timer>:<exploded>;...  locks=<actors locked by player 0>,...,<by player 3>
//   lasers=<gLaserStrength per player>
//   knock=<knockback.x>,<knockback.y>,<knockback.z>,<hitTimer>;... one per player
//   [hits=<actor>:<aiType>:<dmgType>:<dmgSource>:<shooter>;...] [marks=<n>]
//   [torpedo=<owner>:<homing>:<target>:<x>,<y>,<z>]
//
// (all on one line) where x and y are pos.x and pos.y, z is trueZpos, parked is CoopPlayers_IsParked and draw is the
// player's draw flag. tools read it to check what each player did without looking at the screen. The pads are
// gControllerHold and gControllerPress as game code found them at the start of this frame; tframe is
// CoopTestPad_Frame (test_pad.h), -1 without a test pad. shots lists the bombs and lock-search shots in flight, - for
// none: those are the shots that answer to a button after they left the ship. knock is what moves a ship by itself
// after a hit, and hitTimer is 20 on the frame Player_ApplyDamage hits it, one less on each frame after (#243).
// knockback.z runs down in all-range only, so on the rails it can stay where a hit set it.
//
// The last two fields appear only on frames with something to say (#86). hits lists wingmen at a boss with a hit they
// have not read yet. shooter is the sourceId of a shot SF64_COOP_TRACE_STEER carried to that wingman on this frame, -1
// for any other hit. A wingman reads a hit only with work_048 at 0, so one hit can show on many frames, and the line it
// starts shows on the frame after the last. marks counts the laser marks new on this frame. torpedo shows while a Blue
// Marine torpedo flies on Aquas (#16): the player who fired it, whether it homes, that player's lock-on target
// (gActors index + 1, 0 for none) and the torpedo's position.
//
// SF64_COOP_TRACE_STEER=<frames> (test only, needs SF64_COOP_TRACE) is for replays, which cannot aim: at most once in
// that many frames, while the radio is free, a flying shot of a player other than player 0 is carried to a wingman at
// a boss. Everything from there on is the game's own collision and damage code.
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
