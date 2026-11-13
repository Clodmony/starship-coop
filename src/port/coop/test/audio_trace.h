#pragma once

// One line per game frame about the audio reset and the voice sequence player, for the hunt of a voice player that
// runs on one game and not on the other at level frame 0 (#289, #293; docs/coop/NOTES.md, 2026-11-13).
//
// Off unless SF64_COOP_AUDIO_TRACE names a file. Then every game frame appends, after the game's update:
//
//   audio sys=<gSysFrameCount> state=<gGameState> lock=<lockstep frame, -1 without> step=<gAudioResetStep>
//   status=<sAudioResetStatus> voice=<the voice player's enabled> spec=<sAudioSpecId>/<gAudioSpecId>
//
// (all on one line), and a session's level start adds "enter level=<n> run=<n>" before the line of its frame, with
// the same fields as they stood when the level start came. The trace only reads; it changes nothing in the game.

#ifdef __cplusplus
extern "C" {
#endif

void CoopAudioTrace_Init(void);
// Called by CoopLevelStart_Enter before it asks for the audio reset. Does nothing unless the trace is on.
void CoopAudioTrace_Enter(int level, int run);

#ifdef __cplusplus
}
#endif
