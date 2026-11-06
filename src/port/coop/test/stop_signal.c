// Ends a test run cleanly on a stop signal (#71). See stop_signal.h.

#include "stop_signal.h"

// The same condition as the handlers it replaces (libultraship/src/debug/CrashHandler.cpp:416).
#if defined(__linux__) && !defined(__ANDROID__)

#include <signal.h>
#include <stddef.h>
#include <stdlib.h>

static const char* const sTestRunVariables[] = {
    "SF64_COOP_REPLAY_RECORD",
    "SF64_COOP_REPLAY_PLAY",
    "SF64_COOP_HASH_LOG",
    "SF64_COOP_TRACE",
    "SF64_COOP_TEST_MENU",
};

static volatile sig_atomic_t sStopRequested = 0;

static int CoopStopSignal_IsTestRun(void) {
    size_t i;

    for (i = 0; i < sizeof(sTestRunVariables) / sizeof(sTestRunVariables[0]); i++) {
        const char* value = getenv(sTestRunVariables[i]);

        if ((value != NULL) && (value[0] != '\0')) {
            return 1;
        }
    }
    return 0;
}

static void CoopStopSignal_Handle(int sig) {
    (void) sig;
    sStopRequested = 1;
}

void CoopStopSignal_Init(void) {
    struct sigaction action = { 0 };

    if (!CoopStopSignal_IsTestRun()) {
        return;
    }
    action.sa_handler = CoopStopSignal_Handle;
    // The game goes on until the next frame, so whatever the signal interrupted must carry on, not fail with EINTR.
    action.sa_flags = SA_RESTART;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGQUIT, &action, NULL);
}

int CoopStopSignal_Requested(void) {
    return sStopRequested != 0;
}

#else

void CoopStopSignal_Init(void) {
}

int CoopStopSignal_Requested(void) {
    return 0;
}

#endif
