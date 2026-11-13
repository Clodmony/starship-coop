#include "mabomb_probe.h"

#include <stdio.h>
#include <stdlib.h>

#include "global.h"
#include "port/hooks/Events.h"
#include "port/coop/game/players.h"

// The bomb's own setup (src/overlays/ovl_i5/fox_ma.c); no header declares it.
void Macbeth_MaBombDrop_Setup(MaBombDrop* this, f32 xPos, f32 yPos, f32 zPos, f32 xVel, f32 yVel, f32 zVel);

#define MABOMB_START_DELAY 60 // frames from every tank driving to the first case
#define MABOMB_PERIOD 40      // frames from the end of one case to the next
#define MABOMB_GAP 250.0f     // x from the target to every other tank: more than the fire (80) and a beam (100) reach
#define MABOMB_LEAD 3         // the bomb explodes on its second update and its fire tests from its third
#define MABOMB_EXPLODE 999.9f // the x velocity that makes a bomb explode where it is (Macbeth_MaBombDrop_Setup)
#define MABOMB_FALL_Y 20.0f   // a bomb falls 10 a frame for the two frames before it explodes
#define MABOMB_FALL_Z 10.0f   // and drifts back 5 a frame
// Each case for each target this many times. Without a pilot the tanks stop at z about -33800, some 2700 frames in,
// and are seldom MABOMB_GAP apart there: 2 rounds of 5 cases are over by frame 2100, a third was not always.
#define MABOMB_ROUNDS 2
#define MABOMB_JUMP 200.0f  // a move larger than this in one frame is a restart or a jump, not driving
#define MABOMB_WAIT_LOG 100 // frames between two lines of a case that cannot start

// The damage the probe gives its hazard: no other object of the level deals it, so dmgType names the hazard.
#define MABOMB_MARK_BOMB 33
#define MABOMB_MARK_BEAM 23
#define MABOMB_MARK_SHOT 27
#define MABOMB_MARK_BLAST 60 // the blast's own damage, a constant in Macbeth_MaTrainCar7_Update; nothing else deals it

#define MABOMB_SHOT_UP 20.0f      // a shot this far over the tank's place is in its box (30) and over the ground
#define MABOMB_BLAST_SIDE 700.0f  // the car's x from the target: inside the blast's 800
#define MABOMB_BLAST_CLEAR 850.0f // and at least this far from every other tank
#define MABOMB_NO_CAR 100         // a car number (iwork[5]) past the train's, so the blast cuts no car off

typedef enum MaBombKind {
    MABOMB_KIND_BOMB,
    MABOMB_KIND_BEAM,
    MABOMB_KIND_SHOT,
    MABOMB_KIND_BLAST,
} MaBombKind;

typedef struct MaBombCase {
    const char* name;
    MaBombKind kind;
    s32 window; // frames from the hazard's first update in which the target has to be hit
    s32 mark;
    s32 roll; // the target is in a barrel roll (barrelRollAlpha) while the hazard updates
} MaBombCase;

// Keep the names in step with W8_MABOMB_CASES in tools/coop/lockstep_test.py.
static const MaBombCase sMaBombCases[] = {
    { "bomb", MABOMB_KIND_BOMB, 30, MABOMB_MARK_BOMB, 0 }, // the fire burns about 38 frames
    { "beam", MABOMB_KIND_BEAM, 4, MABOMB_MARK_BEAM, 0 },
    { "shot", MABOMB_KIND_SHOT, 4, MABOMB_MARK_SHOT, 0 },
    { "roll", MABOMB_KIND_SHOT, 4, MABOMB_MARK_SHOT, 1 },
    { "blast", MABOMB_KIND_BLAST, 1, MABOMB_MARK_BLAST, 0 }, // the car tests once, on the frame it blows up
};

typedef enum MaBombStage {
    MABOMB_STAGE_WAIT_DRIVING,
    MABOMB_STAGE_WAIT_NEXT,
    MABOMB_STAGE_RUNNING,
    MABOMB_STAGE_DONE,
} MaBombStage;

