#include "pilot.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "fox_co.h"
#include "i5.h"
#include "assets/ast_titania.h"
#include "port/coop/game/players.h"
#include "port/coop/game/vehicles.h"

extern f32 sCoGrangaWork[68]; // src/overlays/ovl_i1/fox_co.c

#define PILOT_STICK_MAX 80
#define PILOT_FIRE_PERIOD 4     // A down for one frame in four: each press is a shot, and it never charges
#define PILOT_SHOT_SPEED 133.0f // Player_ArwingLaser
#define PILOT_SHOT_RANGE 3500.0f
#define PILOT_BREAK_OFF 1300.0f // a run at the boss turns away here: at this speed it takes that much room to miss it
#define PILOT_RUN_START 3600.0f
#define PILOT_RUN_MIN 3000.0f // a run starts no nearer, so that after the turn back in there is room to fire (#238)
#define PILOT_RUN_FRAMES 300  // and gives up after this long: the boss can walk as fast as an Arwing flies
#define PILOT_RUN_HEIGHT 750.0f
#define PILOT_AVOID_FRAMES 20     // looks this far ahead for the boss in the way (#238)
#define PILOT_AVOID_MARGIN 120.0f // the ship's own size and some
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
static s32 sDropping[COOP_PLAYERS_MAX];
static s32 sFoundClear[COOP_PLAYERS_MAX];
static f32 sTargetX[COOP_PLAYERS_MAX];
static s32 sRun[COOP_PLAYERS_MAX];     // frames into a run at the boss, 0 while the player gets into place (#238)
static u16 sBank[COOP_PLAYERS_MAX];    // the trigger held to bank into a turn in all-range, 0 for none (#238)
static s32 sBankOff[COOP_PLAYERS_MAX]; // frames since a bank was let go
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

// The tank's own test, without its matrix: whether a point is in one of an object's hitboxes, each grown by `margin`
// (func_tank_80046E40, Play_CheckSingleHitbox). The rotation goes into the pilot's own matrix, so no game state is
// written. Boxes behind HITBOX_SHADOW or HITBOX_WHOOSH only make a sound and do not count.
static Matrix sPilotMtx;

static bool CoopPilot_InHitboxes(const Object* obj, const f32* data, f32 x, f32 y, f32 z, f32 margin) {
    s32 count = (s32) *data++;
    bool turned = (obj->rot.x != 0.0f) || (obj->rot.y != 0.0f) || (obj->rot.z != 0.0f);
    s32 i;

    for (i = 0; i < count; i++, data += 6) {
        bool rotated = false;
        bool solid = true;
        const Hitbox* box;
        Vec3f from;
        Vec3f to;

        if (data[0] == HITBOX_ROTATED) {
            Matrix_RotateZ(&sPilotMtx, -data[3] * M_DTOR, MTXF_NEW);
            Matrix_RotateX(&sPilotMtx, -data[1] * M_DTOR, MTXF_APPLY);
            Matrix_RotateY(&sPilotMtx, -data[2] * M_DTOR, MTXF_APPLY);
            Matrix_RotateZ(&sPilotMtx, -obj->rot.z * M_DTOR, MTXF_APPLY);
            Matrix_RotateX(&sPilotMtx, -obj->rot.x * M_DTOR, MTXF_APPLY);
            Matrix_RotateY(&sPilotMtx, -obj->rot.y * M_DTOR, MTXF_APPLY);
            data += 4;
            rotated = true;
        } else {
            if (data[0] >= HITBOX_SHADOW) {
                solid = false;
                data++;
            }
            if (turned) {
                Matrix_RotateZ(&sPilotMtx, -obj->rot.z * M_DTOR, MTXF_NEW);
                Matrix_RotateX(&sPilotMtx, -obj->rot.x * M_DTOR, MTXF_APPLY);
                Matrix_RotateY(&sPilotMtx, -obj->rot.y * M_DTOR, MTXF_APPLY);
            }
        }
        if (!solid) {
            continue;
        }
        box = (const Hitbox*) data;
        from.x = x - obj->pos.x;
        from.y = y - obj->pos.y;
        from.z = z - obj->pos.z;
        if (rotated || turned) {
            Matrix_MultVec3fNoTranslate(&sPilotMtx, &from, &to);
        } else {
            to = from;
        }
        if ((fabsf(box->z.offset - to.z) < (box->z.size + margin)) &&
            (fabsf(box->x.offset - to.x) < (box->x.size + margin)) &&
            (fabsf(box->y.offset - to.y) < (box->y.size + margin))) {
            return true;
        }
    }
    return false;
}

#define PILOT_TANK_STEPS 10   // looks this many steps ahead
#define PILOT_TANK_STEP 6     // frames per step: 60 frames, about 900 units at the tank's speed
#define PILOT_TANK_SIDE 9.0f  // how far across the path the tank gets in a frame, at most (vel.x)
#define PILOT_TANK_MARGIN 40.0f

static bool CoopPilot_IsNear(const Object* obj, f32 z) {
    return (obj->status == OBJ_ACTIVE) && (fabsf(obj->pos.z - z) < 2500.0f);
}

// A Titania pillar falls when player 0 comes near (Titania_TiPillar_Update): forward (unk_44 0, rot.x to 90), or to a
// side (rot.z to 90 or -90, with its own hitbox and x). Until it lies, the pilot also tests where it will lie.
static bool CoopPilot_InFallenPillar(const Scenery* s, f32 x, f32 y, f32 z) {
    Object lying = s->obj;
    const f32* hitbox;
    s32 side = s->unk_44;

    if ((side == 1) && (s->state == 0) && (s->obj.pos.x < gPlayer[0].pos.x)) {
        side = 2;
    }
    switch (side) {
        case 0:
            lying.rot.x = 90.0f;
            hitbox = s->info.hitbox;
            break;
        case 1:
            lying.rot.z = 90.0f;
            lying.pos.x = s->vel.z - 75.0f;
            hitbox = SEGMENTED_TO_VIRTUAL(D_TI_6006C28);
            break;
        default:
            lying.rot.z = -90.0f;
            lying.pos.x = s->vel.z + 75.0f;
            hitbox = SEGMENTED_TO_VIRTUAL(D_TI_6006C0C);
            break;
    }
    return CoopPilot_InHitboxes(&lying, hitbox, x, y, z, PILOT_TANK_MARGIN);
}

