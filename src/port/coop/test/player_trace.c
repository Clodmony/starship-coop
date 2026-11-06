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
    fprintf(sTraceFile, "frame=%d level=%d mode=%d players=%d", gGameFrameCount, gCurrentLevel, gLevelMode,
            CoopPlayers_Count());
    for (i = 0; i < CoopPlayers_Count(); i++) {
        fprintf(sTraceFile, " p%d=%d,%d,%.1f,%.1f,%.1f,%d,%d", i, gPlayer[i].state, gPlayer[i].csState, gPlayer[i].pos.x,
                gPlayer[i].pos.y, gPlayer[i].trueZpos, CoopPlayers_IsParked(i), gPlayer[i].draw);
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
