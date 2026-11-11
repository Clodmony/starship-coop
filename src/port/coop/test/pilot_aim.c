// What the pilot aims at outside Corneria (#181). See pilot_aim.h.

#include "pilot_aim.h"

#include <math.h>

#include "port/coop/game/aim.h"
#include "port/coop/game/players.h"

#define AIM_FIRE_PERIOD 4          // as pilot.c: A down one frame in four, a shot each, never a charge
#define AIM_SHOT_SPEED 133.0f      // Player_ArwingLaser
#define AIM_SHOT_RANGE 3500.0f     // as pilot.c
#define AIM_DWELL 45               // frames on one box of a boss before the next
#define AIM_AHEAD_MIN 300.0f       // on rails a box nearer than this is passed before a shot gets there
#define AIM_AHEAD_MAX 8000.0f      // and a boss further than this is not in the fight yet
#define AIM_RAIL_REACH 900.0f      // how far across the path a box may be and still be lined up with
#define AIM_BOMB_LINED_UP 120.0f   // a bomb goes when the ship is this near the box across and up
#define AIM_BELOW_FLOOR 80.0f      // how far under the ships' floor a box's middle may be and still take a level shot
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
    AIM_OBJECTIVE, // an actor that a level's end waits for, at the box whose hits count (campaign run, 2026-11-11)
} AimKind;

// Which kind goes first, lowest first: a missile, then what the level's end waits for, then Star Wolf, a boss box and
// the rest. Bolse's fighters keep coming until the shield is down, so its reactors have to come before them.
static const s32 sAimRank[] = { 9, 1, 3, 4, 5, 2 };

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

