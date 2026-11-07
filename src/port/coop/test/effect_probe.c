#include "effect_probe.h"

#include <stdio.h>
#include <stdlib.h>

#include "global.h"
#include "port/hooks/Events.h"
#include "port/coop/game/hits.h"
#include "port/coop/game/players.h"
#include "public/bridge/consolevariablebridge.h"

#define PROBE_START_DELAY 60   // frames from every player flying to the first case
#define PROBE_PERIOD 40        // frames from one case to the next
#define PROBE_SETTLE 30        // frames a hit takes to come off the shields
#define PROBE_ROLL_ALPHA 180   // the top of a barrel roll, src/engine/fox_play.c
#define PROBE_CLEAR_GAP 400.0f // from one player to the next: more than any effect here reaches

typedef enum ProbeFlag {
    PROBE_ROLL_TARGET = 1 << 0, // the target is in a barrel roll
    PROBE_ROLL_P0 = 1 << 1,     // player 0 is in a barrel roll, whoever the target is
    PROBE_AT_60 = 1 << 2,       // the update takes its position from unk_60 (Effect399)
    PROBE_BOSS = 1 << 3,        // the update takes its position from gBosses[0] (Effect395, state 10)
    PROBE_OUT = 1 << 4,         // the target is out of the level; run for the last player only
    PROBE_TEXLINE = 1 << 5,     // no effect: a textured line through the target, as TexturedLine_Update tests (#136)
    PROBE_BOLSE = 1 << 6,       // no effect: CoopHits_BolseBeam with a beam through the target (#136)
    PROBE_ANDROSS = 1 << 7,     // no effect: CoopHits_AndrossReach from the target (#136)
} ProbeFlag;

typedef struct ProbeCase {
    const char* name;
    u16 id;
    s16 state;
    s16 unk44;
    u16 timer;
    f32 scale1;
    f32 scale2;
    f32 width; // unk_60.x and y
    f32 yOffset;
    s32 flags;
} ProbeCase;

// Each row is what the effect's update needs to reach its test against the player. Keep the names in step with
// EFFECT_CASES in tools/coop/player_trace.py.
static const ProbeCase sCases[] = {
    // Shots: Effect_Effect353_354_Update and Effect_Effect398_Update test the player themselves.
    { "laser", OBJ_EFFECT_ENEMY_LASER_1, 0, 0, 30, 0.0f, 1.0f, 0.0f, 0.0f, 0 },
    { "laser-roll", OBJ_EFFECT_ENEMY_LASER_1, 0, 0, 30, 0.0f, 1.0f, 0.0f, 0.0f, PROBE_ROLL_TARGET },
    { "shot398", OBJ_EFFECT_398, 1, 0, 30, 0.0f, 1.0f, 0.0f, 0.0f, 0 },
    { "shot398-roll", OBJ_EFFECT_398, 1, 0, 30, 0.0f, 1.0f, 0.0f, 0.0f, PROBE_ROLL_TARGET },
    // The callers of func_effect_8007A774 with a bare gPlayer.
    { "e347", OBJ_EFFECT_347, 0, 100, 30, 5.0f, 5.0f, 0.0f, 0.0f, 0 },
    { "e376", OBJ_EFFECT_376, 0, 0, 30, 0.0f, 1.0f, 0.0f, 0.0f, 0 },
    { "e395", OBJ_EFFECT_395, 0, 0, 30, 0.0f, 1.0f, 0.0f, 0.0f, 0 },
    { "e399", OBJ_EFFECT_399, 0, 0, 30, 0.0f, 1.6f, 0.0f, 0.0f, PROBE_AT_60 },
    { "me369", OBJ_EFFECT_369, 0, 0, 30, 0.0f, 1.0f, 0.0f, 0.0f, 0 },
    { "me370", OBJ_EFFECT_370, 0, 0, 30, 0.0f, 1.0f, 0.0f, 0.0f, 0 },
    { "me371", OBJ_EFFECT_371, 0, 0, 30, 0.0f, 1.0f, 0.0f, 0.0f, 0 },
    { "bo397", OBJ_EFFECT_397, 0, 0, 30, 0.0f, 1.0f, 0.0f, 0.0f, 0 },
    { "bo397-roll", OBJ_EFFECT_397, 0, 0, 30, 0.0f, 1.0f, 0.0f, 0.0f, PROBE_ROLL_TARGET },
    { "bo397-p0-roll", OBJ_EFFECT_397, 0, 0, 30, 0.0f, 1.0f, 0.0f, 0.0f, PROBE_ROLL_P0 },
    // Effects with a test of their own and a bare gPlayer in Player_ApplyDamage.
    { "zo374", OBJ_EFFECT_374, 1, 192, 30, 2.5f, 10.0f, 0.0f, -100.0f, 0 },
    { "gorgon-beam", OBJ_EFFECT_395, 10, 1, 0, 0.0f, 1.0f, 1.0f, 0.0f, PROBE_BOSS },
    // Hits that are not effects (#136). The line runs the real update. The Bolse and Andross cases call the co-op
    // test alone, for want of their boss on Corneria, so player 0 is not hit there.
    { "texline", 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, PROBE_TEXLINE },
    { "bo-beam", 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, PROBE_BOLSE },
    { "and-reach", 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, PROBE_ANDROSS },
    // A player who is out cannot be hit.
    { "out", OBJ_EFFECT_ENEMY_LASER_1, 0, 0, 30, 0.0f, 1.0f, 0.0f, 0.0f, PROBE_OUT },
};

