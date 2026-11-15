#pragma once

// Repair of a desync in place (decision 0016, option C of decision 0002): the games stop at one frame, the client
// writes the host's state over its own, and lockstep goes on. Where it cannot, the level starts again (decision 0014).
//
// The host decides. When the desync check finds a desync it names a barrier frame B a little ahead (SYNC_REPAIR to every
// client), every game runs on to B and holds there before the frame runs, the host sends the bytes its state hash runs
// over at that frame (the stream of desync_dump.h, as SYNC_STATE), each client writes them through
// CoopStateHash_Restore and answers with its hash of the result (SYNC_REPAIRED), and once every client answered with the
// host's hash, everyone goes on. A refusal, a different hash or a timeout is the recovery of decision 0014, as before.
//
// Messages: docs/coop/PROTOCOL.md, "Repair". This file holds no lockstep and no session; lockstep_game.c connects it.
// Off with SF64_COOP_TEST_NO_REPAIR=1 (the desync scenarios of lockstep_test.py that look at the level start).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "port/coop/net/transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // With the run in front, as every sync message but SYNC_STATE goes.
    bool (*send)(int peer, uint16_t msgId, const void* data, uint16_t size, CoopNetLane lane);
    // As it is: SYNC_STATE names its own run, frame and sender.
    bool (*sendRaw)(int peer, uint16_t msgId, const void* data, uint16_t size, CoopNetLane lane);
    // One line for the player (the co-op window); may be NULL. The co-op log gets its line from here either way.
    void (*notice)(const char* text);
    bool host;
    int32_t self;     // this game's player index
    uint32_t present; // bit i: player i is in the level
    uint32_t delay;   // the input delay in frames
    uint8_t run;      // the level start's number in its session
} CoopRepairLink;

// Frames after the desync check names a frame, at least, that the barrier lies ahead: every game has to be before it
// when the message comes, and a game is up to `delay` frames ahead of the host.
#define COOP_REPAIR_LEAD 30
// A repair that has not finished by then is given up (ms), and so is the offer a client waits for.
#define COOP_REPAIR_MS 10000
#define COOP_REPAIR_OFFER_MS 3000
// A repair is not tried before this frame of a run, nor within this many frames of the last one.
#define COOP_REPAIR_MIN_FRAME 60
#define COOP_REPAIR_SPACING 300
// Repairs in one run of a level.
#define COOP_REPAIR_MAX 8

// A lockstep run starts (every game, after the desync check is on) and ends.
void CoopRepair_Begin(const CoopRepairLink* link);
void CoopRepair_End(void);

bool CoopRepair_IsMessage(uint16_t msgId);

// The desync check found a desync at `frame` and lockstep stands at `now` (the frame that runs next). `text` is the
// check's line for the log. Host: starts the repair and returns true, or false when none can be made (off, too early,
// too soon after the last one, too many, a client cannot be reached): the caller starts the level again. Client:
// waits COOP_REPAIR_OFFER_MS for the host's offer and returns true, false when repair is off.
bool CoopRepair_Found(uint32_t frame, uint32_t now, uint32_t nowMs, const char* text);

// Client: the offer did not come. True once; the caller goes on as it did without repair.
bool CoopRepair_OfferExpired(uint32_t nowMs);

// Before a frame runs and every input of it is in: 0, nothing holds it; 1, it is held by a repair, ask again; -1, the
// repair failed and `text` says why (the caller starts the level again).
int CoopRepair_Hold(uint32_t frame, uint32_t nowMs, char* text, size_t cap);

// Every time round the wait for a frame, whatever input is in: a game that is past the barrier says so at once
// (-1, `text`), and a barrier that is never reached is given up. 0 otherwise.
int CoopRepair_Check(uint32_t frame, uint32_t nowMs, char* text, size_t cap);

// SYNC_REPAIR (client) and SYNC_REPAIRED (host), the run already taken off. False if it could not be read.
bool CoopRepair_Receive(int peer, uint16_t msgId, const uint8_t* data, uint16_t size, uint32_t nowMs);
// SYNC_STATE, as it came.
void CoopRepair_ReceiveState(int peer, const uint8_t* data, uint16_t size);

// Repairs made in this run, and in all (for the log and the tests).
uint32_t CoopRepair_Count(void);

#ifdef __cplusplus
}
#endif
