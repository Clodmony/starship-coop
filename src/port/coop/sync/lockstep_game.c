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
#include "port/coop/log/coop_log.h"
#include "port/coop/math/atan.h"
#include "port/coop/math/sincos.h"
#include "port/coop/net/protocol.h"
#include "port/hooks/Events.h"
#include "desync.h"
#include "desync_game.h"
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
// A desync names the frame, the level, the players, the regions and the report's path (desync_game.c).
#define SYNC_DESYNC_TEXT_SIZE 1536
// The co-op log (#172): a hold this long is worth a line at the quiet level, and a summary comes this often.
#define SYNC_LOG_HOLD_MS 200
#define SYNC_LOG_SUMMARY_MS 10000

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
static int32_t sWaitPlayer = -1;
static uint32_t sSummaryMs = 0;

// Lockstep cannot go on: the log hears why, then the session's owner.
static void Sync_Fail(const char* text) {
    CoopLog_Write(COOP_LOG_ERROR, false, "%s", text);
    sLink.fail(text);
}

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

// The desync check's messages take the session link as they are: a mute is about input.
static bool Desync_Send(int peer, uint16_t msgId, const void* data, uint16_t size, CoopNetLane lane) {
    return sLink.send(peer, msgId, data, size, lane);
}

static void Desync_Identity(int peer, char* out, size_t cap) {
    out[0] = '\0';
    if (sLink.identity != NULL) {
        sLink.identity(peer, out, cap);
    }
}

static void Sync_Start(void) {
    CoopDesyncGameLink desync = { Desync_Send, Desync_Identity };
    int32_t i;

    CoopLockstep_Start(&sLockstep, &sConfig, SDL_GetTicks());
    for (i = 0; i < COOP_PLAYERS_MAX; i++) {
        CoopInput_SetSource(i, &sSource);
    }
    CoopInput_SetLocalSource(&sSource);
    CoopDesyncGame_Begin(&desync, sConfig.self, sConfig.host, sConfig.present, sConfig.delay);
    sState = SYNC_RUNNING;
    sSummaryMs = SDL_GetTicks();
    CoopLog_Write(COOP_LOG_LOCKSTEP, false, "lockstep started at frame 0: input delay %u frames, players 0x%x, %s",
                  (unsigned) sConfig.delay, (unsigned) sConfig.present, sConfig.host ? "host" : "client");
}

// A desync that is complete ends the session with its text, as a lost player does (WP-23).
static void Sync_CheckDesync(void) {
    char text[SYNC_DESYNC_TEXT_SIZE];

    if ((sState == SYNC_RUNNING) && CoopDesyncGame_Check(SDL_GetTicks(), text, sizeof(text))) {
        Sync_Fail(text);
    }
}

static void Sync_Stalled(int32_t player) {
    char text[SYNC_TEXT_SIZE];

    snprintf(text, sizeof(text), "Player %d stopped sending input, so the session is over.", (int) player + 1);
    CoopLog_Write(COOP_LOG_LOCKSTEP, false, "stall: no input from player %d for %u ms at frame %u", (int) player + 1,
                  (unsigned) (SDL_GetTicks() - sLockstep.progressMs), (unsigned) sLockstep.frame);
    Sync_Fail(text);
}

// True when this frame can run.
static bool Sync_FrameIsReady(void) {
    uint32_t start = SDL_GetTicks();

    while (true) {
        uint32_t now;
        int32_t stalled;

        sLink.pump();
        Sync_CheckDesync();
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
            sWaitPlayer = CoopLockstep_Waiting(&sLockstep);
            CoopLog_Write(COOP_LOG_LOCKSTEP, true, "frame %u waits for player %d", (unsigned) sLockstep.frame,
                          (int) sWaitPlayer + 1);
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
        uint32_t held = SDL_GetTicks() - sWaitSinceMs;

        sHeldMs += held;
        CoopLog_Write(COOP_LOG_LOCKSTEP, held < SYNC_LOG_HOLD_MS, "frame %u held %u ms for player %d",
                      (unsigned) sLockstep.frame, (unsigned) held, (int) sWaitPlayer + 1);
    }
    sWaiting = false;
    return 0;
}