static FILE* sMaBombFile = NULL;
static MaBombStage sMaBombStage = MABOMB_STAGE_WAIT_DRIVING;
static s32 sMaBombRound = 0;
static s32 sMaBombCase = 0;
static s32 sMaBombTarget = 0;
static s32 sMaBombCount = 0;
static s32 sMaBombNext = 0;
static s32 sMaBombLastFrame = -1;
static Actor* sMaBombBomb = NULL;  // the actor of a bomb or blast case
static Effect* sMaBombBeam = NULL; // the effect of a beam, shot or roll case
static s32 sMaBombDeflected = 0;   // the shot of a shot or roll case got a velocity: a deflect
static f32 sMaBombSide = 0.0f;     // a blast's car is this far across the track from its target
static s32 sMaBombUpdated = 0;     // the hazard had an update on this frame
static s32 sMaBombFrames = 0;      // updates of the hazard so far
static s32 sMaBombHit = 0;
static s32 sMaBombFirst[COOP_PLAYERS_MAX];
static f32 sMaBombGap = 0.0f;
static Vec3f sMaBombLast[COOP_PLAYERS_MAX]; // each player's x, y and trueZpos at the end of the frame before
static s32 sMaBombHaveLast = 0;

static s32 CoopMaBombProbe_EveryTankDrives(void) {
    s32 i;

    for (i = 0; i < CoopPlayers_Count(); i++) {
        if (CoopPlayers_IsParked(i) || (gPlayer[i].state != PLAYERSTATE_ACTIVE)) {
            return 0;
        }
    }
    return 1;
}

// How far `i` moved since the frame before, or nonzero when that is not a drive.
static s32 CoopMaBombProbe_Step(s32 i, Vec3f* step) {
    const Player* p = &gPlayer[i];

    step->x = p->pos.x - sMaBombLast[i].x;
    step->y = p->pos.y - sMaBombLast[i].y;
    step->z = p->trueZpos - sMaBombLast[i].z;
    return !sMaBombHaveLast || (fabsf(step->x) > MABOMB_JUMP) || (fabsf(step->y) > MABOMB_JUMP) ||
           (fabsf(step->z) > MABOMB_JUMP);
}

// Nonzero when the target is far enough across the track from every other tank, in front of player 0's camera (an
// effect behind it is culled, Effect_Move) and driving. For a blast, whose box is 800 wide, also when the car can stand
// beside the target with every other tank out of the blast.
static s32 CoopMaBombProbe_CanPlace(MaBombKind kind, s32 target) {
    const Player* t = &gPlayer[target];
    Vec3f step;
    s32 i;

    if (CoopMaBombProbe_Step(target, &step) || !((t->trueZpos + gPathProgress) < (gPlayer[0].cam.eye.z - 100.0f))) {
        return 0;
    }
    sMaBombGap = 100000.0f;
    for (i = 0; i < CoopPlayers_Count(); i++) {
        f32 gap = fabsf(gPlayer[i].pos.x - t->pos.x);

        if (i == target) {
            continue;
        }
        if (gap < MABOMB_GAP) {
            return 0;
        }
        if (gap < sMaBombGap) {
            sMaBombGap = gap;
            // A blast's car goes to the side of the target that is away from the nearest other tank.
            sMaBombSide = (gPlayer[i].pos.x < t->pos.x) ? MABOMB_BLAST_SIDE : -MABOMB_BLAST_SIDE;
        }
    }
    if (kind == MABOMB_KIND_BLAST) {
        for (i = 0; i < CoopPlayers_Count(); i++) {
            if ((i != target) && (fabsf(gPlayer[i].pos.x - (t->pos.x + sMaBombSide)) < MABOMB_BLAST_CLEAR)) {
                return 0;
            }
        }
    }
    return 1;
}

static void CoopMaBombProbe_ClearHits(void) {
    s32 i;

    for (i = 0; i < CoopPlayers_Count(); i++) {
        gPlayer[i].shields = Play_GetMaxShields();
        gPlayer[i].damage = 0;
        gPlayer[i].hitTimer = 0;
        gPlayer[i].mercyTimer = 0;
    }
}

// The last free slot of the hazard's array, as Macbeth_MaBombDrop_Spawn and func_effect_8007EE68 take it; -1 for none.
static s32 CoopMaBombProbe_FreeSlot(MaBombKind kind) {
    s32 i;

    if ((kind == MABOMB_KIND_BOMB) || (kind == MABOMB_KIND_BLAST)) {
        for (i = ARRAY_COUNT(gActors) - 1; (i >= 0) && (gActors[i].obj.status != OBJ_FREE); i--) {}
    } else {
        for (i = ARRAY_COUNT(gEffects) - 1; (i >= 0) && (gEffects[i].obj.status != OBJ_FREE); i--) {}
    }
    return i;
}

