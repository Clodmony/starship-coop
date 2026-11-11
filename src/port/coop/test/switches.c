// Test switches for the determinism spike (WP-02). See switches.h.

#include "switches.h"
#include "start_level.h"
#include "team_trace.h"
#include "aim_trace.h"
#include "boss_trace.h"
#include "score_probe.h"
#include "smoke_trace.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <libultra/gbi.h>
#include <libultraship/luslog.h>

#include "port/coop/game/players.h"
#include "port/coop/sync/lockstep_game.h"
#include "port/hooks/Events.h"

// Sample counts per audio update, chosen each update from the output device's fill level
// (src/port/Engine.cpp:363-364, :387-388).
extern unsigned short samples_high;
extern unsigned short samples_low;

// The frames the game has ticked since boot (src/sys/sys_main.c). It is not simulation state: two clients of one
// session started their games at different times, so they do not share it.
extern uint32_t gSysFrameCount;

// SF64_COOP_TEST_SPEED=max: the limiter then waits under a microsecond a frame, which is no limit, and at 60 frames a
// second the rate still fits an int.
#define SPEED_NO_LIMIT 100000

static int sSpeed = 1;
static int sMostDraws = 0;
static int sNoDraw = 0;
static Gfx sNothingToDraw[] = { gsSPEndDisplayList() };

// SF64_COOP_TEST_SYS_FRAME_OFFSET: on the first game frame, gSysFrameCount jumps ahead by this many, as if this client
// had booted earlier than the others (#129). An even number: the graphics pool alternates on gSysFrameCount % 2
// (Graphics_InitializeTask), and a change of parity in the middle of a frame would hand a frame the pool in use.
static uint32_t sSysFrameOffset = 0;

static void CoopTestSwitches_OnGamePreUpdate(IEvent* event) {
    (void) event;
    gSysFrameCount += sSysFrameOffset;
    sSysFrameOffset = 0;
}

int CoopTestSwitches_Speed(void) {
    return sSpeed;
}

Gfx* CoopTestSwitches_ListToDraw(Gfx* commands) {
    return sNoDraw ? sNothingToDraw : commands;
}

void CoopTestSwitches_NoteDraws(int draws) {
    if (!sNoDraw && (draws > sMostDraws)) {
        sMostDraws = draws;
    }
}

int CoopTestSwitches_TakeMostDraws(void) {
    int most = sMostDraws;

    sMostDraws = 0;
    return most;
}

static int CoopTestSwitches_IsSet(const char* value) {
    return (value != NULL) && (value[0] != '\0') && (strcmp(value, "0") != 0);
}

void CoopTestSwitches_StartSeeds(int* values) {
    const char* seeds = getenv("SF64_COOP_TEST_START_SEEDS");
    char* at = (char*) seeds;
    int i;

    if (!CoopTestSwitches_IsSet(seeds)) {
        return;
    }
    for (i = 0; i < 6; i++) {
        values[i] = (int) strtol(at, &at, 10);
        if (*at == ',') {
            at++;
        }
    }
}

void CoopTestSwitches_Init(void) {
    const char* speed = getenv("SF64_COOP_TEST_SPEED");
    const char* noFma3 = getenv("SF64_COOP_TEST_NO_FMA3");
    const char* audioSamples = getenv("SF64_COOP_TEST_AUDIO_SAMPLES");
    const char* localIndex = getenv("SF64_COOP_TEST_LOCAL_INDEX");
    const char* noDraw = getenv("SF64_COOP_TEST_NO_DRAW");
    const char* noLockstep = getenv("SF64_COOP_TEST_NO_LOCKSTEP");
    const char* sysFrameOffset = getenv("SF64_COOP_TEST_SYS_FRAME_OFFSET");

    if (CoopTestSwitches_IsSet(noDraw)) {
        sNoDraw = 1;
        LUSLOG_INFO("coop: test no draw%s", "");
    }

    if (CoopTestSwitches_IsSet(speed)) {
        sSpeed = (strcmp(speed, "max") == 0) ? SPEED_NO_LIMIT : atoi(speed);
        if (sSpeed > 1) {
            LUSLOG_INFO("coop: test speed %s", speed);
        } else {
            sSpeed = 1;
        }
    }

    if (CoopTestSwitches_IsSet(noFma3)) {
#if defined(_MSC_VER) && defined(_M_X64)
        // _get_FMA3_enable() is 1 only when the CPU has FMA3 and the runtime uses it, so the log shows whether this
        // switch changes anything on this PC.
        int before = _get_FMA3_enable();
        _set_FMA3_enable(0);
        LUSLOG_INFO("coop: test switch: FMA3 in the C runtime math functions %d -> %d", before, _get_FMA3_enable());
#else
        LUSLOG_INFO("coop: test switch SF64_COOP_TEST_NO_FMA3 has no effect on this platform%s", "");
#endif
    }

    if (CoopTestSwitches_IsSet(audioSamples)) {
        if (strcmp(audioSamples, "low") == 0) {
            samples_high = samples_low;
        } else if (strcmp(audioSamples, "high") == 0) {
            samples_low = samples_high;
        }
        LUSLOG_INFO("coop: test switch: audio samples per update high %u, low %u", samples_high, samples_low);
    }

    if (CoopTestSwitches_IsSet(localIndex)) {
        CoopPlayers_SetLocalIndex(atoi(localIndex));
        LUSLOG_INFO("coop: test switch: local player index %d", atoi(localIndex));
    }

    if (CoopTestSwitches_IsSet(noLockstep)) {
        CoopSyncGame_SetOff(true);
        LUSLOG_INFO("coop: test switch: no lockstep%s", "");
    }

    if (CoopTestSwitches_IsSet(sysFrameOffset)) {
        sSysFrameOffset = (uint32_t) atoi(sysFrameOffset) & ~1u;
        LUSLOG_INFO("coop: test switch: gSysFrameCount starts %u ahead", sSysFrameOffset);
        REGISTER_LISTENER(GamePreUpdateEvent, CoopTestSwitches_OnGamePreUpdate, EVENT_PRIORITY_HIGH);
    }

    if (CoopTestSwitches_IsSet(getenv("SF64_COOP_TEST_FOX_REFILL_OFF"))) {
        CoopPlayers_SetFoxRefillOff(1);
        LUSLOG_INFO("coop: test switch: no level is open for the refill of player 0%s", "");
    }

    if (CoopTestSwitches_IsSet(getenv("SF64_COOP_TEST_LEAD_SWAP"))) {
        CoopPlayers_SetLeadSwapOn(1);
        LUSLOG_INFO("coop: test switch: the lead passes to a survivor at pool 0 (spike #183)%s", "");
    }

    CoopTeamTrace_Init(); // no-op unless SF64_COOP_TEAM_* is set (#89)
    CoopAimTrace_Init();  // no-op unless SF64_COOP_AIM_TRACE is set (#193)
    CoopBossTrace_Init(); // no-op unless SF64_COOP_BOSS_TRACE is set (#213)
    CoopScoreProbe_Init(); // no-op unless SF64_COOP_SCORE_PROBE is set (#210)
    CoopSmokeTrace_Init(); // no-op unless SF64_COOP_SMOKE_TRACE is set
    CoopTestStart_Init(); // no-op unless SF64_COOP_TEST_START_LEVEL is set (#148)
}