// Whether a vehicle's body (its four collision points, Player_UpdateHitbox) would touch anything at x, y, z.
// `half` is how far the four points sit from the middle, `lift` how high: 40 and 40 for the tank, the marine's are
// smaller and at its middle (Player_UpdateHitbox).
static bool CoopPilot_BodyHits(f32 x, f32 y, f32 z, f32 half, f32 lift) {
    static const f32 kPoints[4][2] = { { -1.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f } };
    s32 i;
    s32 k;

    for (k = 0; k < 4; k++) {
        f32 px = x + kPoints[k][0] * half;
        f32 py = y + lift;
        f32 pz = z + kPoints[k][1] * half;

        for (i = 0; i < ARRAY_COUNT(gScenery); i++) {
            const Scenery* s = &gScenery[i];

            if (!CoopPilot_IsNear(&s->obj, z) || (s->info.hitbox == NULL)) {
                continue;
            }
            if (CoopPilot_InHitboxes(&s->obj, s->info.hitbox, px, py, pz, PILOT_TANK_MARGIN)) {
                return true;
            }
            if ((s->obj.id == OBJ_SCENERY_TI_PILLAR) && (s->state < 3) && (gCurrentLevel == LEVEL_TITANIA) &&
                CoopPilot_InFallenPillar(s, px, py, pz)) {
                return true;
            }
        }
        for (i = 0; i < ARRAY_COUNT(gActors); i++) {
            const Actor* a = &gActors[i];

            // A Macbeth bomb burns where it lands, for 200 frames, in a box of its own: 40 in z, 80 in x, scale * 35
            // high (Macbeth_MaBombDrop_Update). One still falling is tested where it will land.
            if ((a->obj.status == OBJ_ACTIVE) && (a->obj.id == OBJ_ACTOR_MA_BOMBDROP) &&
                (fabsf(pz - a->obj.pos.z) < (40.0f + PILOT_TANK_MARGIN)) &&
                (fabsf(px - a->obj.pos.x) < (80.0f + PILOT_TANK_MARGIN)) &&
                ((py - 40.0f) < (gGroundHeight + a->scale * 35.0f + PILOT_TANK_MARGIN))) {
                return true;
            }
            if (CoopPilot_IsNear(&a->obj, z) && (a->timer_0C2 == 0) && (a->obj.id != OBJ_ACTOR_TEAM_BOSS) &&
                (a->info.hitbox != NULL) &&
                CoopPilot_InHitboxes(&a->obj, a->info.hitbox, px, py, pz, PILOT_TANK_MARGIN)) {
                return true;
            }
        }
        for (i = 0; i < ARRAY_COUNT(gBosses); i++) {
            const Boss* b = &gBosses[i];

            if (CoopPilot_IsNear(&b->obj, z) && (b->info.hitbox != NULL) &&
                CoopPilot_InHitboxes(&b->obj, b->info.hitbox, px, py, pz, PILOT_TANK_MARGIN)) {
                return true;
            }
        }
    }
    return false;
}

// How high a hover has lifted the tank after `frames`. Z and R held together lift it while the boost meter fills, by 2
// a frame up to 90, then it falls (func_tank_80045678, Tank_UpdateOnRails): gravity -0.4 up to a height of 300.
static f32 CoopPilot_HoverHeight(const Player* p, f32 frames) {
    f32 left = (90.0f - p->boostMeter) / 2.0f;
    f32 up = fminf(frames, left);
    f32 height = fminf(0.2f * SQ(up), fmaxf(0.0f, 300.0f - p->pathHeight));

    if (frames > left) {
        height -= 1.5f * SQ(frames - left);
    }
    return fmaxf(height, 0.0f);
}

// How many steps the tank goes on its way to spotX before it touches something, PILOT_TANK_STEPS + 1 for none.
static bool CoopPilot_TankHits(f32 x, f32 y, f32 z) {
    return CoopPilot_BodyHits(x, y, z, 40.0f, 40.0f);
}

#define PILOT_SHOT_MIN_SPEED 20.0f // slower effects are dust and debris
#define PILOT_SHOT_REACH 90.0f     // how near a shot may pass the tank's middle

// The first step at which a fast effect (a shot) meets the tank on its way to spotX: where the shot's z crosses the
// tank's, both moving on as they move now.
static s32 CoopPilot_ShotStep(const Player* p, f32 spotX, bool hover, f32 speedZ) {
    s32 first = PILOT_TANK_STEPS + 1;
    s32 i;

    for (i = 0; i < ARRAY_COUNT(gEffects); i++) {
        const Effect* e = &gEffects[i];
        f32 closing = e->vel.z - speedZ;
        f32 t;
        f32 reach;
        f32 atX;
        f32 atY;
        s32 step;

        if ((e->obj.status != OBJ_ACTIVE) ||
            ((fabsf(e->vel.x) + fabsf(e->vel.y) + fabsf(e->vel.z)) < PILOT_SHOT_MIN_SPEED) || (fabsf(closing) < 1.0f)) {
            continue;
        }
        t = (p->trueZpos - e->obj.pos.z) / closing;
        if ((t < 0.0f) || (t > PILOT_TANK_STEPS * PILOT_TANK_STEP)) {
            continue;
        }
        reach = t * PILOT_TANK_SIDE;
        atX = p->pos.x + fmaxf(-reach, fminf(reach, spotX - p->pos.x));
        atY = p->pos.y + 40.0f + (hover ? CoopPilot_HoverHeight(p, t) : 0.0f);
        if ((fabsf(e->obj.pos.x + e->vel.x * t - atX) < PILOT_SHOT_REACH) &&
            (fabsf(e->obj.pos.y + e->vel.y * t - atY) < PILOT_SHOT_REACH)) {
            step = (s32) (t / PILOT_TANK_STEP) + 1;
            if (step < first) {
                first = step;
            }
        }
    }
    return first;
}

