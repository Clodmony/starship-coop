// The pilot in Andross' escape (#275). See pilot_andross.h.

#include "pilot_andross.h"

#include <math.h>

#include "port/coop/game/players.h"
#include "port/coop/test/escape_probe.h"

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
// A case of the escape probe on player t (escape_probe.h, #345): t boosts while another player within W8_ANDPILOT_ROOM
// of it on the x or the z axis is less than W8_ANDPILOT_BEHIND behind it along its own flight (beside it in the row,
// or ahead), so that the probe has a place 450 from t and 510 from everyone else (a player 60 behind t is 510 from a
// place 450 ahead of it). The row beside the route is about 235 wide per player, so a row of four is about 700. Only
// t spends its meter: when the others braked to make room, two of four fell back side by side, met at a corner and had
// no meter left to brake out of each other (players 1 and 2 for 116 and 274 frames, 2026-11-16), and one that braked
// from 600 ahead fell back onto player 0 (81 and 109 frames).
#define W8_ANDPILOT_ROOM 800.0f
#define W8_ANDPILOT_BEHIND 200.0f

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

static bool sW8AndPilotBraking[COOP_PLAYERS_MAX];

// The larger of the x, y and z distances from player p to the nearest flying player before player i. `braking` says
// whether another flying player within W8_ANDPILOT_APART, before or after player i, is braking out of another now.
static f32 w8AndPilot_Nearest(const Player* p, s32 i, bool* braking) {
    f32 best = 1e30f;
    s32 j;

    *braking = false;
    for (j = 0; j < CoopPlayers_Count(); j++) {
        const Player* q = &gPlayer[j];

        if ((j != i) && !CoopPlayers_IsParked(j) && (q->state == PLAYERSTATE_ACTIVE)) {
            f32 d =
                fmaxf(fmaxf(fabsf(q->pos.x - p->pos.x), fabsf(q->pos.y - p->pos.y)), fabsf(q->trueZpos - p->trueZpos));

            if (j < i) {
                best = fminf(best, d);
            }
            *braking |= sW8AndPilotBraking[j] && (d < W8_ANDPILOT_APART);
        }
    }
    return best;
}

// Player i brakes out of a player before it (#345). The brake takes an all-range Arwing's speed down by 1 a frame to
// 20 under its own and fills the meter by 1.5 a frame (Player_ArwingBrake): held to W8_ANDPILOT_BRAKE_FULL, about 1200
// of ground; one burst, as a held brake is let go at a meter of W8_ANDPILOT_BRAKE_FULL. (A pilot of #345 that pressed
// the brake every other frame left the meter of players 1 to 3 at 0.1 with the cooldown on, and no brake got past
// that: unknown why.) Two that brake on one frame fall back side by side (players 1 and 2 of four at a corner, 197
// frames): a player waits while one near it brakes. Before it was only one before it, and at that corner player 2
// began to brake at frame 394 and player 1, near player 0, at 400 (#352): the two fell back together.
static void w8AndPilot_KeepApart(const Player* p, s32 i, PilotPad* pad) {
    bool otherBrakes;
    f32 nearest = w8AndPilot_Nearest(p, i, &otherBrakes);

    if ((nearest >= W8_ANDPILOT_APART) || p->boostCooldown || (p->boostMeter >= W8_ANDPILOT_BRAKE_FULL)) {
        sW8AndPilotBraking[i] = false;
    } else if ((nearest < W8_ANDPILOT_NEAR) && !otherBrakes) {
        sW8AndPilotBraking[i] = true;
    }
    if (sW8AndPilotBraking[i]) {
        pad->button |= gBrakeButton[i];
        pad->y = fmaxf(pad->y, W8_ANDPILOT_CLIMB_MAX);
    }
}

// Room for the escape probe's case (#345): its target lets go of the brake and boosts while another flying player
// within W8_ANDPILOT_ROOM of it is less than W8_ANDPILOT_BEHIND behind it along its flight (x and z), until its meter
// is at W8_ANDPILOT_BRAKE_FULL. The boost takes the speed up by 2 a frame to 30 over its own and fills the meter by 1.5
// a frame (Player_ArwingBoost). Every other player, and every player without a case, flies as before.
static void w8AndPilot_MakeRoom(const Player* p, s32 i, PilotPad* pad) {
    s32 target = CoopEscapeProbe_Target();
    f32 speed = sqrtf(SQ(p->vel.x) + SQ(p->vel.z));
    bool room = true;
    s32 j;

    if (target != i) {
        return;
    }
    sW8AndPilotBraking[i] = false;
    pad->button &= ~gBrakeButton[i];
    if ((speed < 1.0f) || p->boostCooldown || (p->boostMeter >= W8_ANDPILOT_BRAKE_FULL)) {
        return;
    }
    for (j = 0; j < CoopPlayers_Count(); j++) {
        const Player* q = &gPlayer[j];
        f32 ahead = ((p->pos.x - q->pos.x) * p->vel.x + (p->trueZpos - q->trueZpos) * p->vel.z) / speed;

        if ((j != i) && !CoopPlayers_IsParked(j) && (q->state == PLAYERSTATE_ACTIVE) &&
            (fmaxf(fabsf(q->pos.x - p->pos.x), fabsf(q->trueZpos - p->trueZpos)) < W8_ANDPILOT_ROOM) &&
            (ahead < W8_ANDPILOT_BEHIND)) {
            room = false;
        }
    }
    if (!room) {
        pad->button |= gBoostButton[i];
        pad->y = fmaxf(pad->y, W8_ANDPILOT_CLIMB_MAX); // as for the brake: under -50 a boost press is a loop
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
    w8AndPilot_MakeRoom(p, i, pad);
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
