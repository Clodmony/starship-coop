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

// A Sector Y robot's pose angles (#281): fwork[20] and [21] (the head's pitch and yaw), fwork[28] and [29] (the gun's
// yaw and pitch) and obj.rot.y (the body's yaw).
#define COOP_AIM_TRACE_POSE 5
static f32 sSyPoseBefore[ARRAY_COUNT(gActors)][COOP_AIM_TRACE_POSE]; // before the robot's update of this frame
static s8 sSyPoseHave[ARRAY_COUNT(gActors)];

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

// " p<n>=<x>,<y>,<z>,<flies>" for every player, as the header says for the syrobot line, and the end of the line.
static void CoopAimTrace_Players(void) {
    s32 n;

    for (n = 0; n < CoopPlayers_Count(); n++) {
        const Player* p = &gPlayer[n];
        s32 flies = (n == 0) || (!CoopPlayers_IsParked(n) &&
                                 ((p->state == PLAYERSTATE_ACTIVE) || (p->state == PLAYERSTATE_U_TURN)));

        fprintf(sTraceFile, " p%d=%.3f,%.3f,%.3f,%d", (int) n, p->pos.x, p->pos.y, p->trueZpos, (int) flies);
    }
    fprintf(sTraceFile, "\n");
    fflush(sTraceFile);
}

// The `plasma` line of a shot of the Granga's gun (#281), as the header says; nothing for other shots.
static void CoopAimTrace_PlasmaShot(const Effect* effect) {
    if ((effect->obj.id != OBJ_EFFECT_376) || (gCurrentLevel != LEVEL_CORNERIA)) {
        return;
    }
    fprintf(sTraceFile, "plasma level=%d frame=%d start=%.3f,%.3f,%.3f dir=%.6f,%.6f,%.6f", (int) gCurrentLevel,
            (int) gGameFrameCount, effect->obj.pos.x, effect->obj.pos.y, effect->obj.pos.z, effect->vel.x - gPathVelX,
            effect->vel.y - gPathVelY, effect->vel.z + gPathVelZ);
    CoopAimTrace_Players();
}

static void CoopAimTrace_SyPose(const Actor* robot, f32* pose) {
    pose[0] = robot->fwork[20];
    pose[1] = robot->fwork[21];
    pose[2] = robot->fwork[28];
    pose[3] = robot->fwork[29];
    pose[4] = robot->obj.rot.y;
}

// The yaw and pitch SectorY_SyRobot_Update turns the robot's head and gun to when it faces the point (x, y, z): its
// own sum, from the robot as it is now (src/overlays/ovl_i6/fox_sy.c, the block under `work_046 == 1 || == 2`).
static void CoopAimTrace_SyWants(const Actor* robot, f32 x, f32 y, f32 z, f32* yaw, f32* pitch) {
    Matrix mtx;
    Vec3f src;
    Vec3f dest;

    Matrix_RotateZ(&mtx, -robot->vwork[29].z * M_DTOR, MTXF_NEW);
    Matrix_RotateX(&mtx, -robot->vwork[29].x * M_DTOR, MTXF_APPLY);
    Matrix_RotateY(&mtx, -robot->vwork[29].y * M_DTOR, MTXF_APPLY);
    Matrix_RotateZ(&mtx, -robot->obj.rot.z * M_DTOR, MTXF_APPLY);
    Matrix_RotateX(&mtx, -robot->obj.rot.x * M_DTOR, MTXF_APPLY);
    if (robot->iwork[17] == 0) {
        Matrix_RotateY(&mtx, -robot->obj.rot.y * M_DTOR, MTXF_APPLY);
    }
    src.x = x - robot->obj.pos.x;
    src.y = y - robot->obj.pos.y;
    src.z = z - robot->obj.pos.z;
    Matrix_MultVec3fNoTranslate(&mtx, &src, &dest);
    *yaw = Math_RadToDeg(Math_Atan2F(dest.x, dest.z));
    *pitch = Math_RadToDeg(-Math_Atan2F(dest.y, sqrtf(SQ(dest.x) + SQ(dest.z))));
    if ((*pitch > 90.0f) && (*pitch < 180.0f)) {
        *pitch = 90.0f;
    }
    if ((*pitch < 270.0f) && (*pitch > 180.0f)) {
        *pitch = 270.0f;
    }
    if (robot->iwork[17] == 0) {
        if ((*yaw > 90.0f) && (*yaw < 180.0f)) {
            *yaw = 90.0f;
        }
        if ((*yaw < 270.0f) && (*yaw > 180.0f)) {
            *yaw = 270.0f;
        }
    }
}

