#include "audio_trace.h"

#include <stdio.h>
#include <stdlib.h>

#include "global.h"
#include "sf64audio_provisional.h"

#include "port/coop/sync/lockstep_game.h"
#include "port/hooks/Events.h"

// The game thread's side of an audio reset (src/audio/audio_general.c) and the frames since boot (src/sys/sys_main.c).
extern u8 sAudioResetStatus;
extern u8 sAudioSpecId;
extern uint32_t gSysFrameCount;

static FILE* sFile = NULL;

static void AudioTrace_Fields(void) {
    fprintf(sFile, "sys=%u state=%d lock=%d step=%d status=%d voice=%d spec=%d/%d\n", (unsigned) gSysFrameCount,
            (int) gGameState, (int) CoopSyncGame_Frame(), (int) gAudioResetStep, (int) sAudioResetStatus,
            (int) gSeqPlayers[SEQ_PLAYER_VOICE].enabled, (int) sAudioSpecId, (int) gAudioSpecId);
}

static void AudioTrace_OnGamePostUpdate(IEvent* event) {
    (void) event;
    fputs("audio ", sFile);
    AudioTrace_Fields();
    fflush(sFile); // tests stop the game from outside
}

void CoopAudioTrace_Enter(int level, int run) {
    if (sFile == NULL) {
        return;
    }
    fprintf(sFile, "enter level=%d run=%d ", level, run);
    AudioTrace_Fields();
}

void CoopAudioTrace_Init(void) {
    const char* path = getenv("SF64_COOP_AUDIO_TRACE");

    if ((path == NULL) || (path[0] == '\0')) {
        return;
    }
    sFile = fopen(path, "a");
    if (sFile != NULL) {
        REGISTER_LISTENER(GamePostUpdateEvent, AudioTrace_OnGamePostUpdate, EVENT_PRIORITY_NORMAL);
    }
}
