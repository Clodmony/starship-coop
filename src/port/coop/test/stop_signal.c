// Ends a test run cleanly on a stop signal (#71) and for sure on a crash (#98, #158). See stop_signal.h.

#include "stop_signal.h"

#include <string.h>

void CoopCrashText_Add(CoopCrashText* t, const char* s) {
    while ((*s != '\0') && (t->used < sizeof(t->text) - 1)) {
        t->text[t->used++] = *s++;
    }
    t->text[t->used] = '\0';
}

void CoopCrashText_AddNumber(CoopCrashText* t, long long value) {
    char digits[24];
    size_t n = 0;
    unsigned long long rest = (value < 0) ? 0ULL - (unsigned long long) value : (unsigned long long) value;

    do {
        digits[n++] = (char) ('0' + (rest % 10));
        rest /= 10;
    } while (rest != 0);
    if (value < 0) {
        digits[n++] = '-';
    }
    while ((n > 0) && (t->used < sizeof(t->text) - 1)) {
        t->text[t->used++] = digits[--n];
    }
    t->text[t->used] = '\0';
}

// The same condition as the handlers it replaces (libultraship/src/debug/CrashHandler.cpp:416).
#if defined(__linux__) && !defined(__ANDROID__)

#include "stop_signal_log.h"

#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

// Far above what a crash takes to reach exit (0.1 to 0.9 s at load 40 on two cores, #158), and below the 10 s
// exit_test.py allows and the 15 s of replay.py.
#define CRASH_LIMIT_SECONDS 5
// What the stack in the crash report of the alarm may take; a second alarm then leaves without the rest of it.
#define TRACEBACK_LIMIT_SECONDS 2
#define CRASH_END_MAX 4

static const char* const sTestRunVariables[] = {
    "SF64_COOP_REPLAY_RECORD",
    "SF64_COOP_REPLAY_PLAY",
    "SF64_COOP_HASH_LOG",
    "SF64_COOP_TRACE",
    "SF64_COOP_TEST_MENU",
};

// The signals libultraship's crash handler takes (libultraship/src/debug/CrashHandler.cpp:423-426), and the line it
// writes for each (:154-165).
static const int sCrashSignals[] = { SIGILL, SIGABRT, SIGFPE, SIGSEGV };
static const char* const sCrashLines[] = { "ILLEGAL INSTRUCTION", "ABORT", "ERRONEUS ARITHEMETIC OPERATION",
                                           "INVALID ACCESS TO STORAGE" };
#define CRASH_SIGNAL_COUNT (sizeof(sCrashSignals) / sizeof(sCrashSignals[0]))
static struct sigaction sCrashHandlers[CRASH_SIGNAL_COUNT];

static volatile sig_atomic_t sStopRequested = 0;
static volatile sig_atomic_t sCrashing = 0;
static volatile sig_atomic_t sCrashSignal = 0;
static volatile sig_atomic_t sAlarms = 0;
static int sNoWindow = 0;
static char sLogPath[1024];
static void (*sCrashEnds[CRASH_END_MAX])(void);
static int sCrashEndCount = 0;

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

static void CoopStopSignal_Write(int fd, const CoopCrashText* t) {
    size_t done = 0;

    while (done < t->used) {
        ssize_t n = write(fd, t->text + done, t->used - done);

        if (n <= 0) {
            return;
        }
        done += (size_t) n;
    }
}

// SIGALRM, CRASH_LIMIT_SECONDS after a crash in a run without a window: libultraship's handler did not get to exit().
// Installed with SA_NODEFER, so the second alarm, for the stack, can stop this one. Only calls that are safe in a
// signal handler: backtrace was called once at start, so it does not load libgcc here (glibc's backtrace(3)).
static void CoopStopSignal_OnStuck(int sig) {
    CoopCrashText t;
    void* frames[64];
    int count;
    int fd;
    int i;

    (void) sig;
    if (++sAlarms > 1) {
        _exit(1); // the stack took too long; the test output and the first lines are written
    }
    // The test output first: it is what a tool waits for.
    for (i = 0; i < sCrashEndCount; i++) {
        sCrashEnds[i]();
    }
    fd = (sLogPath[0] != '\0') ? open(sLogPath, O_WRONLY | O_APPEND | O_CREAT, 0644) : -1;
    if (fd >= 0) {
        t.used = 0;
        CoopCrashText_Add(&t, "\n" COOP_CRASH_STUCK_LINE " within ");
        CoopCrashText_AddNumber(&t, CRASH_LIMIT_SECONDS);
        CoopCrashText_Add(&t, " s; report of the test run (src/port/coop/test/stop_signal.c, #158)\nSignal: ");
        CoopCrashText_AddNumber(&t, sCrashSignal);
        CoopCrashText_Add(&t, "\n");
        for (i = 0; i < (int) CRASH_SIGNAL_COUNT; i++) {
            if (sCrashSignals[i] == sCrashSignal) {
                CoopCrashText_Add(&t, sCrashLines[i]);
                CoopCrashText_Add(&t, "\n");
            }
        }
        CoopCrashText_Add(&t, "Traceback: the thread that took the alarm; on the crashed thread, where its handler "
                              "stopped, above the crash\n");
        CoopStopSignal_Write(fd, &t);
        alarm(TRACEBACK_LIMIT_SECONDS);
        count = backtrace(frames, (int) (sizeof(frames) / sizeof(frames[0])));
        backtrace_symbols_fd(frames, count, fd);
        close(fd);
    }
    _exit(1);
}

static void CoopStopSignal_HandleCrash(int sig, siginfo_t* info, void* context) {
    size_t i;

    if (!sCrashing) {
        sCrashing = 1;
        sCrashSignal = sig;
        // libultraship's handler can stop for good on a lock the crashed code held (stop_signal.h). With a window it
        // also waits for its message box, before it writes the log, and someone may be reading that: no limit there.
        if (sNoWindow) {
            struct sigaction stuck = { 0 };

            stuck.sa_handler = CoopStopSignal_OnStuck;
            stuck.sa_flags = SA_NODEFER;
            sigaction(SIGALRM, &stuck, NULL);
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
    void* warm[4];
    size_t i;

    sNoWindow = (video != NULL) && (strcmp(video, "offscreen") == 0);
    if (sNoWindow) {
        // backtrace loads libgcc the first time, which allocates (glibc's backtrace(3)); not in the alarm's handler.
        backtrace(warm, 4);
        CoopStopSignal_LogPath(sLogPath, sizeof(sLogPath));
    }
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

void CoopStopSignal_AddCrashEnd(void (*end)(void)) {
    if ((end != NULL) && (sCrashEndCount < CRASH_END_MAX)) {
        sCrashEnds[sCrashEndCount++] = end;
    }
}

#else

void CoopStopSignal_Init(void) {
}

int CoopStopSignal_Requested(void) {
    return 0;
}

void CoopStopSignal_AddCrashEnd(void (*end)(void)) {
    (void) end;
}

#endif
