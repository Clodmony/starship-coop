// What the pilot aims at outside Corneria (#181). See pilot_aim.h.

#include "pilot_aim.h"

#include <math.h>

#include "port/coop/game/players.h"

#define AIM_FIRE_PERIOD 4          // as pilot.c: A down one frame in four, a shot each, never a charge
#define AIM_SHOT_SPEED 133.0f      // Player_ArwingLaser
#define AIM_SHOT_RANGE 3500.0f     // as pilot.c
#define AIM_DWELL 45               // frames on one box of a boss before the next
#define AIM_AHEAD_MIN 300.0f       // on rails a box nearer than this is passed before a shot gets there
#define AIM_AHEAD_MAX 8000.0f      // and a boss further than this is not in the fight yet
#define AIM_RAIL_REACH 900.0f      // how far across the path a box may be and still be lined up with
#define AIM_BREAK_OFF_BOSS 1300.0f // as pilot.c's run at Granga: at this speed it takes that much room to miss it
#define AIM_BREAK_OFF_ACTOR 450.0f
#define AIM_ESCAPE_FRAMES 50 // after a break-off, frames flown away from the target before the next run
#define AIM_ESCAPE_DISTANCE 3000.0f
#define AIM_STICKY_FRAMES 120 // a target is kept this long unless it goes, so a run is flown to its end

typedef enum {
    AIM_NONE,
    AIM_MISSILE,   // Sector Z: a missile at the Great Fox (AI360_MISSILE)
    AIM_STAR_WOLF, // AI360_WOLF to AI360_ANDREW
    AIM_BOSS,      // a box of a boss's hitbox
    AIM_ACTOR,     // any other foe
} AimKind;

typedef struct {
    s32 kind;
    s32 index; // in gActors or gBosses
    s32 box;   // for a boss
    Vec3f at;
    Vec3f vel;
} AimTarget;

static AimTarget sAim[COOP_PLAYERS_MAX];
static s32 sAimFrames[COOP_PLAYERS_MAX];
static s32 sEscape[COOP_PLAYERS_MAX];
static Vec3f sEscapeTo[COOP_PLAYERS_MAX];

static s32 CoopPilotAim_BoxCount(const f32* hitbox) {
    return (hitbox != NULL) ? (s32) hitbox[0] : 0;
}

#define AIM_BOXES_MAX 8

// Level knowledge, for a boss the generic cycle takes too long with: the boxes whose hits count now. Each returns how
// many it wrote, 0 for every box, or -1 for none now: then the pilot leaves the boss alone and shoots at something
// else.

// The Gorgon (Area 6, src/overlays/ovl_i3/fox_a6.c): while swork[25] counts rounds (3 at the start), its three arms,
// hitbox entries 1 to 3, take hits in state 4 (Area6_8018A2C4), and a round ends with all three down; the generic
// cycle gets through them (two rounds in 24000 frames in #181's run, where aiming at the three alone did no damage in
// 40000: why is not known). After the last round only the core, entry 0, takes hits, 660 of 780 health by then
// (Area6_A6Gorgon_Update).
static s32 CoopPilotAim_Gorgon(const Boss* b, s32* boxes) {
    if (b->swork[25] == 0) {
        boxes[0] = 0;
        return 1;
    }
    return 0;
}

// Andross (src/overlays/ovl_i6/fox_andross.c, Andross_8018D2B0): his hands, hitbox entries 2 and 3, take hits until
// each has lost its 200 (swork[4], swork[5]); after both, only his eyes, entries 0 and 1, take hits.
static s32 CoopPilotAim_Andross(const Boss* b, s32* boxes) {
    s32 n = 0;

    if ((b->swork[4] < 0) && (b->swork[5] < 0)) {
        boxes[0] = 0;
        boxes[1] = 1;
        return 2;
    }
    if (b->swork[4] >= 0) {
        boxes[n++] = 2;
    }
    if (b->swork[5] >= 0) {
        boxes[n++] = 3;
    }
    return n;
}