// Places the hazard of `c` for `target`; nonzero when its array has no free slot (nothing placed).
static s32 CoopMaBombProbe_Place(const MaBombCase* c, s32 target) {
    const Player* t = &gPlayer[target];
    s32 slot = CoopMaBombProbe_FreeSlot(c->kind);
    Vec3f step;
    s32 i;

    if (slot < 0) {
        return 1;
    }
    CoopMaBombProbe_Step(target, &step);
    CoopMaBombProbe_ClearHits();
    sMaBombHit = 0;
    sMaBombFrames = 0;
    sMaBombUpdated = 0;
    sMaBombDeflected = 0;
    for (i = 0; i < COOP_PLAYERS_MAX; i++) {
        sMaBombFirst[i] = -1;
    }
    if (c->kind == MABOMB_KIND_BOMB) {
        // The bomb falls and drifts for two updates, then burns where it is: MABOMB_LEAD frames of the target's drive
        // ahead of it.
        sMaBombBomb = &gActors[slot];
        Macbeth_MaBombDrop_Setup(sMaBombBomb, t->pos.x + (MABOMB_LEAD * step.x), t->pos.y + MABOMB_FALL_Y,
                                 t->trueZpos + (MABOMB_LEAD * step.z) - MABOMB_FALL_Z, MABOMB_EXPLODE, -10.0f, 5.0f);
        sMaBombBomb->info.damage = c->mark;
    } else if (c->kind == MABOMB_KIND_BLAST) {
        // A car in the state it blows up in (state 1; its load is gone by then, iwork[13] 2), beside the target's next
        // place: actors update after the players. Not set up as a car of the train (Macbeth_Train_Init counts those).
        sMaBombBomb = &gActors[slot];
        Actor_Initialize(sMaBombBomb);
        sMaBombBomb->obj.status = OBJ_ACTIVE;
        sMaBombBomb->obj.id = OBJ_ACTOR_MA_TRAIN_CAR_7;
        Object_SetInfo(&sMaBombBomb->info, sMaBombBomb->obj.id);
        sMaBombBomb->obj.pos.x = t->pos.x + step.x + sMaBombSide;
        sMaBombBomb->obj.pos.y = t->pos.y + step.y;
        sMaBombBomb->obj.pos.z = t->trueZpos + step.z;
        sMaBombBomb->state = 1;
        sMaBombBomb->iwork[5] = MABOMB_NO_CAR;
        sMaBombBomb->iwork[13] = 2;
    } else {
        // At rest on the target's next place: players move before effects.
        sMaBombBeam = &gEffects[slot];
        Effect_Initialize(sMaBombBeam);
        sMaBombBeam->obj.status = OBJ_ACTIVE;
        sMaBombBeam->obj.id = (c->kind == MABOMB_KIND_BEAM) ? OBJ_EFFECT_380 : OBJ_EFFECT_378;
        Object_SetInfo(&sMaBombBeam->info, sMaBombBeam->obj.id);
        sMaBombBeam->obj.pos.x = t->pos.x + step.x;
        sMaBombBeam->obj.pos.y = t->pos.y + step.y + ((c->kind == MABOMB_KIND_SHOT) ? MABOMB_SHOT_UP : 0.0f);
        sMaBombBeam->obj.pos.z = t->trueZpos + step.z;
        sMaBombBeam->scale2 = (c->kind == MABOMB_KIND_BEAM) ? 5.0f : 1.0f; // the left gun's, state 16; a turret's
        sMaBombBeam->info.damage = c->mark;
    }
    return 0;
}

// Nonzero while the hazard the probe placed is still in its slot.
static s32 CoopMaBombProbe_HazardAlive(const MaBombCase* c) {
    if (c->kind == MABOMB_KIND_BOMB) {
        return (sMaBombBomb != NULL) && (sMaBombBomb->obj.status != OBJ_FREE) &&
               (sMaBombBomb->obj.id == OBJ_ACTOR_MA_BOMBDROP);
    }
    if (c->kind == MABOMB_KIND_BLAST) {
        return (sMaBombBomb != NULL) && (sMaBombBomb->obj.status != OBJ_FREE) &&
               (sMaBombBomb->obj.id == OBJ_ACTOR_MA_TRAIN_CAR_7);
    }
    return (sMaBombBeam != NULL) && (sMaBombBeam->obj.status != OBJ_FREE) &&
           (sMaBombBeam->obj.id == ((c->kind == MABOMB_KIND_BEAM) ? OBJ_EFFECT_380 : OBJ_EFFECT_378));
}