typedef enum ProbeStage {
    PROBE_STAGE_WAIT_FLYING,
    PROBE_STAGE_WAIT_NEXT,
    PROBE_STAGE_RUNNING,
    PROBE_STAGE_SETTLING,
    PROBE_STAGE_DONE,
} ProbeStage;

static FILE* sFile = NULL;
static ProbeStage sStage = PROBE_STAGE_WAIT_FLYING;
static s32 sCase = 0;
static s32 sTarget = 0;
static s32 sNextFrame = 0;
static s32 sHit = 0;
static s32 sDeflect = 0;
static s32 sDamage[COOP_PLAYERS_MAX];
static s32 sDeflectBefore[COOP_PLAYERS_MAX];
static Vec3f sBossPos;
static Vec3f sBossRot;
static s32 sBossStatus;
static Effect* sEffect = NULL;

static s32 EveryPlayerFlies(void) {
    s32 i;

    for (i = 0; i < CoopPlayers_Count(); i++) {
        if (CoopPlayers_IsParked(i) || (gPlayer[i].state != PLAYERSTATE_ACTIVE)) {
            return 0;
        }
    }
    return 1;
}

// Nonzero when a case may run. Nobody flies in the cut to all-range or in a U-turn, and that cut parks all but player
// 0, who then rightly cannot be hit. A run that jumps to all-range waits for it, so that no case runs on rails instead.
static s32 CanRunCase(void) {
    if ((CVarGetInteger("gDebugJumpToAllRange", 0) != 0) && (gLevelMode != LEVELMODE_ALL_RANGE)) {
        return 0;
    }
    return EveryPlayerFlies();
}

static void ClearHits(void) {
    s32 i;

    for (i = 0; i < CoopPlayers_Count(); i++) {
        Player* p = &gPlayer[i];

        p->shields = Play_GetMaxShields();
        p->damage = 0;
        p->hitTimer = 0;
        p->mercyTimer = 0;
        sDeflectBefore[i] = p->deflectCount;
    }
}

static void HealPlayers(void) {
    s32 i;

    ClearHits();
    for (i = 0; i < CoopPlayers_Count(); i++) {
        gPlayer[i].barrelRollAlpha = 0;
    }
}

// The effect is next, in the last free slot, so all that updates before it has run. A hit up to here is not the one
// this test asks about: scenery hits a player the probe moved, and the mercy that leaves would shield it from the
// effect.
static void CoopEffectProbe_OnObjectUpdate(ObjectUpdateEvent* event) {
    if ((event->type == OBJECT_TYPE_EFFECT) && (event->object == sEffect)) {
        sEffect = NULL;
        ClearHits();
    }
}

// Moves everyone but player 0 out of each other's reach, towards the middle of the flight box. They drift close by
// themselves, and a hit on one of them would then be right: Effect374's column reaches a player above the target.
// Player 0 stays, because the camera follows it: on a jump of the camera Effect374's draw empties the interpolation
// path and the game aborts (docs/coop/NOTES.md, #100).
static void SpreadPlayers(void) {
    f32 step = (gPlayer[0].pos.x > 0.0f) ? -PROBE_CLEAR_GAP : PROBE_CLEAR_GAP;
    s32 i;

    for (i = 1; i < CoopPlayers_Count(); i++) {
        gPlayer[i].pos.x = gPlayer[0].pos.x + (step * i);
    }
}

static Effect* FreeEffect(void) {
    s32 i;

    for (i = ARRAY_COUNT(gEffects) - 1; i > 0; i--) {
        if (gEffects[i].obj.status == OBJ_FREE) {
            break;
        }
    }
    return &gEffects[i];
}

#define PROBE_NOT_EFFECT (PROBE_TEXLINE | PROBE_BOLSE | PROBE_ANDROSS)

static s32 sLine = -1;
static s32 sReadDone = 0; // the case was read where it ran (CallTest)

