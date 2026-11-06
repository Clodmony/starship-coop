// Ends a test run cleanly on a stop signal (#71) and for sure on a crash (#98). See stop_signal.h.

#include "stop_signal.h"

// The same condition as the handlers it replaces (libultraship/src/debug/CrashHandler.cpp:416).
#if defined(__linux__) && !defined(__ANDROID__)

#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Far above what a crash takes to reach exit, and below the 10 s exit_test.py allows and the 15 s of replay.py.
#define CRASH_LIMIT_SECONDS 5

static const char* const sTestRunVariables[] = {
    "SF64_COOP_REPLAY_RECORD",
    "SF64_COOP_REPLAY_PLAY",
    "SF64_COOP_HASH_LOG",
    "SF64_COOP_TRACE",
    "SF64_COOP_TEST_MENU",
};

// The signals libultraship's crash handler takes (libultraship/src/debug/CrashHandler.cpp:423-426).
static const int sCrashSignals[] = { SIGILL, SIGABRT, SIGFPE, SIGSEGV };
#define CRASH_SIGNAL_COUNT (sizeof(sCrashSignals) / sizeof(sCrashSignals[0]))
static struct sigaction sCrashHandlers[CRASH_SIGNAL_COUNT];

static volatile sig_atomic_t sStopRequested = 0;
static volatile sig_atomic_t sCrashing = 0;
static int sNoWindow = 0;

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

// Runs inside the exit() that ends libultraship's crash handler. Registered before the atexit handlers that write the
// test output, so after them, and after the destructor of audio was registered, so before it.
static void CoopStopSignal_OnExit(void) {
    if (sCrashing) {
        _exit(1); // the code that exit() was given
    }
}

static void CoopStopSignal_HandleCrash(int sig, siginfo_t* info, void* context) {
    size_t i;

    if (!sCrashing) {
        sCrashing = 1;
        // The crash handler is not safe in a signal handler and can stop for good, on a lock the crashed code held.
        // Then nothing reaches exit(), and this alarm ends the process. With a window the handler also waits for its
        // message box, before it writes the log, and someone may be reading that: no limit there.
        if (sNoWindow) {
            signal(SIGALRM, SIG_DFL);
            alarm(CRASH_LIMIT_SECONDS);
        }
    }
    for (i = 0; i < CRASH_SIGNAL_COUNT; i++) {
        if (sCrashSignals[i] == sig) {
            sCrashHandlers[i].sa_sigaction(sig, info, context);
        }
    }
}

static void CoopStopSignal_WrapCrashHandlers(void) {
    struct sigaction action = { 0 };
    const char* video = getenv("SDL_VIDEODRIVER");
    size_t i;

    sNoWindow = (video != NULL) && (strcmp(video, "offscreen") == 0);
    action.sa_sigaction = CoopStopSignal_HandleCrash;
    action.sa_flags = SA_SIGINFO;
    for (i = 0; i < CRASH_SIGNAL_COUNT; i++) {
        sigaction(sCrashSignals[i], NULL, &sCrashHandlers[i]);
        // Only over a handler that is there to call.
        if (sCrashHandlers[i].sa_flags & SA_SIGINFO) {
            sigaction(sCrashSignals[i], &action, NULL);
        }
    }
    atexit(CoopStopSignal_OnExit);
}

void CoopStopSignal_Init(void) {
    struct sigaction action = { 0 };

    if (!CoopStopSignal_IsTestRun()) {
        return;
    }
    CoopStopSignal_WrapCrashHandlers();
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
