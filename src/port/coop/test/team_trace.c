#include "team_trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "port/hooks/Events.h"
#include "port/coop/game/players.h"
#include "port/coop/game/team.h"

#define TEAM_TRACE_HEARTBEAT 300

// What a line shows, compared as a whole to find out whether there is a new line to write.
typedef struct {
    s32 gameState;
    s32 level;
    s32 phase;
    s32 playState;
    s32 playerState;
    s32 mission;
    s32 team[3];
    s32 saved[3];
    s32 prev[3];
    s32 map[3];
    s32 ships[3];
    s32 p1;          // player 1's shields, -1 without a player 1 in co-op
    u8 scenes[16];   // CoopTeam_Choices: who flies each scene of a human-flown wingman (#15)
    s32 ai[3];       // all-range and boss-fight wingmen flying on their own AI, per team (#15)
    s32 body[3];     // per team: 0 no body, 1 a body on its human, 2 a body away, 3 a body elsewhere (#15)
    s32 shown[3];    // CoopTeam_DisplayShields: what the wingmen's bars draw in the status rows and the radio
    s32 radio[2];    // the last radio line: gRadioMsgId and gRadioMsgRadioId (#15, decision 0013)
    s32 guests[2];   // event actors flying as Katt and Bill in their own ships (#15, decision 0012 B4)
} TeamTraceLine;

static FILE* sTraceFile = NULL;
static TeamTraceLine sLast;
static bool sWroteLine = false;
static bool sStarted = false;
static u32 sFrame = 0;

static bool sPokeWaiting = false;
static s32 sPokeLevel;
static s32 sPokeFrames;
static s32 sPokeId;
static s32 sPokeValue;
static s32 sPokeId2 = 0; // an optional second id and value in the same poke, 0 for none (#15, the guests)
static s32 sPokeValue2;
static s32 sPokeActiveFrames = 0;

static bool sDownWaiting = false; // SF64_COOP_TEAM_DOWN, team_trace.h
static s32 sDownLevel;
static s32 sDownFrames;
static s32 sDownPlayer;
static s32 sDownActiveFrames = 0;

static bool sRingsWaiting = false; // SF64_COOP_TEAM_RINGS, team_trace.h
static s32 sRingsLevel;
static s32 sRingsFrames;
static s32 sRingsPlayer;
static s32 sRingsShields;
static s32 sRingsActiveFrames = 0;

static void CoopTeamTrace_Read(TeamTraceLine* line) {
    s32 i;

    memset(line, 0, sizeof(*line));
    line->gameState = gGameState;
    line->level = gCurrentLevel;
    line->phase = gLevelPhase;
    line->playState = gPlayState;
    line->playerState = ((gGameState == GSTATE_PLAY) && (gPlayer != NULL)) ? (s32) gPlayer[0].state : -1;
    line->mission = gMissionStatus;
    for (i = 0; i < 3; i++) {
        line->team[i] = gTeamShields[TEAM_ID_FALCO + i];
        line->saved[i] = gSavedTeamShields[TEAM_ID_FALCO + i];
        line->prev[i] = gPrevPlanetTeamShields[TEAM_ID_FALCO + i];
        line->map[i] = D_ctx_80177C58[TEAM_ID_FALCO + i];
    }
    {
        s32 count;
        const u8* choices = CoopTeam_Choices(&count);

        memcpy(line->scenes, choices, MIN(count, (s32) sizeof(line->scenes)));
    }
    line->radio[0] = gRadioMsgId;
    line->radio[1] = (s32) gRadioMsgRadioId;
    line->p1 = -1;
    if (gGameState != GSTATE_PLAY) {
        return;
    }
    if ((gPlayer != NULL) && CoopPlayers_IsCoop() && (CoopPlayers_Count() > 1) && !CoopPlayers_IsParked(1)) {
        line->p1 = gPlayer[1].shields;
    }
    for (i = 0; i < 3; i++) {
        line->shown[i] = CoopTeam_DisplayShields(TEAM_ID_FALCO + i);
    }
    for (i = 0; i < ARRAY_COUNT(gActors); i++) {
        const Actor* actor = &gActors[i];

        if ((actor->obj.status == OBJ_ACTIVE) && (actor->obj.id == OBJ_ACTOR_EVENT) &&
            (actor->iwork[12] >= TEAM_ID_FALCO) && (actor->iwork[12] <= TEAM_ID_PEPPY)) {
            line->ships[actor->iwork[12] - TEAM_ID_FALCO]++;
        }
        if ((actor->obj.status == OBJ_ACTIVE) && (actor->obj.id == OBJ_ACTOR_EVENT) &&
            (((actor->iwork[12] == TEAM_ID_KATT) && (actor->eventType == EVID_KATT)) ||
             ((actor->iwork[12] == TEAM_ID_BILL) && (actor->eventType == EVID_BILL)))) {
            line->guests[actor->iwork[12] - TEAM_ID_KATT]++;
        }
        if ((actor->obj.status == OBJ_ACTIVE) &&
            ((actor->obj.id == OBJ_ACTOR_ALLRANGE) || (actor->obj.id == OBJ_ACTOR_TEAM_BOSS)) &&
            (actor->aiType >= AI360_FALCO) && (actor->aiType <= AI360_PEPPY)) {
            s32 team = actor->aiType - AI360_FALCO;
            s32 body = CoopTeam_Body(actor);

            if (body == COOP_TEAM_BODY_NONE) {
                line->ai[team]++;
            } else {
                line->body[team] = body;
            }
        }
    }
}

