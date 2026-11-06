#pragma once

// Ends a test run cleanly on a stop signal (#71). See docs/coop/NOTES.md (2026-11-06, #71).
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
// Those are the variables that make the game write test output, and every game the tools in tools/coop start has one.
// Then this replaces that handler with one that only sets a flag; the main loop in src/port/Game.cpp reads it at the
// next frame, and the game ends the way it does when the window is closed. With none set, the game keeps upstream's
// handler and Requested() stays false. Other platforms keep what they have.

#ifdef __cplusplus
extern "C" {
#endif

void CoopStopSignal_Init(void);
int CoopStopSignal_Requested(void);

#ifdef __cplusplus
}
#endif
