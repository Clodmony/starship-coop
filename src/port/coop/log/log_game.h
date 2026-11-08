#pragma once

// The co-op log in the game (#172): where the files go, what frame and level a line carries, and the lines that come
// from the game itself: state and level changes, pause, a player going down or coming back, a state line of every
// player once a second, and warnings and errors from the game's own log (log_spdlog.h). The session, lockstep and the
// desync check write their own lines where they happen.
//
// Everything here reads game state and never writes it. Off with SF64_COOP_LOG=off; SF64_COOP_LOG=verbose gives more,
// and so does the CVar gCoopLogVerbose. Without a session the log is closed and the listener returns at once.

#ifdef __cplusplus
extern "C" {
#endif

void CoopLogGame_Init(void);

#ifdef __cplusplus
}
#endif
