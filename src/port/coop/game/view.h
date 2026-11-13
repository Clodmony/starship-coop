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
// What a client shows and hears of its own player, and of the others (#30), is here too, and none of it writes anything
// the simulation reads.
// Each write of the draw path, and why it is safe: docs/coop/NOTES.md, 2026-11-06, #96. Outside co-op nothing here
// does anything.

#ifdef __cplusplus
extern "C" {
#endif

struct Player;
struct Object;

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

// Whether the object last given to CoopView_DrawAnyway is in the camera this client draws, for what only shows it.
// Outside co-op `seen`, the game's own answer.
int32_t CoopView_InDrawnView(int32_t seen);

// Where `pos` sits for this client's ears: in the camera it draws, into `sfxSrc`. Leaves gCalcMatrix alone, because
// this runs in the simulation too and that matrix must stay the same on every client.
void CoopView_ToDrawnCamera(const Vec3f* pos, float* sfxSrc);

// The red of a hit on this client's own player. In co-op a hit does not touch gFillScreen*, which is a fade every
// client shares: Game_Update calls this after the screen fill.
void CoopView_DrawHitFlash(void);

// The flash of Macbeth's train car 7 (#303, the owner's answer of 2026-11-13: per client). Upstream the blast turns the
// fill white, and red when player 0 is in it (Macbeth_MaTrainCar7_Update, state 1). The fill is one fade every game
// of a session holds alike and stays as the game writes it; what differs is the colour a client draws it in: red on
// the client whose own player (the one it looks through) is in the blast, white on the others, with the game's alpha.
// MacbethBlast is called by the simulation on the blast's frame with a bit for each player in the blast; it writes
// only what FillColor reads. FillColor is called by Game_Update with the fill's colour right before it draws the
// fill, and changes it while that flash fades: until the fill's alpha is 0, its colour is no longer the blast's
// white or red, or its alpha rises again (another flash). Nothing the simulation reads is written, and outside co-op
// neither does anything.
void CoopView_MacbethBlast(int32_t inBlast);
void CoopView_FillColor(int32_t* red, int32_t* green, int32_t* blue);

// A bar with the shields of every other player, over its Arwing, and above the bar its name (#257, #258, names.h). See
// takes each player while gGfxMatrix holds the camera drawn, Draw comes after that matrix is popped, as
// Display_ActorMarks does. A client marks every other player that flies in front of its camera, Fox included, never the
// one it looks through; nothing off screen.
void CoopView_SeePlayer(const struct Player* player);
void CoopView_DrawShieldBars(void);

// What still followed player 0 on every client (#140). All of it is for this client's eyes and ears; none of it writes
// what the simulation reads.
//
// Solar's heat alarm, in place of Audio_SetHeatAlarmParams with player 0's numbers: the shields and height of the
// player this client looks through. A sound's settings only; the drain stays in the simulation (CoopPlayers_SolarHeat).
void CoopView_HeatAlarm(uint8_t shields, uint8_t heightParam);
// R in the pause menu. Nonzero in co-op: each player's own R showed or hid its own reticle, from its own pad slot,
// the same on every client, and only its own client heard it. gShowReticles is no game state: the reticle is drawn
// for the player looked through only (Display_Reticle). Outside co-op 0, and the game does as upstream.
int32_t CoopView_PauseReticles(void);
// The yaw Object_SetMatrix draws `obj` with: obj->rot.y, the state every client holds, which for Corneria's facades
// (`facade`) and the doodads Sprite_UpdateDoodad turns (poles, trees) faces player 0's eye. A client that looks through
// another player draws them facing its own eye instead. flipped: a facade turned since its last draw, so the frame
// interpolation must not blend the two (left alone otherwise).
float CoopView_DrawnRotY(const struct Object* obj, int32_t facade, int32_t* flipped);

// Test only, for src/port/coop/test/view_trace.c. The alpha of the red of a hit on this client's screen, and the shield
// bar drawn on player `index` this frame, as a fraction, negative for none.
int32_t CoopView_HitFlashAlpha(void);
// The full screen fill as this client drew it on this frame: red, green, blue, alpha (#303).
void CoopView_DrawnFill(int32_t* rgba);
float CoopView_ShieldBar(int32_t index);
// Nonzero when player `index`'s name was drawn this frame; what it says is CoopNames_Label.
int32_t CoopView_TagShown(int32_t index);
// The last heat alarm settings of this frame, nonzero if any were set; clears them.
int32_t CoopView_HeatAlarmSet(int32_t* shields, int32_t* heightParam);
// How many billboards were drawn facing this client's own eye in place of player 0's since the last call; billboards:
// how many were drawn at all.
int32_t CoopView_TakeTurned(int32_t* billboards);

#ifdef __cplusplus
}
#endif

#endif
