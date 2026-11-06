#include "player_trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "port/hooks/Events.h"
#include "port/coop/game/players.h"
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

static void CoopTrace_OnGamePostUpdate(IEvent* event) {
    s32 i;

    if ((sTraceFile == NULL) || (gGameState != GSTATE_PLAY) || (gPlayer == NULL)) {
        return;
    }
    fprintf(sTraceFile, "frame=%d level=%d mode=%d players=%d path=%.3f pathvel=%.3f scroll=%.3f", gGameFrameCount,
            gCurrentLevel, gLevelMode, CoopPlayers_Count(), gPathProgress, gPathVelZ, gPathTexScroll);
    for (i = 0; i < CoopPlayers_Count(); i++) {
        const Player* p = &gPlayer[i];

        fprintf(sTraceFile, " p%d=%d,%d,%.1f,%.1f,%.1f,%d,%d,%.3f,%.1f,%.3f,%.3f,%.1f,%d,%d,%d", i, p->state,
                p->csState, p->pos.x, p->pos.y, p->trueZpos, CoopPlayers_IsParked(i), p->draw, p->zPath, p->camDist,
                p->xPath, p->yPath, p->pathWidth, p->shields, p->somersault, p->form);
    }
    fprintf(sTraceFile, " tframe=%d", sPadFrame);
    for (i = 0; i < COOP_PLAYERS_MAX; i++) {
        fprintf(sTraceFile, " pad%d=%d,%d,%d,%d,%d,%d", i, sHold[i].button, sHold[i].stick_x, sHold[i].stick_y,
                sPress[i].button, sPress[i].stick_x, sPress[i].stick_y);
    }
    CoopTrace_WriteShots();
    CoopTrace_WriteLocks();
    fputc('\n', sTraceFile);
    if ((gGameFrameCount % 60) == 0) {
        fflush(sTraceFile); // tests may kill the game on a timeout
    }
}

void CoopTrace_Init(void) {
    const char* path = getenv("SF64_COOP_TRACE");

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
}
