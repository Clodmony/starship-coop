#pragma once

// Input for a game in a session with nobody at the controls (#20). tools/coop/lockstep_test.py drives it: the session
// itself comes from join_driver.h or the real menu, and this plays the person at the pad. What it decides goes to port
// 0 before the input layer reads the local controller, so it takes the road a pad takes from there on: stamped,
// delayed, sent, and read back from lockstep like everybody else's.
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
//   SF64_COOP_LOCKSTEP_POKE_AFTER=<n>  flip the bit n frames after the first checkpoint of the first run
//   SF64_COOP_LOCKSTEP_SLOW=<n>:<ms>   every n frames this game takes that much longer
//   SF64_COOP_LOCKSTEP_ZERO_SEED=<frame>:<n>  sRandSeed<n> is 0 from this frame on, as a stray write leaves it
//   SF64_COOP_LOCKSTEP_ITEM=<frame>:<player>[:<kind>[:<setup>]]  on this frame (#201) the setup is made (a sum of 1
//                                      player 0's wings broken, 2 player 1's wings broken, 4 player 0 twin lasers, 8
//                                      player 1's wing health 0), then kind 0 a laser item (the default), 1 a wing
//                                      repair item, 2 a supply crate about to burst or 3 a checkpoint (the ring a
//                                      rails level saves its checkpoint at) appears where that player will be. Report
//                                      lines: `item placed frame=N player=P kind=K`, `lasers frame=N strength=a,b
//                                      wings=l0,l1 states=s0,s1` (players 0 and 1, states = 10 * left + right wing
//                                      state) whenever one of them changes, and for a crate `drop frame=N id=<item id>
//                                      lasers=<0 or 1>`
// With none set, no listener is registered and the game runs unchanged. Without a feed or a pilot port 0 stays empty,
// so no pad on this PC reaches a test.
//
// Report lines: `start`, `done frame=N held=N held_ms=N`, then, once a session has gone away, `over frame=N` and
// `alive frames=N` every 120 frames: the game goes on, which is what a test of a lost player looks for. `over` comes
// each time lockstep stops, and `again run=N` when the session starts a level again (the lockstep log gets `# run N`
// there). `checkpoint frame=N run=N load=N allrange=N path=N hits=N` whenever the game's checkpoint changes in a level,
// and `run_state run=N frame=5 ... load=N path=N hits=N ...` once per run: where that run's level began.

#ifdef __cplusplus
extern "C" {
#endif

void CoopLockstepDriver_Init(void);

#ifdef __cplusplus
}
#endif
