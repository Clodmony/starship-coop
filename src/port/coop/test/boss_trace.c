#include "boss_trace.h"

#include <stdio.h>
#include <stdlib.h>

#include "global.h"
#include "port/coop/game/aim.h"
#include "port/coop/game/players.h"
#include "port/hooks/Events.h"

static FILE* sFile = NULL;

static s32 CoopBossTrace_InLevel(void) {
    return (gGameState == GSTATE_PLAY) && (gPlayer != NULL) && CoopPlayers_IsCoop();
}

static void CoopBossTrace_OnGamePostUpdate(IEvent* event) {
    int32_t count;
    const int8_t* kept;
    s32 i;

    if (!CoopBossTrace_InLevel()) {
        return;
    }
    kept = CoopAim_KeptSlots(&count);
    fprintf(sFile, "frame=%d level=%d mode=%d load=%d path=%.1f", (int) gGameFrameCount, (int) gCurrentLevel,
            (int) gLevelMode, (int) gObjectLoadIndex, gPathProgress);
    for (i = 0; i < ARRAY_COUNT(gBosses); i++) {
        const Boss* boss = &gBosses[i];

        if (boss->obj.status != OBJ_FREE) {
            fprintf(sFile, " b%d=%d,%d,%.1f,%.1f,%.1f,%.2f,%.2f,%d,%d", (int) i, (int) boss->obj.id, (int) boss->state,
                    boss->obj.pos.x, boss->obj.pos.y, boss->obj.pos.z, boss->obj.rot.x, boss->obj.rot.y, (int) kept[i],
                    (int) kept[ARRAY_COUNT(gBosses) + ARRAY_COUNT(gActors) + i]);
        }
    }
    for (i = 0; i < CoopPlayers_Count(); i++) {
        const Player* p = &gPlayer[i];

        fprintf(sFile, " p%d=%d,%d,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%d,%d,%d,%d", (int) i, (int) p->state,
                (int) CoopPlayers_IsParked(i), p->pos.x, p->pos.y, p->trueZpos, p->cam.eye.x, p->cam.eye.y,
                p->cam.eye.z, (int) p->draw, (int) p->hitTimer, (int) p->csState, (int) p->hitDirection);
    }
    for (i = 0; i < ARRAY_COUNT(gActors); i++) {
        const Actor* actor = &gActors[i];

        if ((actor->obj.status == OBJ_ACTIVE) && (actor->obj.id == OBJ_ACTOR_ALLRANGE) &&
            (actor->aiIndex == AI360_FOX) &&
            (((actor->aiType >= AI360_WOLF) && (actor->aiType <= AI360_ANDREW)) ||
             ((actor->aiType >= AI360_ENEMY) && (actor->aiType < AI360_GREAT_FOX)))) {
            fprintf(sFile, " a%d=%d,%d,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%d", (int) i, (int) actor->aiType,
                    (int) actor->state, actor->obj.pos.x, actor->obj.pos.y, actor->obj.pos.z, actor->fwork[4],
                    actor->fwork[5], actor->fwork[6], (int) kept[ARRAY_COUNT(gBosses) + i]);
        }
    }
    fputc('\n', sFile);
    fflush(sFile); // tests stop the game from outside
}

void CoopBossTrace_Init(void) {
    const char* path = getenv("SF64_COOP_BOSS_TRACE");

    if ((path == NULL) || (path[0] == '\0')) {
        return;
    }
    sFile = fopen(path, "a");
    if (sFile == NULL) {
        return;
    }
    REGISTER_LISTENER(GamePostUpdateEvent, CoopBossTrace_OnGamePostUpdate, EVENT_PRIORITY_NORMAL);
}
