#pragma once

// Forces effects onto one player and logs who they hit, for unattended tests (#100).
//
// Off unless the environment variable SF64_COOP_EFFECT_PROBE names a file. Once every player flies, the probe takes the
// cases of sCases in effect_probe.c one at a time, each for every player in turn: it heals the players, places the
// effect where the target will be on the next frame, and lets the game run it. The effect's own update does the rest,
// as in play; only the spawn is forced, so a level can test effects that belong to another level. One line per case and
// target:
//
//   effect frame=<gGameFrameCount> case=<name> target=<player> hit=<mask> deflect=<mask> damage=<per player>
//   shields=<per player>
//
// (all on one line) where hit has bit i set when the effect hit player i on the frame it ran, deflect when player i
// deflected it, damage is what that frame left to take off, and shields is what player i has PROBE_SETTLE frames later.
// The last line is "effect done". The probe changes the game, so a run with it says nothing about any other run.

#ifdef __cplusplus
extern "C" {
#endif

void CoopEffectProbe_Init(void);

#ifdef __cplusplus
}
#endif
