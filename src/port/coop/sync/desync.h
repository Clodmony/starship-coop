#pragma once

// Desync detection (WP-23, decision 0002): every client hashes its state on every lockstep frame, before the frame
// runs, and sends the totals to the games it talks to, a batch every COOP_DESYNC_PERIOD frames. A receiver compares
// each total with its own for that frame. The first frame whose totals differ is the desync: both sides of it then
// exchange the hash of that frame region by region in SYNC_DESYNC, so each knows which regions differ, and end the
// session (lockstep_game.c). The wire forms are in docs/coop/PROTOCOL.md, Desync check.
//
// No game code and no clock in here: tools/coop/desynctest runs it in a virtual network, and desync_game.c connects
// it to the game. Nothing here changes the state it hashes, and nothing waits for a hash: a late or lost hash costs a
// frame unchecked, never a frame held.

#include <stdbool.h>
#include <stdint.h>

#include "port/coop/game/players.h"
#include "port/coop/net/transport.h"
#include "state_hash.h"

#ifdef __cplusplus
extern "C" {
#endif

// Frames per SYNC_HASH. Hashing is per frame, as decision 0002 says; this is how often the totals go out. One message
// a frame would double lockstep's datagram rate for 8 bytes of payload, and once every 10 frames a desync is still
// named within a third of a second plus the round trip. The ring below has to hold what the slowest client can still
// ask about: a peer is at most COOP_LOCKSTEP_MAX_DELAY frames ahead when its batch leaves, plus the batch itself,
// plus a resend or two on the reliable lane.
#define COOP_DESYNC_PERIOD 10
#define COOP_DESYNC_RING 64
// After the first SYNC_DESYNC went out, how long to wait for the other side's before ending without it.
#define COOP_DESYNC_GRACE_MS 1000
// Batches of one peer kept until this game has run their frames.
#define COOP_DESYNC_PENDING 4
#define COOP_DESYNC_MAX_BATCH 32
#define COOP_DESYNC_HASH_HEADER 5
#define COOP_DESYNC_HASH_MESSAGE_MAX (COOP_DESYNC_HASH_HEADER + 8 * COOP_DESYNC_MAX_BATCH)
#define COOP_DESYNC_HASHES (1 + COOP_HASH_REGION_COUNT)
#define COOP_DESYNC_MESSAGE_SIZE (12 + 8 * COOP_DESYNC_HASHES)

typedef struct {
    // One message to a peer, on the reliable lane.
    bool (*send)(void* user, int peer, uint16_t msgId, const void* data, uint16_t size);
    void* user;
    int32_t self;     // our player index
    bool host;        // player n is then the client at transport peer n - 1; a client has the host at peer 0
    uint32_t present; // bit i: player i is in the level
    uint32_t period;  // COOP_DESYNC_PERIOD, unless a test asks for another one (1 to COOP_DESYNC_MAX_BATCH)
} CoopDesyncConfig;

typedef struct {
    bool held;
    uint32_t frame;
    uint8_t state[4]; // gGameState, gCurrentLevel, gLevelMode, gPlayState as the frame started
    CoopStateHash hash;
} CoopDesyncEntry;

typedef struct {
    uint32_t first;
    uint8_t count;
    uint8_t next; // the first of them not compared yet
    uint64_t totals[COOP_DESYNC_MAX_BATCH];
} CoopDesyncBatch;

typedef struct {
    bool open;
    CoopDesyncBatch pending[COOP_DESYNC_PENDING];
    uint8_t pendingCount;
} CoopDesyncLink;

typedef struct {
    CoopDesyncConfig config;
    CoopDesyncEntry ring[COOP_DESYNC_RING];
    uint32_t frames;    // recorded so far, so the next frame to record
    uint32_t checked;   // frames of a peer compared with our own
    uint32_t unchecked; // frames of a peer that this game no longer held, or batches it had no room for
    CoopDesyncLink links[COOP_NET_MAX_PEERS];
    // The desync, once found.
    bool found;
    uint32_t frame;
    int32_t pair[2];      // the two players whose totals differed, in order
    int32_t foundFrom;    // whose hashes showed it to this game: the peer compared, or the sender of SYNC_DESYNC
    uint32_t foundMs;     // when
    bool sent;            // our SYNC_DESYNC went out
    CoopDesyncEntry mine; // our entry for that frame, held or not
    uint64_t theirTotal;  // what the peer's batch said, when a batch showed it
    bool haveTheirTotal;
    CoopDesyncEntry theirs; // the other side's, from its SYNC_DESYNC; held says whether it had the frame
    int32_t theirPlayer;    // whose `theirs` is, -1 while none came
    uint32_t theirFrame;    // the frame that side named, which can differ from ours
    bool answered;          // `theirs` is from the side this game needs: the other of the pair, or whoever told us
} CoopDesync;

// Frame 0 is next. Nothing is compared before anything is recorded.
void CoopDesync_Start(CoopDesync* d, const CoopDesyncConfig* config);

// The hash of `frame`, the one that runs next, before it runs; frames come in order from 0. Compares what peers have
// sent for it, and sends a batch when one is full.
void CoopDesync_Record(CoopDesync* d, uint32_t frame, const CoopStateHash* hash, const uint8_t state[4],
                       uint32_t nowMs);

// A SYNC_HASH or SYNC_DESYNC from `peer`. Other ids are left alone (true). False if it could not be read or is not
// this peer's to send: the session ends on that.
bool CoopDesync_Receive(CoopDesync* d, int peer, uint16_t msgId, const uint8_t* data, uint16_t size, uint32_t nowMs);

// True once a desync was found and this game has what it needs for its report, or has waited the grace for it: the
// caller writes the report and ends the session. A game outside the pair waits the grace either way.
bool CoopDesync_Done(const CoopDesync* d, uint32_t nowMs);

// The entry of `frame`, NULL if this game no longer holds it or has not reached it.
const CoopDesyncEntry* CoopDesync_Entry(const CoopDesync* d, uint32_t frame);

// Bit i: region i differs between `mine` and `theirs`. 0 unless both are held.
uint32_t CoopDesync_Regions(const CoopDesync* d);

// The player whose SYNC_DESYNC this game waits for, -1 when it needs none.
int32_t CoopDesync_NeedFrom(const CoopDesync* d);

bool CoopDesync_IsMessage(uint16_t msgId);

#ifdef __cplusplus
}
#endif
