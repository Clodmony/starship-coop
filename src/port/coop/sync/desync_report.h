#pragma once

// The desync report (WP-23): a text file in the game's own folder with both sides' identity, the frame, the regions
// that differ and a dump of this game's state at that frame as hashed. Two of them, one per game, compare with
// tools/coop/desync_report.py, which names the first differing value. Nothing personal goes in: build id, platform,
// ROM, game state and frame numbers only.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "desync.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* folder;  // where to write; "" for the working directory
    const char* self;    // identity line of this game
    const char* other;   // identity line of the game whose hash differed, or what is known of it
    int32_t players;     // in the level
    uint32_t inputDelay; // frames
} CoopDesyncReportInfo;

// Writes coop-desync-<frame>-player<n>.txt, with a number appended if that exists. `dump` is this game's dump of the
// frame, NULL if it has none. pathOut gets the file written. False if no file could be written.
bool CoopDesyncReport_Write(const CoopDesync* d, const CoopStateDump* dump, const CoopDesyncReportInfo* info,
                            char* pathOut, size_t cap);

// "players, rng"; "none" when both hashes are known and no region differs; "unknown" otherwise.
void CoopDesyncReport_Regions(const CoopDesync* d, char* out, size_t cap);

// "Corneria" for a LevelId, or "level <n>"; "rails" or "all-range" for a level mode.
const char* CoopDesyncReport_LevelName(int32_t level, char* buffer, size_t cap);
const char* CoopDesyncReport_ModeName(int32_t mode);

// Where the games were, from a frame's state bytes (game state, level, level mode, play state): "Corneria (rails)" in a
// level, "the map" on the map, whose level byte is the last level flown (#276), "the ending", or "game state <n>".
const char* CoopDesyncReport_PlaceName(const uint8_t state[4], char* buffer, size_t cap);

#ifdef __cplusplus
}
#endif