static s32 CoopPilot_ClearSteps(const Player* p, f32 spotX, bool hover) {
    f32 speedZ = fminf(p->vel.z, -8.0f);
    s32 shot = CoopPilot_ShotStep(p, spotX, hover, speedZ);
    s32 step;

    for (step = 1; step < shot; step++) {
        f32 frames = step * PILOT_TANK_STEP;
        f32 reach = frames * PILOT_TANK_SIDE;
        f32 atX = p->pos.x + fmaxf(-reach, fminf(reach, spotX - p->pos.x));
        f32 atY = p->pos.y + (hover ? CoopPilot_HoverHeight(p, frames) : 0.0f);

        if (CoopPilot_TankHits(atX, atY, p->trueZpos + speedZ * frames)) {
            break;
        }
    }
    return step;
}

static s32 sAimPart = -1;
static Vec3f sAimAt;
extern s16 D_i5_801BE320[35]; // fox_ma.c

// Macbeth's boss is actor 207 (Macbeth_Actor207_Update, it talks as RCID_BOSS_MACBETH). In state 8 any hit takes
// its health, D_i5_801BE320[29]; if the train reaches the end of the track first (D_i5_801BE320[18] == 4) it goes to
// state 18 and then kills player 0 outright. Aim at its middle whenever it is ahead.
// Where box n of an object's hitbox list sits in the world: its offset turned by the object's rotation, as it is drawn
// (Y, then X, then Z). Markers are skipped. Returns false past the list's end.
static bool CoopPilot_BoxCenter(const Object* obj, const f32* data, s32 n, Vec3f* at) {
    s32 count = (s32) *data++;
    s32 i;

    for (i = 0; i < count; i++, data += 6) {
        if (data[0] == HITBOX_ROTATED) {
            data += 4;
        } else if (data[0] >= HITBOX_SHADOW) {
            data++;
        }
        if (i == n) {
            const Hitbox* box = (const Hitbox*) data;
            Vec3f local = { box->x.offset, box->y.offset, box->z.offset };
            Vec3f turned;

            Matrix_RotateY(&sPilotMtx, obj->rot.y * M_DTOR, MTXF_NEW);
            Matrix_RotateX(&sPilotMtx, obj->rot.x * M_DTOR, MTXF_APPLY);
            Matrix_RotateZ(&sPilotMtx, obj->rot.z * M_DTOR, MTXF_APPLY);
            Matrix_MultVec3fNoTranslate(&sPilotMtx, &local, &turned);
            at->x = obj->pos.x + turned.x;
            at->y = obj->pos.y + turned.y;
            at->z = obj->pos.z + turned.z;
            return true;
        }
    }
    return false;
}

static bool CoopPilot_MacbethBossTarget(const Player* p, Vec3f* at) {
    s32 k;

    if (gCurrentLevel != LEVEL_MACBETH) {
        return false;
    }
    for (k = 0; k < ARRAY_COUNT(gActors); k++) {
        const Actor* a = &gActors[k];
        s32 part;

        if ((a->obj.status != OBJ_ACTIVE) || (a->obj.id != OBJ_ACTOR_207) || (a->state < 2) ||
            ((p->trueZpos - a->obj.pos.z) < 200.0f) || ((p->trueZpos - a->obj.pos.z) > 4000.0f)) {
            continue;
        }
        // Part 0 takes D_i5_801BE320[9], part 1 [10]. A hit on part 1 of the locomotive sets [4]
        // (Macbeth_MaLocomotive_HandleDamage), which opens the boss (state 8); then any hit on it takes [29].
        part = (D_i5_801BE320[9] > 0) ? 0 : ((D_i5_801BE320[10] > 0) ? 1 : -1);
        if ((part < 0) && (a->state != 8)) {
            s32 m;

            for (m = 0; m < ARRAY_COUNT(gActors); m++) {
                const Actor* engine = &gActors[m];

                if ((engine->obj.status == OBJ_ACTIVE) && (engine->obj.id == OBJ_ACTOR_MA_LOCOMOTIVE) &&
                    (engine->obj.pos.z < p->trueZpos) && (engine->info.hitbox != NULL) &&
                    CoopPilot_BoxCenter(&engine->obj, engine->info.hitbox, 1, at)) {
                    sAimPart = 2001;
                    sAimAt = *at;
                    return true;
                }
            }
        }
        if ((part < 0) || (a->info.hitbox == NULL) || !CoopPilot_BoxCenter(&a->obj, a->info.hitbox, part, at)) {
            *at = a->obj.pos;
        }
        sAimPart = 1000 + part;
        sAimAt = *at;
        return true;
    }
    return false;
}

// Goras, Titania's boss (Titania_TiGoras_Update): its hitbox is rebuilt each frame (aTiGorasHitbox, plain boxes, no
// markers), and box n is part D_i5_801B7904[n]. From state 7 to 13 a beam hit on a part with a number in
// D_i5_801BAA78 (1 to 4) takes that part's health, swork[8 + number]; once all four are gone and the health bar shows,
// part 23 takes swork[21]. Finds the box of a part that still counts, nearest to the tank across the path.
extern u8 D_i5_801B7904[92];
extern const s16 D_i5_801BAA78[34];

