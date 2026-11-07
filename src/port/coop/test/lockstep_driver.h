#pragma once

// Input for a game in a session with nobody at the controls (#20). tools/coop/lockstep_test.py drives it: the session
// itself comes from join_driver.h or the real menu, and this plays the person at the pad. What it decides goes to port 0
// before the input layer reads the local controller, so it takes the road a pad takes from there on: stamped, delayed,
// sent, and read back from lockstep like everybody else's.
//
// Off unless one of these is set when the game starts:
//   SF64_COOP_LOCKSTEP_REPORT=<file>   one line for each thing that happened, see below
//   SF64_COOP_LOCKSTEP_LOG=<file>      one line per lockstep frame: `frame gamestate playstate`, then for each player
//                                      `holdbutton x y pressbutton x y`. Two clients have to write the same file.
//   SF64_COOP_LOCKSTEP_FEED=<file>     play this player's columns of such a log, or of a file made to look like one:
//                                      a line is the input that has to steer its frame, so the input delay is taken
//                                      into account here
//   SF64_COOP_LOCKSTEP_PILOT=1         the pilot (pilot.h) flies this client's own player, and player 0 presses A every
//                                      30 frames while it is not flying, as input by rule does through the menus
//   SF64_COOP_LOCKSTEP_END=<frame>     report `done` on this frame
//   SF64_COOP_LOCKSTEP_MUTE=<frame>    stop sending input from this frame on, with the connection left up
//   SF64_COOP_LOCKSTEP_POKE=<frame>    flip a bit of game state on this frame, as SF64_COOP_HASH_POKE does
//   SF64_COOP_LOCKSTEP_SLOW=<n>:<ms>   every n frames this game takes that much longer
//   SF64_COOP_LOCKSTEP_CLEARED=1       this game's save says Corneria is cleared; saves are each client's own
//   SF64_COOP_LOCKSTEP_ZERO_SEED=<frame>:<n>  sRandSeed<n> is 0 from this frame on, as a stray write can leave it (#141)
// With none set, no listener is registered and the game runs unchanged. Without a feed or a pilot port 0 stays empty, so
// no pad on this PC reaches a test.
//
// Report lines: `start`, `done frame=N held=N held_ms=N`, then, once a session has gone away, `over frame=N` and
// `alive frames=N` every 120 frames: the game goes on, which is what a test of a lost player looks for.

#ifdef __cplusplus
extern "C" {
#endif

void CoopLockstepDriver_Init(void);

#ifdef __cplusplus
}
#endif
