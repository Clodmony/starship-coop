#pragma once

// The handover at the end of a session (#189): before a game closes a session, it sends its desync report to every
// player still connected and takes theirs, and it lets the co-op log drain first, so that every folder ends with every
// player's report and log. Before this, the session closed at once and the report stayed on its own PC.
//
// One message, HANDOVER (0x0080, docs/coop/PROTOCOL.md), on the reliable lane, with a kind byte first:
//
//   BEGIN reason                  I am ending the session: end yours too, and hand over
//   FILE_START file size name     one of my files is coming: its number (0 to 3), its size and its name
//   FILE_DATA file offset bytes   a part of it, at that offset
//   DONE files                    all my files are sent, `files` of them
//   BYE                           my last log line has gone out; nothing more comes from me
//
// Each side sends BEGIN, its files and DONE to every peer at once, without waiting for anything. It sends BYE once DONE
// came from every peer and its log has drained, and it closes once BYE came from every peer and everything it sent is
// acknowledged, or the time is up (CoopHandover_TimeUp). So both sides do the same, in either order, and neither waits
// on the other before it has sent everything. A peer that is gone is waited for no more.
//
// This file holds the rules and the state of one handover, with no game, no session, no clock and no file system:
// handover_game.c connects it to the session, the folder and the desync report, and tools/coop/handover_test.py
// compiles it alone. What a peer sends is untrusted: a file name has to be exactly a desync report name of that peer's
// own player (desync_report.c), sizes are capped, every length is checked before anything is copied, and anything else
// is dropped and counted, never the end of anything.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Clients on one host, COOP_NET_MAX_PEERS.
#define COOP_HANDOVER_PEERS 3
// Files each way per peer. A session writes one report.
#define COOP_HANDOVER_FILES 4
// A report is about 215 KB for Corneria (docs/coop/NOTES.md, WP-23).
#define COOP_HANDOVER_FILE_MAX (2u * 1024u * 1024u)
// What all peers together may hand in.
#define COOP_HANDOVER_TOTAL_MAX (4u * 1024u * 1024u)
// Bytes of a file in one FILE_DATA. With its 6 bytes in front that is 1156, below COOP_NET_MAX_MESSAGE (1176).
#define COOP_HANDOVER_CHUNK 1150
// The longest name: coop-desync-4294967295-player4-9.txt has 36 characters.
#define COOP_HANDOVER_NAME_MAX 48
// The largest message this file sends or takes.
#define COOP_HANDOVER_MESSAGE_MAX (6 + COOP_HANDOVER_CHUNK)
// How long a game waits at most before it closes anyway, unless the caller says otherwise.
#define COOP_HANDOVER_MS 8000

typedef enum {
    COOP_HANDOVER_BEGIN = 1,      // u8 reason. Size 2.
    COOP_HANDOVER_FILE_START = 2, // u8 file, u32 size, u8 name length n, n bytes of name. Size 7 + n.
    COOP_HANDOVER_FILE_DATA = 3,  // u8 file, u32 offset, 1 to COOP_HANDOVER_CHUNK bytes. Size 7 to 1156.
    COOP_HANDOVER_DONE = 4,       // u8 files sent. Size 2.
    COOP_HANDOVER_BYE = 5,        // nothing more. Size 1.
} CoopHandoverKind;

typedef enum {
    COOP_HANDOVER_REASON_LEAVE = 1,  // the player chose to leave
    COOP_HANDOVER_REASON_FAIL = 2,   // the session cannot go on in the sender's game: a desync, a stall, a lost player
    COOP_HANDOVER_REASON_ANSWER = 3, // the sender ends because another game began a handover with it
} CoopHandoverReason;

typedef struct {
    // One message to a peer on the reliable lane. False if it could not go now: it is tried again on the next pump.
    bool (*send)(void* user, int peer, const uint8_t* data, uint16_t size);
    // A file came in whole and passed every check: name is a valid report name of that peer's player. May be NULL.
    void (*save)(void* user, int peer, const char* name, const uint8_t* data, uint32_t size);
    void* user;
    int32_t self; // our player index, 0 to 3
} CoopHandoverConfig;

typedef struct {
    char name[COOP_HANDOVER_NAME_MAX + 1];
    const uint8_t* data; // the caller's, until CoopHandover_Free
    uint32_t size;
} CoopHandoverOut;

typedef enum {
    COOP_HANDOVER_IN_NONE,
    COOP_HANDOVER_IN_RECEIVING,
    COOP_HANDOVER_IN_SAVED,
    COOP_HANDOVER_IN_REFUSED,
} CoopHandoverInState;

