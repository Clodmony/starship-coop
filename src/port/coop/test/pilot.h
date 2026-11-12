#pragma once

// A pilot for unattended tests (#142): it reads game state and flies every player, so that a replay can do what input
// by rule cannot, as a replay cannot see. It knows Corneria: its rails and Granga.
//
// Off unless SF64_COOP_PILOT=<file> is set together with SF64_COOP_REPLAY_PLAY. The replay plays on, and whenever a
// player is active in an Arwing its pad is the pilot's. <file> gets every frame as it was played, with its checkpoint:
// an ordinary replay, which then plays without the pilot. Only the pads come from here; nothing in the game is written.
// SF64_COOP_PILOT_LOG=<file> notes every change of shields and how the boss stands. tools/coop/full_level.py uses this.
// SF64_COOP_PILOT_LANES=<lane 0>,<lane 1>,... (#220, wave 7) gives each player's Landmaster on rails a lane of its own
// across the path, in percent of the half-width from the middle: -100 the left edge (low x), 100 the right edge. The
// tank keeps it instead of lining up under a boss or going for rings; it still dodges, hovers and fires ahead. For a
// test that needs the tanks apart. Players without a lane drive as before.

#include <libultra/controller.h>

#ifdef __cplusplus
extern "C" {
#endif

void CoopPilot_Init(void);

// Whether a pilot flies this run.
int CoopPilot_IsOn(void);

// Replaces the pad of every player in flight. `hold` and `press` are the replay's four slots of this frame.
void CoopPilot_Fly(OSContPad* hold, OSContPad* press);

#ifdef __cplusplus
}
#endif
