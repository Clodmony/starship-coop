#include "pilot.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "fox_co.h"
#include "port/coop/game/players.h"

extern f32 sCoGrangaWork[68]; // src/overlays/ovl_i1/fox_co.c

#define PILOT_STICK_MAX 80
#define PILOT_FIRE_PERIOD 4     // A down for one frame in four: each press is a shot, and it never charges
#define PILOT_SHOT_SPEED 133.0f // Player_ArwingLaser
#define PILOT_SHOT_RANGE 3500.0f
#define PILOT_BREAK_OFF 1300.0f // a run at the boss turns away here: at this speed it takes that much room to miss it
#define PILOT_RUN_START 2600.0f
#define PILOT_RUN_HEIGHT 750.0f
#define PILOT_MIN_HEIGHT 220.0f
#define PILOT_LOOK_AHEAD 1700.0f // on rails, about 40 frames at the speed of the path
#define PILOT_BOX_MAX 256

typedef struct {
    u16 button;
    f32 x;
    f32 y;
} PilotPad;

// Something ahead on rails, as a box across the path that a spot has to stay out of.
typedef struct {
    f32 x0;
    f32 x1;
    f32 y0;
    f32 y1;
} PilotBox;

static int sOn = 0;
static FILE* sLog = NULL;
static s32 sShields[COOP_PLAYERS_MAX];
static u16 sLastButton[COOP_PLAYERS_MAX];
static PilotBox sBoxes[PILOT_BOX_MAX];
static s32 sBoxCount;

static void CoopPilot_AddBox(f32 x, f32 y, f32 halfX, f32 halfY) {
    if (sBoxCount < PILOT_BOX_MAX) {
        PilotBox* box = &sBoxes[sBoxCount++];

        box->x0 = x - halfX - 90.0f;
        box->x1 = x + halfX + 90.0f;
        box->y0 = y - halfY - 70.0f;
        box->y1 = y + halfY + 70.0f;
    }
}

static bool CoopPilot_IsAhead(f32 z, f32 halfZ, f32 playerZ) {
    return ((z - halfZ) < (playerZ + 150.0f)) && ((z + halfZ) > (playerZ - PILOT_LOOK_AHEAD));
}

// The game's hitbox lists: a count, then per box an optional marker and z, y and x as offset and size. A box behind a
// marker of HITBOX_SHADOW or more only makes a sound (Player_CheckHitboxCollision).
static void CoopPilot_AddHitboxes(const Object* obj, const f32* data, f32 playerZ) {
    s32 count = (s32) *data++;
    f32 sinY = SIN_DEG(obj->rot.y);
    f32 cosY = COS_DEG(obj->rot.y);
    s32 i;

    for (i = 0; i < count; i++) {
        bool solid = true;
        bool rotated = false;
        const Hitbox* box;

        if (data[0] == HITBOX_ROTATED) {
            rotated = true;
            data += 4;
        } else if (data[0] >= HITBOX_SHADOW) {
            solid = false;
            data++;
        }
        box = (const Hitbox*) data;
        data += 6;
        if (solid) {
            f32 x = box->x.offset * cosY + box->z.offset * sinY + obj->pos.x;
            f32 z = box->z.offset * cosY - box->x.offset * sinY + obj->pos.z;
            f32 halfX = fabsf(box->x.size * cosY) + fabsf(box->z.size * sinY);
            f32 halfZ = fabsf(box->z.size * cosY) + fabsf(box->x.size * sinY);
            f32 halfY = box->y.size;

            if (rotated || (obj->rot.x != 0.0f) || (obj->rot.z != 0.0f)) {
                halfX = halfZ = halfY = sqrtf(SQ(box->x.size) + SQ(box->y.size) + SQ(box->z.size));
            }
            if (CoopPilot_IsAhead(z, halfZ, playerZ)) {
                CoopPilot_AddBox(x, box->y.offset + obj->pos.y, halfX, halfY);
            }
        }
    }
}

// Corneria's hills have a mesh and no hitbox (Play_CheckPolyCollision). The bounds of the mesh stand in for it.
static void CoopPilot_AddHill(const Object* obj, s32 colId, f32 playerZ) {
    const CollisionHeader2* header = &D_800D2CA0[colId];
    f32 radius = sqrtf(SQ(fmaxf(fabsf(header->min.x), fabsf(header->max.x))) +
                       SQ(fmaxf(fabsf(header->min.z), fabsf(header->max.z))));

    if (CoopPilot_IsAhead(obj->pos.z, radius, playerZ)) {
        CoopPilot_AddBox(obj->pos.x, obj->pos.y + (header->min.y + header->max.y) * 0.5f, radius,
                         (header->max.y - header->min.y) * 0.5f);
    }
}

