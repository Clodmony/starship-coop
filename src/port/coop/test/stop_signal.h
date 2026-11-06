#pragma once

// Ends the game cleanly on a stop signal (#71). See docs/coop/NOTES.md (2026-11-06, #71).
//
// On Linux libultraship handles SIGINT, SIGTERM and SIGQUIT by calling exit() from inside the signal handler
// (libultraship/src/debug/CrashHandler.cpp:205-207). That runs the atexit handlers, then hangs for good in the
// destructor of the audio thread's condition variable, because the audio thread still waits on it
// (src/port/audio/GameAudio.h:4). This replaces that handler with one that only sets a flag; the main loop in
// src/port/Game.cpp reads it at the next frame, and the game ends the way it does when the window is closed. Without a
// signal nothing changes. Other platforms keep what they have: Requested() stays false there.

#ifdef __cplusplus
extern "C" {
#endif

void CoopStopSignal_Init(void);
int CoopStopSignal_Requested(void);

#ifdef __cplusplus
}
#endif
