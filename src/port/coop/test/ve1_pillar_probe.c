// Venom 1's pillars from the last row, for the test of #276. See ve1_pillar_probe.h.

#include "ve1_pillar_probe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "port/hooks/Events.h"

#define PILLAR_COUNT 5 // swork[21] and swork[22]: the most pillars a pillar attack raises, less one
#define PILLAR_ROW 4   // swork[23]: the last row of D_i1_80199B40 and D_i1_80199CD0

static FILE* sFile = NULL;
static s32 sSeen[ARRAY_COUNT(gActors)]; // the frame each actor slot last updated as a pillar
static bool sBossSeen = false;
static f32 sBossZ = 0.0f;

static u32 Bits(f32 value) {
    u32 bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void CoopVe1PillarProbe_OnObjectUpdate(ObjectUpdateEvent* event) {
    if ((gGameState != GSTATE_PLAY) || (gCurrentLevel != LEVEL_VENOM_1)) {
        return;
    }
    if (event->type == OBJECT_TYPE_BOSS) {
        Boss* boss = (Boss*) event->object;

        if (boss->obj.id != OBJ_BOSS_VE1_GOLEMECH) {
            return;
        }
        if (!sBossSeen) {
            sBossSeen = true;
            fprintf(sFile, "golemech frame=%d\n", gGameFrameCount);
            fflush(sFile);
        }
        boss->swork[21] = boss->swork[22] = PILLAR_COUNT;
        boss->swork[23] = PILLAR_ROW;
        sBossZ = boss->obj.pos.z;
    } else if (event->type == OBJECT_TYPE_ACTOR) {
        Actor* actor = (Actor*) event->object;
        s32 slot = actor->index;

        if ((actor->obj.id != OBJ_ACTOR_VE1_PILLAR_1) || (slot < 0) || (slot >= ARRAY_COUNT(gActors))) {
            return;
        }
        // A pillar this slot did not hold on the frame before is a new one: it has not moved yet.
        if ((sSeen[slot] + 1 < gGameFrameCount) || (sSeen[slot] > gGameFrameCount)) {
            fprintf(sFile, "pillar frame=%d slot=%d x=%08x y=%08x z=%08x bossz=%08x\n", gGameFrameCount, (int) slot,
                    (unsigned) Bits(actor->obj.pos.x), (unsigned) Bits(actor->obj.pos.y),
                    (unsigned) Bits(actor->obj.pos.z), (unsigned) Bits(sBossZ));
            fflush(sFile); // tests stop the game from outside
        }
        sSeen[slot] = gGameFrameCount;
    }
}

void CoopVe1PillarProbe_Init(void) {
    const char* path = getenv("SF64_COOP_VE1_PILLAR_PROBE");
    size_t i;

    if ((path == NULL) || (path[0] == '\0')) {
        return;
    }
    sFile = fopen(path, "w");
    if (sFile == NULL) {
        return;
    }
    for (i = 0; i < ARRAY_COUNT(sSeen); i++) {
        sSeen[i] = -10;
    }
    REGISTER_LISTENER(ObjectUpdateEvent, CoopVe1PillarProbe_OnObjectUpdate, EVENT_PRIORITY_NORMAL);
}
