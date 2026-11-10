#include "player_trace.h"
#include "effect_probe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "port/hooks/Events.h"
#include "port/coop/game/players.h"
#include "port/coop/game/vehicles.h"
#include "port/coop/test/test_pad.h"

static FILE* sTraceFile = NULL;
static OSContPad sHold[COOP_PLAYERS_MAX];
static OSContPad sPress[COOP_PLAYERS_MAX];
static s32 sPadFrame = -1;

// Game code writes to the pads during a frame, so the trace keeps what every other listener left there before it.
static void CoopTrace_OnGamePreUpdate(IEvent* event) {
    memcpy(sHold, gControllerHold, sizeof(sHold));
    memcpy(sPress, gControllerPress, sizeof(sPress));
    sPadFrame = CoopTestPad_Frame();
}

// The shots of the players that answer to a button after they left: id, owner, timer, and whether a bomb went off.
static void CoopTrace_WriteShots(void) {
    s32 i;
    s32 count = 0;

    fputs(" shots=", sTraceFile);
    for (i = 0; i < ARRAY_COUNT(gPlayerShots); i++) {
        const PlayerShot* shot = &gPlayerShots[i];

        if ((shot->obj.status != SHOT_FREE) &&
            ((shot->obj.id == PLAYERSHOT_BOMB) || (shot->obj.id == PLAYERSHOT_LOCK_SEARCH))) {
            fprintf(sTraceFile, "%s%d:%d:%d:%d", (count++ == 0) ? "" : ";", shot->obj.id, shot->sourceId, shot->timer,
                    shot->unk_5C);
        }
    }
    if (count == 0) {
        fputc('-', sTraceFile);
    }
}

// How many actors each player has locked on to.
static void CoopTrace_WriteLocks(void) {
    s32 locks[COOP_PLAYERS_MAX] = { 0 };
    s32 i;
    s32 j;

    for (i = 0; i < ARRAY_COUNT(gActors); i++) {
        for (j = 0; (gActors[i].obj.status == OBJ_ACTIVE) && (j < COOP_PLAYERS_MAX); j++) {
            locks[j] += gActors[i].lockOnTimers[j] != 0;
        }
    }
    fprintf(sTraceFile, " locks=%d,%d,%d,%d", locks[0], locks[1], locks[2], locks[3]);
}

// The Blue Marine's torpedo, on Aquas only (#16): Aquas_BlueMarineTorpedo fires into the last gPlayerShots slot alone,
// so there is at most one for all marines. Its owner, the owner's lock-on target and whether it homes, its position.
static void CoopTrace_WriteTorpedo(void) {
    const PlayerShot* shot = &gPlayerShots[ARRAY_COUNT(gPlayerShots) - 1];
    s32 target;
    s32 homing;

    if ((gCurrentLevel != LEVEL_AQUAS) || (shot->obj.status != SHOT_ACTIVE) || (shot->obj.id != PLAYERSHOT_LOCK_ON)) {
        return;
    }
    CoopVehicles_MarineLock(shot->sourceId, &target, &homing);
    fprintf(sTraceFile, " torpedo=%d:%d:%d:%.1f,%.1f,%.1f", shot->sourceId, homing, target, shot->obj.pos.x,
            shot->obj.pos.y, shot->obj.pos.z);
}

static s32 sSteerPeriod = 0; // SF64_COOP_TRACE_STEER, see player_trace.h
static s32 sSteerWait = 0;
static s32 sSteerNext = 0;     // the gActors slot to start looking for a wingman at
static s32 sSteerActor = -1;   // the wingman a shot was carried to on this frame
static s32 sSteerShooter = -1; // whose shot that was