static bool CoopPilot_GorasTarget(const Player* p, Vec3f* at) {
    const Boss* boss = &gBosses[0];
    const f32* data = aTiGorasHitbox;
    s32 count;
    f32 best = 1e9f;
    s32 n;

    if ((gCurrentLevel != LEVEL_TITANIA) || (boss->obj.status != OBJ_ACTIVE) || (boss->obj.id != OBJ_BOSS_TI_GORAS) ||
        (boss->state < 7) || (boss->state >= 14)) {
        return false;
    }
    count = MIN((s32) *data++, ARRAY_COUNT(D_i5_801B7904));
    for (n = 0; n < count; n++, data += 6) {
        s32 part = D_i5_801B7904[n];
        s32 number = (part < ARRAY_COUNT(D_i5_801BAA78)) ? D_i5_801BAA78[part] : 0;
        bool counts = ((number >= 1) && (number <= 4) && (boss->swork[8 + number] > 0)) ||
                      ((part == 23) && (boss->swork[29] != 0) && (gBossHealthBar > 0) && (boss->swork[21] > 0));
        const Hitbox* box = (const Hitbox*) data;
        f32 x = boss->obj.pos.x + box->x.offset;

        if (counts && (fabsf(x - p->pos.x) < best)) {
            best = fabsf(x - p->pos.x);
            at->x = x;
            at->y = boss->obj.pos.y + box->y.offset;
            at->z = boss->obj.pos.z + box->z.offset;
            sAimPart = part;
            sAimAt = *at;
        }
    }
    return best < 1e9f;
}

// A Landmaster on rails (#16) only moves across the path: the stick sets rot.y, and the tank drives along it at its own
// speed (func_tank_80044868), so stick x right is x up. For each spot across the path it plays the next second forward:
// the tank turns toward the spot as fast as it can and drives on at its speed, and every step is tested against the
// hitboxes as the game tests them. The spot whose first touch comes last wins, nearer spots and the tank's own side
// first. When a hover gets further than any spot on the ground, it hovers. It fires all the time; at a boss part it
// lines up under the part and raises the barrel to it.
static void CoopPilot_Drive(const Player* p, s32 i, PilotPad* pad) {
    f32 t = gGameFrameCount;
    f32 edge = p->pathWidth - 120.0f;
    f32 wantX = p->xPath + ((i == 0) ? -0.3f : 0.3f) * edge + 0.25f * edge * sinf(t * 0.03f + i);
    f32 wantWeight = 0.3f;
    bool canHover = !p->boostCooldown;
    bool aiming = false;
    Vec3f aim;
    f32 targetX;
    f32 best = -1e9f;
    bool hover = false;
    s32 h;
    f32 x;

    if (CoopPilot_GorasTarget(p, &aim) || CoopPilot_MacbethBossTarget(p, &aim)) {
        aiming = true;
        wantX = aim.x;
        wantWeight = 1.5f;
    } else if ((gBosses[0].obj.status == OBJ_ACTIVE) && (gBosses[0].obj.pos.z < p->trueZpos)) {
        wantX = gBosses[0].obj.pos.x + ((i == 0) ? -60.0f : 60.0f);
    } else {
        // A ring ahead fills the shields: go for the nearest one.
        f32 nearest = 1500.0f;
        s32 k;

        for (k = 0; k < ARRAY_COUNT(gItems); k++) {
            const Item* item = &gItems[k];
            f32 ahead = p->trueZpos - item->obj.pos.z;

            if ((item->obj.status == OBJ_ACTIVE) &&
                ((item->obj.id == OBJ_ITEM_SILVER_RING) || (item->obj.id == OBJ_ITEM_GOLD_RING) ||
                 (item->obj.id == OBJ_ITEM_SILVER_STAR) || (item->obj.id == OBJ_ITEM_1UP)) &&
                (ahead > 100.0f) && (ahead < nearest) && (item->obj.pos.y < (p->pos.y + 250.0f))) {
                nearest = ahead;
                wantX = item->obj.pos.x;
                wantWeight = 1.0f;
            }
        }
    }
    targetX = wantX;
    for (h = 0; h < (canHover ? 2 : 1); h++) {
        for (x = -edge; x <= edge; x += 40.0f) {
            f32 spotX = p->xPath + x;
            s32 steps = CoopPilot_ClearSteps(p, spotX, h == 1);
            // A hover has to win by a step: it spends the boost meter.
            f32 score = (steps - h) * 1000.0f - fabsf(spotX - p->pos.x) - fabsf(spotX - wantX) * wantWeight;

            if (score > best) {
                best = score;
                targetX = spotX;
                hover = h == 1;
            }
        }
    }
    sFoundClear[i] = (best > PILOT_TANK_STEPS * 1000.0f) ? (hover ? 2 : 1) : 0;
    sTargetX[i] = targetX;
    pad->x = (targetX - p->pos.x) * 0.4f - p->vel.x * 3.0f;
    if (hover) {
        pad->button |= Z_TRIG | R_TRIG;
    } else if ((gBosses[0].obj.status == OBJ_ACTIVE) && ((p->trueZpos - gBosses[0].obj.pos.z) > 0.0f) &&
               ((p->trueZpos - gBosses[0].obj.pos.z) < 750.0f)) {
        // A boss close ahead that stops at times: brake (func_tank_80045348, speed 5).
        pad->button |= gBrakeButton[i];
    }
    if (aiming) {
        // The barrel: stick y sets unk_17C = stick_y * 0.7 - 8, from -40 to 0 (func_tank_80044868), and the shot
        // leaves along it (Player_SetupTankShot). A negative unk_17C is taken as up.
        f32 rise = aim.y - (p->pos.y + 51.0f);
        f32 ahead = fmaxf(p->trueZpos - aim.z, 100.0f);
        f32 tilt = -Math_RadToDeg(Math_Atan2F(rise, ahead));

        tilt = fmaxf(-40.0f, fminf(0.0f, tilt));
        pad->y = (tilt + 8.0f) / 0.7f;
    }
    if ((gGameFrameCount % PILOT_FIRE_PERIOD) == 0) {
        pad->button |= A_BUTTON;
    }
}

#define PILOT_MARINE_SIDE 8.0f // how far across or up the marine gets in a frame, taken low (Aquas_BlueMarineMove)
#define PILOT_MARINE_UP -1.0f   // the sign of stick y that makes the marine climb: with +1 it stuck at the top (#16)

