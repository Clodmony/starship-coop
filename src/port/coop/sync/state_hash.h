#pragma once

// State hash per game frame (WP-04). See docs/coop/QA.md, "State hash log".
//
// Off unless one of these environment variables is set when the game starts:
//   SF64_COOP_HASH_LOG=<file>     write one line per game frame: frame number, total hash and one hash per region
//   SF64_COOP_HASH_POKE=<frame>   test only: flip one bit of one byte of game state at that frame, so the log shows
//                                 a deliberate divergence (needs SF64_COOP_HASH_LOG)
// With neither set, no listener is registered and the game runs unchanged.
//
// Frames are counted the same way as the input replay (src/port/coop/test/replay.c): frame 0 is the first frame in
// GSTATE_START, so a hash log and a replay file line up frame by frame.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    COOP_HASH_PLAYERS,
    COOP_HASH_ACTORS,
    COOP_HASH_BOSSES,
    COOP_HASH_EFFECTS,
    COOP_HASH_ITEMS,
    COOP_HASH_SCENERY,
    COOP_HASH_SPRITES,
    COOP_HASH_SCENERY360,
    COOP_HASH_SHOTS,
    COOP_HASH_MISC, // textured lines, bonus text, radar marks
    COOP_HASH_RNG,
    COOP_HASH_GLOBALS,
    COOP_HASH_REGION_COUNT,
} CoopHashRegion;

typedef struct {
    uint64_t total;
    uint64_t region[COOP_HASH_REGION_COUNT];
} CoopStateHash;

// Hash the current game state. Safe to call at any time; usable without the log (for WP-02, WP-22, WP-23).
void CoopStateHash_Compute(CoopStateHash* out);

// Name of a region as written in the log header.
const char* CoopStateHash_RegionName(CoopHashRegion region);

void CoopStateHash_Init(void);

#ifdef __cplusplus
}
#endif