static void Snapshot(void);

// A line through where the target will be, along z, as an event script's laser (EVOP_MAKE_TEXLINE) would be.
static void PlaceLine(const Player* p) {
    TexturedLine* line;
    s32 i;

    for (i = ARRAY_COUNT(gTexturedLines) - 1; (i > 0) && (gTexturedLines[i].mode != 0); i--) {}
    sLine = i;
    line = &gTexturedLines[i];
    line->mode = 1;
    line->timer = 0;
    line->xyScale = 3.0f;
    line->prim.r = line->prim.a = 255;
    line->prim.g = line->prim.b = 0;
    line->posAA.x = line->posBB.x = p->pos.x + p->vel.x;
    line->posAA.y = line->posBB.y = p->pos.y + p->vel.y;
    line->posAA.z = p->trueZpos + p->vel.z + 100.0f;
    line->posBB.z = line->posAA.z - 200.0f;
}

// The co-op tests of #136 that need their boss, called right here with the target inside them, and read at once:
// by the next frame scenery can have hit someone (it hit both players on Corneria's rails, frame 3208).
static void CallTest(const ProbeCase* c, s32 target) {
    static Boss base; // only obj.pos is read
    Player* p = &gPlayer[target];
    f32 y[COOP_PLAYERS_MAX];
    s32 i;

    ClearHits();
    if (c->flags & PROBE_BOLSE) {
        base.obj.pos.x = p->pos.x;
        base.obj.pos.y = p->pos.y - 580.0f;
        base.obj.pos.z = p->trueZpos - 10.0f;
        CoopHits_BolseBeam(&base, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f);
        Snapshot();
        return;
    }
    // Andross reaches 700 in x and y, more than the spread: take the others away for the call.
    for (i = 0; i < CoopPlayers_Count(); i++) {
        y[i] = gPlayer[i].pos.y;
        if (i != target) {
            gPlayer[i].pos.y += 5000.0f;
        }
    }
    CoopHits_AndrossReach(p->pos.x, p->pos.y);
    for (i = 0; i < CoopPlayers_Count(); i++) {
        gPlayer[i].pos.y = y[i];
    }
    Snapshot();
}

static void Place(const ProbeCase* c, s32 target) {
    Player* p = &gPlayer[target];
    Effect* e;

    HealPlayers();
    SpreadPlayers();
    if (c->flags & PROBE_TEXLINE) {
        PlaceLine(p);
        return;
    }
    if (c->flags & (PROBE_BOLSE | PROBE_ANDROSS)) {
        CallTest(c, target);
        return;
    }
    e = FreeEffect();
    if (c->flags & PROBE_OUT) {
        // Out players are not updated, so bring this one along to where the level is now.
        CoopPlayers_OnPlayerDown(p);
        p->pos.z = gPlayer[0].pos.z;
        p->trueZpos = gPlayer[0].trueZpos;
        p->vel = gPlayer[0].vel;
    }
    if (c->flags & PROBE_ROLL_TARGET) {
        p->barrelRollAlpha = PROBE_ROLL_ALPHA;
    }
    if (c->flags & PROBE_ROLL_P0) {
        gPlayer[0].barrelRollAlpha = PROBE_ROLL_ALPHA;
    }

    sEffect = e;
    Effect_Initialize(e);
    e->obj.status = OBJ_ACTIVE;
    e->obj.id = c->id;
    Object_SetInfo(&e->info, c->id);
    e->state = c->state;
    e->unk_44 = c->unk44;
    e->timer_50 = c->timer;
    e->scale1 = c->scale1;
    e->scale2 = c->scale2;
    e->unk_60.x = e->unk_60.y = c->width;
    // Players move before objects, so aim for where this one will be.
    e->obj.pos.x = p->pos.x + p->vel.x;
    e->obj.pos.y = p->pos.y + p->vel.y + c->yOffset;
    e->obj.pos.z = p->trueZpos + p->vel.z;
    if (c->flags & PROBE_AT_60) {
        e->unk_60.x = e->obj.pos.x;
        e->unk_60.y = e->obj.pos.y;
    }
    if (c->flags & PROBE_BOSS) {
        sBossPos = gBosses[0].obj.pos;
        sBossRot = gBosses[0].obj.rot;
        sBossStatus = gBosses[0].obj.status;
        gBosses[0].obj.status = OBJ_FREE; // in all-range a live boss would ram whoever is here
        gBosses[0].obj.pos = e->obj.pos;
        gBosses[0].obj.rot.x = gBosses[0].obj.rot.y = gBosses[0].obj.rot.z = 0.0f;
    }
}

