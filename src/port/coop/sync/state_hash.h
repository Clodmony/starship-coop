#pragma once

// State hash per game frame (WP-04). See docs/coop/QA.md, "State hash log".
//
// Off unless one of these environment variables is set when the game starts:
//   SF64_COOP_HASH_LOG=<file>     write one line per game frame: frame number, total hash and one hash per region
//   SF64_COOP_HASH_POKE=<frame>   test only: flip one bit of one byte of game state at that frame, so the log shows
//                                 a deliberate divergence (needs SF64_COOP_HASH_LOG)
//   SF64_COOP_HASH_POKE_PLAYER=<i> test only: the poke flips a bit of player i instead of an actor
//   SF64_COOP_HASH_PROBE=<n>      test only: every n frames, change single values one at a time, hash, change them
//                                 back and log which regions noticed, as "# probe" lines. Needs no second run and
//                                 leaves the game as it was (needs SF64_COOP_HASH_LOG)
//   SF64_COOP_HASH_SLOTS_EVERY=<n> test only: the "# effects" and "# slots" lines every n frames instead of every 100,
//                                 to see a burst that lasts fewer frames than that (#284; needs SF64_COOP_HASH_LOG)
// With none set, no listener is registered and the game runs unchanged.
//
// Frames are counted the same way as the input replay (src/port/coop/test/replay.c): frame 0 is the first frame in
// GSTATE_START, so a hash log and a replay file line up frame by frame.

#include <stdbool.h>
#include <stddef.h>
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

// The bytes the hash ran over (WP-23): one frame's state as hashed, pointers normalised, in named chunks, so two
// games' dumps of one frame compare byte for byte and the first difference has a name. A chunk is one hashed value
// or one element of an array; its region is where it counted.
typedef struct {
    const char* name; // the variable or array, as in the source
    int32_t index;    // the element, or the first slot of a range; -1 for a single value
    uint8_t region;   // CoopHashRegion
    uint32_t size;
    uint32_t offset; // into bytes
} CoopStateChunk;

typedef struct {
    uint8_t* bytes;
    size_t cap;
    size_t used;
    CoopStateChunk* chunks;
    uint32_t chunkCap;
    uint32_t count;
    bool overflow; // not everything fit: the dump is short, the hash is whole
} CoopStateDump;

// Chunks a dump has at most, and the bytes it needs at most.
#define COOP_STATE_DUMP_CHUNKS 1024
size_t CoopStateHash_DumpBytes(void);
// As Compute, and every hashed byte also goes into dump, in hashing order. dump->used and count start from 0 here.
void CoopStateHash_ComputeDump(CoopStateHash* out, CoopStateDump* dump);

// Test only: the poke of SF64_COOP_HASH_POKE on the frame now starting, for a frame only the caller can name.
void CoopStateHash_PokeNow(void);

// Name of a region as written in the log header.
const char* CoopStateHash_RegionName(CoopHashRegion region);

void CoopStateHash_Init(void);

#ifdef __cplusplus
}
#endif
