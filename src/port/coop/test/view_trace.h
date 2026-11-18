#pragma once

// What this client shows and hears, one line per game frame in a level, for unattended tests (#30).
//
// Off unless SF64_COOP_VIEW_TRACE names a file. tools/coop/view_check.py reads it. Each line, after the frame is drawn:
//
//   frame=<gGameFrameCount> level=<gCurrentLevel> mode=<gLevelMode> local=<CoopPlayers_LocalIndex>
//   view=<CoopPlayers_ViewIndex>
//   eye=<x>,<y>,<z> at=<x>,<y>,<z>      the camera this client drew
//   shield=<fraction> bombs=<n>         what the HUD holds for its shield gauge and bomb counter
//   cockpit=<0|1>                       the cockpit glass is drawn
//   fill=<r>,<g>,<b>,<a> flash=<a>      the full screen fill, and the red of a hit on this client's own player
//   drawn=<r>,<g>,<b>,<a>               the full screen fill as this client drew it on this frame (#303): fill is
//                                       what the game holds after the frame, the same on every client
//   bars=<player>:<fraction>,...        shield bars drawn on other players, - for none
//   tags=<player>:<name>,...            the name drawn over other players (#257, #258), - for none; a space in a name
//   is
//                                       written as _
//   p0=<state>,<parked>,<shields>,<bombs>,<lives>,<alternateView>,<sfx x>,<sfx y>,<sfx z>,<x>,<y>,<z> ...
//   seen=<0|1>,...                      per player, the camera drawn had it in view (sPlayersVisible), so its draw ran;
//                                       always 0 for the player looked through, which Display_Update clears at its end
//   guns=<laserGunsYpos>/<laserGunsXpos>,...  per player, where its laser guns are (#192)
//   noise=<form>,...                    the engine sound each player has, FORM_NONE for none
//   pause=<0|1>                         the game is paused
//   reticles=<0|1>,...                  per player, gShowReticles: R in the pause menu shows or hides it (#140)
//   heat=<shields>,<height>             what this client last gave Solar's heat alarm this frame, - for nothing (#140)
//   turned=<n>/<all>                    billboards (poles, trees, facades) drawn facing this client's eye in place of
//                                       player 0's, of all drawn (#140)
//   hudlives=<n>                        the lives number the HUD drew this frame (HUD_LivesCount2_Draw, #335), - for
//                                       none
//   marks=<player>:<x>,<y>,<z>,<scale>,<turn>;...  the lock-on marks drawn this frame (Display_LockOnIndicator, #379):
//                                       place in the camera drawn, the settle animation's scale (3 on a new lock, 1
//                                       settled) and turn in degrees (300 on a new lock, 0 settled); - for none
//   locks=<n>,...                       per player, the active actors it holds a lock-on on (lockOnTimers)
//   charge=<n>,...                      per player, gChargeTimers (20 and up: charged, the reticle turns red)
//   lockz=<depth>                       how deep in the camera drawn the nearest target sits that the player looked
//                                       through still holds a lock-on on after the draw (#382): negative in front of
//                                       the camera, 0 and up behind it; - for no lock-on
//   sfx=<id>@<source>,...               sound requests of this frame, - for none. The source is d for the default
//                                       source, p<i> for player i and o for anything else
//
// SF64_COOP_VIEW_POKE changes the game on chosen frames, to give two players different numbers. Both clients of a test
// get the same list, so they stay one game: <frame>:<player>:<what>:<value>,... with what one of shields, bombs, lives,
// boost, hit (Player_ApplyDamage, direction 0), down (Player_Down follows), guns (laserGunsYpos, -10 for the laser
// guns fully out; the one value with a fraction, such as -9.5) or lasers (gLaserStrength: 0 single, 1 twin, 2 hyper;
// #380). Frames are gGameFrameCount, applied after that frame. With @<level> behind the value a poke also waits for
// that gCurrentLevel, so it can land in the level a session flies next: 0:1:lasers:2@1 lands on the first frame Meteo
// is the current level. On a level's first frames the counter still holds the count of the map before it (Play_Init
// sets it to 0, src/engine/fox_play.c:2882), so there a frame above 0 does not count from the level's start.
//
// SF64_COOP_TEST_NAMES=<name 0>,<name 1>,... gives the players these names, as a session would (names.h), with or
// without a trace. An empty entry is no name.

#ifdef __cplusplus
extern "C" {
#endif

void CoopViewTrace_Init(void);

#ifdef __cplusplus
}
#endif
