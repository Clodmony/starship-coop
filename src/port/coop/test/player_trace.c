#include "player_trace.h"

#include <stdio.h>
#include <stdlib.h>

#include "global.h"
#include "port/hooks/Events.h"
#include "port/coop/game/players.h"

static FILE* sTraceFile = NULL;
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
    const char* shields = getenv("SF64_COOP_TRACE_SHIELDS");

    if ((path == NULL) || (path[0] == '\0')) {
        return;
    }
    sTraceFile = fopen(path, "w");
    if (sTraceFile == NULL) {
        return;
    }
    setvbuf(sTraceFile, NULL, _IOFBF, 1 << 16);
    REGISTER_LISTENER(GamePostUpdateEvent, CoopTrace_OnGamePostUpdate, EVENT_PRIORITY_NORMAL);
    if ((shields != NULL) && (shields[0] != '\0')) {
        CoopTrace_HoldShields(shields);
    }
}