static void CoopPilot_ScanRails(const Player* p) {
    s32 i;

    sBoxCount = 0;
    for (i = 0; i < ARRAY_COUNT(gScenery); i++) {
        const Scenery* scenery = &gScenery[i];

        if (scenery->obj.status != OBJ_ACTIVE) {
            continue;
        }
        switch (scenery->obj.id) {
            case OBJ_SCENERY_CO_BUMP_1:
                CoopPilot_AddHill(&scenery->obj, COL2_0, p->trueZpos);
                break;
            case OBJ_SCENERY_CO_BUMP_2:
                CoopPilot_AddHill(&scenery->obj, COL2_2, p->trueZpos);
                break;
            case OBJ_SCENERY_CO_BUMP_3:
                CoopPilot_AddHill(&scenery->obj, COL2_3, p->trueZpos);
                break;
            case OBJ_SCENERY_CO_BUMP_4:
            case OBJ_SCENERY_CO_BUMP_5:
                CoopPilot_AddHill(&scenery->obj, COL2_1, p->trueZpos);
                break;
            default:
                CoopPilot_AddHitboxes(&scenery->obj, scenery->info.hitbox, p->trueZpos);
                break;
        }
    }
    for (i = 0; i < ARRAY_COUNT(gActors); i++) {
        const Actor* actor = &gActors[i];

        if ((actor->obj.status == OBJ_ACTIVE) && (actor->timer_0C2 == 0) && (actor->obj.id != OBJ_ACTOR_TEAM_BOSS)) {
            CoopPilot_AddHitboxes(&actor->obj, actor->info.hitbox, p->trueZpos);
        }
    }
    // A shot counts where it will cross the player's z.
    for (i = 0; i < ARRAY_COUNT(gEffects); i++) {
        const Effect* effect = &gEffects[i];

        if ((effect->obj.status == OBJ_ACTIVE) && (effect->obj.id == OBJ_EFFECT_ENEMY_LASER_1)) {
            f32 closing = effect->vel.z + gPathVelZ;
            f32 t = (closing > 1.0f) ? (p->trueZpos - effect->obj.pos.z) / closing : -1.0f;

            if ((t > 0.0f) && (t < 45.0f)) {
                CoopPilot_AddBox(effect->obj.pos.x + effect->vel.x * t, effect->obj.pos.y + effect->vel.y * t, 40.0f,
                                 40.0f);
            }
        }
    }
}

static bool CoopPilot_IsClear(f32 x, f32 y) {
    s32 i;

    for (i = 0; i < sBoxCount; i++) {
        if ((x > sBoxes[i].x0) && (x < sBoxes[i].x1) && (y > sBoxes[i].y0) && (y < sBoxes[i].y1)) {
            return false;
        }
    }
    return true;
}

// On rails the stick sets rot, and rot sets the speed across the path. Each player keeps to its own side and sways, as
// a shot goes where a ship was. Of the spots across the path that are clear of what is ahead, it takes the one nearest
// to where it is and where it wants to be.
static void CoopPilot_FlyRails(const Player* p, s32 i, PilotPad* pad) {
    f32 t = gGameFrameCount;
    f32 wantX = p->xPath + ((i == 0) ? -250.0f : 250.0f) + 180.0f * sinf(t * 0.045f + i);
    f32 wantY = p->yPath + 500.0f + 90.0f * sinf(t * 0.07f + i);
    f32 targetX = wantX;
    f32 targetY = wantY;
    f32 best = 1e9f;
    f32 x;
    f32 y;

    for (y = 60.0f; y <= 660.0f; y += 40.0f) {
        for (x = -640.0f; x <= 640.0f; x += 80.0f) {
            f32 spotX = p->xPath + x;
            f32 spotY = p->yPath + y;

            if (CoopPilot_IsClear(spotX, spotY)) {
                f32 cost = fabsf(spotX - p->pos.x) + fabsf(spotY - p->pos.y) +
                           (fabsf(spotX - wantX) + fabsf(spotY - wantY)) * 0.3f;

                if (cost < best) {
                    best = cost;
                    targetX = spotX;
                    targetY = spotY;
                }
            }
        }
    }
    pad->x = (targetX - p->pos.x) * 0.25f - p->vel.x * 2.0f;
    pad->y = -((targetY - p->pos.y) * 0.25f - p->vel.y * 2.0f);
    if ((gGameFrameCount % PILOT_FIRE_PERIOD) == 0) {
        pad->button |= A_BUTTON;
    }
}

