#pragma once

// A pilot for unattended tests (#142): it reads game state and flies every player, so that a replay can do what input
// by rule cannot, as a replay cannot see. It knows Corneria: its rails and Granga.
//
// Off unless SF64_COOP_PILOT=<file> is set together with SF64_COOP_REPLAY_PLAY. The replay plays on, and whenever a
// player is active in an Arwing its pad is the pilot's. <file> gets every frame as it was played, with its checkpoint:
// an ordinary replay, which then plays without the pilot. Only the pads come from here; nothing in the game is written.
// SF64_COOP_PILOT_LOG=<file> notes every change of shields and how the boss stands. tools/coop/full_level.py uses this.
// SF64_COOP_PILOT_LANES=<lane 0>,<lane 1>,... (#220, wave 7) gives each player's Landmaster on rails a lane of its own
// across the path in a boss fight (a boss in gBosses[0], or Macbeth's train end once it leaves the train), in percent
// of the half-width from the middle: -100 the left edge (low x), 100 the right edge. There the tank keeps its lane
// instead of lining up under the boss; it still dodges, hovers and fires ahead. For a test that needs the tanks apart.
// Before the fight, and for players without a lane, the pilot drives as before.
//
// Twice a frame in a lockstep session (#283). With SF64_COOP_LOCKSTEP_PILOT (lockstep_driver.h) CoopPilot_IsOn is true,
// so CoopPilot_Fly runs from both GamePreUpdate listeners: the replay's playback (replay.c, into the pads of the replay
// frame, which a session does not use) for as long as the session's replay file lasts (lockstep_test.py and
// campaign.py make it longer than the run), and the lockstep driver (lockstep_driver.c, Driver_Pad), whose pads the
// game gets. So the pilot's state steps per call, not per frame: its counters (sRun, sBankOff in pilot.c, sAimFrames
// and sEscape in pilot_aim.c) count two a frame, every SF64_COOP_PILOT_LOG line is there twice, and a press (a button
// not held by the call before, sLastButton) is against the other call of that frame. Both clients do the same, so their
// games stay equal. It is left so on purpose: every pilot scenario of lockstep_test.py and campaign.py was tuned with
// it, and one call a frame would change their flights. A pilot that times something in frames reads gGameFrameCount, as
// pilot_andross.c does (sW8AndPilotLast), or allows for the second call.

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
