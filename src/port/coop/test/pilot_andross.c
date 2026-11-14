// The pilot in Andross' escape (#275). See pilot_andross.h.

#include "pilot_andross.h"

#include <math.h>

#include "port/coop/game/players.h"

#define W8_ANDPILOT_REACH 400.0f // a waypoint is reached this near on each axis (James takes 300, and turns slower)
#define W8_ANDPILOT_PASS 1500.0f // or once the ship is past it toward the next one, this near
#define W8_ANDPILOT_BLAST 500.0f // an explosion's reach in x and z (Andross_AndExplosion_Update)
#define W8_ANDPILOT_CLOSE 800.0f // the log notes a player 0 this near an explosion
// A player that came back beside player 0 flew in it (#345: within 16 from frame 560 to the end of the escape, as both
// steer at the same waypoint and two ships do not collide). One that comes this near a player before it on each axis
// brakes until it is W8_ANDPILOT_APART away along any axis, or its meter is nearly full.
#define W8_ANDPILOT_NEAR 200.0f
#define W8_ANDPILOT_APART 600.0f
#define W8_ANDPILOT_BRAKE_FULL 80.0f // at 90 the meter locks the brake until it is empty (Player_ArwingBrake)
#define W8_ANDPILOT_CLIMB_MAX -45.0f // stick y while braking: under -50 a brake press is a U-turn (Player_ArwingBrake)

static s32 sW8AndPilotNext[COOP_PLAYERS_MAX]; // the waypoint each player flies at
static s32 sW8AndPilotLast[COOP_PLAYERS_MAX]; // the frame of its last call, to see an escape start again
static bool sW8AndPilotList = false;          // the route is to be written to the log
static s32 sW8AndPilotCloseFrame = 0;         // the last frame a close explosion was noted

// The escape: the brain in its state 21, after the frame its maze is loaded (gCsFrameCount 600,
// Andross_AndBrain_Update; a restart at the escape's checkpoint begins at 599, Andross_801961AC).
static bool w8AndPilot_InEscape(void) {
    const Boss* brain = &gBosses[0];

    return (gCurrentLevel == LEVEL_VENOM_ANDROSS) && (gLevelMode == LEVELMODE_ALL_RANGE) && (gScenery360 != NULL) &&
           (brain->obj.status == OBJ_ACTIVE) && (brain->obj.id == OBJ_BOSS_AND_BRAIN) && (brain->state == 21) &&
           (gCsFrameCount > 600);
}

// Waypoint n, found as James finds it: the first level object with that pathIndex (Andross_ActorTeamArwing_Update).
static const Scenery360* w8AndPilot_Waypoint(s32 n) {
    s32 k;

    if ((n < 0) || (n > 255)) {
        return NULL;
    }
    for (k = 0; k < 200; k++) {
        const Scenery360* s = &gScenery360[k];

        if ((s->obj.id == OBJ_SCENERY_LEVEL_OBJECTS) && (s->pathIndex == (u8) n)) {
            return s;
        }
    }
    return NULL;
}

static const Actor* w8AndPilot_Trigger(void) {
    s32 k;

    for (k = 0; k < ARRAY_COUNT(gActors); k++) {
        if ((gActors[k].obj.status == OBJ_ACTIVE) && (gActors[k].obj.id == OBJ_ACTOR_AND_JAMES_TRIGGER)) {
            return &gActors[k];
        }
    }
    return NULL;
}

static f32 w8AndPilot_Distance(const Player* p, const Vec3f* at) {
    return sqrtf(SQ(at->x - p->pos.x) + SQ(at->y - p->pos.y) + SQ(at->z - p->trueZpos));
}

// Whether player p is done with waypoint n: near it, or past it on the way to the next one.
static bool w8AndPilot_Done(const Player* p, const Scenery360* wp, const Scenery360* next) {
    const Vec3f* at = &wp->obj.pos;
    f32 ax;
    f32 ay;
    f32 az;

    if ((fabsf(at->x - p->pos.x) < W8_ANDPILOT_REACH) && (fabsf(at->y - p->pos.y) < W8_ANDPILOT_REACH) &&
        (fabsf(at->z - p->trueZpos) < W8_ANDPILOT_REACH)) {
        return true;
    }
    if (next == NULL) {
        return false;
    }
    ax = next->obj.pos.x - at->x;
    ay = next->obj.pos.y - at->y;
    az = next->obj.pos.z - at->z;
    return ((p->pos.x - at->x) * ax + (p->pos.y - at->y) * ay + (p->trueZpos - at->z) * az > 0.0f) &&
           (w8AndPilot_Distance(p, at) < W8_ANDPILOT_PASS);
}

