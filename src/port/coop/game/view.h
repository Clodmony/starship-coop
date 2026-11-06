#ifndef COOP_GAME_VIEW_H
#define COOP_GAME_VIEW_H

#include <stdint.h>

#include "sf64math.h"

// Keeps the camera a client draws out of the simulation (#96). The game's draw code is part of its simulation: it frees
// objects it does not see and works out values the AI reads. In co-op each client draws its own camera and all of them
// must simulate the same game, so:
// - every client runs the draw code of every object, seen or not (CoopView_DrawAnyway);
// - what the simulation takes from a camera comes from the view of player 0, which every client has
//   (CoopView_ToReference), never from the camera drawn;
// - gPlayerNum is the local player only inside Game_Draw (CoopView_EndDraw).
// Each write of the draw path, and why it is safe: docs/coop/NOTES.md, 2026-11-06, #96. Outside co-op nothing here
// does anything.

#ifdef __cplusplus
extern "C" {
#endif

// Around Game_Draw, every frame. Begin gives every player the camera angles Display_Update would give it, End puts
// them back after the camera drawn has changed its own, and points gPlayerNum at player 0 again.
void CoopView_BeginDraw(void);
void CoopView_EndDraw(void);

// For a culling test in draw code. Nonzero in co-op: run the draw code anyway. When `inView` is 0 what it draws is
// dropped, up to CoopView_EndHidden. The caller has to call that too, also when nothing was hidden.
int32_t CoopView_DrawAnyway(int32_t inView);
void CoopView_EndHidden(void);

// Where a point of the world is in the view of player 0, for a value the simulation reads.
void CoopView_ToReference(const Vec3f* world, Vec3f* view);

// The view depth a lock-on on the object at `pos` is judged by: `drawnZ`, from the camera drawn, outside co-op.
float CoopView_LockOnZ(const Vec3f* pos, float drawnZ);

// Test only, for src/port/coop/test/view_trace.c. The alpha of the red of a hit on this client's screen, and the shield
// bar drawn on player `index` this frame, as a fraction, negative for none. Nothing draws either yet.
int32_t CoopView_HitFlashAlpha(void);
float CoopView_ShieldBar(int32_t index);

#ifdef __cplusplus
}
#endif

#endif
