// Lockstep in the game, see lockstep_game.h.

#include "lockstep_game.h"

#ifdef USE_NETWORKING

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "global.h"
#include "port/coop/game/input.h"
#include "port/coop/game/players.h"
#include "port/coop/net/protocol.h"
#include "port/hooks/Events.h"
#include "lockstep.h"

// src/port/Game.cpp
extern void Graphics_PushFrame(Gfx* data);

// A frame that has to wait looks again this often, and hands over to the window after this long: a message that is
// nearly here costs no frame, and a long wait still keeps the window alive.
#define SYNC_WAIT_STEP_MS 1
#define SYNC_WAIT_SLICE_MS 16
// A wait shorter than this says nothing: it is a frame or two.
#define SYNC_WAIT_SHOWN_MS 1000
#define SYNC_TEXT_SIZE 192

typedef enum {
    SYNC_OFF,
    SYNC_ARMED,   // the session started a level, and this game is on its way there
    SYNC_RUNNING, // every frame belongs to lockstep
} SyncState;

static SyncState sState = SYNC_OFF;
static CoopSyncLink sLink;
static CoopLockstep sLockstep;
static CoopLockstepConfig sConfig;
static uint32_t sWaitSinceMs = 0;
static bool sWaiting = false;
static bool sMuted = false;
static bool sNoExchange = false;
static uint32_t sHeldFrames = 0;
static uint32_t sHeldMs = 0;

static int32_t Source_Read(void* user, int32_t player, CoopInputFrame* out) {
    (void) user;
    CoopLockstep_Read(&sLockstep, sLockstep.frame, player, out);
    return 1;
}

static const CoopInputSource sSource = { Source_Read, NULL };

static bool Send(void* user, int peer, const void* data, uint16_t size) {
    (void) user;
    return sMuted || sLink.send(peer, COOP_MSG_SYNC_INPUT, data, size, COOP_NET_UNRELIABLE);
}

static void Sync_Start(void) {
    int32_t i;

    CoopLockstep_Start(&sLockstep, &sConfig, SDL_GetTicks());
    for (i = 0; i < COOP_PLAYERS_MAX; i++) {
        CoopInput_SetSource(i, &sSource);
    }
    CoopInput_SetLocalSource(&sSource);
    sState = SYNC_RUNNING;
}

static void Sync_Stalled(int32_t player) {
    char text[SYNC_TEXT_SIZE];

    snprintf(text, sizeof(text), "Player %d stopped sending input, so the session is over.", (int) player + 1);
    sLink.fail(text);
}

// True when this frame can run.
static bool Sync_FrameIsReady(void) {
    uint32_t start = SDL_GetTicks();

    while (true) {
        uint32_t now;
        int32_t stalled;

        sLink.pump();
        if (sState != SYNC_RUNNING) {
            return true; // the session ended: this game goes on alone
        }
        now = SDL_GetTicks();
        CoopLockstep_Tick(&sLockstep, now);
        if (CoopLockstep_Waiting(&sLockstep) < 0) {
            return true;
        }
        if (!sWaiting) {
            sWaiting = true;
            sWaitSinceMs = now;
        }
        stalled = CoopLockstep_StalledOn(&sLockstep, now);
        if (stalled >= 0) {
            Sync_Stalled(stalled);
            return true;
        }
        if ((now - start) >= SYNC_WAIT_SLICE_MS) {
            return false;
        }
        SDL_Delay(SYNC_WAIT_STEP_MS);
    }
}

int32_t CoopSyncGame_HoldFrame(void) {
    if (sState == SYNC_ARMED) {
        // Play_Init ran on the last frame, and this is the first one in the level.
        if ((gGameState != GSTATE_PLAY) || (gPlayState != PLAY_UPDATE)) {
            return 0;
        }
        Sync_Start();
    }
    if (sState != SYNC_RUNNING) {
        return 0;
    }
    if (!Sync_FrameIsReady()) {
        sHeldFrames++;
        // What the debugger's pause does (Graphics_ThreadUpdate): the last frame again, so the window answers.
        Graphics_PushFrame(gGfxPool->masterDL);
        return 1;
    }
    if (sWaiting) {
        sHeldMs += SDL_GetTicks() - sWaitSinceMs;
    }
    sWaiting = false;
    return 0;
}

// After the input layer (EVENT_PRIORITY_NORMAL), which read the local controller and this frame's inputs.
static void CoopSyncGame_OnGamePreUpdate(IEvent* event) {
    (void) event;
    if (sState == SYNC_RUNNING) {
        CoopLockstep_Advance(&sLockstep, CoopInput_Controller(), SDL_GetTicks());
    }
}