// The larger of the x, y and z distances from player p to the nearest flying player before player i.
static f32 w8AndPilot_Nearest(const Player* p, s32 i) {
    f32 best = 1e30f;
    s32 j;

    for (j = 0; j < i; j++) {
        const Player* q = &gPlayer[j];

        if (!CoopPlayers_IsParked(j) && (q->state == PLAYERSTATE_ACTIVE)) {
            best = fminf(best, fmaxf(fmaxf(fabsf(q->pos.x - p->pos.x), fabsf(q->pos.y - p->pos.y)),
                                     fabsf(q->trueZpos - p->trueZpos)));
        }
    }
    return best;
}

// Player i brakes out of a player before it (#345). The brake takes an all-range Arwing's speed down by 1 a frame to
// 20 under its own and fills the meter by 1.5 a frame (Player_ArwingBrake): held to W8_ANDPILOT_BRAKE_FULL, about 1200
// of ground. A returned player is a copy of player 0, with an empty meter. Players after player 0 held a meter of 0.1
// with the cooldown on in the escape's first run of #345, which no brake gets past: unknown why.
static bool sW8AndPilotBraking[COOP_PLAYERS_MAX];

static void w8AndPilot_KeepApart(const Player* p, s32 i, PilotPad* pad) {
    f32 nearest = w8AndPilot_Nearest(p, i);

    if ((nearest >= W8_ANDPILOT_APART) || p->boostCooldown || (p->boostMeter >= W8_ANDPILOT_BRAKE_FULL)) {
        sW8AndPilotBraking[i] = false;
    } else if (nearest < W8_ANDPILOT_NEAR) {
        sW8AndPilotBraking[i] = true;
    }
    if (sW8AndPilotBraking[i]) {
        pad->button |= gBrakeButton[i];
        pad->y = fmaxf(pad->y, W8_ANDPILOT_CLIMB_MAX);
    }
}

bool w8AndPilot_Escape(const Player* p, s32 i, PilotPad* pad) {
    const Scenery360* wp;
    const Scenery360* next;
    const Actor* trigger;

    if (!w8AndPilot_InEscape() || (i < 0) || (i >= COOP_PLAYERS_MAX)) {
        return false;
    }
    // A lockstep session calls the pilot twice in a frame: the same frame again goes on as well.
    if (((gGameFrameCount != sW8AndPilotLast[i]) && (gGameFrameCount != sW8AndPilotLast[i] + 1)) ||
        (gGameFrameCount <= 1)) {
        // A new escape, or the same one again from its checkpoint: from the waypoint nearest to the ship.
        f32 best = 1e30f;
        s32 n;

        sW8AndPilotNext[i] = 0;
        for (n = 0; (wp = w8AndPilot_Waypoint(n)) != NULL; n++) {
            if (w8AndPilot_Distance(p, &wp->obj.pos) < best) {
                best = w8AndPilot_Distance(p, &wp->obj.pos);
                sW8AndPilotNext[i] = n;
            }
        }
        if (i == 0) {
            sW8AndPilotList = true;
        }
    }
    sW8AndPilotLast[i] = gGameFrameCount;
    wp = w8AndPilot_Waypoint(sW8AndPilotNext[i]);
    while (wp != NULL) {
        next = w8AndPilot_Waypoint(sW8AndPilotNext[i] + 1);
        if ((next == NULL) || !w8AndPilot_Done(p, wp, next)) {
            break;
        }
        sW8AndPilotNext[i]++;
        wp = next;
    }
    trigger = w8AndPilot_Trigger();
    if ((wp != NULL) &&
        ((w8AndPilot_Waypoint(sW8AndPilotNext[i] + 1) != NULL) || (trigger == NULL) || !w8AndPilot_Done(p, wp, NULL))) {
        CoopPilot_SteerAt(p, wp->obj.pos.x, wp->obj.pos.y, wp->obj.pos.z, pad);
    } else if (trigger != NULL) {
        CoopPilot_SteerAt(p, trigger->obj.pos.x, trigger->obj.pos.y, trigger->obj.pos.z, pad);
    } else {
        return false;
    }
    w8AndPilot_KeepApart(p, i, pad);
    return true;
}

