#pragma once

// Whom the bosses and the all-range enemies that chase Fox go after, frame by frame, for unattended tests (#213).
//
// Off unless SF64_COOP_BOSS_TRACE names a file. Then, after each game frame in a co-op level, one line is appended:
//
//   frame=<gGameFrameCount> level=<gCurrentLevel> mode=<gLevelMode> load=<gObjectLoadIndex> path=<gPathProgress>
//   b<i>=<id>,<state>,<x>,<y>,<z>,<rot x>,<rot y>,<kept>,<held>        every boss slot that is not free
//   p<i>=<state>,<parked>,<x>,<y>,<z>,<eye x>,<eye y>,<eye z>,<draw>   every player; z is trueZpos
//   a<i>=<aiType>,<state>,<x>,<y>,<z>,<to x>,<to y>,<to z>,<kept>      every active all-range enemy aimed at Fox
//                                                                      (aiIndex AI360_FOX), with the point it flies to
//
// kept and held are the slots of src/port/coop/game/aim.h, -1 for an empty one. The trace only reads; it does not ask
// the aim code to pick, so it changes nothing.

#ifdef __cplusplus
extern "C" {
#endif

void CoopBossTrace_Init(void);

#ifdef __cplusplus
}
#endif