// Wingmen at a boss with a hit not read yet, and laser marks new on this frame. A new effect is still OBJ_INIT here,
// until the next Object_Update.
static void CoopTrace_WriteHits(void) {
    const char* sep = " hits=";
    s32 marks = 0;
    s32 i;

    for (i = 0; i < ARRAY_COUNT(gActors); i++) {
        const Actor* actor = &gActors[i];

        if ((actor->obj.status != OBJ_FREE) && (actor->obj.id == OBJ_ACTOR_TEAM_BOSS) && (actor->dmgType != DMG_NONE)) {
            fprintf(sTraceFile, "%s%d:%d:%d:%d:%d", sep, i, actor->aiType, actor->dmgType, actor->dmgSource,
                    (i == sSteerActor) ? sSteerShooter : -1);
            sep = ";";
        }
    }
    sSteerActor = -1;
    for (i = 0; i < ARRAY_COUNT(gEffects); i++) {
        if ((gEffects[i].obj.status == OBJ_INIT) && (gEffects[i].obj.id == OBJ_EFFECT_LASER_MARK_1)) {
            marks++;
        }
    }
    if (marks != 0) {
        fprintf(sTraceFile, " marks=%d", marks);
    }
}

// A wingman at a boss that will read a hit on the next frame: it reads dmgType only with work_048 at 0.
static Actor* CoopTrace_SteerTarget(void) {
    s32 i;

    for (i = 0; i < ARRAY_COUNT(gActors); i++) {
        Actor* actor = &gActors[(sSteerNext + i) % ARRAY_COUNT(gActors)];

        if ((actor->obj.status == OBJ_ACTIVE) && (actor->obj.id == OBJ_ACTOR_TEAM_BOSS) && (actor->work_048 == 0) &&
            (actor->state != 3) && (actor->timer_0C2 == 0) && (actor->dmgType == DMG_NONE)) {
            return actor;
        }
    }
    return NULL;
}

// See SF64_COOP_TRACE_STEER in player_trace.h. The shot moves by its velocity and the wingman about by its own before
// PlayerShot_CollisionCheck, so the shot is put one step short.
static void CoopTrace_OnSteerShot(IEvent* event) {
    Actor* target;
    s32 i;

    if (sSteerWait > 0) {
        sSteerWait--;
        return;
    }
    if ((gGameState != GSTATE_PLAY) || (gRadioState != 0) || ((target = CoopTrace_SteerTarget()) == NULL)) {
        return;
    }
    for (i = 0; i < ARRAY_COUNT(gPlayerShots); i++) {
        PlayerShot* shot = &gPlayerShots[i];

        if ((shot->obj.status == SHOT_ACTIVE) && (shot->sourceId > 0) && (shot->sourceId < COOP_PLAYERS_MAX)) {
            shot->obj.pos.x = target->obj.pos.x + target->vel.x - shot->vel.x;
            shot->obj.pos.y = target->obj.pos.y + target->vel.y - shot->vel.y;
            shot->obj.pos.z = target->obj.pos.z + target->vel.z - shot->vel.z;
            sSteerNext = target->index + 1;
            sSteerWait = sSteerPeriod;
            sSteerActor = target->index;
            sSteerShooter = shot->sourceId;
            return;
        }
    }
}

static s32 sHeldShields[COOP_PLAYERS_MAX]; // 0: not held

