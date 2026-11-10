#pragma once

// Names every fire-smoke spawn (OBJ_EFFECT_FIRE_SMOKE_1, EffectFireSmoke_Setup in src/engine/fox_effect.c), for the
// hunt of a desync that showed in one dead smoke puff (docs/coop/NOTES.md, 2026-11-11).
//
// Off unless SF64_COOP_SMOKE_TRACE names a file. Then every spawn appends one line:
//
//   smoke frame=<gGameFrameCount> level=<gCurrentLevel> slot=<index in gEffects> pos=<x>,<y>,<z> scale=<scale2>
//   path=<gPathProgress> callers=<+offset>,<+offset>,...
//
// (all on one line). callers are the return addresses above the spawn, nearest first, as offsets from the address of
// EffectFireSmoke_Setup, so they are the same in every run of one build: add the offset to that symbol's address
// (`nm <exe>`) and ask `addr2line -f -e <exe> <address>` for the function and the line. They are written on Linux and
// macOS (glibc's backtrace); elsewhere the list is empty and the position, the scale and the frame have to name the
// caller. A puff far from the level's path (compare pos z with -path) is one that was spawned from a stale or stray
// position. The trace only reads; it changes nothing in the game.

#ifdef __cplusplus
extern "C" {
#endif

struct Effect;

void CoopSmokeTrace_Init(void);
// Called by EffectFireSmoke_Setup with the slot it filled. Does nothing unless the trace is on.
void CoopSmokeTrace_Note(const struct Effect* effect);

#ifdef __cplusplus
}
#endif
