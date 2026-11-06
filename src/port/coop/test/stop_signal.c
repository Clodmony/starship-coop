// Ends the game cleanly on a stop signal (#71). See stop_signal.h.

#include "stop_signal.h"

// The same condition as the handlers it replaces (libultraship/src/debug/CrashHandler.cpp:416).
#if defined(__linux__) && !defined(__ANDROID__)

#include <signal.h>
#include <stddef.h>

static volatile sig_atomic_t sStopRequested = 0;

static void CoopStopSignal_Handle(int sig) {
    (void) sig;
    sStopRequested = 1;
}

void CoopStopSignal_Init(void) {
    struct sigaction action = { 0 };

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
