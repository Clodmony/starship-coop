#pragma once

// Warnings and errors of the game's own log (spdlog, logs/Starship.log) for the co-op log (#172): a sink added from
// port code to the logger libultraship made, so nothing in libultraship changes. The logger is asynchronous, so the
// sink runs on spdlog's thread: it keeps the lines in a queue of its own, and log_game.c takes them on the game thread,
// one per call, with the spdlog level (3 warn, 4 err, 5 critical). The queue is bounded; past that the newest lines are
// dropped and the next line taken says how many.

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Adds the sink to the default logger. Call it once the logger exists; a second call does nothing.
void CoopLogSpdlog_Install(void);
// The oldest line waiting, scrubbed (coop_log.h), and its level. False when none waits.
bool CoopLogSpdlog_Take(char* out, size_t cap, int* levelOut);
// Throws away what waits: outside a session nobody reads it.
void CoopLogSpdlog_Drop(void);

#ifdef __cplusplus
}
#endif
