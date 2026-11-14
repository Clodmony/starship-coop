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
//   plasma level=<...> frame=<...> start=<x>,<y>,<z> dir=<x>,<y>,<z> p0=<x>,<y>,<z>,<flies> p1=...
//     after the shot line of an OBJ_EFFECT_376 in Corneria, a shot of the Granga's gun (#281), with the fields of the
//     syrobot line: the Granga spreads these shots around the human it aims at.
//   sypose level=<...> frame=<...> slot=<gActors index> body=<0|1> before=<5 angles> after=<5 angles>
//          start=<fwork[16..18]> w0=<yaw>,<pitch> w1=... p0=<x>,<y>,<z>,<flies> p1=...
//     after each frame in which a Sector Y robot turns its head and gun to a player (work_046 1 or 2) (#281). The 5
//     angles are fwork[20] and [21] (the head's pitch and yaw), fwork[28] and [29] (the gun's yaw and pitch) and
//     obj.rot.y, before and after the robot's update of this frame; body is 1 when it turns its body, not its head
//     (iwork[17] != 0). w<n> is the yaw and pitch SectorY_SyRobot_Update would turn to for player n, computed here
//     with the game's own sum from the robot as it is after the frame. The update steps each angle a fixed part of
//     the way to its target (Math_SmoothStepToAngle), so before and after give the target the game used, and the w
//     fields say whose it was. start and the p fields are as in the syrobot line: the nearest to start is the one the
//     robot's shot flies at.
//   turret level=<...> frame=<...> slot=<gActors index> car=<iwork[5]> last=<0|1> boulder=<0|1> step=<before>,<after>
//          hp=<health> wait=<timer_0BC> yaw=<before>,<after> pitch=<before>,<after> at=<x>,<y>,<z>
//          w0=<yaw>,<pitch>,<in window> w1=... p0=<x>,<y>,<z>,<flies> p1=...
//     after each frame in which the turret of a Macbeth train car runs (Macbeth_8019F164, #302). step is the step of
//     its salvo (iwork[4]) before and after the car's update: it turns to a player, and asks whether to fire, in step
//     0 only, and 0 to 1 is its first shot. last is 1 for the train's last car, which fires whatever the player's x
//     is; boulder is 1 while it turns to a falling boulder and fires at that. yaw and pitch are the angles it turns
//     to (fwork[4], fwork[3]) before and after the update, at is the car. w<n> is the yaw and pitch it would turn to
//     for player n, computed here with the game's own sum from the car as it is after the frame, and 1 when player n
//     is in the window the turret fires in (300 to 3000 beside the car, or any x for the last car, and 200 to 5000
//     behind it). The p fields are as in the syrobot line.
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
