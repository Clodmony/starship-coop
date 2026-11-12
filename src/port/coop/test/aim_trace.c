#include "aim_trace.h"

#include <stdio.h>
#include <stdlib.h>

#include "global.h"
#include "port/coop/game/players.h"
#include "port/hooks/Events.h"

// A shot fired at a player flies at it to within the error of the game's arctangent and sine; players fly 250 or more
// apart, which from 10000 away is 1.4 degrees.
#define COOP_AIM_TRACE_DEGREES 0.5

// How near a missile's target point must be to a player's position to be that player's.
#define COOP_AIM_TRACE_MISSILE_REACH 1.0f

static FILE* sTraceFile = NULL;
static s32 sMissileTarget[ARRAY_COUNT(gActors)]; // the player each actor slot seeks, -1 for none

static s32 CoopAimTrace_InLevel(void) {
    return (gGameState == GSTATE_PLAY) && (gPlayer != NULL) && CoopPlayers_IsCoop();
}

static s32 CoopAimTrace_IsEnemyShot(s32 id) {
    return (id == OBJ_EFFECT_ENEMY_LASER_1) || (id == OBJ_EFFECT_354) || (id == OBJ_EFFECT_355) ||
           (id == OBJ_EFFECT_356) || (id == OBJ_EFFECT_376) || (id == OBJ_EFFECT_377);
}

// The player the shot flies at, as the header says, or -1.
static s32 CoopAimTrace_ShotTarget(const Effect* effect) {
    double dir[3];
    double dirLength;
    double bestCos = -2.0;
    s32 best = -1;
    s32 i;

    dir[0] = effect->vel.x - gPathVelX;
    dir[1] = effect->vel.y - gPathVelY;
    dir[2] = effect->vel.z + gPathVelZ;
    dirLength = sqrt((dir[0] * dir[0]) + (dir[1] * dir[1]) + (dir[2] * dir[2]));
    if (dirLength <= 0.0) {
        return -1;
    }
    for (i = 0; i < CoopPlayers_Count(); i++) {
        const Player* p = &gPlayer[i];
        double to[3];
        double toLength;
        double cosine;

        if (CoopPlayers_IsParked(i)) {
            continue;
        }
        to[0] = p->pos.x - effect->obj.pos.x;
        to[1] = p->pos.y - effect->obj.pos.y;
        to[2] = p->trueZpos - effect->obj.pos.z;
        toLength = sqrt((to[0] * to[0]) + (to[1] * to[1]) + (to[2] * to[2]));
        if (toLength <= 0.0) {
            continue;
        }
        cosine = ((dir[0] * to[0]) + (dir[1] * to[1]) + (dir[2] * to[2])) / (dirLength * toLength);
        if (cosine > bestCos) {
            bestCos = cosine;
            best = i;
        }
    }
    if ((best < 0) || (bestCos < cos(COOP_AIM_TRACE_DEGREES * M_PI / 180.0))) {
        return -1;
    }
    return best;
}

// Nonzero for a Sector Y robot (SectorY_SyRobot_Update's actors).
static s32 CoopAimTrace_IsSyRobot(const Actor* actor) {
    if (actor->obj.id == OBJ_ACTOR_SY_ROBOT) {
        return 1;
    }
    return (actor->obj.id == OBJ_ACTOR_EVENT) &&
           ((actor->eventType == EVID_SY_ROBOT_1) || (actor->eventType == EVID_SY_ROBOT_2) ||
            (actor->eventType == EVID_SY_ROBOT_3));
}

