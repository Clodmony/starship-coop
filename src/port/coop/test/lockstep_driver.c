// Input for a game in a session, for tests (#20). See lockstep_driver.h.

#include "lockstep_driver.h"

#ifdef USE_NETWORKING

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "global.h"
#include "pilot.h"
#include "port/coop/game/input.h"
#include "port/coop/game/players.h"
#include "port/coop/sync/lockstep_game.h"
#include "port/coop/sync/state_hash.h"
#include "port/hooks/Events.h"

// src/sys/sys_math.c
extern s32 sRandSeed1;
extern s32 sRandSeed2;
extern s32 sRandSeed3;

#define DRIVER_RULE_START 60 // as hash_check.py: input by rule presses A every 30 frames from here on
#define DRIVER_RULE_PERIOD 30
#define DRIVER_ALIVE_PERIOD 120
#define DRIVER_LINE_SIZE 256

typedef struct {
    s16 hold[3];
    s16 press[3];
} FeedPad;

typedef struct {
    FeedPad pad[COOP_PLAYERS_MAX];
} FeedRow;

static FILE* sReport = NULL;
static FILE* sLog = NULL;
static FeedRow* sFeed = NULL;
static s32 sFeedRows = 0;
static bool sPilot = false;
static bool sCleared = false;
static s32 sEndFrame = -1;
static s32 sMuteFrame = -1;
static s32 sPokeFrame = -1;
static s32 sZeroFrame = -1;
static s32 sZeroSeed = 0;
static s32 sSlowEvery = 0;
static s32 sSlowMs = 0;
static s32 sLastFrame = -1;
static s32 sOwnFrame = -1;
static s32 sFramesAfter = 0;
static bool sOverSaid = false;
static bool sStartSaid = false;

static void Report(const char* format, ...) {
    va_list args;

    if (sReport == NULL) {
        return;
    }
    va_start(args, format);
    vfprintf(sReport, format, args);
    va_end(args);
    fputc('\n', sReport);
    fflush(sReport);
}

static void Feed_Load(const char* path) {
    FILE* f = fopen(path, "r");
    char line[DRIVER_LINE_SIZE];
    s32 capacity = 0;

    if (f == NULL) {
        Report("error cannot read the feed");
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        FeedRow row;
        char* at = line;
        long frame = strtol(at, &at, 10);
        s32 i;
        s32 j;

        (void) strtol(at, &at, 10); // game state
        (void) strtol(at, &at, 10); // play state
        memset(&row, 0, sizeof(row));
        for (i = 0; i < COOP_PLAYERS_MAX; i++) {
            for (j = 0; j < 3; j++) {
                row.pad[i].hold[j] = (s16) strtol(at, &at, 10);
            }
            for (j = 0; j < 3; j++) {
                row.pad[i].press[j] = (s16) strtol(at, &at, 10);
            }
        }
        if (frame >= capacity) {
            s32 grown = (capacity != 0) ? capacity * 2 : 4096;

            while (frame >= grown) {
                grown *= 2;
            }
            sFeed = realloc(sFeed, sizeof(FeedRow) * grown);
            memset(&sFeed[capacity], 0, sizeof(FeedRow) * (grown - capacity));
            capacity = grown;
        }
        sFeed[frame] = row;
        if (frame >= sFeedRows) {
            sFeedRows = (s32) frame + 1;
        }
    }
    fclose(f);
}

// What this client's person does now. With a delay of d, a line of the feed has to go in d frames before its own frame.
static void Driver_Pad(s32 frame) {
    s32 self = CoopPlayers_LocalIndex();

    memset(&gControllerHold[0], 0, sizeof(gControllerHold[0]));
    memset(&gControllerPress[0], 0, sizeof(gControllerPress[0]));
    if (sPilot) {
        OSContPad hold[COOP_PLAYERS_MAX];
        OSContPad press[COOP_PLAYERS_MAX];

        memset(hold, 0, sizeof(hold));
        memset(press, 0, sizeof(press));
        if ((frame >= DRIVER_RULE_START) && ((frame % DRIVER_RULE_PERIOD) == 0)) {
            hold[0].button = press[0].button = A_BUTTON;
        }
        CoopPilot_Fly(hold, press);
        gControllerHold[0] = hold[self];
        gControllerPress[0] = press[self];
    } else if (sFeed != NULL) {
        s32 row = frame + (s32) CoopSyncGame_Delay();

        if (row < sFeedRows) {
            const FeedPad* pad = &sFeed[row].pad[self];

            gControllerHold[0].button = (u16) pad->hold[0];
            gControllerHold[0].stick_x = (s8) pad->hold[1];
            gControllerHold[0].stick_y = (s8) pad->hold[2];
            gControllerPress[0].button = (u16) pad->press[0];
            gControllerPress[0].stick_x = (s8) pad->press[1];
            gControllerPress[0].stick_y = (s8) pad->press[2];
        }
    }
}