static void CoopTeamTrace_Write(const TeamTraceLine* l) {
    s32 i;

    fprintf(sTraceFile,
            "frame=%u gs=%d level=%d phase=%d ps=%d p0=%d mission=%d path=%.1f team=%d,%d,%d saved=%d,%d,%d "
            "prev=%d,%d,%d map=%d,%d,%d ships=%d,%d,%d p1=%d scenes=",
            sFrame, l->gameState, l->level, l->phase, l->playState, l->playerState, l->mission, gPathProgress,
            l->team[0], l->team[1], l->team[2], l->saved[0], l->saved[1], l->saved[2], l->prev[0], l->prev[1],
            l->prev[2], l->map[0], l->map[1], l->map[2], l->ships[0], l->ships[1], l->ships[2], l->p1);
    for (i = 0; i < ARRAY_COUNT(l->scenes); i++) {
        fprintf(sTraceFile, "%s%d", (i > 0) ? "," : "", l->scenes[i]);
    }
    fprintf(sTraceFile, " ai=%d,%d,%d body=%d,%d,%d shown=%d,%d,%d radio=%d,%d guests=%d,%d\n", l->ai[0], l->ai[1],
            l->ai[2], l->body[0], l->body[1], l->body[2], l->shown[0], l->shown[1], l->shown[2], l->radio[0],
            l->radio[1], l->guests[0], l->guests[1]);
    fflush(sTraceFile); // tests stop the game from outside, and there are few lines
}

static void CoopTeamTrace_UpdatePoke(void) {
    if (!sPokeWaiting || (gGameState != GSTATE_PLAY) || (gCurrentLevel != sPokeLevel) || (gPlayer == NULL) ||
        (gPlayer[0].state != PLAYERSTATE_ACTIVE)) {
        return;
    }
    if (sPokeActiveFrames++ < sPokeFrames) {
        return;
    }
    gTeamShields[sPokeId] = sPokeValue;
    if (sPokeId2 != 0) {
        gTeamShields[sPokeId2] = sPokeValue2;
    }
    sPokeWaiting = false;
    if (sTraceFile != NULL) {
        fprintf(sTraceFile, "# poke frame=%u id=%d value=%d\n", sFrame, sPokeId, sPokeValue);
    }
}

static void CoopTeamTrace_UpdateDown(void) {
    if (!sDownWaiting || (gGameState != GSTATE_PLAY) || (gCurrentLevel != sDownLevel) || (gPlayer == NULL) ||
        (gPlayer[0].state != PLAYERSTATE_ACTIVE) || (sDownPlayer >= CoopPlayers_Count())) {
        return;
    }
    if (sDownActiveFrames++ < sDownFrames) {
        return;
    }
    // Shields at 0 alone do not take a player down: Player_Update asks for a hit on that frame too, as
    // radioDamageTimer (src/engine/fox_play.c:5736), which Player_ApplyDamage sets to 4 (:998).
    gPlayer[sDownPlayer].shields = 0;
    gPlayer[sDownPlayer].radioDamageTimer = 4;
    sDownWaiting = false;
    if (sTraceFile != NULL) {
        fprintf(sTraceFile, "# down frame=%u player=%d\n", sFrame, sDownPlayer);
    }
}