// Bolse (src/overlays/ovl_i4/fox_bo.c): the base's core takes hits only in state 2, on its parts 0 to 7
// (Bolse_BoBaseCore_Update), and it gets there once the shield reactors are down, actors the lock-on can take
// (Bolse_8018D584). Until then the pilot leaves the core alone, so it goes for the reactors.
static s32 CoopPilotAim_BolseCore(const Boss* b, s32* boxes) {
    s32 n;

    if (b->state != 2) {
        return -1;
    }
    for (n = 0; n < 8; n++) {
        boxes[n] = n;
    }
    return 8;
}

static const struct {
    s32 id;
    s32 (*boxes)(const Boss* b, s32* boxes);
} sBossBoxes[] = {
    { OBJ_BOSS_A6_GORGON, CoopPilotAim_Gorgon },
    { OBJ_BOSS_AND_ANDROSS, CoopPilotAim_Andross },
    { OBJ_BOSS_BO_BASE_CORE, CoopPilotAim_BolseCore },
};

static s32 CoopPilotAim_Table(const Boss* b, s32* boxes) {
    s32 k;

    for (k = 0; k < ARRAY_COUNT(sBossBoxes); k++) {
        if (sBossBoxes[k].id == b->obj.id) {
            return sBossBoxes[k].boxes(b, boxes);
        }
    }
    return 0;
}

static bool CoopPilotAim_BossUp(const Boss* b) {
    s32 boxes[AIM_BOXES_MAX];

    return (b->obj.status == OBJ_ACTIVE) && (CoopPilotAim_BoxCount(b->info.hitbox) > 0) &&
           (CoopPilotAim_Table(b, boxes) >= 0);
}

// Box n of the boxes to try on boss b, counted from the first one: every box, or the table's.
static s32 CoopPilotAim_Box(const Boss* b, s32 n, s32* count) {
    s32 boxes[AIM_BOXES_MAX];
    s32 found = CoopPilotAim_Table(b, boxes);

    if (found > 0) {
        *count = found;
        return boxes[n % found];
    }
    *count = CoopPilotAim_BoxCount(b->info.hitbox);
    return n % *count;
}

bool CoopPilot_RailAim(const Player* p, s32 i, f32* x, f32* y) {
    s32 k;

    for (k = 0; k < ARRAY_COUNT(gBosses); k++) {
        const Boss* b = &gBosses[k];
        s32 count;
        s32 first;
        s32 n;

        if (!CoopPilotAim_BossUp(b) || ((p->trueZpos - b->obj.pos.z) < AIM_AHEAD_MIN) ||
            ((p->trueZpos - b->obj.pos.z) > AIM_AHEAD_MAX)) {
            continue;
        }
        CoopPilotAim_Box(b, 0, &count);
        first = (s32) ((gGameFrameCount / AIM_DWELL) + (u32) (i * ((count + 1) / 2))) % count;
        for (n = 0; n < count; n++) {
            s32 box = CoopPilotAim_Box(b, first + n, &count);
            Vec3f at;

            if (CoopPilot_BoxCenter(&b->obj, b->info.hitbox, box, &at) && ((p->trueZpos - at.z) > AIM_AHEAD_MIN) &&
                (fabsf(at.x - p->xPath) < AIM_RAIL_REACH) && (fabsf(at.y - (p->yPath + 360.0f)) < AIM_RAIL_REACH)) {
                *x = at.x;
                *y = at.y;
                sAim[i].kind = AIM_BOSS;
                sAim[i].index = k;
                sAim[i].box = box;
                sAim[i].at = at;
                return true;
            }
        }
    }
    sAim[i].kind = AIM_NONE;
    return false;
}