// The first step at which the marine, on its way to (spotX, spotY), meets a hitbox or a fast effect. As
// CoopPilot_ClearSteps, with the marine's own body and room up and down.
static s32 CoopPilot_SwimSteps(const Player* p, f32 spotX, f32 spotY) {
    f32 speedZ = fminf(p->vel.z, -8.0f);
    s32 first = PILOT_TANK_STEPS + 1;
    s32 step;
    s32 i;

    for (i = 0; i < ARRAY_COUNT(gEffects); i++) {
        const Effect* e = &gEffects[i];
        f32 closing = e->vel.z - speedZ;
        f32 t;
        f32 reach;

        if ((e->obj.status != OBJ_ACTIVE) ||
            ((fabsf(e->vel.x) + fabsf(e->vel.y) + fabsf(e->vel.z)) < PILOT_SHOT_MIN_SPEED) || (fabsf(closing) < 1.0f)) {
            continue;
        }
        t = (p->trueZpos - e->obj.pos.z) / closing;
        if ((t < 0.0f) || (t > PILOT_TANK_STEPS * PILOT_TANK_STEP)) {
            continue;
        }
        reach = t * PILOT_MARINE_SIDE;
        if ((fabsf(e->obj.pos.x + e->vel.x * t - (p->pos.x + fmaxf(-reach, fminf(reach, spotX - p->pos.x)))) <
             PILOT_SHOT_REACH) &&
            (fabsf(e->obj.pos.y + e->vel.y * t - (p->pos.y + fmaxf(-reach, fminf(reach, spotY - p->pos.y)))) <
             PILOT_SHOT_REACH)) {
            first = MIN(first, (s32) (t / PILOT_TANK_STEP) + 1);
        }
    }
    for (step = 1; step < first; step++) {
        f32 frames = step * PILOT_TANK_STEP;
        f32 reach = frames * PILOT_MARINE_SIDE;
        f32 atX = p->pos.x + fmaxf(-reach, fminf(reach, spotX - p->pos.x));
        f32 atY = p->pos.y + fmaxf(-reach, fminf(reach, spotY - p->pos.y));

        if (CoopPilot_BodyHits(atX, atY, p->trueZpos + speedZ * frames, 30.0f, 0.0f)) {
            break;
        }
    }
    return step;
}

extern s32 D_i3_801C42A0[25]; // fox_aq.c
extern f32 D_i3_801C4308[82];

#define PILOT_BACOON_FILM 119 // Bacoon's hitbox entries that a torpedo can lock on to (Aquas_801A9ED0): z at [j], y at
#define PILOT_BACOON_CORE 169 // [j + 2], x at [j + 4]. 119 is the eye film (part 13), 169 the core (part 18).

// Aquas's boss, Bacoon (Aquas_AqBacoon_Update; NOTES.md, #16). Three steps, read from its state:
// 1. While D_i3_801C42A0[23] counts actors 256 left, lasers at the nearest one push the shell open; once it is wide
//    enough the boss opens a window (swork[0] == 1, the actor's health 10), and one torpedo (damage 31) ends it.
// 2. From state 15, lasers at the eye film, ten hits (swork[12]).
// 3. Then torpedoes at the core, 31 off its health of 150 each, unless swork[1] == 2 (just hit).
// Sets where to line up, and whether a torpedo should go now.
static bool CoopPilot_BacoonTarget(Vec3f* at, bool* torpedo) {
    const Boss* boss = &gBosses[0];
    s32 j;

    *torpedo = false;
    if ((gCurrentLevel != LEVEL_AQUAS) || (boss->obj.status != OBJ_ACTIVE) || (boss->obj.id != OBJ_BOSS_AQ_BACOON) ||
        (boss->state < 10) || (boss->health == 0) || (boss->info.hitbox == NULL)) {
        return false;
    }
    if (D_i3_801C42A0[23] > 0) {
        f32 best = 1e9f;
        s32 k;

        for (k = 0; k < ARRAY_COUNT(gActors); k++) {
            const Actor* a = &gActors[k];
            s32 n;

            if ((a->obj.status != OBJ_ACTIVE) || (a->obj.id != OBJ_ACTOR_256) || (a->state != 0) ||
                (a->info.hitbox == NULL)) {
                continue;
            }
            // The box the lock-on tests (Aquas_BlueMarineShoot): offsets after the count, or after a rotation.
            n = (a->info.hitbox[1] == HITBOX_ROTATED) ? 5 : 1;
            // The one with a window open first, then the one with the lowest index: both pilots on the same one.
            if (((a->health > 0) ? 0.0f : 1000.0f) + k < best) {
                best = ((a->health > 0) ? 0.0f : 1000.0f) + k;
                at->x = a->obj.pos.x + a->info.hitbox[n + 4];
                at->y = a->obj.pos.y + a->info.hitbox[n + 2];
                at->z = a->obj.pos.z + a->info.hitbox[n + 0];
                *torpedo = (boss->swork[0] == 1) && (a->health > 0);
                sAimPart = 2560 + k;
            }
        }
        if (best < 1e9f) {
            sAimAt = *at;
            return true;
        }
    }
    if (boss->state != 15) {
        return false;
    }
    j = (boss->swork[12] > 0) ? PILOT_BACOON_FILM : PILOT_BACOON_CORE;
    at->x = boss->obj.pos.x + boss->info.hitbox[j + 4];
    at->y = boss->obj.pos.y + boss->info.hitbox[j + 2];
    at->z = boss->obj.pos.z + boss->info.hitbox[j + 0];
    *torpedo = (j == PILOT_BACOON_CORE) && (boss->swork[1] != 2);
    sAimPart = j;
    sAimAt = *at;
    return true;
}