static void CoopTeamTrace_UpdateRings(void) {
    if (!sRingsWaiting || (gGameState != GSTATE_PLAY) || (gCurrentLevel != sRingsLevel) || (gPlayer == NULL) ||
        (gPlayer[0].state != PLAYERSTATE_ACTIVE) || (sRingsPlayer >= CoopPlayers_Count())) {
        return;
    }
    if (sRingsActiveFrames++ < sRingsFrames) {
        return;
    }
    gGoldRingCount[0] = 3; // the team's shield upgrade: Play_GetMaxShields is 383 from here on
    gPlayer[sRingsPlayer].shields = sRingsShields;
    sRingsWaiting = false;
    if (sTraceFile != NULL) {
        fprintf(sTraceFile, "# rings frame=%u player=%d shields=%d\n", sFrame, sRingsPlayer, sRingsShields);
    }
}

// Counts as a replay does: on this event, from GSTATE_START (src/port/coop/test/replay.c). GamePostUpdateEvent cannot
// count, the game skips it on some frames.
static void CoopTeamTrace_OnGamePreUpdate(IEvent* event) {
    if (sStarted) {
        sFrame++;
    } else if (gGameState == GSTATE_START) {
        sStarted = true;
    }
}

static void CoopTeamTrace_OnGamePostUpdate(IEvent* event) {
    TeamTraceLine line;

    if (!sStarted) {
        return;
    }
    CoopTeamTrace_UpdatePoke();
    CoopTeamTrace_UpdateDown();
    CoopTeamTrace_UpdateRings();
    if (sTraceFile == NULL) {
        return;
    }
    CoopTeamTrace_Read(&line);
    if (!sWroteLine || (memcmp(&line, &sLast, sizeof(line)) != 0) || ((sFrame % TEAM_TRACE_HEARTBEAT) == 0)) {
        CoopTeamTrace_Write(&line);
        sLast = line;
        sWroteLine = true;
    }
}

void CoopTeamTrace_Init(void) {
    const char* path = getenv("SF64_COOP_TEAM_TRACE");
    const char* poke = getenv("SF64_COOP_TEAM_POKE");
    const char* down = getenv("SF64_COOP_TEAM_DOWN");
    const char* rings = getenv("SF64_COOP_TEAM_RINGS");

    if ((poke != NULL) &&
        (sscanf(poke, "%d,%d,%d,%d,%d,%d", &sPokeLevel, &sPokeFrames, &sPokeId, &sPokeValue, &sPokeId2, &sPokeValue2) >=
         4) &&
        (sPokeId >= TEAM_ID_FALCO) && (sPokeId <= TEAM_ID_PEPPY) && (sPokeId2 >= 0) && (sPokeId2 <= TEAM_ID_PEPPY)) {
        sPokeWaiting = true;
    }
    if ((path != NULL) && (path[0] != '\0')) {
        sTraceFile = fopen(path, "w");
    }
    if ((down != NULL) && (sscanf(down, "%d,%d,%d", &sDownLevel, &sDownFrames, &sDownPlayer) == 3) &&
        (sDownPlayer >= 1) && (sDownPlayer < COOP_PLAYERS_MAX)) {
        sDownWaiting = true;
    }
    if ((rings != NULL) &&
        (sscanf(rings, "%d,%d,%d,%d", &sRingsLevel, &sRingsFrames, &sRingsPlayer, &sRingsShields) == 4) &&
        (sRingsPlayer >= 1) && (sRingsPlayer < COOP_PLAYERS_MAX) && (sRingsShields > 0)) {
        sRingsWaiting = true;
    }
    if (sPokeWaiting || sDownWaiting || sRingsWaiting || (sTraceFile != NULL)) {
        REGISTER_LISTENER(GamePreUpdateEvent, CoopTeamTrace_OnGamePreUpdate, EVENT_PRIORITY_NORMAL);
        REGISTER_LISTENER(GamePostUpdateEvent, CoopTeamTrace_OnGamePostUpdate, EVENT_PRIORITY_NORMAL);
    }
}