static void Driver_Log(s32 frame) {
    CoopInputFrame input;
    s32 i;

    if (sLog == NULL) {
        return;
    }
    fprintf(sLog, "%d %d %d", (int) frame, (int) gGameState, (int) gPlayState);
    for (i = 0; i < COOP_PLAYERS_MAX; i++) {
        CoopSyncGame_Read(i, &input);
        fprintf(sLog, " %u %d %d %u %d %d", input.hold.button, input.hold.stick_x, input.hold.stick_y,
                input.press.button, input.press.stick_x, input.press.stick_y);
    }
    fputc('\n', sLog);
    fflush(sLog);
}

// With no lockstep to count frames, the control run of the test counts them from the same place.
static s32 Driver_Frame(void) {
    s32 frame = CoopSyncGame_Frame();

    if (frame >= 0) {
        return frame;
    }
    if ((sOwnFrame < 0) && !CoopSyncGame_IsOn() && CoopPlayers_IsCoop() && (gGameState == GSTATE_PLAY) &&
        (gPlayState == PLAY_UPDATE) && (sLastFrame < 0)) {
        sOwnFrame = 0;
    }
    return (sOwnFrame >= 0) ? sOwnFrame++ : -1;
}

static void Driver_After(void) {
    if (sLastFrame < 0) {
        return;
    }
    if (!sOverSaid) {
        sOverSaid = true;
        Report("over frame=%d", (int) sLastFrame);
    }
    sFramesAfter++;
    if ((sFramesAfter % DRIVER_ALIVE_PERIOD) == 0) {
        Report("alive frames=%d", (int) sFramesAfter);
    }
}

static void CoopLockstepDriver_OnGamePreUpdate(IEvent* event) {
    s32 frame;

    (void) event;
    frame = Driver_Frame();
    if (frame < 0) {
        Driver_After();
        return;
    }
    if (!sStartSaid) {
        sStartSaid = true;
        Report("start");
    }
    if (CoopSyncGame_Frame() >= 0) {
        sLastFrame = frame;
        Driver_Log(frame);
    }
    if (sCleared) {
        gSaveFile.save.data.planet[PLANET_CORNERIA].normalClear = 1;
    }
    Driver_Pad(frame);
    if (frame == sMuteFrame) {
        CoopSyncGame_Mute();
    }
    if (frame == sPokeFrame) {
        CoopStateHash_PokeNow();
    }
    if (frame == sZeroFrame) {
        *((sZeroSeed == 1) ? &sRandSeed1 : (sZeroSeed == 2) ? &sRandSeed2 : &sRandSeed3) = 0;
    }
    if ((sSlowEvery > 0) && ((frame % sSlowEvery) == 0)) {
        SDL_Delay((u32) sSlowMs);
    }
    if (frame == sEndFrame) {
        Report("done frame=%d held=%u held_ms=%u", (int) frame, (unsigned) CoopSyncGame_HeldFrames(),
               (unsigned) CoopSyncGame_HeldMs());
    }
}

static const char* Env_Text(const char* name) {
    const char* value = getenv(name);

    return ((value != NULL) && (value[0] != '\0')) ? value : NULL;
}

static s32 Env_Number(const char* name) {
    const char* value = Env_Text(name);

    return (value != NULL) ? atoi(value) : -1;
}

void CoopLockstepDriver_Init(void) {
    const char* report = Env_Text("SF64_COOP_LOCKSTEP_REPORT");
    const char* log = Env_Text("SF64_COOP_LOCKSTEP_LOG");
    const char* feed = Env_Text("SF64_COOP_LOCKSTEP_FEED");
    const char* slow = Env_Text("SF64_COOP_LOCKSTEP_SLOW");
    const char* zero = Env_Text("SF64_COOP_LOCKSTEP_ZERO_SEED");

    if (report != NULL) {
        sReport = fopen(report, "w");
    }
    if (log != NULL) {
        sLog = fopen(log, "w");
    }
    if (feed != NULL) {
        Feed_Load(feed);
    }
    sPilot = Env_Text("SF64_COOP_LOCKSTEP_PILOT") != NULL;
    sCleared = Env_Text("SF64_COOP_LOCKSTEP_CLEARED") != NULL;
    sEndFrame = Env_Number("SF64_COOP_LOCKSTEP_END");
    sMuteFrame = Env_Number("SF64_COOP_LOCKSTEP_MUTE");
    sPokeFrame = Env_Number("SF64_COOP_LOCKSTEP_POKE");
    if (slow != NULL) {
        sSlowEvery = atoi(slow);
        sSlowMs = (strchr(slow, ':') != NULL) ? atoi(strchr(slow, ':') + 1) : 0;
    }
    if (zero != NULL) {
        sZeroFrame = atoi(zero);
        sZeroSeed = (strchr(zero, ':') != NULL) ? atoi(strchr(zero, ':') + 1) : 1;
    }
    if ((report == NULL) && (log == NULL) && (feed == NULL) && !sPilot) {
        return;
    }
    if (sPilot) {
        CoopPilot_Init();
    }
    // LOW: before the input layer reads port 0. Lockstep's frame is ready by then, as a frame that lacks input never
    // gets this far (CoopSyncGame_HoldFrame).
    REGISTER_LISTENER(GamePreUpdateEvent, CoopLockstepDriver_OnGamePreUpdate, EVENT_PRIORITY_LOW);
}

#else

void CoopLockstepDriver_Init(void) {
}

#endif
