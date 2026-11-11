#pragma once

// Whom the bosses and the all-range enemies that chase Fox go after, frame by frame, for unattended tests (#213).
//
// Off unless SF64_COOP_BOSS_TRACE names a file. Then, after each game frame in a co-op level, one line is appended:
//
//   frame=<gGameFrameCount> level=<gCurrentLevel> mode=<gLevelMode> load=<gObjectLoadIndex> path=<gPathProgress>
//   b<i>=<id>,<state>,<x>,<y>,<z>,<rot x>,<rot y>,<kept>,<held>        every boss slot that is not free
//   p<i>=<state>,<parked>,<x>,<y>,<z>,<eye x>,<eye y>,<eye z>,<draw>,<hitTimer>,<csState>,<hitDirection>
//                                                                      every player; z is trueZpos; hitTimer is set by
//                                                                      any hit, hitDirection is 0 for an effect's and
//                                                                      1 to 4 for a hitbox's (Player_ApplyDamage)
//   a<i>=<aiType>,<state>,<x>,<y>,<z>,<to x>,<to y>,<to z>,<kept>      every active all-range enemy aimed at Fox
//                                                                      (aiIndex AI360_FOX), with the point it flies to
//   e<i>=<id>,<x>,<y>,<z>                                              every active Meteo Crusher ring (OBJ_EFFECT_371)
//   o<i>=<id>,<x>,<y>,<z>                                              every active Sarumarine ball (OBJ_ACTOR_ZO_BALL)
//                                                                      (#220): the bosses' shots that go where they aim
//   aq=<head yaw>,<head pitch>,<eye yaw>,<eye yaw>                     Aquas' Bacoon, where it looks (#220)
//   so=<head yaw>,<head pitch>                                         Solar's Vulkain, where its head and fireballs
//                                                                      point, from its body (campaign run, #220)
//   ma=<slot>,<state>,<x>,<y>,<z>,<rot x>,<rot y>,<kept>               Macbeth's train end (actor 207), an actor
//                                                                      with a kept slot of its own (#220)
//
// kept and held are the slots of src/port/coop/game/aim.h, -1 for an empty one. The trace only reads; it does not ask
// the aim code to pick, so it changes nothing.
//
// SF64_COOP_BOSS_POKE=<slot>:<from>:<to>:<timer> (#220), separate from the trace: the first time gBosses[slot] is in
// state `from` after a game frame of a co-op level, it is put in state `to` with timer_050 `timer`, once per level, so
// a test reaches a boss's attack that takes the players' hits to get to. Every game of a session gets the same value,
// so they stay one game.

#ifdef __cplusplus
extern "C" {
#endif

void CoopBossTrace_Init(void);

#ifdef __cplusplus
}
#endif
