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
    // A message that goes as it is, without the run in front: SYNC_DUMP names its own run (#189).
    bool (*sendRaw)(int peer, uint16_t msgId, const void* data, uint16_t size, CoopNetLane lane);
    uint8_t run; // the level start's number in its session
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
// and `text` holds what happened, for the player or the log: the frame, the level, the two players, the regions and
// the report's path. What the session does about it is the caller's to add. Call it after every frame and every
// message. From here on this game's dump of that frame goes to the other game of the pair (CoopDesyncGame_Pump).
bool CoopDesyncGame_Check(uint32_t nowMs, char* text, size_t cap);

// #189, the first field that differs, in every co-op log. After a desync each game of the pair sends the other its
// dump of the frame (SYNC_DUMP, desync_dump.h), and the one that holds both writes one line, category `desync`, into
// its log and its report: `first difference at frame F, players A and B: <region>: <field> differs in ...`. Both
// games write the same line, and the log's stream puts each into both folders. This runs beside lockstep and across
// a new level start: Pump sends what the window takes and gives up after COOP_DESYNC_DUMP_MS, ReceiveDump takes a
// part, Drop ends it when the session goes.
void CoopDesyncGame_Pump(uint32_t nowMs);
void CoopDesyncGame_ReceiveDump(int peer, const uint8_t* data, uint16_t size, uint32_t nowMs);
void CoopDesyncGame_DropDump(void);

// Frees the rings. Nothing happens here until the next Begin.
void CoopDesyncGame_End(void);

// For the handover at the end of a session (#189): the path of the report this lockstep wrote, once, so that a later
// session does not hand it over again. A desync that is found and not reported yet is reported now. False, and "" in
// out, if there is none.
bool CoopDesyncGame_TakeReport(char* out, size_t cap);

#ifdef __cplusplus
}
#endif