// How near an explosion is in the explosion's own test: the larger of the x and z distances.
static f32 w8AndPilot_Blast(const Player* p, s32* which) {
    f32 best = 1e30f;
    s32 k;

    *which = -1;
    for (k = 0; k < ARRAY_COUNT(gActors); k++) {
        const Actor* a = &gActors[k];
        f32 d;

        if ((a->obj.status != OBJ_ACTIVE) || (a->obj.id != OBJ_ACTOR_AND_EXPLOSION)) {
            continue;
        }
        d = fmaxf(fabsf(a->obj.pos.x - p->pos.x), fabsf(a->obj.pos.z - p->trueZpos));
        if (d < best) {
            best = d;
            *which = k;
        }
    }
    return best;
}

void w8AndPilot_Note(FILE* log, const Player* p, s32 i) {
    const Scenery360* wp;
    s32 which;
    f32 blast;
    s32 n;

    if ((log == NULL) || !w8AndPilot_InEscape() || (i < 0) || (i >= COOP_PLAYERS_MAX)) {
        return;
    }
    if ((i == 0) && sW8AndPilotList) {
        const Actor* trigger = w8AndPilot_Trigger();
        s32 k;

        sW8AndPilotList = false;
        fprintf(log, "frame=%d escape route from=%d trigger=%.0f,%.0f,%.0f\n", gGameFrameCount, sW8AndPilotNext[0],
                (trigger != NULL) ? trigger->obj.pos.x : 0.0f, (trigger != NULL) ? trigger->obj.pos.y : 0.0f,
                (trigger != NULL) ? trigger->obj.pos.z : 0.0f);
        for (n = 0; (wp = w8AndPilot_Waypoint(n)) != NULL; n++) {
            fprintf(log, "  waypoint %d at=%.0f,%.0f,%.0f\n", n, wp->obj.pos.x, wp->obj.pos.y, wp->obj.pos.z);
        }
        for (k = 0; k < ARRAY_COUNT(gActors); k++) {
            const Actor* a = &gActors[k];

            if ((a->obj.status == OBJ_ACTIVE) && (a->obj.id == OBJ_ACTOR_AND_EXPLOSION)) {
                fprintf(log, "  explosion actor %d at=%.0f,%.0f,%.0f rot=%.0f\n", k, a->obj.pos.x, a->obj.pos.y,
                        a->obj.pos.z, a->obj.rot.y);
            }
        }
    }
    if (p->state != PLAYERSTATE_ACTIVE) {
        return;
    }
    blast = w8AndPilot_Blast(p, &which);
    if ((i == 0) && (blast < W8_ANDPILOT_CLOSE) &&
        ((gGameFrameCount >= sW8AndPilotCloseFrame + 10) || (gGameFrameCount < sW8AndPilotCloseFrame))) {
        sW8AndPilotCloseFrame = gGameFrameCount;
        fprintf(log,
                "frame=%d escape player=0 close to explosion actor %d: %.0f (it kills under %.0f) pos=%.0f,%.0f,%.0f\n",
                gGameFrameCount, which, blast, W8_ANDPILOT_BLAST, p->pos.x, p->pos.y, p->trueZpos);
    }
    if ((gGameFrameCount % 60) == 0) {
        wp = w8AndPilot_Waypoint(sW8AndPilotNext[i]);
        fprintf(log, "frame=%d escape player=%d waypoint=%d at=%.0f,%.0f,%.0f pos=%.0f,%.0f,%.0f explosion=%d@%.0f\n",
                gGameFrameCount, i, sW8AndPilotNext[i], (wp != NULL) ? wp->obj.pos.x : 0.0f,
                (wp != NULL) ? wp->obj.pos.y : 0.0f, (wp != NULL) ? wp->obj.pos.z : 0.0f, p->pos.x, p->pos.y,
                p->trueZpos, which, blast);
    }
}