typedef struct {
    uint8_t state; // CoopHandoverInState
    char name[COOP_HANDOVER_NAME_MAX + 1];
    uint8_t* data; // while receiving
    uint32_t size;
    uint32_t got;
} CoopHandoverIn;

typedef struct {
    bool open;      // connected when the handover began, and not gone since
    int32_t player; // its player index, from the session: never from a message
    // What we send it.
    uint8_t phase; // see handover.c
    uint8_t file;
    bool fileStarted;
    uint32_t offset;
    // What came from it.
    bool begun;
    uint8_t reason;
    bool done;
    bool bye;
    uint8_t announced; // the file count in its DONE
    uint8_t started;   // FILE_START taken, refused ones too
    CoopHandoverIn in[COOP_HANDOVER_FILES];
} CoopHandoverPeer;

typedef struct {
    uint32_t saved;      // files that came in whole and were saved
    uint32_t refused;    // files refused: a name, a size, a count or a total out of bounds, a part out of place,
                         // or not whole when its sender said DONE or went
    uint32_t dropped;    // messages that could not be read, or came out of place
    uint32_t duplicates; // parts that came a second time, the same bytes
    uint32_t bytesIn;    // bytes of the files saved
    uint32_t filesOut;   // our files sent whole, once per peer
    uint32_t messagesOut;
} CoopHandoverStats;

typedef struct {
    CoopHandoverConfig config;
    bool begun;   // CoopHandover_Begin ran: we send
    bool closing; // CoopHandover_Close ran: BYE may go
    uint8_t reason;
    uint32_t startMs;
    uint32_t capMs;
    CoopHandoverOut out[COOP_HANDOVER_FILES];
    uint8_t outCount;
    CoopHandoverPeer peers[COOP_HANDOVER_PEERS];
    uint32_t totalIn; // bytes taken in from all peers, by the sizes their FILE_START gave
    CoopHandoverStats stats;
} CoopHandover;

void CoopHandover_Init(CoopHandover* h, const CoopHandoverConfig* config);
// A peer connected now, which gets our files and whose files we take. player is its player index, from the session.
void CoopHandover_AddPeer(CoopHandover* h, int peer, int32_t player);
// One of our own files, for every peer, before Begin. The data stays the caller's until CoopHandover_Free. False, and
// it does not go, for a name the receivers would refuse, an empty or too large file, or one file too many.
bool CoopHandover_AddFile(CoopHandover* h, const char* name, const uint8_t* data, uint32_t size);
// We hand over from now on, and close at the latest capMs after nowMs. A second call changes nothing.
void CoopHandover_Begin(CoopHandover* h, uint8_t reason, uint32_t nowMs, uint32_t capMs);
// A HANDOVER message from a peer. False if it was dropped or a file was refused: the handover goes on either way. A
// second BEGIN changes nothing.
bool CoopHandover_Receive(CoopHandover* h, int peer, const uint8_t* data, uint16_t size);
// The peer is gone: nothing more goes to it or is waited for, and a file not yet whole is refused.
void CoopHandover_Gone(CoopHandover* h, int peer);
// Sends what is due to every open peer while send takes it: BEGIN, the files, DONE, and BYE after Close.
void CoopHandover_Pump(CoopHandover* h);
// True once DONE came from every open peer: everything they had is in.
bool CoopHandover_Received(const CoopHandover* h);
// Our last word is out (the caller's log has drained): BYE goes on the next pump.
void CoopHandover_Close(CoopHandover* h);
// True once BYE went to every open peer and came from each of them.
bool CoopHandover_Over(const CoopHandover* h);
// True once capMs have passed since Begin.
bool CoopHandover_TimeUp(const CoopHandover* h, uint32_t nowMs);
// True if some open peer sent BEGIN: we end because of it.
bool CoopHandover_Answering(const CoopHandover* h);
// Frees what is held of files not yet whole. The handover is over.
void CoopHandover_Free(CoopHandover* h);

// True if name, len bytes long, is exactly what desync_report.c writes for player `sender`:
// coop-desync-<frame>-player<n>.txt or coop-desync-<frame>-player<n>-<k>.txt, frame a number below 2^32 without a
// leading zero, n = sender + 1, k from 2 to 9. Never for the receiver's own player: receiver -1 checks no receiver.
bool CoopHandover_NameValid(const char* name, size_t len, int32_t sender, int32_t receiver);

#ifdef __cplusplus
}
#endif