// The `syrobot` line of a shot that a Sector Y robot fired (#273), as the header says; nothing for other shots.
static void CoopAimTrace_SyRobotShot(const Effect* effect) {
    s32 i;
    s32 n;

    if (effect->obj.id != OBJ_EFFECT_354) {
        return;
    }
    for (i = 0; i < ARRAY_COUNT(gActors); i++) {
        const Actor* robot = &gActors[i];

        if ((robot->obj.status == OBJ_FREE) || !CoopAimTrace_IsSyRobot(robot) ||
            (robot->fwork[16] != effect->obj.pos.x) || (robot->fwork[17] != effect->obj.pos.y) ||
            (robot->fwork[18] != effect->obj.pos.z)) {
            continue;
        }
        fprintf(sTraceFile,
                "syrobot level=%d frame=%d slot=%d script=%d spread=%lld start=%.3f,%.3f,%.3f "
                "dir=%.6f,%.6f,%.6f",
                (int) gCurrentLevel, (int) gGameFrameCount, (int) i, (int) robot->aiType,
                (long long) (intptr_t) robot->iwork[19], effect->obj.pos.x, effect->obj.pos.y, effect->obj.pos.z,
                effect->vel.x - gPathVelX, effect->vel.y - gPathVelY, effect->vel.z + gPathVelZ);
        for (n = 0; n < CoopPlayers_Count(); n++) {
            const Player* p = &gPlayer[n];
            s32 flies = (n == 0) || (!CoopPlayers_IsParked(n) &&
                                     ((p->state == PLAYERSTATE_ACTIVE) || (p->state == PLAYERSTATE_U_TURN)));

            fprintf(sTraceFile, " p%d=%.3f,%.3f,%.3f,%d", (int) n, p->pos.x, p->pos.y, p->trueZpos, (int) flies);
        }
        fprintf(sTraceFile, "\n");
        fflush(sTraceFile);
        return;
    }
}

static void CoopAimTrace_OnObjectInit(IEvent* event) {
    const ObjectInitEvent* init = (const ObjectInitEvent*) event;
    const Effect* effect;

    if ((init->type != OBJECT_TYPE_EFFECT) || !CoopAimTrace_InLevel()) {
        return;
    }
    effect = (const Effect*) init->object;
    if (!CoopAimTrace_IsEnemyShot(effect->obj.id)) {
        return;
    }
    fprintf(sTraceFile, "shot level=%d frame=%d id=%d player=%d\n", (int) gCurrentLevel, (int) gGameFrameCount,
            (int) effect->obj.id, CoopAimTrace_ShotTarget(effect));
    fflush(sTraceFile); // tests stop the game from outside
    CoopAimTrace_SyRobotShot(effect);
}

static s32 CoopAimTrace_MissileTarget(const Actor* actor) {
    s32 i;

    for (i = 0; i < CoopPlayers_Count(); i++) {
        const Player* p = &gPlayer[i];

        if (!CoopPlayers_IsParked(i) && (fabsf(actor->fwork[27] - p->pos.x) < COOP_AIM_TRACE_MISSILE_REACH) &&
            (fabsf(actor->fwork[28] - p->pos.y) < COOP_AIM_TRACE_MISSILE_REACH) &&
            (fabsf(actor->fwork[29] - p->trueZpos) < COOP_AIM_TRACE_MISSILE_REACH)) {
            return i;
        }
    }
    return -1;
}

static void CoopAimTrace_OnGamePostUpdate(IEvent* event) {
    s32 i;

    if (!CoopAimTrace_InLevel()) {
        for (i = 0; i < ARRAY_COUNT(gActors); i++) {
            sMissileTarget[i] = -1;
        }
        return;
    }
    for (i = 0; i < ARRAY_COUNT(gActors); i++) {
        const Actor* actor = &gActors[i];
        s32 target = -1;

        if ((actor->obj.status == OBJ_ACTIVE) &&
            ((actor->obj.id == OBJ_ACTOR_MISSILE_SEEK_TEAM) || (actor->obj.id == OBJ_ACTOR_MISSILE_SEEK_PLAYER))) {
            target = CoopAimTrace_MissileTarget(actor);
        }
        if ((target >= 0) && (target != sMissileTarget[i])) {
            fprintf(sTraceFile, "missile level=%d frame=%d slot=%d player=%d\n", (int) gCurrentLevel,
                    (int) gGameFrameCount, i, target);
            fflush(sTraceFile);
        }
        sMissileTarget[i] = target;
    }
}

void CoopAimTrace_Init(void) {
    const char* path = getenv("SF64_COOP_AIM_TRACE");
    s32 i;

    if ((path == NULL) || (path[0] == '\0')) {
        return;
    }
    sTraceFile = fopen(path, "a");
    if (sTraceFile == NULL) {
        return;
    }
    for (i = 0; i < ARRAY_COUNT(gActors); i++) {
        sMissileTarget[i] = -1;
    }
    REGISTER_LISTENER(ObjectInitEvent, CoopAimTrace_OnObjectInit, EVENT_PRIORITY_NORMAL);
    REGISTER_LISTENER(GamePostUpdateEvent, CoopAimTrace_OnGamePostUpdate, EVENT_PRIORITY_NORMAL);
}