// Who is hit or deflected now.
static void Snapshot(void) {
    s32 i;

    sHit = 0;
    sDeflect = 0;
    for (i = 0; i < CoopPlayers_Count(); i++) {
        Player* p = &gPlayer[i];

        if (p->hitTimer != 0) {
            sHit |= 1 << i;
        }
        if (p->deflectCount != sDeflectBefore[i]) {
            sDeflect |= 1 << i;
        }
        sDamage[i] = p->damage;
    }
    sReadDone = 1;
}

// The effect had its frame. Take it and whatever it spawned out, so that only this frame counts.
static void Read(const ProbeCase* c) {
    s32 i;

    if (!sReadDone) {
        Snapshot();
    }
    sReadDone = 0;
    for (i = 0; i < CoopPlayers_Count(); i++) {
        gPlayer[i].barrelRollAlpha = 0;
    }
    if (sLine >= 0) {
        gTexturedLines[sLine].mode = 0;
        sLine = -1;
    }
    for (i = 0; (i < ARRAY_COUNT(gEffects)) && !(c->flags & PROBE_NOT_EFFECT); i++) {
        if (gEffects[i].obj.id == c->id) {
            Object_Kill(&gEffects[i].obj, gEffects[i].sfxSource);
        }
    }
    if (c->flags & PROBE_BOSS) {
        gBosses[0].obj.pos = sBossPos;
        gBosses[0].obj.rot = sBossRot;
        gBosses[0].obj.status = sBossStatus;
    }
}

static void Report(const ProbeCase* c) {
    s32 i;

    fprintf(sFile, "effect frame=%d case=%s target=%d hit=%d deflect=%d damage=", gGameFrameCount, c->name, sTarget,
            sHit, sDeflect);
    for (i = 0; i < CoopPlayers_Count(); i++) {
        fprintf(sFile, "%s%d", (i != 0) ? "," : "", sDamage[i]);
    }
    fprintf(sFile, " shields=");
    for (i = 0; i < CoopPlayers_Count(); i++) {
        fprintf(sFile, "%s%d", (i != 0) ? "," : "", gPlayer[i].shields);
    }
    fputc('\n', sFile);
    fflush(sFile); // tests may kill the game on a timeout
}

// The next case and target, or nonzero when there is none left.
static s32 Advance(void) {
    s32 last = CoopPlayers_Count() - 1;

    if (sTarget < last) {
        sTarget++;
        return 0;
    }
    sCase++;
    if (sCase >= ARRAY_COUNT(sCases)) {
        return 1;
    }
    sTarget = (sCases[sCase].flags & PROBE_OUT) ? last : 0;
    return 0;
}

static void CoopEffectProbe_OnGamePostUpdate(IEvent* event) {
    const ProbeCase* c = &sCases[sCase];

    if ((gGameState != GSTATE_PLAY) || (gPlayer == NULL) || !CoopPlayers_IsCoop()) {
        return;
    }
    switch (sStage) {
        case PROBE_STAGE_WAIT_FLYING:
            if (CanRunCase()) {
                sNextFrame = gGameFrameCount + PROBE_START_DELAY;
                sStage = PROBE_STAGE_WAIT_NEXT;
            }
            break;

        case PROBE_STAGE_WAIT_NEXT:
            if ((gGameFrameCount >= sNextFrame) && CanRunCase()) {
                Place(c, sTarget);
                sStage = PROBE_STAGE_RUNNING;
            }
            break;

        case PROBE_STAGE_RUNNING:
            Read(c);
            sNextFrame = gGameFrameCount + PROBE_SETTLE;
            sStage = PROBE_STAGE_SETTLING;
            break;

        case PROBE_STAGE_SETTLING:
            if (gGameFrameCount >= sNextFrame) {
                Report(c);
                sNextFrame = gGameFrameCount + PROBE_PERIOD - PROBE_SETTLE;
                if (Advance()) {
                    fprintf(sFile, "effect done\n");
                    fflush(sFile);
                    sStage = PROBE_STAGE_DONE;
                } else {
                    sStage = PROBE_STAGE_WAIT_NEXT;
                }
            }
            break;

        case PROBE_STAGE_DONE:
        default:
            break;
    }
}

void CoopEffectProbe_Init(void) {
    const char* path = getenv("SF64_COOP_EFFECT_PROBE");

    if ((path == NULL) || (path[0] == '\0')) {
        return;
    }
    sFile = fopen(path, "w");
    if (sFile == NULL) {
        return;
    }
    REGISTER_LISTENER(GamePostUpdateEvent, CoopEffectProbe_OnGamePostUpdate, EVENT_PRIORITY_NORMAL);
    REGISTER_LISTENER(ObjectUpdateEvent, CoopEffectProbe_OnObjectUpdate, EVENT_PRIORITY_NORMAL);
}