static void CoopMaBombProbe_TakeOut(const MaBombCase* c) {
    if (CoopMaBombProbe_HazardAlive(c)) {
        if ((c->kind == MABOMB_KIND_BOMB) || (c->kind == MABOMB_KIND_BLAST)) {
            Object_Kill(&sMaBombBomb->obj, sMaBombBomb->sfxSource);
        } else {
            Object_Kill(&sMaBombBeam->obj, sMaBombBeam->sfxSource);
        }
    }
    if (c->kind == MABOMB_KIND_BLAST) {
        // The car's later states count its shake down; the probe's car does not reach them. Its flash fades by itself
        // (Play_UpdateFillScreen, 16 a frame), as in play, and the view trace shows it (#303).
        gCameraShake = 0;
    }
    if (c->roll) {
        gPlayer[sMaBombTarget].barrelRollAlpha = 0;
    }
    sMaBombBomb = NULL;
    sMaBombBeam = NULL;
}

// Right before the hazard's own update: no earlier hit may shield a player from it, and its hit carries the mark.
static void CoopMaBombProbe_OnObjectUpdate(ObjectUpdateEvent* event) {
    const MaBombCase* c = &sMaBombCases[sMaBombCase];
    s32 i;

    if ((sMaBombStage != MABOMB_STAGE_RUNNING) || (event->object == NULL) ||
        !(((event->type == OBJECT_TYPE_ACTOR) && (event->object == sMaBombBomb)) ||
          ((event->type == OBJECT_TYPE_EFFECT) && (event->object == sMaBombBeam)))) {
        return;
    }
    for (i = 0; i < CoopPlayers_Count(); i++) {
        gPlayer[i].mercyTimer = 0;
    }
    if (c->kind == MABOMB_KIND_BOMB) {
        sMaBombBomb->info.damage = c->mark;
    } else if (c->kind != MABOMB_KIND_BLAST) {
        sMaBombBeam->info.damage = c->mark;
    }
    if (c->roll) {
        // A tank never has it (only Player_UpdateArwingRoll raises it): forced, to reach the shot's deflect.
        gPlayer[sMaBombTarget].barrelRollAlpha = 180;
    }
    sMaBombUpdated = 1;
}

static void CoopMaBombProbe_Report(const MaBombCase* c, s32 lost) {
    s32 i;

    fprintf(sMaBombFile, "mabomb frame=%d case=%s target=%d hit=%d first=", (int) gGameFrameCount, c->name,
            (int) sMaBombTarget, (int) sMaBombHit);
    for (i = 0; i < CoopPlayers_Count(); i++) {
        fprintf(sMaBombFile, "%s%d", (i != 0) ? "," : "", (int) sMaBombFirst[i]);
    }
    fprintf(sMaBombFile, " lost=%d dx=%.0f defl=%d\n", (int) lost, sMaBombGap, (int) sMaBombDeflected);
    fflush(sMaBombFile); // tests may kill the game on a timeout
}

// What a case that cannot start is waiting for, as the header says.
static void CoopMaBombProbe_ReportWait(const MaBombCase* c) {
    const Player* t = &gPlayer[sMaBombTarget];
    Vec3f step;
    s32 jump = CoopMaBombProbe_Step(sMaBombTarget, &step);
    s32 i;

    fprintf(sMaBombFile,
            "mabomb wait frame=%d case=%s target=%d drives=%d jump=%d front=%d slot=%d players=", (int) gGameFrameCount,
            c->name, (int) sMaBombTarget, (int) CoopMaBombProbe_EveryTankDrives(), (int) jump,
            (int) ((t->trueZpos + gPathProgress) < (gPlayer[0].cam.eye.z - 100.0f)),
            (int) CoopMaBombProbe_FreeSlot(c->kind));
    for (i = 0; i < CoopPlayers_Count(); i++) {
        fprintf(sMaBombFile, "%s%d:%.0f:%.0f", (i != 0) ? "," : "", (int) gPlayer[i].state, gPlayer[i].pos.x,
                gPlayer[i].trueZpos);
    }
    fprintf(sMaBombFile, "\n");
    fflush(sMaBombFile);
}

// The next case and target, or nonzero when there is none left.
static s32 CoopMaBombProbe_Advance(void) {
    sMaBombCount++;
    if (sMaBombTarget < (CoopPlayers_Count() - 1)) {
        sMaBombTarget++;
        return 0;
    }
    sMaBombTarget = 0;
    if (sMaBombCase < (ARRAY_COUNT(sMaBombCases) - 1)) {
        sMaBombCase++;
        return 0;
    }
    sMaBombCase = 0;
    sMaBombRound++;
    return sMaBombRound >= MABOMB_ROUNDS;
}