// An actor the all-range pilot shoots at: an all-range enemy, Star Wolf or a missile, or another actor the lock-on can
// take (a targetOffset of 0 cannot be targeted, sf64object.h). The team and the Great Fox are not.
static s32 CoopPilotAim_ActorKind(const Actor* a) {
    if ((a->obj.status != OBJ_ACTIVE) || (a->info.hitbox == NULL) || (a->timer_0C2 != 0)) {
        return AIM_NONE;
    }
    if (a->obj.id == OBJ_ACTOR_ALLRANGE) {
        if (a->aiType == AI360_MISSILE) {
            return AIM_MISSILE;
        }
        if ((a->aiType >= AI360_WOLF) && (a->aiType <= AI360_ANDREW)) {
            return AIM_STAR_WOLF;
        }
        return ((a->aiType >= AI360_ENEMY) && (a->aiType < AI360_GREAT_FOX)) ? AIM_ACTOR : AIM_NONE;
    }
    if ((a->obj.id == OBJ_ACTOR_TEAM_ARWING) || (a->obj.id == OBJ_ACTOR_TEAM_BOSS) ||
        (a->obj.id == OBJ_ACTOR_CUTSCENE)) {
        return AIM_NONE;
    }
    return (a->info.targetOffset != 0.0f) ? AIM_ACTOR : AIM_NONE;
}

static f32 CoopPilotAim_Distance(const Player* p, const Vec3f* at) {
    return sqrtf(SQ(at->x - p->pos.x) + SQ(at->y - p->pos.y) + SQ(at->z - p->pos.z));
}

// Where a target is now. Returns false once it is gone.
static bool CoopPilotAim_Refresh(AimTarget* t) {
    if ((t->kind == AIM_BOSS) && (t->index >= 0) && (t->index < ARRAY_COUNT(gBosses))) {
        const Boss* b = &gBosses[t->index];

        if (!CoopPilotAim_BossUp(b) || !CoopPilot_BoxCenter(&b->obj, b->info.hitbox, t->box, &t->at)) {
            return false;
        }
        t->vel = b->vel;
        return true;
    }
    if ((t->kind != AIM_NONE) && (t->index >= 0) && (t->index < ARRAY_COUNT(gActors))) {
        const Actor* a = &gActors[t->index];

        if (CoopPilotAim_ActorKind(a) != t->kind) {
            return false;
        }
        t->at = a->obj.pos;
        t->vel = a->vel;
        return true;
    }
    return false;
}

// The nearest foe, the most urgent kind first: a missile, then Star Wolf, then a boss box, then the rest.
static bool CoopPilotAim_Pick(const Player* p, s32 i, AimTarget* out) {
    f32 best = 1e30f;
    s32 k;

    out->kind = AIM_NONE;
    for (k = 0; k < ARRAY_COUNT(gActors); k++) {
        s32 kind = CoopPilotAim_ActorKind(&gActors[k]);
        f32 score;

        if (kind == AIM_NONE) {
            continue;
        }
        score = kind * 1e6f + CoopPilotAim_Distance(p, &gActors[k].obj.pos);
        if (score < best) {
            best = score;
            out->kind = kind;
            out->index = k;
            out->box = 0;
        }
    }
    for (k = 0; k < ARRAY_COUNT(gBosses); k++) {
        const Boss* b = &gBosses[k];
        s32 count;
        s32 box;
        Vec3f at;

        if (!CoopPilotAim_BossUp(b)) {
            continue;
        }
        CoopPilotAim_Box(b, 0, &count);
        box =
            CoopPilotAim_Box(b, (s32) ((gGameFrameCount / AIM_STICKY_FRAMES) + (u32) (i * ((count + 1) / 2))), &count);
        if (CoopPilot_BoxCenter(&b->obj, b->info.hitbox, box, &at) &&
            ((AIM_BOSS * 1e6f + CoopPilotAim_Distance(p, &at)) < best)) {
            best = AIM_BOSS * 1e6f + CoopPilotAim_Distance(p, &at);
            out->kind = AIM_BOSS;
            out->index = k;
            out->box = box;
        }
    }
    return (out->kind != AIM_NONE) && CoopPilotAim_Refresh(out);
}

