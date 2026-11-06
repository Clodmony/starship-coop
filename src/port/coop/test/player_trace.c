#include "player_trace.h"

#include <stdio.h>
#include <stdlib.h>

#include "global.h"
#include "port/hooks/Events.h"
#include "port/coop/game/players.h"

static FILE* sTraceFile = NULL;

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
    REGISTER_LISTENER(GamePostUpdateEvent, CoopTrace_OnGamePostUpdate, EVENT_PRIORITY_NORMAL);
}
