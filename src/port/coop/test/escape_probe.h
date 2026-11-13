#pragma once

// What each player gets in Andross' escape, for unattended tests (#282). An observation tool: it changes no rule.
//
// Off unless SF64_COOP_ESCAPE_PROBE names a file. In the escape (the brain in its state 21 after the maze is loaded,
// src/overlays/ovl_i6/fox_andross.c) it writes:
//
//   escape start run=<n> frame=<gGameFrameCount> trigger=<x,y,z> explosions=<n> lives=<gLifeCount[0]> inv=<gInvincible>
//                                            each time an escape begins (again after a restart at its checkpoint)
//   escape player=<i> frame=<f> level=<gCurrentLevel> state=<state> cs=<csState> parked=<0|1> down=<0|1> draw=<0|1>
//          shields=<n> lives=<gLifeCount[0]>   whenever player i's state, csState, parked or down changes, in Andross
//                                            and in the Venom 2 ending the exit leads to
//   escape gamestate frame=<f> from=<a> to=<b>   whenever gGameState changes after the first escape
//   escape exit run=<n> frame=<f> case=<name or ->   the exit's level complete (player 0 in csState 3 in Venom 2's id,
//                                            Andross_AndJamesTrigger_Update)
//   escape run_end run=<n> frame=<f> why=<exit|left> nearest=<per player> touch=<per player>
//                                            nearest: the least reach of the level's own explosions to that player
//                                            over the run, while it flew (the explosion's own test: the larger of the
//                                            x and z distances, it acts under 500); touch: frames under 500
//
// SF64_COOP_ESCAPE_CASES=<case>[,<case>...] runs cases in that order, one at a time, once every player flies in an
// escape (a restart at the checkpoint goes on with the next case). A case spawns one object of the level, as the maze
// table does (Andross_80187C5C), and before each of its updates puts it 450 from its target in x or in z, on the side
// that keeps every other player farthest out, so that the target is in its reach (500) and every other player at
// least 510 from it; when no side does that (the players closer than about 60 on both axes) it waits 3000 away
// instead (`off`). Its velocity is held at 0, so Actor_Move leaves it there. Only the place is forced; the object's own
// update does the test, as in play:
//
//   blast<i>  an explosion (OBJ_ACTOR_AND_EXPLOSION, Andross_AndExplosion_Update) in reach of player i
//   exit<i>   the exit's trigger (OBJ_ACTOR_AND_JAMES_TRIGGER, Andross_AndJamesTrigger_Update) in reach of player i
//
// for 60 of its updates; then it is taken out. Each frame of a case:
//
//   escape t=<update> frame=<f> case=<name> p<i>=<state>,<parked>,<shields>,<reach>,<hitTimer>,<dmgType> ...
//          lives=<n> level=<n>
//
// and at its end one line:
//
//   escape case=<name> target=<i> run=<n> frame=<first> end=<last> updates=<n> off=<n> lost=<0|1> exit=<0|1>
//          hit=<mask> down=<mask> lives=<before>/<after> backdrop=<gDrawBackdrop>
//          p<i>=<least reach>/<most reach>,<shields before>/<least>/<after>,<states seen, as a-b-c> ...
//
// hit has bit i set when player i got a hit on a frame of the case (hitTimer 20, Player_ApplyDamage), down when it
// went down (PLAYERSTATE_DOWN or PLAYERSTATE_NEXT, or out after a down as decision 0007 rule 4 has it). lost is 1 when
// the object was gone or the escape over before its 60 updates. The probe changes the game, so a run with cases says
// nothing about any other run; every game of a session must get the same values, and then they stay one game.

#ifdef __cplusplus
extern "C" {
#endif

void CoopEscapeProbe_Init(void);

#ifdef __cplusplus
}
#endif
