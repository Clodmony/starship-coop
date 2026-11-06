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
//   bars=<player>:<fraction>,...        shield bars drawn on other players, - for none
//   p0=<state>,<parked>,<shields>,<bombs>,<lives>,<alternateView>,<sfx x>,<sfx y>,<sfx z>,<x>,<y>,<z> ...
//   noise=<form>,...                    the engine sound each player has, FORM_NONE for none
//   pause=<0|1>                         the game is paused
//   sfx=<id>@<source>,...               sound requests of this frame, - for none. The source is d for the default
//                                       source, p<i> for player i and o for anything else
//
// SF64_COOP_VIEW_POKE changes the game on chosen frames, to give two players different numbers. Both clients of a test
// get the same list, so they stay one game: <frame>:<player>:<what>:<value>,... with what one of shields, bombs, lives,
// boost, hit (Player_ApplyDamage, direction 0) or down (Player_Down follows). Frames are gGameFrameCount, applied after
// that frame.

#ifdef __cplusplus
extern "C" {
#endif

void CoopViewTrace_Init(void);

#ifdef __cplusplus
}
#endif