static f32 CoopPilot_Wrap(f32 degrees) {
    while (degrees > 180.0f) {
        degrees -= 360.0f;
    }
    while (degrees <= -180.0f) {
        degrees += 360.0f;
    }
    return degrees;
}

// Steers at a point in all-range and returns how far off the nose is, in degrees. rot follows 0.68 of the stick, and
// the ship flies along yRot_114 + rot.y + 180 (Player_MoveArwing360). A negative stick_y climbs.
static f32 CoopPilot_SteerAt(const Player* p, f32 x, f32 y, f32 z, PilotPad* pad) {
    f32 dx = x - p->pos.x;
    f32 dy = y - p->pos.y;
    f32 dz = z - p->pos.z;
    f32 heading = p->yRot_114 + p->rot.y + 180.0f;
    f32 yaw = CoopPilot_Wrap(Math_RadToDeg(Math_Atan2F(dx, dz)) - heading);
    f32 pitch = CoopPilot_Wrap(Math_RadToDeg(Math_Atan2F(dy, sqrtf(SQ(dx) + SQ(dz)))));

    if ((p->pos.y < PILOT_MIN_HEIGHT) && (pitch < 12.0f)) {
        pitch = 12.0f;
    }
    pad->x = -(p->rot.y + yaw) / 0.68f;
    pad->y = -(pitch - p->xRot_120) / 0.68f;
    return fabsf(yaw) + fabsf(CoopPilot_Wrap(pitch - (p->xRot_120 + p->rot.x)));
}

// One run at a part of the boss from one side: side 1 is where it faces, -1 is behind it. A run starts far out, fires
// on the way in with the shot's flight time in the aim, and turns away before the boss is in the way.
static void CoopPilot_AttackRun(const Player* p, const Boss* boss, s32 part, f32 side, PilotPad* pad) {
    f32 forwardX = SIN_DEG(boss->obj.rot.y) * side;
    f32 forwardZ = COS_DEG(boss->obj.rot.y) * side;
    f32 x = sCoGrangaWork[part];
    f32 y = sCoGrangaWork[part + 1];
    f32 z = sCoGrangaWork[part + 2];
    f32 dx = p->pos.x - x;
    f32 dz = p->pos.z - z;
    f32 dist = sqrtf(SQ(dx) + SQ(dz));
    f32 flight = dist / PILOT_SHOT_SPEED;

    if (((dx * forwardX + dz * forwardZ) < (dist * 0.75f)) || (dist < PILOT_BREAK_OFF)) {
        CoopPilot_SteerAt(p, x + forwardX * PILOT_RUN_START, PILOT_RUN_HEIGHT, z + forwardZ * PILOT_RUN_START, pad);
    } else if ((CoopPilot_SteerAt(p, x + boss->vel.x * flight, y, z + boss->vel.z * flight, pad) < 6.0f) &&
               (dist < PILOT_SHOT_RANGE) && ((gGameFrameCount % PILOT_FIRE_PERIOD) == 0)) {
        pad->button |= A_BUTTON;
    }
}

static bool CoopPilot_IsAlone(s32 i) {
    s32 j;

    for (j = 0; j < CoopPlayers_Count(); j++) {
        if ((j != i) && !CoopPlayers_IsParked(j) && (gPlayer[j].state == PLAYERSTATE_ACTIVE)) {
            return false;
        }
    }
    return true;
}

