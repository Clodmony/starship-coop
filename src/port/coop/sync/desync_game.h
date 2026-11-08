#pragma once

// Connects desync.c to the game (WP-23). lockstep_game.c owns the session link and calls this from the lockstep
// frame: hash, record, send; receive; and once a desync is complete, the report and the text for the player.
// Everything here reads game state and never writes it.
//
// Off with SF64_COOP_TEST_NO_DESYNC_CHECK=1 (src/port/coop/test/switches.h): nothing is hashed or sent, and the
// peer's hash messages are dropped, so a run can show what the check is there for.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "port/coop/net/transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool (*send)(int peer, uint16_t msgId, const void* data, uint16_t size, CoopNetLane lane);
    // One line on a game's identity, for the report: peer -1 is this game. "" if it is not known.
    void (*identity)(int peer, char* out, size_t cap);
} CoopDesyncGameLink;

void CoopDesyncGame_Init(void);

// Lockstep starts at frame 0 with these players; the rings are allocated here.
void CoopDesyncGame_Begin(const CoopDesyncGameLink* link, int32_t self, bool host, uint32_t present,
                          uint32_t inputDelay);

// The frame that runs next, before anything of it runs.
void CoopDesyncGame_Frame(uint32_t frame, uint32_t nowMs);

// A message from `peer`; ids that are not the desync check's are left alone (true). False if it could not be read.
bool CoopDesyncGame_Receive(int peer, uint16_t msgId, const uint8_t* data, uint16_t size, uint32_t nowMs);

// True once a desync is complete, or the grace for the other side's answer is over: the report is written then,
// and `text` holds what the player reads. Call it after every frame and every message.
bool CoopDesyncGame_Check(uint32_t nowMs, char* text, size_t cap);

// Frees the rings. Nothing happens here until the next Begin.
void CoopDesyncGame_End(void);

// For the handover at the end of a session (#189): the path of the report this lockstep wrote, once, so that a later
// session does not hand it over again. A desync that is found and not reported yet is reported now. False, and "" in
// out, if there is none.
bool CoopDesyncGame_TakeReport(char* out, size_t cap);

#ifdef __cplusplus
}
#endif