// One frame of a running case: who the hazard hit, and the end of the case.
static void CoopMaBombProbe_Watch(const MaBombCase* c) {
    s32 alive;
    s32 lost;
    s32 i;

    if (sMaBombUpdated) {
        for (i = 0; i < CoopPlayers_Count(); i++) {
            const Player* p = &gPlayer[i];

            if (!(sMaBombHit & (1 << i)) && (p->hitTimer == 20) && (p->hitDirection == 0) && (p->dmgType == c->mark)) {
                sMaBombHit |= 1 << i;
                sMaBombFirst[i] = sMaBombFrames;
            }
        }
        sMaBombFrames++;
        sMaBombUpdated = 0;
    }
    alive = CoopMaBombProbe_HazardAlive(c);
    // The probe's shot is placed at rest, and nothing but a deflect gives it a velocity.
    if (alive && (c->kind == MABOMB_KIND_SHOT) &&
        ((sMaBombBeam->vel.x != 0.0f) || (sMaBombBeam->vel.y != 0.0f) || (sMaBombBeam->vel.z != 0.0f))) {
        sMaBombDeflected = 1;
    }
    if (!(sMaBombHit & (1 << sMaBombTarget)) && !sMaBombDeflected && (sMaBombFrames < c->window) && alive) {
        return;
    }
    lost = !alive && !(sMaBombHit & (1 << sMaBombTarget)) && (sMaBombFrames < c->window);
    CoopMaBombProbe_Report(c, lost);
    CoopMaBombProbe_TakeOut(c);
    sMaBombNext = gGameFrameCount + MABOMB_PERIOD;
    if (CoopMaBombProbe_Advance()) {
        fprintf(sMaBombFile, "mabomb done cases=%d\n", (int) sMaBombCount);
        fflush(sMaBombFile);
        sMaBombStage = MABOMB_STAGE_DONE;
    } else {
        sMaBombStage = MABOMB_STAGE_WAIT_NEXT;
    }
}

static void CoopMaBombProbe_KeepLast(void) {
    s32 i;

    for (i = 0; i < CoopPlayers_Count(); i++) {
        sMaBombLast[i].x = gPlayer[i].pos.x;
        sMaBombLast[i].y = gPlayer[i].pos.y;
        sMaBombLast[i].z = gPlayer[i].trueZpos;
    }
    sMaBombHaveLast = 1;
}

static void CoopMaBombProbe_OnGamePostUpdate(IEvent* event) {
    const MaBombCase* c = &sMaBombCases[sMaBombCase];

    if ((gGameState != GSTATE_PLAY) || (gPlayer == NULL) || !CoopPlayers_IsCoop() || (gCurrentLevel != LEVEL_MACBETH)) {
        sMaBombHaveLast = 0;
        return;
    }
    if ((s32) gGameFrameCount == sMaBombLastFrame) { // no game frame ran since the last call
        return;
    }
    sMaBombLastFrame = gGameFrameCount;
    switch (sMaBombStage) {
        case MABOMB_STAGE_WAIT_DRIVING:
            if (CoopMaBombProbe_EveryTankDrives()) {
                sMaBombNext = gGameFrameCount + MABOMB_START_DELAY;
                sMaBombStage = MABOMB_STAGE_WAIT_NEXT;
            }
            break;

        case MABOMB_STAGE_WAIT_NEXT:
            if (((s32) gGameFrameCount >= sMaBombNext) && CoopMaBombProbe_EveryTankDrives() &&
                CoopMaBombProbe_CanPlace(c->kind, sMaBombTarget) && !CoopMaBombProbe_Place(c, sMaBombTarget)) {
                sMaBombStage = MABOMB_STAGE_RUNNING;
            } else if (((s32) gGameFrameCount >= (sMaBombNext + MABOMB_WAIT_LOG)) &&
                       (((gGameFrameCount - sMaBombNext) % MABOMB_WAIT_LOG) == 0)) {
                CoopMaBombProbe_ReportWait(c);
            }
            break;

        case MABOMB_STAGE_RUNNING:
            CoopMaBombProbe_Watch(c);
            break;

        case MABOMB_STAGE_DONE:
        default:
            break;
    }
    CoopMaBombProbe_KeepLast();
}

void CoopMaBombProbe_Init(void) {
    const char* path = getenv("SF64_COOP_MABOMB_PROBE");

    if ((path == NULL) || (path[0] == '\0')) {
        return;
    }
    sMaBombFile = fopen(path, "w");
    if (sMaBombFile == NULL) {
        return;
    }
    REGISTER_LISTENER(GamePostUpdateEvent, CoopMaBombProbe_OnGamePostUpdate, EVENT_PRIORITY_NORMAL);
    REGISTER_LISTENER(ObjectUpdateEvent, CoopMaBombProbe_OnObjectUpdate, EVENT_PRIORITY_NORMAL);
}