// Only the backpack ends the fight, and Granga turns to player 0 whenever it walks (Corneria_CoGranga_Update). So with
// a second player in the air, player 0 stays in front of it and takes off what fires at it, arms first as they launch
// the missiles. Then it shoots a leg: a hit stops the boss, and a leg shot off leaves it lying still. The other player
// comes from behind.
static void CoopPilot_FlyAllRange(const Player* p, s32 i, PilotPad* pad) {
    const Boss* boss = &gBosses[0];

    if ((boss->obj.status != OBJ_ACTIVE) || (boss->obj.id != OBJ_BOSS_CO_GRANGA)) {
        CoopPilot_SteerAt(p, 0.0f, 500.0f, 0.0f, pad);
    } else if ((i != 0) || CoopPilot_IsAlone(i)) {
        CoopPilot_AttackRun(p, boss, GRANGA_WORK_62, -1.0f, pad);
    } else if (boss->swork[GRANGA_RIGHT_ARM_DMG_IND] != DMG_DESTROYED) {
        CoopPilot_AttackRun(p, boss, GRANGA_WORK_00, 1.0f, pad);
    } else if (boss->swork[GRANGA_LEFT_ARM_DMG_IND] != DMG_DESTROYED) {
        CoopPilot_AttackRun(p, boss, GRANGA_WORK_06, 1.0f, pad);
    } else if (boss->swork[GRANGA_GUN_DMG_IND] != DMG_DESTROYED) {
        CoopPilot_AttackRun(p, boss, GRANGA_WORK_12, 1.0f, pad);
    } else {
        CoopPilot_AttackRun(p, boss, GRANGA_WORK_56, 1.0f, pad);
    }
}

static s8 CoopPilot_Stick(f32 value) {
    return (s8) fmaxf(-PILOT_STICK_MAX, fminf(PILOT_STICK_MAX, value));
}

// One line per change of a player's shields, and one every 60 frames of the fight.
static void CoopPilot_Note(const Player* p, s32 i) {
    const Boss* boss = &gBosses[0];

    if (sLog == NULL) {
        return;
    }
    if (p->shields != sShields[i]) {
        fprintf(sLog, "frame=%d player=%d shields=%d was=%d mode=%d pos=%.0f,%.0f,%.0f path=%.0f\n", gGameFrameCount, i,
                p->shields, sShields[i], gLevelMode, p->pos.x, p->pos.y, p->trueZpos, gPathProgress);
        sShields[i] = p->shields;
    }
    if ((gLevelMode == LEVELMODE_ALL_RANGE) && (i == 0) && ((gGameFrameCount % 60) == 0) &&
        (boss->obj.status == OBJ_ACTIVE) && (boss->obj.id == OBJ_BOSS_CO_GRANGA)) {
        fprintf(sLog, "frame=%d boss=%d pos=%.0f,%.0f,%.0f hp=%d arms=%d,%d gun=%d legs=%d,%d\n", gGameFrameCount,
                boss->state, boss->obj.pos.x, boss->obj.pos.y, boss->obj.pos.z, boss->swork[GRANGA_BACKPACK_HP],
                boss->swork[GRANGA_RIGHT_ARM_HP], boss->swork[GRANGA_LEFT_ARM_HP], boss->swork[GRANGA_PLASMA_GUN_HP],
                boss->swork[GRANGA_RIGHT_LEG_HP], boss->swork[GRANGA_LEFT_LEG_HP]);
    }
}

void CoopPilot_Fly(OSContPad* hold, OSContPad* press) {
    s32 i;

    if (!sOn || (gGameState != GSTATE_PLAY) || (gPlayState != PLAY_UPDATE) || (gPlayer == NULL)) {
        return;
    }
    for (i = 0; i < CoopPlayers_Count(); i++) {
        const Player* p = &gPlayer[i];
        PilotPad pad = { 0, 0.0f, 0.0f };

        if (CoopPlayers_IsParked(i)) {
            continue;
        }
        CoopPilot_Note(p, i);
        if ((p->state != PLAYERSTATE_ACTIVE) || (p->form != FORM_ARWING)) {
            continue;
        }
        if (gLevelMode == LEVELMODE_ON_RAILS) {
            CoopPilot_ScanRails(p);
            CoopPilot_FlyRails(p, i, &pad);
        } else {
            CoopPilot_FlyAllRange(p, i, &pad);
        }
        memset(&hold[i], 0, sizeof(hold[i]));
        memset(&press[i], 0, sizeof(press[i]));
        hold[i].button = pad.button;
        press[i].button = pad.button & ~sLastButton[i];
        hold[i].stick_x = press[i].stick_x = CoopPilot_Stick(pad.x);
        hold[i].stick_y = press[i].stick_y = CoopPilot_Stick(pad.y);
        sLastButton[i] = pad.button;
    }
}

int CoopPilot_IsOn(void) {
    return sOn;
}

void CoopPilot_Init(void) {
    const char* log = getenv("SF64_COOP_PILOT_LOG");

    sOn = 1;
    if ((log != NULL) && (log[0] != '\0')) {
        sLog = fopen(log, "w");
    }
}
