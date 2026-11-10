#include "aim.h"

#include "global.h"
#include "port/coop/game/players.h"
#include "port/hooks/Events.h"

#define AIM_BOSSES ARRAY_COUNT(gBosses)
#define AIM_ACTORS ARRAY_COUNT(gActors)

#define AIM_HELD (AIM_BOSSES + AIM_ACTORS) // where the held players start in sKept

// The kept targets (#213): one per boss, then one per actor, then the player each boss holds. -1 is an empty slot.
static s8 sKept[AIM_BOSSES + AIM_ACTORS + AIM_BOSSES];

// Nonzero when an enemy may aim at player `i`. Player 0 always counts: it is upstream's target, and while it does not
// fly the others are parked (CoopPlayers_SkipUpdate).
static s32 CoopAim_CanAimAt(s32 i) {
    const Player* p = &gPlayer[i];

    if (i == 0) {
        return 1;
    }
    return !CoopPlayers_IsParked(i) && ((p->state == PLAYERSTATE_ACTIVE) || (p->state == PLAYERSTATE_U_TURN));
}

int32_t CoopAim_Nearest(float x, float y, float z) {
    s32 best = 0;
    f32 bestDist = 0.0f;
    s32 i;

    if (!CoopPlayers_IsCoop() || (gPlayer == NULL)) {
        return 0;
    }
    for (i = 0; i < CoopPlayers_Count(); i++) {
        const Player* p = &gPlayer[i];
        f32 dx;
        f32 dy;
        f32 dz;
        f32 dist;

        if (!CoopAim_CanAimAt(i)) {
            continue;
        }
        dx = p->pos.x - x;
        dy = p->pos.y - y;
        dz = p->trueZpos - z;
        dist = (dx * dx) + (dy * dy) + (dz * dz);
        if ((i == 0) || (dist < bestDist)) { // strictly nearer: a tie stays with the lower index
            best = i;
            bestDist = dist;
        }
    }
    return best;
}

// The kept player of `slot`, picked again from (x, y, z) when the slot is empty or that player cannot be aimed at.
static s32 CoopAim_Keep(s32 slot, f32 x, f32 y, f32 z) {
    s32 kept = sKept[slot];

    if (!CoopPlayers_IsCoop() || (gPlayer == NULL)) {
        return 0;
    }
    if ((kept < 0) || (kept >= CoopPlayers_Count()) || !CoopAim_CanAimAt(kept)) {
        kept = CoopAim_Nearest(x, y, z);
        sKept[slot] = (s8) kept;
    }
    return kept;
}

int32_t CoopAim_BossTarget(int32_t boss, float x, float y, float z) {
    if ((boss < 0) || (boss >= AIM_BOSSES)) {
        return 0;
    }
    return CoopAim_Keep(boss, x, y, z);
}

void CoopAim_ForgetBoss(int32_t boss) {
    if ((boss >= 0) && (boss < AIM_BOSSES)) {
        sKept[boss] = -1;
    }
}

int32_t CoopAim_RepickBoss(int32_t boss, float x, float y, float z) {
    CoopAim_ForgetBoss(boss);
    return CoopAim_BossTarget(boss, x, y, z);
}

int32_t CoopAim_ActorTarget(int32_t actor, float x, float y, float z) {
    if ((actor < 0) || (actor >= AIM_ACTORS)) {
        return 0;
    }
    return CoopAim_Keep(AIM_BOSSES + actor, x, y, z);
}

int32_t CoopAim_Chased(const Actor* actor) {
    s32 hostile = ((actor->aiType >= AI360_WOLF) && (actor->aiType <= AI360_ANDREW)) ||
                  ((actor->aiType >= AI360_ENEMY) && (actor->aiType < AI360_GREAT_FOX));

    if (!hostile) {
        return 0; // a wingman, Katt or Bill escorts Fox
    }
    return CoopAim_ActorTarget(actor->index, actor->obj.pos.x, actor->obj.pos.y, actor->obj.pos.z);
}

void CoopAim_ForgetActor(int32_t actor) {
    if ((actor >= 0) && (actor < AIM_ACTORS)) {
        sKept[AIM_BOSSES + actor] = -1;
    }
}