// The `sypose` line of a Sector Y robot that turns to a player on this frame (#281), as the header says.
static void CoopAimTrace_SyRobotPose(s32 slot) {
    const Actor* robot = &gActors[slot];
    f32 now[COOP_AIM_TRACE_POSE];
    s32 n;

    CoopAimTrace_SyPose(robot, now);
    fprintf(sTraceFile, "sypose level=%d frame=%d slot=%d body=%d before=%.4f,%.4f,%.4f,%.4f,%.4f", (int) gCurrentLevel,
            (int) gGameFrameCount, (int) slot, (int) (robot->iwork[17] != 0), sSyPoseBefore[slot][0],
            sSyPoseBefore[slot][1], sSyPoseBefore[slot][2], sSyPoseBefore[slot][3], sSyPoseBefore[slot][4]);
    fprintf(sTraceFile, " after=%.4f,%.4f,%.4f,%.4f,%.4f start=%.3f,%.3f,%.3f", now[0], now[1], now[2], now[3], now[4],
            robot->fwork[16], robot->fwork[17], robot->fwork[18]);
    for (n = 0; n < CoopPlayers_Count(); n++) {
        f32 yaw;
        f32 pitch;

        CoopAimTrace_SyWants(robot, gPlayer[n].pos.x, gPlayer[n].pos.y, gPlayer[n].pos.z, &yaw, &pitch);
        fprintf(sTraceFile, " w%d=%.3f,%.3f", (int) n, yaw, pitch);
    }
    CoopAimTrace_Players();
}

// The `syrobot` line of a shot that a Sector Y robot fired (#273), as the header says; nothing for other shots.
static void CoopAimTrace_SyRobotShot(const Effect* effect) {
    s32 i;

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
        CoopAimTrace_Players();
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
    CoopAimTrace_PlasmaShot(effect);
}

// A Sector Y robot's pose before its update, for the sypose line.
static void CoopAimTrace_OnObjectUpdate(ObjectUpdateEvent* event) {
    const Actor* actor = (const Actor*) event->object;

    if ((event->type != OBJECT_TYPE_ACTOR) || (actor == NULL) || !CoopAimTrace_InLevel() ||
        (gCurrentLevel != LEVEL_SECTOR_Y) || !CoopAimTrace_IsSyRobot(actor)) {
        return;
    }
    CoopAimTrace_SyPose(actor, sSyPoseBefore[actor - gActors]);
    sSyPoseHave[actor - gActors] = 1;
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
            sSyPoseHave[i] = 0;
        }
        return;
    }
    for (i = 0; i < ARRAY_COUNT(gActors); i++) {
        const Actor* actor = &gActors[i];
        s32 target = -1;

        // The robot turns its head and gun to a player in its states 1 and 2 (work_046).
        if (sSyPoseHave[i] && (actor->obj.status == OBJ_ACTIVE) && CoopAimTrace_IsSyRobot(actor) &&
            ((actor->work_046 == 1) || (actor->work_046 == 2))) {
            CoopAimTrace_SyRobotPose(i);
        }
        sSyPoseHave[i] = 0;

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
    REGISTER_LISTENER(ObjectUpdateEvent, CoopAimTrace_OnObjectUpdate, EVENT_PRIORITY_NORMAL);
    REGISTER_LISTENER(GamePostUpdateEvent, CoopAimTrace_OnGamePostUpdate, EVENT_PRIORITY_NORMAL);
}