// A Blue Marine on rails (#16): stick x turns it across the path as the tank's does, stick y pitches it
// (Aquas_BlueMarineMove), and it keeps between the floor and the top of the path. Spots on a grid across and up, each
// played a second forward as for the tank. It fires the laser all the time, and player 1 a torpedo now and then.
static void CoopPilot_Swim(const Player* p, s32 i, PilotPad* pad) {
    f32 t = gGameFrameCount;
    f32 edge = p->pathWidth - 120.0f;
    f32 floor = fmaxf(p->pathFloor, gGroundHeight) + 90.0f;
    f32 top = p->pathHeight - 90.0f;
    f32 wantX = p->xPath + ((i == 0) ? -0.3f : 0.3f) * edge + 0.25f * edge * sinf(t * 0.03f + i);
    f32 wantY = (floor + top) * 0.5f + 0.25f * (top - floor) * sinf(t * 0.02f + i);
    f32 wantWeight = 0.3f;
    f32 targetX;
    f32 targetY;
    f32 best = -1e9f;
    bool aiming;
    bool torpedo;
    Vec3f aim;
    f32 x;
    f32 y;

    if (top < floor) {
        top = floor;
    }
    aiming = CoopPilot_BacoonTarget(&aim, &torpedo);
    if (aiming) {
        // Line up in front of it: shots and the lock-on (1200 ahead, Aquas_BlueMarineShoot) go straight on.
        wantX = aim.x;
        wantY = fmaxf(floor, fminf(top, aim.y));
        wantWeight = 1.5f;
    }
    targetX = wantX;
    targetY = wantY;
    for (y = floor; y <= top; y += 60.0f) {
        for (x = -edge; x <= edge; x += 60.0f) {
            f32 spotX = p->xPath + x;
            s32 steps = CoopPilot_SwimSteps(p, spotX, y);
            f32 score = steps * 1000.0f - fabsf(spotX - p->pos.x) - fabsf(y - p->pos.y) -
                        (fabsf(spotX - wantX) + fabsf(y - wantY)) * wantWeight;

            if (score > best) {
                best = score;
                targetX = spotX;
                targetY = y;
            }
        }
    }
    sFoundClear[i] = best > PILOT_TANK_STEPS * 1000.0f;
    sTargetX[i] = targetX;
    pad->x = (targetX - p->pos.x) * 0.4f - p->vel.x * 3.0f;
    pad->y = PILOT_MARINE_UP * ((targetY - p->pos.y) * 0.4f - p->vel.y * 3.0f);
    if ((gGameFrameCount % PILOT_FIRE_PERIOD) == 0) {
        pad->button |= A_BUTTON;
    }
    if (aiming) {
        // At the boss a torpedo goes only when it is the step's weapon and this marine has a lock and none in flight
        // (all marines share one torpedo slot, gPlayerShots[15]).
        s32 target;
        s32 homing;

        CoopVehicles_MarineLock(i, &target, &homing);
        if (torpedo && (target != 0) && (homing == 0) && (gPlayerShots[15].obj.status == SHOT_FREE) &&
            ((gGameFrameCount % 10) == (u32) (i * 5))) {
            pad->button |= B_BUTTON;
        }
    } else if ((i == 1) && ((gGameFrameCount % 150) == 75)) {
        pad->button |= B_BUTTON;
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
    s32 i = p - gPlayer;

    if ((p->pos.y < PILOT_MIN_HEIGHT) && (pitch < 12.0f)) {
        pitch = 12.0f;
    }
    // The heading is yRot_114 + rot.y, and yRot_114 goes on turning by yRotVel_11C (0.03 of rot.y) every frame, while
    // rot.y closes a tenth of its gap a frame. Asking for rot.y + yaw left the nose about 0.3 rot.y behind after a
    // turn, 10 degrees and more, so the pilot seldom fired and a Granga fight could outlast the run (#238). The stick
    // now also takes out ten frames of that turn. In a model of these steps a gain of 3 on yaw settles within about 3
    // degrees also when the stick acts three frames late, as in a lockstep session (COOP_LOCKSTEP_INPUT_DELAY).
    pad->x = -(p->rot.y + 3.0f * yaw - 10.0f * p->yRotVel_11C) / 0.68f;
    pad->y = -(pitch - p->xRot_120) / 0.68f;
    // A wide turn is flown banked: Z held into a left turn or R into a right one doubles the turn rate
    // (Player_MoveArwing360), so the ship turns back in a circle about 1700 wide instead of 2800 (#238). It banks again
    // only 20 frames after letting go, as a second press within 10 frames is a barrel roll (Player_ArwingBank).
    if ((sBank[i] != 0) && ((fabsf(yaw) < 10.0f) || ((sBank[i] == Z_TRIG) != (yaw > 0.0f)))) {
        sBank[i] = 0;
        sBankOff[i] = 0;
    } else if ((sBank[i] == 0) && (fabsf(yaw) > 30.0f) && (sBankOff[i] > 20)) {
        sBank[i] = (yaw > 0.0f) ? Z_TRIG : R_TRIG;
    }
    if (sBank[i] == 0) {
        sBankOff[i]++;
    }
    pad->button |= sBank[i];
    return fabsf(yaw) + fabsf(CoopPilot_Wrap(pitch - (p->xRot_120 + p->rot.x)));
}

// Whether the ship flies into the boss within PILOT_AVOID_FRAMES, both moving on as they move now (#238). Granga walks
// at up to 40 a frame (Corneria_CoGranga_Update), as fast as the ship, and a crash takes 40 shields.
static bool CoopPilot_BossAhead(const Player* p, const Boss* boss) {
    Object ahead = boss->obj;
    s32 t;

    for (t = 2; t <= PILOT_AVOID_FRAMES; t += 2) {
        ahead.pos.x = boss->obj.pos.x + boss->vel.x * t;
        ahead.pos.y = boss->obj.pos.y + boss->vel.y * t;
        ahead.pos.z = boss->obj.pos.z + boss->vel.z * t;
        if (CoopPilot_InHitboxes(&ahead, boss->info.hitbox, p->pos.x + p->vel.x * t, p->pos.y + p->vel.y * t,
                                 p->trueZpos + p->vel.z * t, PILOT_AVOID_MARGIN)) {
            return true;
        }
    }
    return false;
}

// One run at a part of the boss from one side: side 1 is where it faces, -1 is behind it. A run starts far out, fires
// on the way in with the shot's flight time in the aim, and turns away before the boss is in the way. Once started it
// goes on until it turns away or PILOT_RUN_FRAMES have passed (#238): the ship turns back in a circle 2800 wide (rot.y
// at most 54, yRot_114 0.03 of that a frame, at 40 a frame; 1700 banked), so a run that started 1300 out and had to
// stay in the narrow cone behind its part left it before the nose came round, and the pilot circled a boss lying
// still without ever firing.
static void CoopPilot_AttackRun(const Player* p, const Boss* boss, s32 part, f32 side, PilotPad* pad) {
    s32 i = p - gPlayer;
    f32 forwardX = SIN_DEG(boss->obj.rot.y) * side;
    f32 forwardZ = COS_DEG(boss->obj.rot.y) * side;
    f32 x = sCoGrangaWork[part];
    f32 y = sCoGrangaWork[part + 1];
    f32 z = sCoGrangaWork[part + 2];
    f32 dx = p->pos.x - x;
    f32 dz = p->pos.z - z;
    f32 dist = sqrtf(SQ(dx) + SQ(dz));
    f32 flight = dist / PILOT_SHOT_SPEED;
    f32 outX = x - boss->obj.pos.x;
    f32 outZ = z - boss->obj.pos.z;
    f32 out = sqrtf(SQ(outX) + SQ(outZ));

    // The backpack is come at from where it points: behind the boss while it stands, to a side once it has fallen over
    // (rot.x and rot.z go to 60).
    if ((part == GRANGA_WORK_62) && (out > 100.0f)) {
        forwardX = outX / out;
        forwardZ = outZ / out;
    }
    if ((dist < PILOT_BREAK_OFF) || (sRun[i] > PILOT_RUN_FRAMES)) {
        sRun[i] = 0;
    } else if ((sRun[i] > 0) || (((dx * forwardX + dz * forwardZ) >= (dist * 0.75f)) && (dist >= PILOT_RUN_MIN))) {
        sRun[i]++;
    }
    if (CoopPilot_BossAhead(p, boss)) {
        CoopPilot_SteerAt(p, p->pos.x + p->vel.x * 30.0f, p->pos.y + 1200.0f, p->trueZpos + p->vel.z * 30.0f, pad);
    } else if (sRun[i] == 0) {
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

// When a hit starts: what is near the player, so a log shows what the pilot did not see. A hit takes its shields
// down a little each frame for a while, so only the first frame of a drop counts.
static void CoopPilot_NoteNear(const Player* p) {
    s32 i;

    fprintf(sLog, "  hit dmgType=%d direction=%d damage=%d grounded=%d vel=%.0f,%.0f,%.0f clear=%d target=%.0f\n",
            p->dmgType, p->hitDirection, p->damage, p->grounded, p->vel.x, p->vel.y, p->vel.z,
            sFoundClear[p - gPlayer], sTargetX[p - gPlayer]);
    for (i = 0; i < sBoxCount; i++) {
        fprintf(sLog, "  box x=%.0f..%.0f y=%.0f..%.0f\n", sBoxes[i].x0, sBoxes[i].x1, sBoxes[i].y0, sBoxes[i].y1);
    }
    for (i = 0; i < ARRAY_COUNT(gSprites); i++) {
        const Sprite* s = &gSprites[i];

        if ((s->obj.status == OBJ_ACTIVE) && (fabsf(s->obj.pos.z - p->trueZpos) < 600.0f)) {
            fprintf(sLog, "  near sprite id=%d pos=%.0f,%.0f,%.0f\n", s->obj.id, s->obj.pos.x, s->obj.pos.y,
                    s->obj.pos.z);
        }
    }
    for (i = 0; i < ARRAY_COUNT(gActors); i++) {
        const Actor* a = &gActors[i];

        if ((a->obj.status == OBJ_ACTIVE) && (fabsf(a->obj.pos.z - p->trueZpos) < 600.0f)) {
            fprintf(sLog, "  near actor id=%d pos=%.0f,%.0f,%.0f timer=%d\n", a->obj.id, a->obj.pos.x, a->obj.pos.y,
                    a->obj.pos.z, a->timer_0C2);
        }
    }
    for (i = 0; i < ARRAY_COUNT(gScenery); i++) {
        const Scenery* s = &gScenery[i];

        if ((s->obj.status == OBJ_ACTIVE) && (fabsf(s->obj.pos.z - p->trueZpos) < 600.0f)) {
            fprintf(sLog, "  near scenery id=%d pos=%.0f,%.0f,%.0f\n", s->obj.id, s->obj.pos.x, s->obj.pos.y,
                    s->obj.pos.z);
        }
    }
    for (i = 0; i < ARRAY_COUNT(gEffects); i++) {
        const Effect* e = &gEffects[i];

        if ((e->obj.status == OBJ_ACTIVE) && (fabsf(e->obj.pos.z - p->trueZpos) < 600.0f)) {
            fprintf(sLog, "  near effect id=%d pos=%.0f,%.0f,%.0f vel=%.0f,%.0f,%.0f\n", e->obj.id, e->obj.pos.x,
                    e->obj.pos.y, e->obj.pos.z, e->vel.x, e->vel.y, e->vel.z);
        }
    }
    for (i = 0; i < ARRAY_COUNT(gBosses); i++) {
        const Boss* b = &gBosses[i];

        if (b->obj.status == OBJ_ACTIVE) {
            fprintf(sLog, "  boss id=%d pos=%.0f,%.0f,%.0f\n", b->obj.id, b->obj.pos.x, b->obj.pos.y, b->obj.pos.z);
        }
    }
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
        if ((p->shields < sShields[i]) && (sDropping[i] == 0)) {
            CoopPilot_NoteNear(p);
        }
        if ((sShields[i] - p->shields) > 50) {
            // Far more than a hit takes in a frame: something set the shields. Every actor and its state.
            s32 k;

            for (k = 0; k < ARRAY_COUNT(gActors); k++) {
                const Actor* a = &gActors[k];

                if (a->obj.status != OBJ_FREE) {
                    fprintf(sLog, "  drop actor %d id=%d status=%d state=%d dmgType=%d pos=%.0f,%.0f,%.0f\n", k,
                            a->obj.id, a->obj.status, a->state, a->dmgType, a->obj.pos.x, a->obj.pos.y, a->obj.pos.z);
                }
            }
        }
        sDropping[i] = 2;
        sShields[i] = p->shields;
    } else if (sDropping[i] > 0) {
        sDropping[i]--;
    }
    if ((gLevelMode == LEVELMODE_ALL_RANGE) && (i == 0) && ((gGameFrameCount % 60) == 0) &&
        (boss->obj.status == OBJ_ACTIVE) && (boss->obj.id == OBJ_BOSS_CO_GRANGA)) {
        fprintf(sLog, "frame=%d boss=%d pos=%.0f,%.0f,%.0f hp=%d arms=%d,%d gun=%d legs=%d,%d\n", gGameFrameCount,
                boss->state, boss->obj.pos.x, boss->obj.pos.y, boss->obj.pos.z, boss->swork[GRANGA_BACKPACK_HP],
                boss->swork[GRANGA_RIGHT_ARM_HP], boss->swork[GRANGA_LEFT_ARM_HP], boss->swork[GRANGA_PLASMA_GUN_HP],
                boss->swork[GRANGA_RIGHT_LEG_HP], boss->swork[GRANGA_LEFT_LEG_HP]);
    }
    // Macbeth's boss, actor 207: its state, its health D_i5_801BE320[29] and the track's stage D_i5_801BE320[18].
    if ((gCurrentLevel == LEVEL_MACBETH) && (i == 0) && ((gGameFrameCount % 60) == 0)) {
        s32 k;

        for (k = 0; k < ARRAY_COUNT(gActors); k++) {
            const Actor* a = &gActors[k];

            if ((a->obj.status == OBJ_ACTIVE) && (a->obj.id == OBJ_ACTOR_207)) {
                fprintf(sLog,
                        "frame=%d maboss state=%d health=%d stage=%d parts=%d,%d aim=%d@%.0f,%.0f,%.0f "
                        "pos=%.0f,%.0f,%.0f player=%.0f,%.0f,%.0f\n",
                        gGameFrameCount, a->state, D_i5_801BE320[29], D_i5_801BE320[18], D_i5_801BE320[9],
                        D_i5_801BE320[10], sAimPart, sAimAt.x, sAimAt.y, sAimAt.z, a->obj.pos.x, a->obj.pos.y,
                        a->obj.pos.z, p->pos.x, p->pos.y, p->trueZpos);
            }
        }
    }
    // Aquas's boss: health, the actors 256 left, the shell's opening, the window, the eye film's hits left.
    if ((gCurrentLevel == LEVEL_AQUAS) && ((gGameFrameCount % 30) == 0) && (boss->obj.status == OBJ_ACTIVE) &&
        (boss->obj.id == OBJ_BOSS_AQ_BACOON)) {
        s32 target;
        s32 homing;

        CoopVehicles_MarineLock(i, &target, &homing);
        fprintf(sLog,
                "frame=%d player=%d bacoon state=%d health=%d left=%d shell=%.1f window=%d film=%d hit=%d aim=%d@%.0f,"
                "%.0f,%.0f lock=%d homing=%d torpedo=%d pos=%.0f,%.0f,%.0f\n",
                gGameFrameCount, i, boss->state, boss->health, D_i3_801C42A0[23], D_i3_801C4308[10], boss->swork[0],
                boss->swork[12], boss->swork[1], sAimPart, sAimAt.x, sAimAt.y, sAimAt.z, target, homing,
                gPlayerShots[15].obj.status, p->pos.x, p->pos.y, p->trueZpos);
    }
    // A boss on rails (the vehicle levels): its state and swork[21], which is Goras's health (Titania_TiGoras_Init).
    if ((gLevelMode == LEVELMODE_ON_RAILS) && (i == 0) && ((gGameFrameCount % 60) == 0) &&
        (boss->obj.status == OBJ_ACTIVE)) {
        fprintf(sLog,
                "frame=%d boss id=%d state=%d pos=%.0f,%.0f,%.0f health=%d parts=%d,%d,%d,%d bar=%d player=%.0f,%.0f "
                "tilt=%.0f\n",
                gGameFrameCount, boss->obj.id, boss->state, boss->obj.pos.x, boss->obj.pos.y, boss->obj.pos.z,
                boss->swork[21], boss->swork[9], boss->swork[10], boss->swork[11], boss->swork[12], gBossHealthBar,
                p->pos.x, p->trueZpos, p->unk_17C, boss->swork[29], sAimPart, sAimAt.x, sAimAt.y, sAimAt.z);
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
        // The scan comes first, so that a hit noted below shows what this player's pilot saw.
        if ((p->state == PLAYERSTATE_ACTIVE) && (gLevelMode == LEVELMODE_ON_RAILS) && (p->form == FORM_ARWING)) {
            CoopPilot_ScanRails(p);
        }
        CoopPilot_Note(p, i);
        if (p->state != PLAYERSTATE_ACTIVE) {
            continue;
        }
        if ((p->form == FORM_LANDMASTER) && (gLevelMode == LEVELMODE_ON_RAILS)) {
            CoopPilot_Drive(p, i, &pad);
        } else if ((p->form == FORM_BLUE_MARINE) && (gLevelMode == LEVELMODE_ON_RAILS)) {
            CoopPilot_Swim(p, i, &pad);
        } else if (p->form != FORM_ARWING) {
            continue;
        } else if (gLevelMode == LEVELMODE_ON_RAILS) {
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
    if (sLog != NULL) {
        fflush(sLog); // a game that ends hard keeps what was written
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
