#pragma once

// What the pilot (pilot.h) aims at outside Corneria (#181), and the parts of pilot.c that pilot_aim.c shares. Test code
// only: it runs when pilot.h's switches are set, and only writes pads.
//
// The rule is generic, with no table of levels: on rails a player lines up with one box of a boss's hitbox after
// another; in all-range it makes runs at the nearest foe, a boss box or an actor the lock-on could take. Both say
// what they aim at in the pilot log.

#include <stdbool.h>
#include <stdio.h>

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    u16 button;
    f32 x;
    f32 y;
} PilotPad;

// pilot.c
f32 CoopPilot_SteerAt(const Player* p, f32 x, f32 y, f32 z, PilotPad* pad);
bool CoopPilot_BoxCenter(const Object* obj, const f32* data, s32 n, Vec3f* at);

// On rails: where player i should line up to shoot a boss ahead, if there is one. Moves the point every
// PILOT_AIM_DWELL frames to the next box, the two players on different boxes, so that every part gets shots.
bool CoopPilot_RailAim(const Player* p, s32 i, f32* x, f32* y);

// In all-range, anywhere but at Granga: a run at the nearest foe, firing on the way in.
void CoopPilot_FlyAllRangeAim(const Player* p, s32 i, PilotPad* pad);

// Every 60 frames: what player i aims at, and every active boss's state and health.
void CoopPilot_NoteAim(FILE* log, const Player* p, s32 i);

#ifdef __cplusplus
}
#endif