static void CoopTrace_OnGamePostUpdate(IEvent* event) {
    s32 i;

    if ((sTraceFile == NULL) || (gGameState != GSTATE_PLAY) || (gPlayer == NULL)) {
        return;
    }
    fprintf(sTraceFile, "frame=%d level=%d mode=%d players=%d path=%.3f pathvel=%.3f scroll=%.3f", gGameFrameCount,
            gCurrentLevel, gLevelMode, CoopPlayers_Count(), gPathProgress, gPathVelZ, gPathTexScroll);
    fprintf(sTraceFile, " lowmsg=%d radio=%d,%d", gTeamLowHealthMsgTimer, gRadioState, gRadioMsgId);
    for (i = 0; i < CoopPlayers_Count(); i++) {
        const Player* p = &gPlayer[i];

        fprintf(sTraceFile, " p%d=%d,%d,%.1f,%.1f,%.1f,%d,%d,%.3f,%.1f,%.3f,%.3f,%.1f,%d,%d,%d", i, p->state,
                p->csState, p->pos.x, p->pos.y, p->trueZpos, CoopPlayers_IsParked(i), p->draw, p->zPath, p->camDist,
                p->xPath, p->yPath, p->pathWidth, p->shields, p->somersault, p->form);
    }
    // Each player's boostSpeed: on the rails the team's sets the speed along the path (#194, #214).
    fprintf(sTraceFile, " boost=%.3f", gPlayer[0].boostSpeed);
    for (i = 1; i < CoopPlayers_Count(); i++) {
        fprintf(sTraceFile, ",%.3f", gPlayer[i].boostSpeed);
    }
    fprintf(sTraceFile, " tframe=%d", sPadFrame);
    for (i = 0; i < COOP_PLAYERS_MAX; i++) {
        fprintf(sTraceFile, " pad%d=%d,%d,%d,%d,%d,%d", i, sHold[i].button, sHold[i].stick_x, sHold[i].stick_y,
                sPress[i].button, sPress[i].stick_x, sPress[i].stick_y);
    }
    CoopTrace_WriteShots();
    CoopTrace_WriteLocks();
    fprintf(sTraceFile, " lasers=%d", gLaserStrength[0]);
    for (i = 1; i < CoopPlayers_Count(); i++) {
        fprintf(sTraceFile, ",%d", gLaserStrength[i]);
    }
    CoopTrace_WriteHits();
    CoopTrace_WriteTorpedo();
    fputc('\n', sTraceFile);
    if ((gGameFrameCount % 60) == 0) {
        fflush(sTraceFile); // tests may kill the game on a timeout
    }
}

// Player_Update sends this event first, so the rest of the update reads the held value.
static void CoopTrace_OnPlayerPreUpdate(IEvent* event) {
    Player* player = ((PlayerPreUpdateEvent*) event)->player;

    if ((player->num >= 0) && (player->num < COOP_PLAYERS_MAX) && (sHeldShields[player->num] > 0)) {
        player->shields = sHeldShields[player->num];
    }
}

static void CoopTrace_HoldShields(const char* text) {
    s32 i;

    for (i = 0; (i < COOP_PLAYERS_MAX) && (text[0] != '\0'); i++) {
        char* end;

        sHeldShields[i] = strtol(text, &end, 10);
        text = (end[0] == ',') ? end + 1 : end;
    }
    REGISTER_LISTENER(PlayerPreUpdateEvent, CoopTrace_OnPlayerPreUpdate, EVENT_PRIORITY_NORMAL);
}

void CoopTrace_Init(void) {
    const char* path = getenv("SF64_COOP_TRACE");
    const char* steer = getenv("SF64_COOP_TRACE_STEER");
    const char* shields = getenv("SF64_COOP_TRACE_SHIELDS");

    CoopEffectProbe_Init();
    if ((path == NULL) || (path[0] == '\0')) {
        return;
    }
    sTraceFile = fopen(path, "w");
    if (sTraceFile == NULL) {
        return;
    }
    setvbuf(sTraceFile, NULL, _IOFBF, 1 << 16);
    REGISTER_LISTENER(GamePreUpdateEvent, CoopTrace_OnGamePreUpdate, EVENT_PRIORITY_HIGH);
    REGISTER_LISTENER(GamePostUpdateEvent, CoopTrace_OnGamePostUpdate, EVENT_PRIORITY_NORMAL);
    if ((steer != NULL) && (atoi(steer) > 0)) {
        sSteerPeriod = atoi(steer);
        REGISTER_LISTENER(GamePreUpdateEvent, CoopTrace_OnSteerShot, EVENT_PRIORITY_NORMAL);
    }
    if ((shields != NULL) && (shields[0] != '\0')) {
        CoopTrace_HoldShields(shields);
    }
}