void CoopSyncGame_Init(void) {
    const char* delay = getenv("SF64_COOP_TEST_INPUT_DELAY");

    memset(&sConfig, 0, sizeof(sConfig));
    sConfig.delay = COOP_LOCKSTEP_INPUT_DELAY;
    if ((delay != NULL) && (delay[0] != '\0')) {
        int value = atoi(delay);

        if ((value >= 1) && (value <= COOP_LOCKSTEP_MAX_DELAY)) {
            sConfig.delay = (uint32_t) value;
        }
    }
    REGISTER_LISTENER(GamePreUpdateEvent, CoopSyncGame_OnGamePreUpdate, EVENT_PRIORITY_HIGH);
}

void CoopSyncGame_Begin(const CoopSyncLink* link, const uint8_t* roster, int32_t self, bool host) {
    uint32_t present = 0;
    int32_t players = 0;
    int32_t i;

    for (i = 0; i < COOP_PLAYERS_MAX; i++) {
        if (roster[i] != 0) {
            present |= 1u << i;
            players++;
        }
    }
    CoopPlayers_SetLocalIndex(self);
    if ((players < 2) || sNoExchange) {
        return;
    }
    sLink = *link;
    sConfig.send = Send;
    sConfig.self = self;
    sConfig.host = host;
    sConfig.present = present;
    sWaiting = false;
    sMuted = false;
    sState = SYNC_ARMED;
}

void CoopSyncGame_Receive(int peer, uint16_t msgId, const uint8_t* data, uint16_t size) {
    // Another game can get to the level first. What it sends before this one is there is dropped, as it repeats it.
    if ((msgId != COOP_MSG_SYNC_INPUT) || (sState != SYNC_RUNNING)) {
        return;
    }
    if (!CoopLockstep_Receive(&sLockstep, peer, data, size, SDL_GetTicks())) {
        sLink.fail("Another player sent input that could not be read, so the session is over.");
    }
}

void CoopSyncGame_End(void) {
    if (sState == SYNC_RUNNING) {
        CoopInput_ClearSources();
    }
    sState = SYNC_OFF;
    sWaiting = false;
}

int32_t CoopSyncGame_IsOn(void) {
    return sState != SYNC_OFF;
}

int32_t CoopSyncGame_Frame(void) {
    return (sState == SYNC_RUNNING) ? (int32_t) sLockstep.frame : -1;
}

uint32_t CoopSyncGame_Waiting(int32_t* player) {
    uint32_t waited;

    if ((sState != SYNC_RUNNING) || !sWaiting) {
        return 0;
    }
    waited = SDL_GetTicks() - sWaitSinceMs;
    if (waited < SYNC_WAIT_SHOWN_MS) {
        return 0;
    }
    *player = CoopLockstep_Waiting(&sLockstep);
    return (*player >= 0) ? waited : 0;
}

void CoopSyncGame_Read(int32_t player, void* frame) {
    CoopLockstep_Read(&sLockstep, sLockstep.frame, player, (CoopInputFrame*) frame);
}

uint32_t CoopSyncGame_Delay(void) {
    return sConfig.delay;
}

void CoopSyncGame_Mute(void) {
    sMuted = true;
}

uint32_t CoopSyncGame_HeldFrames(void) {
    return sHeldFrames;
}

uint32_t CoopSyncGame_HeldMs(void) {
    return sHeldMs;
}

void CoopSyncGame_SetOff(bool off) {
    sNoExchange = off;
}

#else

void CoopSyncGame_Init(void) {
}

void CoopSyncGame_Begin(const CoopSyncLink* link, const uint8_t* roster, int32_t self, bool host) {
    (void) link;
    (void) roster;
    (void) self;
    (void) host;
}

void CoopSyncGame_Receive(int peer, uint16_t msgId, const uint8_t* data, uint16_t size) {
    (void) peer;
    (void) msgId;
    (void) data;
    (void) size;
}

void CoopSyncGame_End(void) {
}

int32_t CoopSyncGame_HoldFrame(void) {
    return 0;
}

int32_t CoopSyncGame_IsOn(void) {
    return 0;
}

int32_t CoopSyncGame_Frame(void) {
    return -1;
}

uint32_t CoopSyncGame_Waiting(int32_t* player) {
    (void) player;
    return 0;
}

void CoopSyncGame_Read(int32_t player, void* frame) {
    (void) player;
    (void) frame;
}

uint32_t CoopSyncGame_Delay(void) {
    return 0;
}

void CoopSyncGame_Mute(void) {
}

uint32_t CoopSyncGame_HeldFrames(void) {
    return 0;
}

uint32_t CoopSyncGame_HeldMs(void) {
    return 0;
}

void CoopSyncGame_SetOff(bool off) {
    (void) off;
}

#endif
