#pragma once

// Log of whom the enemies aim at, for unattended tests (#193).
//
// Off unless the environment variable SF64_COOP_AIM_TRACE names a file; then nothing else is registered. It watches the
// game's objects from outside and does not ask the aim code (src/port/coop/game/aim.h), so a build without that code
// writes its lines too. It reads, never writes, the simulation. In co-op levels only (CoopPlayers_IsCoop), it appends:
//
//   shot level=<gCurrentLevel> frame=<gGameFrameCount> id=<obj.id> player=<index, -1 for none>
//     for each new enemy shot (OBJ_EFFECT_ENEMY_LASER_1, 354 to 356, 376, 377), on its ObjectInitEvent: before it
//     first moves, in the frame it was fired, after the players' update. player is the player it flies at: its
//     velocity less the path's points within COOP_AIM_TRACE_DEGREES of that player's pos.x, pos.y, trueZpos, and
//     nearer it than any other player that is not parked. A shot fired straight ahead, or at a camera, is -1.
//   missile level=<...> frame=<...> slot=<gActors index> player=<index>
//     when a homing missile (OBJ_ACTOR_MISSILE_SEEK_*) starts to seek a player, or another one: its target point
//     (fwork[27..29]) is that player's position after the frame. A missile that seeks a wingman writes no line.
//   syrobot level=<...> frame=<...> slot=<gActors index> script=<aiType> spread=<iwork[19], signed>
//           start=<x>,<y>,<z> dir=<x>,<y>,<z> p0=<x>,<y>,<z>,<flies> p1=... (one per player of the session)
//     after the shot line of an OBJ_EFFECT_354 that a Sector Y robot fired (#273): the robot whose fwork[16..18], where
//     SectorY_SyRobot_Update starts its shot, is the shot's position. dir is the shot's velocity less the path's, each
//     player is pos.x, pos.y, trueZpos and 1 when an enemy may aim at it (player 0 always; another one while it is not
//     parked and active or in a U-turn, as src/port/coop/game/aim.c counts them). From these a test finds where the
//     shot crosses each player's depth, which is where the robot aimed it.
//
// gGameFrameCount restarts with each level; in a session it is the same on every game, so the files of two games of
// one session must be equal.

#ifdef __cplusplus
extern "C" {
#endif

void CoopAimTrace_Init(void);

#ifdef __cplusplus
}
#endif