int32_t CoopAim_BrainCatch(float x, float y, float z) {
    s32 best = -1;
    f32 bestDist = 0.0f;
    s32 count = CoopPlayers_IsCoop() ? CoopPlayers_Count() : 1;
    s32 i;

    if (gPlayer == NULL) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        const Player* p = &gPlayer[i];
        f32 dx = x - p->pos.x;
        f32 dy = y - 300.0f - p->pos.y;
        f32 dz = z - p->trueZpos;
        f32 dist;

        // The box of Andross_AndBrain_Update, in its own order. Player 0 needs no state, as upstream.
        if (!((fabsf(dx) < 300.0f) && (fabsf(dy) < 300.0f) && (fabsf(dz) < 300.0f))) {
            continue;
        }
        if ((i > 0) && !CoopAim_CanAimAt(i)) {
            continue;
        }
        dist = (dx * dx) + (dy * dy) + (dz * dz);
        if ((best < 0) || (dist < bestDist)) { // strictly nearer: a tie stays with the lower index
            best = i;
            bestDist = dist;
        }
    }
    return best;
}

void CoopAim_AndrossPull(float x, float y, float step) {
    s32 i;

    if (!CoopPlayers_IsCoop() || (gPlayer == NULL)) {
        return;
    }
    for (i = 1; i < CoopPlayers_Count(); i++) {
        Player* p = &gPlayer[i];

        if (CoopAim_CanAimAt(i)) { // as Andross_AndAndross_Update pulls player 0
            Math_SmoothStepToF(&p->pos.x, x, 0.1f, step, 0);
            Math_SmoothStepToF(&p->pos.y, y, 0.1f, step, 0);
        }
    }
}

int32_t CoopAim_AndrossSwallow(float x, float y, float z) {
    s32 best = -1;
    f32 bestDist = 0.0f;
    s32 count = CoopPlayers_IsCoop() ? CoopPlayers_Count() : 1;
    s32 i;

    if (gPlayer == NULL) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        const Player* p = &gPlayer[i];
        f32 dx;
        f32 dy;
        f32 dz;
        f32 dist;

        // The test of Andross_AndAndross_Update, state 17. Player 0 needs no state, as upstream.
        if (!(fabsf(p->trueZpos - z) < 200.0f) || ((i > 0) && !CoopAim_CanAimAt(i))) {
            continue;
        }
        dx = p->pos.x - x;
        dy = p->pos.y - y;
        dz = p->trueZpos - z;
        dist = (dx * dx) + (dy * dy) + (dz * dz);
        if ((best < 0) || (dist < bestDist)) { // strictly nearer: a tie stays with the lower index
            best = i;
            bestDist = dist;
        }
    }
    return best;
}

int32_t CoopAim_InMouth(int32_t boss) {
    s32 held;

    if (!CoopPlayers_IsCoop() || (gPlayer == NULL) || (boss < 0) || (boss >= AIM_BOSSES)) {
        return -1;
    }
    held = sKept[AIM_HELD + boss];
    if ((held < 0) || (held >= CoopPlayers_Count()) || (gPlayer[held].state != PLAYERSTATE_ANDROSS_MOUTH)) {
        return -1;
    }
    return held;
}

void CoopAim_SetHeld(int32_t boss, int32_t player) {
    if ((boss >= 0) && (boss < AIM_BOSSES)) {
        sKept[AIM_HELD + boss] = (s8) player;
    }
}

int32_t CoopAim_Held(int32_t boss) {
    s32 held;

    if (!CoopPlayers_IsCoop() || (boss < 0) || (boss >= AIM_BOSSES)) {
        return 0;
    }
    held = sKept[AIM_HELD + boss];
    return ((held >= 0) && (held < CoopPlayers_Count())) ? held : 0;
}

void CoopAim_LevelInit(void) {
    s32 i;

    for (i = 0; i < ARRAY_COUNT(sKept); i++) {
        sKept[i] = -1;
    }
}

int8_t* CoopAim_KeptSlots(int32_t* count) {
    *count = ARRAY_COUNT(sKept);
    return sKept;
}

// A new boss or actor in a slot picks its own target.
static void CoopAim_OnObjectInit(IEvent* event) {
    const ObjectInitEvent* init = (const ObjectInitEvent*) event;

    if ((init->type == OBJECT_TYPE_BOSS) && ((const Boss*) init->object >= gBosses) &&
        ((const Boss*) init->object < gBosses + AIM_BOSSES)) {
        CoopAim_ForgetBoss((s32) ((const Boss*) init->object - gBosses));
    } else if (((init->type == OBJECT_TYPE_ACTOR) || (init->type == OBJECT_TYPE_ACTOR_EVENT)) &&
               ((const Actor*) init->object >= gActors) && ((const Actor*) init->object < gActors + AIM_ACTORS)) {
        CoopAim_ForgetActor((s32) ((const Actor*) init->object - gActors));
    }
}

void CoopAim_Init(void) {
    CoopAim_LevelInit();
    REGISTER_LISTENER(ObjectInitEvent, CoopAim_OnObjectInit, EVENT_PRIORITY_NORMAL);
}
