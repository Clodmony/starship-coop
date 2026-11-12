#include "boss_trace.h"

#include <stdio.h>
#include <stdlib.h>

#include "global.h"
#include "port/coop/game/aim.h"
#include "port/coop/game/players.h"
#include "port/hooks/Events.h"

static FILE* sFile = NULL;

extern f32 D_i3_801C4308[82]; // src/overlays/ovl_i3/fox_aq.c: the Bacoon's head and eye angles, among others
extern f32 D_i3_801C2768[14]; // src/overlays/ovl_i3/fox_so.c: the Vulkain's head, [0] pitch and [1] yaw, among others
extern s16 D_i5_801BE320[35]; // src/overlays/ovl_i5/fox_ma.c: Macbeth's train end, [18] the track's stage

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
    // Where Aquas' Bacoon turns its head and eyes (#220): yaw and pitch of the head, the two eyes' yaws.
    for (i = 0; i < ARRAY_COUNT(gBosses); i++) {
        if ((gBosses[i].obj.status == OBJ_ACTIVE) && (gBosses[i].obj.id == OBJ_BOSS_AQ_BACOON)) {
            fprintf(sFile, " aq=%.2f,%.2f,%.2f,%.2f", D_i3_801C4308[15], D_i3_801C4308[14], D_i3_801C4308[18],
                    D_i3_801C4308[19]);
            break;
        }
    }
    // Where Solar's Vulkain turns its head, along which it spits its fireballs (campaign run, #220): yaw and pitch.
    for (i = 0; i < ARRAY_COUNT(gBosses); i++) {
        if ((gBosses[i].obj.status == OBJ_ACTIVE) && (gBosses[i].obj.id == OBJ_BOSS_SO_VULKAIN)) {
            fprintf(sFile, " so=%.2f,%.2f", D_i3_801C2768[1], D_i3_801C2768[0]);
            break;
        }
    }
    // Macbeth's train end (#220): an actor, not a boss slot, so its kept slot is the actor's.
    for (i = 0; i < ARRAY_COUNT(gActors); i++) {
        const Actor* actor = &gActors[i];

        if ((actor->obj.status == OBJ_ACTIVE) && (actor->obj.id == OBJ_ACTOR_207)) {
            fprintf(sFile, " ma=%d,%d,%.1f,%.1f,%.1f,%.2f,%.2f,%d", (int) i, (int) actor->state, actor->obj.pos.x,
                    actor->obj.pos.y, actor->obj.pos.z, actor->obj.rot.x, actor->obj.rot.y,
                    (int) kept[ARRAY_COUNT(gBosses) + i]);
            break;
        }
    }
    // Bosses' shots that home in or fly where the boss aims (#220): Meteo's Crusher rings and Zoness' Sarumarine balls.
    for (i = 0; i < ARRAY_COUNT(gEffects); i++) {
        const Effect* effect = &gEffects[i];

        if ((effect->obj.status == OBJ_ACTIVE) && (effect->obj.id == OBJ_EFFECT_371)) {
            fprintf(sFile, " e%d=%d,%.1f,%.1f,%.1f", (int) i, (int) effect->obj.id, effect->obj.pos.x,
                    effect->obj.pos.y, effect->obj.pos.z);
        }
    }
    for (i = 0; i < ARRAY_COUNT(gActors); i++) {
        const Actor* actor = &gActors[i];

        if ((actor->obj.status == OBJ_ACTIVE) && (actor->obj.id == OBJ_ACTOR_ZO_BALL)) {
            fprintf(sFile, " o%d=%d,%.1f,%.1f,%.1f", (int) i, (int) actor->obj.id, actor->obj.pos.x, actor->obj.pos.y,
                    actor->obj.pos.z);
        }
    }
    fputc('\n', sFile);
    fflush(sFile); // tests stop the game from outside
}

// SF64_COOP_BOSS_POKE (#220): <slot>:<from>:<to>:<timer>. The first time gBosses[slot] is in state `from` after a
// frame, it is put in state `to` with timer_050 `timer`, once per level.
static s32 sPoke[4];
static s32 sPoked = 1;
static s32 sPokeLevelFrame = -1;

static void CoopBossTrace_OnPokeUpdate(IEvent* event) {
    Boss* boss;

    if (!CoopBossTrace_InLevel()) {
        return;
    }
    if ((s32) gGameFrameCount < sPokeLevelFrame) { // a new level
        sPoked = 0;
    }
    sPokeLevelFrame = gGameFrameCount;
    if (sPoked || (sPoke[0] < 0) || (sPoke[0] >= ARRAY_COUNT(gBosses))) {
        return;
    }
    boss = &gBosses[sPoke[0]];
    if ((boss->obj.status == OBJ_ACTIVE) && (boss->state == sPoke[1])) {
        boss->state = sPoke[2];
        boss->timer_050 = sPoke[3];
        sPoked = 1;
    }
}

// SF64_COOP_MA_TRACK_END (#220, wave 7): the track's end for Macbeth's train end, once its fight is on.
static s32 sTrackEndDone = 0;

static void CoopBossTrace_OnTrackEndUpdate(IEvent* event) {
    s32 i;

    if (sTrackEndDone || !CoopBossTrace_InLevel() || (gCurrentLevel != LEVEL_MACBETH)) {
        return;
    }
    for (i = 0; i < ARRAY_COUNT(gActors); i++) {
        if ((gActors[i].obj.status == OBJ_ACTIVE) && (gActors[i].obj.id == OBJ_ACTOR_207) && (gActors[i].state == 4)) {
            D_i5_801BE320[18] = 4;
            sTrackEndDone = 1;
            return;
        }
    }
}

void CoopBossTrace_Init(void) {
    const char* path = getenv("SF64_COOP_BOSS_TRACE");
    const char* poke = getenv("SF64_COOP_BOSS_POKE");
    const char* trackEnd = getenv("SF64_COOP_MA_TRACK_END");

    if ((trackEnd != NULL) && (trackEnd[0] == '1')) {
        REGISTER_LISTENER(GamePostUpdateEvent, CoopBossTrace_OnTrackEndUpdate, EVENT_PRIORITY_NORMAL);
    }

    if ((poke != NULL) && (sscanf(poke, "%d:%d:%d:%d", &sPoke[0], &sPoke[1], &sPoke[2], &sPoke[3]) == 4)) {
        sPoked = 0;
        REGISTER_LISTENER(GamePostUpdateEvent, CoopBossTrace_OnPokeUpdate, EVENT_PRIORITY_NORMAL);
    }
    if ((path == NULL) || (path[0] == '\0')) {
        return;
    }
    sFile = fopen(path, "a");
    if (sFile == NULL) {
        return;
    }
    REGISTER_LISTENER(GamePostUpdateEvent, CoopBossTrace_OnGamePostUpdate, EVENT_PRIORITY_NORMAL);
}
