#pragma once

// Ends a test run cleanly on a stop signal (#71) and for sure on a crash (#98). See docs/coop/NOTES.md (2026-11-06, #71
// and #98).
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
// test output. An atexit handler then leaves with _exit(1) before the destructor of audio. If the crash handler never
// gets to exit() in a run without a window (SDL_VIDEODRIVER=offscreen), an alarm ends the process after
// CRASH_LIMIT_SECONDS by SIGALRM, without a crash log.

#ifdef __cplusplus
extern "C" {
#endif

void CoopStopSignal_Init(void);
int CoopStopSignal_Requested(void);

#ifdef __cplusplus
}
#endif
