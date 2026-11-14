#pragma once

// The pilot (pilot.h) in Andross' second part (#275): the escape from Venom after the brain. Test code only: it runs
// when pilot.h's switches are set, and only writes pads.
//
// After the brain dies the level becomes a maze of passages (Andross_80187C5C, src/overlays/ovl_i6/fox_andross.c), and
// explosions stand in it (OBJ_ACTOR_AND_EXPLOSION). One within 500 of player 0 in x and z sets its shields to 0 and
// kills it, whatever gInvincible says (Andross_AndExplosion_Update). The all-range pilot (pilot_aim.c) knew no route:
// it went on chasing the dead brain's boxes and flew into one, every time, until the team was out of lives. Here each
// player flies the route James flies: the level's waypoints (OBJ_SCENERY_LEVEL_OBJECTS) in the order of their
// pathIndex (Andross_ActorTeamArwing_Update), to the trigger at the exit (Andross_AndJamesTrigger_Update).

#include <stdbool.h>
#include <stdio.h>

#include "pilot_aim.h"

#ifdef __cplusplus
extern "C" {
#endif

// In the escape: steers player i along the route and returns true. Anywhere else returns false and writes nothing.
bool w8AndPilot_Escape(const Player* p, s32 i, PilotPad* pad);

// For the pilot log: the route and the explosions when an escape starts, and every 60 frames where player i is on it.
void w8AndPilot_Note(FILE* log, const Player* p, s32 i);

#ifdef __cplusplus
}
#endif