// The Gorgon (Area 6, src/overlays/ovl_i3/fox_a6.c). While swork[25] counts rounds (3 at the start), a round ends when
// its three bars, swork[15] to swork[17], hitbox entries 4 to 6, are all shot to 0 while its cover is open (fwork[0]
// over 5, Area6_8018B9BC); at the next arm phase (state 4, timer_050 1400) they are 40 again. In #181's run the generic
// cycle spread its shots over all 15 boxes and finished two rounds in 24000 frames, and on the base of 2026-11-11 none
// in 40000. With the cover shut, the arms, entries 1 to 3 (swork[33] to swork[35]) in state 4 under a cover of 20
// (Area6_8018A2C4), shot down end the arm phase early, and the cover opens. After the last round only the core, entry
// 0, takes hits (Area6_A6Gorgon_Update): with the cover open and fwork[29] under 30, which it is from the round's end
// to the next arm phase; that phase brings a round back (swork[25] 1).
static s32 CoopPilotAim_Gorgon(const Boss* b, s32* boxes) {
    s32 n = 0;
    s32 k;

    if (b->swork[25] == 0) {
        boxes[0] = 0;
        return 1;
    }
    if (b->fwork[0] > 5.0f) {
        for (k = 0; k < 3; k++) {
            if (b->swork[15 + k] > 0) {
                boxes[n++] = 4 + k;
            }
        }
    } else if ((b->state == 4) && (b->fwork[0] < 20.0f)) {
        for (k = 0; k < 3; k++) {
            if (b->swork[33 + k] > 0) {
                boxes[n++] = 1 + k;
            }
        }
    }
    return n;
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
// (Bolse_8018D584). Until then the pilot leaves the core alone, so it goes for the reactors. A part is down at a health
// (swork[part]) of 0; only the parts still up count, or the last one gets a turn in eight (2026-11-11: it stood at 20
// for the last 8000 frames of a 30000 frame run).
static s32 CoopPilotAim_BolseCore(const Boss* b, s32* boxes) {
    s32 count = 0;
    s32 n;

    if (b->state != 2) {
        return -1;
    }
    for (n = 0; n < 8; n++) {
        if (b->swork[n] > 0) {
            boxes[count++] = n;
        }
    }
    return (count > 0) ? count : -1;
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

// Whether player i is the one boss k follows, where that moves its boxes with the player (#213: Andross' face keeps to
// the x of the player it keeps, Andross_AndAndross_Update). That player lining up with a hand drags the hand along, and
// on 2026-11-11 the two pilots chased Andross' hands across the path for 30000 frames; it flies its own line, and the
// other player shoots.
static bool CoopPilotAim_Leads(const Boss* b, s32 k, s32 i) {
    s32 count;
    const int8_t* kept;

    if ((b->obj.id != OBJ_BOSS_AND_ANDROSS) || (CoopPlayers_Count() < 2) || ((b->swork[4] < 0) && (b->swork[5] < 0))) {
        return false; // with both hands down, everyone shoots his eyes
    }
    kept = CoopAim_KeptSlots(&count);
    return (k < count) && (kept[k] == i);
}

// Where the box player i lined up with last frame was, to lead a box that moves (2026-11-11): Andross' hands swing
// and punch, and a shot that goes where a hand was takes about 15 frames to get there.
static s32 sRailKey[COOP_PLAYERS_MAX];
static u32 sRailFrame[COOP_PLAYERS_MAX];
static Vec3f sRailLast[COOP_PLAYERS_MAX];
bool CoopPilot_RailAim(const Player* p, s32 i, f32* x, f32* y) {
    s32 k;

    for (k = 0; k < ARRAY_COUNT(gBosses); k++) {
        const Boss* b = &gBosses[k];
        s32 table[AIM_BOXES_MAX];
        s32 count;
        s32 first;
        s32 n;

        if (!CoopPilotAim_BossUp(b) || ((p->trueZpos - b->obj.pos.z) < AIM_AHEAD_MIN) ||
            ((p->trueZpos - b->obj.pos.z) > AIM_AHEAD_MAX)) {
            continue;
        }
        if (CoopPilotAim_Leads(b, k, i)) {
            // The face goes where this player goes, its right hand (swork[4], vwork[0]) about 1100 to its +x and its
            // left one as far to -x, both some 600 under it. This player takes it high and to the side away from the
            // hand still up, which brings that hand up and over to the other player.
            *x = p->xPath + ((b->swork[4] >= 0) ? -640.0f : 640.0f);
            *y = p->yPath + 660.0f;
            sAim[i].kind = AIM_NONE;
            return true;
        }
        CoopPilotAim_Box(b, 0, &count);
        // Every box of the generic cycle takes a turn, as some of them do not count. The boxes of a table all count:
        // there each player keeps its own, as a swap every AIM_DWELL frames sent both across the path and back
        // (Andross' two hands, 2026-11-11).
        if (CoopPilotAim_Table(b, table) > 0) {
            first = i % count;
        } else {
            first = (s32) ((gGameFrameCount / AIM_DWELL) + (u32) (i * ((count + 1) / 2))) % count;
        }
        for (n = 0; n < count; n++) {
            s32 box = CoopPilotAim_Box(b, first + n, &count);
            s32 key = k * 256 + box;
            Vec3f at;

            // A box below the path's floor is out of reach where the floor is near the ground: there the nose does
            // not go down (Player_MoveArwingOnRails, under gGroundHeight + 50), and Andross' hands rest at y -200 to
            // -540 with the ships held at 40 (2026-11-11). The next box in the order takes its place.
            if (CoopPilot_BoxCenter(&b->obj, b->info.hitbox, box, &at) && ((p->trueZpos - at.z) > AIM_AHEAD_MIN) &&
                (fabsf(at.x - p->xPath) < AIM_RAIL_REACH) && (fabsf(at.y - (p->yPath + 360.0f)) < AIM_RAIL_REACH) &&
                ((at.y > (p->yPath + p->pathFloor - AIM_BELOW_FLOOR)) ||
                 ((p->yPath + p->pathFloor) > (gGroundHeight + 50.0f)))) {
                f32 flight = (p->trueZpos - at.z) / AIM_SHOT_SPEED;

                *x = at.x;
                *y = at.y;
                if ((sRailKey[i] == key) && (sRailFrame[i] + 1 == gGameFrameCount)) {
                    *x += (at.x - sRailLast[i].x) * flight;
                    *y += (at.y - sRailLast[i].y) * flight;
                }
                sRailKey[i] = key;
                sRailFrame[i] = gGameFrameCount;
                sRailLast[i] = at;
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

// On rails, a bomb where it ends a fight much sooner than shots do (2026-11-11). The bomb slot is one for every player
// (gPlayerShots[15], Player_SmartBomb); a bomb that meets the boss bursts there, and while it burns every hit on a part
// in its reach counts with the bomb's damage of 20 (PlayerShot_CheckBossHitbox).
// The bosses that player 0 bombs wherever it flies, where campaign.py turns on gInfiniteBombs (the owner's choice of
// 2026-11-11: player 0 always has 9; NOTES.md). Each in the states where its parts take a bomb.
// - Andross (src/overlays/ovl_i6/fox_andross.c): his hands rest out of a shot's reach, wide of the path or under its
//   floor, about 900 from his face, as far as a bomb's burst reaches (its scale grows to 30, by 30 each:
//   PlayerShot_UpdateBomb, PlayerShot_CheckBossHitbox). With the hands down the burst takes his eyes, which count from
//   then on in a state under 12 (Andross_8018D2B0).
// - The Sarumarine (src/overlays/ovl_i3/fox_zo.c, Zoness_ZoSarumarine_Update): its parts 3 to 5 and 8 to 11 take only
//   bombs, in state 3, and parts 7 and 12 in state 5; once they are down, any hit on parts 5 and 6 takes its health.
static bool CoopPilotAim_BombsAt(const Boss* b) {
    if (b->obj.status != OBJ_ACTIVE) {
        return false;
    }
    switch (b->obj.id) {
        case OBJ_BOSS_AND_ANDROSS:
            return (b->state >= 2) && (b->state <= 10);
        case OBJ_BOSS_ZO_SARUMARINE:
            return (b->state == 3) || (b->state == 5);
        default:
            return false;
    }
}

bool CoopPilot_RailBomb(const Player* p, s32 i) {
    const AimTarget* t = &sAim[i];
    const PlayerShot* bomb = &gPlayerShots[ARRAY_COUNT(gPlayerShots) - 1];
    const Boss* b;
    s32 k;

    // Player 0 lets one go at such a boss wherever it flies, and sets it off at the boss's depth with a second press
    // (allowed once the bomb's timer is under 25).
    for (k = 0; k < ARRAY_COUNT(gBosses); k++) {
        b = &gBosses[k];
        if (!CoopPilotAim_BombsAt(b)) {
            continue;
        }
        if ((bomb->obj.status == SHOT_ACTIVE) && (bomb->obj.id == PLAYERSHOT_BOMB) && (bomb->unk_5C == 0) &&
            (bomb->sourceId == i)) {
            return (bomb->timer < 25) && (bomb->obj.pos.z < (b->obj.pos.z + 100.0f));
        }
        if ((bomb->obj.status == SHOT_FREE) && (gBombCount[i] > 0) && (i == 0) &&
            ((p->trueZpos - b->obj.pos.z) > 1200.0f)) {
            return true;
        }
    }
    if ((t->kind != AIM_BOSS) || (t->index < 0) || (t->index >= ARRAY_COUNT(gBosses)) || (gBombCount[i] <= 0) ||
        (gPlayerShots[ARRAY_COUNT(gPlayerShots) - 1].obj.status != SHOT_FREE) ||
        (fabsf(p->pos.x - t->at.x) > AIM_BOMB_LINED_UP) || (fabsf(p->pos.y - t->at.y) > AIM_BOMB_LINED_UP)) {
        return false;
    }
    b = &gBosses[t->index];
    switch (b->obj.id) {
        case OBJ_BOSS_A6_GORGON:
            // While a bomb burns, a hit on any bar takes the damage off all three (Area6_8018B9BC: gGroundClipMode set
            // and the bomb's timer running), so one bomb on a fresh round with the cover wide open ends it. After the
            // rounds, on the core while it is open.
            if (b->fwork[0] < 60.0f) {
                return false;
            }
            if (b->swork[25] != 0) {
                return (t->box >= 4) && (b->swork[15] == 40) && (b->swork[16] == 40) && (b->swork[17] == 40);
            }
            return (t->box == 0) && (b->fwork[29] < 30.0f);
        case OBJ_BOSS_AND_ANDROSS:
            // Lined up with a hand, the other players' bombs as well, as long as they last.
            return ((b->swork[4] >= 0) || (b->swork[5] >= 0)) && (b->state >= 2) && (b->state <= 7) &&
                   ((p->trueZpos - b->obj.pos.z) > 1200.0f);
        default:
            return false;
    }
}

// An actor the all-range pilot shoots at: an all-range enemy, Star Wolf or a missile, or another actor the lock-on can
// take (a targetOffset of 0 cannot be targeted, sf64object.h). The team and the Great Fox are not.
static s32 CoopPilotAim_ActorKind(const Actor* a) {
    if ((a->obj.status != OBJ_ACTIVE) || (a->info.hitbox == NULL) || (a->timer_0C2 != 0)) {
        return AIM_NONE;
    }
    // Bolse's six shield reactors (src/overlays/ovl_i4/fox_bo.c, Bolse_8018D584): in state 0 a hit on box 0 or 1 adds
    // its damage to the health, and at 100 the reactor is down. With all six down the shield goes, and only then can
    // the base's core open (Bolse_BoBaseCore_Update). The fighters around them come back for as long as the shield
    // stands, so a pilot that shoots the nearest foe never gets there (#181).
    if (a->obj.id == OBJ_ACTOR_BO_SHIELD_REACTOR) {
        return (a->state == 0) ? AIM_OBJECTIVE : AIM_NONE;
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
        if ((t->kind != AIM_OBJECTIVE) || !CoopPilot_BoxCenter(&a->obj, a->info.hitbox, t->box, &t->at)) {
            t->at = a->obj.pos;
        }
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
        score = sAimRank[kind] * 1e6f + CoopPilotAim_Distance(p, &gActors[k].obj.pos);
        if (score < best) {
            best = score;
            out->kind = kind;
            out->index = k;
            out->box = (kind == AIM_OBJECTIVE) ? (i % 2) : 0; // the players on the reactor's two boxes
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
            ((sAimRank[AIM_BOSS] * 1e6f + CoopPilotAim_Distance(p, &at)) < best)) {
            best = sAimRank[AIM_BOSS] * 1e6f + CoopPilotAim_Distance(p, &at);
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
    breakOff = ((t->kind == AIM_BOSS) || (t->kind == AIM_OBJECTIVE)) ? AIM_BREAK_OFF_BOSS : AIM_BREAK_OFF_ACTOR;
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

extern s32 sZoSwork[]; // src/overlays/ovl_i3/fox_zo.c

// What each boss the pilot has level knowledge of counts, in its own words (the swork and fwork named in the tables'
// comments above), so a log says why a fight does not end.
static void CoopPilotAim_NoteBoss(FILE* log, const Boss* b) {
    switch (b->obj.id) {
        case OBJ_BOSS_A6_GORGON:
            fprintf(log, "frame=%d gorgon rounds=%d bars=%d,%d,%d arms=%d,%d,%d open=%.0f turn=%.0f timer=%d\n",
                    gGameFrameCount, b->swork[25], b->swork[15], b->swork[16], b->swork[17], b->swork[33], b->swork[34],
                    b->swork[35], b->fwork[29], b->fwork[0], b->timer_050);
            break;
        case OBJ_BOSS_AND_ANDROSS:
            fprintf(
                log,
                "frame=%d andross hands=%d,%d eyes=%d busy=%d state=%d at=%.0f,%.0f,%.0f/%.0f,%.0f,%.0f bombs=%d,%d\n",
                gGameFrameCount, b->swork[4], b->swork[5], b->swork[6], b->swork[7], b->state, b->vwork[0].x,
                b->vwork[0].y, b->vwork[0].z, b->vwork[1].x, b->vwork[1].y, b->vwork[1].z, gBombCount[0],
                gBombCount[1]);
            break;
        case OBJ_BOSS_BO_BASE_CORE:
            fprintf(log, "frame=%d core parts=%d,%d,%d,%d,%d,%d,%d,%d left=%d\n", gGameFrameCount, b->swork[0],
                    b->swork[1], b->swork[2], b->swork[3], b->swork[4], b->swork[5], b->swork[6], b->swork[7],
                    b->swork[36]);
            break;
        case OBJ_BOSS_ZO_SARUMARINE:
            fprintf(log, "frame=%d sarumarine parts=%d,%d,%d,%d,%d,%d bombs=%d,%d\n", gGameFrameCount, sZoSwork[8],
                    sZoSwork[9], sZoSwork[10], sZoSwork[11], sZoSwork[12], sZoSwork[13], gBombCount[0], gBombCount[1]);
            break;
        default:
            break;
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
    fprintf(log,
            "frame=%d player=%d aim kind=%d index=%d box=%d at=%.0f,%.0f,%.0f pos=%.0f,%.0f,%.0f mode=%d "
            "path=%.0f,%.0f\n",
            gGameFrameCount, i, t->kind, t->index, t->box, t->at.x, t->at.y, t->at.z, p->pos.x, p->pos.y, p->trueZpos,
            gLevelMode, p->xPath, p->yPath);
    if (i != 0) {
        return;
    }
    for (k = 0; k < ARRAY_COUNT(gBosses); k++) {
        const Boss* b = &gBosses[k];

        if (b->obj.status == OBJ_ACTIVE) {
            fprintf(log, "frame=%d boss %d id=%d state=%d health=%d boxes=%d pos=%.0f,%.0f,%.0f bar=%d\n",
                    gGameFrameCount, k, b->obj.id, b->state, b->health, CoopPilotAim_BoxCount(b->info.hitbox),
                    b->obj.pos.x, b->obj.pos.y, b->obj.pos.z, gBossHealthBar);
            CoopPilotAim_NoteBoss(log, b);
        }
    }
}
