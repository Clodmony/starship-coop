#pragma once

// The co-op log (#172): one text file per player and per session in the game's own folder, and during the session each
// game streams its lines to every other player, so every folder ends up with every player's log. A line has the wall
// clock time, the lockstep frame, the level and a category, so tools/coop/coop_log.py can merge the files of all
// players into one timeline and filter them.
//
// No game code and no clock in here: the glue (log_game.c) gives the folder, the frame and the level through
// CoopLogContext, and session.c drives the streaming from its poll with the time it has. tools/coop/menutest and
// sessiontest link this file with no context, and then nothing is written or sent: the log is closed until something
// opens it with a folder.
//
// Privacy: the log goes to other people. Before a line is written or sent, the home folder becomes `~`, the user name
// `<user>` and any IPv4 address `<ip>` (CoopLog_Scrub). Nothing here writes a peer's address: a player is "player 2".
//
// Streaming: DIAG_LOG (0x00C4, docs/coop/PROTOCOL.md) carries whole lines of one origin player, on the reliable lane,
// at most one datagram per peer every COOP_LOG_SEND_MS and only while the peer's reliable window has room, so lockstep
// and the desync check never wait on log traffic. A client sends to the host; the host writes what it gets and passes
// it on to the other clients. Lines that find a peer's queue full are counted and said later, never lost in silence;
// what is still queued when the session closes gets one last send and is then dropped with a count.
//
// Nothing here reads or writes game state or the RNG. With co-op off no session opens the log, so nothing runs.

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COOP_LOG_PLAYERS 4
#define COOP_LOG_PEERS 3
// A line, stamp and text, at most this long; longer text is cut.
#define COOP_LOG_LINE 512
// One datagram of lines: origin, flags, then text. COOP_NET_MAX_MESSAGE is 1176.
#define COOP_LOG_BATCH_HEADER 2
#define COOP_LOG_BATCH_TEXT (1176 - COOP_LOG_BATCH_HEADER)
// Lines waiting to go to one peer, as bytes.
#define COOP_LOG_QUEUE (32 * 1024)
// At most one batch per peer this often, so at most about 23 KB/s per peer.
#define COOP_LOG_SEND_MS 50
// No batch goes out to a peer with this many reliable messages still in flight: the window is 128.
#define COOP_LOG_MAX_PENDING 32
// Lines of a client before it knows its player index, kept until then.
#define COOP_LOG_EARLY 16384

typedef enum {
    COOP_LOG_OFF,
    COOP_LOG_QUIET, // the default: what a person reads after a playtest
    COOP_LOG_VERBOSE,
} CoopLogLevel;

typedef enum {
    COOP_LOG_SESSION,  // host, join, refusals, picks, level start, leave, end
    COOP_LOG_NET,      // the transport: ping, resends, lost and duplicate packets, per interval
    COOP_LOG_LOCKSTEP, // input delay, frames held, stalls
    COOP_LOG_DESYNC,   // hashes compared, a desync, the report
    COOP_LOG_GAME,     // game state, level and mode changes, pause
    COOP_LOG_PLAYER,   // a player's state changed: down, parked, back
    COOP_LOG_STATE,    // the short state line of every player, once a second
    COOP_LOG_WARN,     // a warning from the game's own log (spdlog)
    COOP_LOG_ERROR,    // an error from the game's own log
    COOP_LOG_LOG,      // the log about itself: files, dropped lines
    COOP_LOG_CATEGORY_COUNT,
} CoopLogCategory;

typedef struct {
    // The game's own folder, without a trailing separator, "" if unknown: then nothing is written.
    void (*folder)(char* out, size_t cap);
    // The lockstep frame that runs next, -1 outside lockstep.
    int32_t (*frame)(void);
    // The level as one word without spaces, "" outside a level.
    void (*level)(char* out, size_t cap);
} CoopLogContext;

// One message to a peer on the reliable lane. False if it could not be sent now.
typedef bool (*CoopLogSendFn)(void* user, int peer, const uint8_t* data, uint16_t size);
// Reliable messages not yet acknowledged by this peer; false if the peer is not connected.
typedef bool (*CoopLogPendingFn)(void* user, int peer, uint32_t* pending);

// How much goes in. Off writes and sends nothing. The default is QUIET.
void CoopLog_SetLevel(CoopLogLevel level);
CoopLogLevel CoopLog_Level(void);
// May be NULL: then no folder, no frame and no level are known.
void CoopLog_SetContext(const CoopLogContext* context);

// A session begins: a host knows it is player 1, a client learns its index at the welcome (CoopLog_SetPlayer) and its
// lines wait until then. Opens the session's file when the folder is known. A second Begin closes what was open.
void CoopLog_Begin(bool host, uint32_t nowMs);
void CoopLog_SetPlayer(int player); // 0 to 3
bool CoopLog_IsOpen(void);
// The session is over: one last send to every peer that is still open, then what is left is dropped and counted, and
// the files are closed. send may be NULL.
void CoopLog_End(uint32_t nowMs, CoopLogSendFn send, void* user);

// One line, into our own file and the queue of every open peer. verboseOnly lines are kept out at QUIET.
void CoopLog_Write(CoopLogCategory category, bool verboseOnly, const char* format, ...);
void CoopLog_WriteV(CoopLogCategory category, bool verboseOnly, const char* format, va_list args);

// Streaming. A peer that joined gets our lines from now on; a peer that left gets nothing more, and what it was owed is
// dropped and counted.
void CoopLog_PeerOpen(int peer);
void CoopLog_PeerClose(int peer);
// Sends what is due, within the rate limit. Call it once per poll.
void CoopLog_Pump(uint32_t nowMs, CoopLogSendFn send, CoopLogPendingFn pending, void* user);
// A DIAG_LOG from `peer`. A host takes it as player peer + 1's and passes it on to its other peers; a client takes the
// origin from the message. The lines go to that player's file next to our own. False if it could not be read; the
// caller drops it, the session goes on, as a log must never end a session.
bool CoopLog_Receive(int peer, bool host, const uint8_t* data, uint16_t size);

// The privacy filter, in place: the home folder becomes `~`, the user name `<user>`, an IPv4 address `<ip>`. Public for
// tests and for the spdlog sink.
void CoopLog_Scrub(char* line, size_t cap);
// The environment the filter reads, for tests: NULL goes back to the real one.
void CoopLog_SetPrivate(const char* home, const char* user);

// Counters, for tests and the end-of-session line.
typedef struct {
    uint32_t written;      // lines in our own file
    uint32_t sent;         // batches sent
    uint32_t received;     // batches received
    uint32_t relayed;      // lines passed on by a host
    uint32_t droppedFull;  // lines that found a queue full
    uint32_t droppedAtEnd; // lines still queued when a peer or the session closed
    uint32_t badBatches;   // DIAG_LOG that could not be read
} CoopLogStats;
void CoopLog_Stats(CoopLogStats* out);

const char* CoopLog_CategoryName(CoopLogCategory category);

#ifdef __cplusplus
}
#endif