void CoopPilot_FlyAllRangeAim(const Player* p, s32 i, PilotPad* pad) {
    AimTarget* t = &sAim[i];
    f32 dist;
    f32 flight;
    f32 breakOff;

    if ((sAimFrames[i] <= 0) || !CoopPilotAim_Refresh(t)) {
        if (!CoopPilotAim_Pick(p, i, t)) {
            // Nothing to shoot: stay in the arena.
            t->kind = AIM_NONE;
            CoopPilot_SteerAt(p, 0.0f, 500.0f, 0.0f, pad);
            return;
        }
        sAimFrames[i] = AIM_STICKY_FRAMES;
    }
    sAimFrames[i]--;
    dist = CoopPilotAim_Distance(p, &t->at);
    breakOff = (t->kind == AIM_BOSS) ? AIM_BREAK_OFF_BOSS : AIM_BREAK_OFF_ACTOR;
    if ((sEscape[i] <= 0) && (dist < breakOff)) {
        // Too close to turn in time: fly out past it, away from where it is, and come back.
        f32 dx = p->pos.x - t->at.x;
        f32 dz = p->pos.z - t->at.z;
        f32 across = sqrtf(SQ(dx) + SQ(dz));

        if (across < 1.0f) {
            dx = 1.0f;
            across = 1.0f;
        }
        sEscape[i] = AIM_ESCAPE_FRAMES;
        sEscapeTo[i].x = t->at.x + dx / across * AIM_ESCAPE_DISTANCE;
        sEscapeTo[i].y = fmaxf(t->at.y + 400.0f, 600.0f);
        sEscapeTo[i].z = t->at.z + dz / across * AIM_ESCAPE_DISTANCE;
    }
    if (sEscape[i] > 0) {
        sEscape[i]--;
        CoopPilot_SteerAt(p, sEscapeTo[i].x, sEscapeTo[i].y, sEscapeTo[i].z, pad);
        return;
    }
    flight = dist / AIM_SHOT_SPEED;
    if ((CoopPilot_SteerAt(p, t->at.x + t->vel.x * flight, t->at.y + t->vel.y * flight, t->at.z + t->vel.z * flight,
                           pad) < 8.0f) &&
        (dist < AIM_SHOT_RANGE) && ((gGameFrameCount % AIM_FIRE_PERIOD) == 0)) {
        pad->button |= A_BUTTON;
    }
}

void CoopPilot_NoteAim(FILE* log, const Player* p, s32 i) {
    const AimTarget* t = &sAim[i];
    s32 k;

    if ((log == NULL) || ((gGameFrameCount % 60) != 0) || (p->state != PLAYERSTATE_ACTIVE)) {
        return;
    }
    if ((gCurrentLevel == LEVEL_CORNERIA) && (gLevelMode == LEVELMODE_ALL_RANGE)) {
        return; // pilot.c's own Granga line
    }
    fprintf(log, "frame=%d player=%d aim kind=%d index=%d box=%d at=%.0f,%.0f,%.0f pos=%.0f,%.0f,%.0f mode=%d\n",
            gGameFrameCount, i, t->kind, t->index, t->box, t->at.x, t->at.y, t->at.z, p->pos.x, p->pos.y, p->trueZpos,
            gLevelMode);
    if (i != 0) {
        return;
    }
    for (k = 0; k < ARRAY_COUNT(gBosses); k++) {
        const Boss* b = &gBosses[k];

        if (b->obj.status == OBJ_ACTIVE) {
            fprintf(log, "frame=%d boss %d id=%d state=%d health=%d boxes=%d pos=%.0f,%.0f,%.0f bar=%d\n",
                    gGameFrameCount, k, b->obj.id, b->state, b->health, CoopPilotAim_BoxCount(b->info.hitbox),
                    b->obj.pos.x, b->obj.pos.y, b->obj.pos.z, gBossHealthBar);
        }
    }
}