// After the input layer (EVENT_PRIORITY_NORMAL), which read the local controller and this frame's inputs.
static void CoopSyncGame_OnGamePreUpdate(IEvent* event) {
    (void) event;
    if (sState == SYNC_RUNNING) {
        uint32_t now = SDL_GetTicks();

        // The state this frame starts from, hashed before anything of the frame runs, then the frame's input.
        CoopDesyncGame_Frame(sLockstep.frame, now);
        CoopLockstep_Advance(&sLockstep, CoopInput_Controller(), now);
        Sync_CheckDesync();
        if (now - sSummaryMs >= SYNC_LOG_SUMMARY_MS) {
            sSummaryMs = now;
            CoopLog_Write(COOP_LOG_LOCKSTEP, false, "frame %u: %u frames held for %u ms so far",
                          (unsigned) sLockstep.frame, (unsigned) sHeldFrames, (unsigned) sHeldMs);
        }
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
    CoopDesyncGame_Init();
    REGISTER_LISTENER(GamePreUpdateEvent, CoopSyncGame_OnGamePreUpdate, EVENT_PRIORITY_HIGH);
}

// The N64's sine, cosine and arctangent from here until CoopSyncGame_End (decision 0011, #186). Begin is where the menu
// and the test join driver both arm the lockstep, before the level is set up. The line is the same on every platform,
// so two logs side by side show that both games use them.
static void Sync_MathOn(void) {
    uint32_t sinIn = 0x406A927Fu;  // 210 degrees as SIN_DEG hands it to __sinf: the input of the first desync (#186)
    uint32_t atanIn = 0x3F000000u; // 0.5
    uint32_t sinOut, atanOut;
    float x, s, a;

    CoopMath_SetSession(1);
    memcpy(&x, &sinIn, sizeof(x));
    s = CoopMath_Sinf(x);
    memcpy(&sinOut, &s, sizeof(sinOut));
    memcpy(&x, &atanIn, sizeof(x));
    a = CoopMath_FAtanF(x);
    memcpy(&atanOut, &a, sizeof(atanOut));
    CoopLog_Write(COOP_LOG_SESSION, false,
                  "math the N64's sine, cosine and arctangent are on for this session (sin of 0x%08x = 0x%08x, atan of "
                  "0x%08x = 0x%08x)",
                  (unsigned) sinIn, (unsigned) sinOut, (unsigned) atanIn, (unsigned) atanOut);
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
    Sync_MathOn();
}

void CoopSyncGame_Receive(int peer, uint16_t msgId, const uint8_t* data, uint16_t size) {
    // Another game can get to the level first. What it sends before this one is there is dropped, as it repeats it.
    if (sState != SYNC_RUNNING) {
        return;
    }
    if (CoopDesync_IsMessage(msgId)) {
        if (!CoopDesyncGame_Receive(peer, msgId, data, size, SDL_GetTicks())) {
            Sync_Fail("Another player sent a state hash that could not be read, so the session is over.");
        }
        Sync_CheckDesync();
        return;
    }
    if (msgId != COOP_MSG_SYNC_INPUT) {
        return;
    }
    if (!CoopLockstep_Receive(&sLockstep, peer, data, size, SDL_GetTicks())) {
        Sync_Fail("Another player sent input that could not be read, so the session is over.");
    }
}

void CoopSyncGame_End(void) {
    if (sState == SYNC_RUNNING) {
        CoopInput_ClearSources();
        CoopLog_Write(COOP_LOG_LOCKSTEP, false, "lockstep ended at frame %u: %u frames held for %u ms in all",
                      (unsigned) sLockstep.frame, (unsigned) sHeldFrames, (unsigned) sHeldMs);
    }
    CoopDesyncGame_End();
    CoopMath_SetSession(0); // the C library again, as upstream (decision 0011)
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
