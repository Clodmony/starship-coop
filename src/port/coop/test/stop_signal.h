#pragma once

// Ends a test run cleanly on a stop signal (#71) and for sure on a crash (#98, #158). See docs/coop/NOTES.md
// (2026-11-06, #71 and #98; 2026-11-10, #158).
//
// On Linux libultraship handles SIGINT, SIGTERM and SIGQUIT by calling exit() from inside the signal handler
// (libultraship/src/debug/CrashHandler.cpp:205-207). That runs the atexit handlers, then hangs for good in the
// destructor of the audio thread's condition variable, because the audio thread still waits on it
// (src/port/audio/GameAudio.h:4).
//
// Off unless one of these environment variables is set, and not empty, when the game starts:
//   SF64_COOP_REPLAY_RECORD, SF64_COOP_REPLAY_PLAY   see replay.h
//   SF64_COOP_HASH_LOG                               see src/port/coop/sync/state_hash.h
//   SF64_COOP_TRACE                                  see player_trace.h
//   SF64_COOP_TEST_MENU                              see menu_driver.h
// Those are the variables that make the game write test output, and every game the tools in tools/coop start has one.
// Then this replaces that handler with one that only sets a flag; the main loop in src/port/Game.cpp reads it at the
// next frame, and the game ends the way it does when the window is closed. With none set, the game keeps upstream's
// handler and Requested() stays false. Other platforms keep what they have.
//
// A crash ends in the same exit(), from libultraship/src/debug/CrashHandler.cpp:202, and hangs in the same destructor.
// In a test run libultraship's crash handler still runs and writes the crash log, and so do the atexit handlers of the
// test output. An atexit handler then leaves with _exit(1) before the destructor of audio.
//
// libultraship's crash handler is not safe in a signal handler: it allocates, logs through spdlog and builds strings
// before it writes the log, so it can stop for good on a lock the crashed code held (#158). In a run without a window
// (SDL_VIDEODRIVER=offscreen) an alarm comes CRASH_LIMIT_SECONDS after the crash. If the game is still running then,
// the alarm's handler ends the test output with what is safe in a signal handler: every function given to
// CoopStopSignal_AddCrashEnd, then a crash report of its own appended to the log (the line COOP_CRASH_STUCK_LINE, the
// signal, the stack of the thread that took the alarm, which shows where the crash handler stopped when it is that
// thread), and it leaves with _exit(1), the exit code of a crash. With a window there is no limit: the crash handler
// waits for its message box, and someone may be reading that.

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// The first line of the crash report the alarm writes; tools/coop/exit_test.py looks for it.
#define COOP_CRASH_STUCK_LINE "COOP: the crash handler did not finish"

void CoopStopSignal_Init(void);
int CoopStopSignal_Requested(void);

// end runs from a signal handler when a crash handler did not finish (see above), at most once, before the crash
// report. It may only use calls that are safe there: open, write, pwrite, fstat, ftruncate, close; no stdio, no malloc,
// no locks. Up to four; on other platforms this does nothing.
void CoopStopSignal_AddCrashEnd(void (*end)(void));

// Text built in a signal handler: Add and AddNumber append and never go past the end. Safe in a signal handler.
typedef struct {
    char text[1024];
    size_t used;
} CoopCrashText;
void CoopCrashText_Add(CoopCrashText* t, const char* s);
void CoopCrashText_AddNumber(CoopCrashText* t, long long value);

#ifdef __cplusplus
}
#endif
