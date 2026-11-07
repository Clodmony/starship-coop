// The desync check in the game, see desync_game.h.

#include "desync_game.h"

#ifdef USE_NETWORKING

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "port/coop/game/app_folder.h"
#include "port/coop/game/players.h"
#include "port/coop/log/coop_log.h"
#include "port/coop/net/protocol.h"
#include "desync.h"
#include "desync_report.h"
#include "state_hash.h"

#define IDENTITY_SIZE 256
#define PATH_SIZE 1024
// The co-op log (#172): how often the compared frames are counted in a line.
#define LOG_CHECKED_FRAMES 300

static bool sOff = false;
static bool sOn = false;
static CoopDesyncGameLink sLink;
static CoopDesync sDesync;
static uint32_t sInputDelay = 0;
static int32_t sPlayers = 0;
// One dump per ring entry, written with it: the dump at frame % COOP_DESYNC_RING is the entry's frame.
static CoopStateDump sDumps[COOP_DESYNC_RING];
static uint8_t* sDumpBytes = NULL;
static CoopStateChunk* sDumpChunks = NULL;
static char sReportPath[PATH_SIZE];
static bool sReported = false;
// Identity lines, taken when lockstep starts: a report written as the session goes away could not ask any more.
static char sSelfIdentity[IDENTITY_SIZE];
static char sPeerIdentity[COOP_NET_MAX_PEERS][IDENTITY_SIZE];
static bool sFoundSaid = false;

// The co-op log hears of a desync the moment this game finds it or hears of it, before the report.
static void Log_Found(void) {
    if (!sDesync.found || sFoundSaid) {
        return;
    }
    sFoundSaid = true;
    CoopLog_Write(COOP_LOG_DESYNC, false, "desync found at frame %u: players %d and %d differ, seen through player %d",
                  (unsigned) sDesync.frame, (int) sDesync.pair[0] + 1, (int) sDesync.pair[1] + 1,
                  (int) sDesync.foundFrom + 1);
}

static bool Send(void* user, int peer, uint16_t msgId, const void* data, uint16_t size) {
    (void) user;
    return sLink.send(peer, msgId, data, size, COOP_NET_RELIABLE);
}

static void Dumps_Free(void) {
    free(sDumpBytes);
    free(sDumpChunks);
    sDumpBytes = NULL;
    sDumpChunks = NULL;
    memset(sDumps, 0, sizeof(sDumps));
}

// A ring of dumps, about 8 MB for Corneria, only while a session runs. Without the memory the check still runs, and
// the report says it has no dump.
static void Dumps_Allocate(void) {
    size_t bytes = CoopStateHash_DumpBytes();
    int i;

    sDumpBytes = malloc(bytes * COOP_DESYNC_RING);
    sDumpChunks = malloc(sizeof(CoopStateChunk) * COOP_STATE_DUMP_CHUNKS * COOP_DESYNC_RING);
    if ((sDumpBytes == NULL) || (sDumpChunks == NULL)) {
        Dumps_Free();
        return;
    }
    for (i = 0; i < COOP_DESYNC_RING; i++) {
        sDumps[i].bytes = sDumpBytes + bytes * i;
        sDumps[i].cap = bytes;
        sDumps[i].chunks = sDumpChunks + (size_t) COOP_STATE_DUMP_CHUNKS * i;
        sDumps[i].chunkCap = COOP_STATE_DUMP_CHUNKS;
    }
}

void CoopDesyncGame_Init(void) {
    const char* off = getenv("SF64_COOP_TEST_NO_DESYNC_CHECK");

    sOff = (off != NULL) && (off[0] != '\0');
}

static void Identity(int peer, char* out, size_t cap) {
    out[0] = '\0';
    if (sLink.identity != NULL) {
        sLink.identity(peer, out, cap);
    }
    if (out[0] == '\0') {
        snprintf(out, cap, "unknown");
    }
}

void CoopDesyncGame_Begin(const CoopDesyncGameLink* link, int32_t self, bool host, uint32_t present,
                          uint32_t inputDelay) {
    CoopDesyncConfig config;
    int32_t i;

    CoopDesyncGame_End();
    if (sOff) {
        return;
    }
    sLink = *link;
    Identity(-1, sSelfIdentity, sizeof(sSelfIdentity));
    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        Identity(i, sPeerIdentity[i], sizeof(sPeerIdentity[i]));
    }
    sInputDelay = inputDelay;
    sPlayers = 0;
    for (i = 0; i < COOP_PLAYERS_MAX; i++) {
        if (present & (1u << i)) {
            sPlayers++;
        }
    }
    memset(&config, 0, sizeof(config));
    config.send = Send;
    config.self = self;
    config.host = host;
    config.present = present;
    config.period = COOP_DESYNC_PERIOD;
    CoopDesync_Start(&sDesync, &config);
    Dumps_Allocate();
    sReportPath[0] = '\0';
    sReported = false;
    sFoundSaid = false;
    sOn = true;
    CoopLog_Write(COOP_LOG_DESYNC, false, "desync check on: a hash every frame, totals every %d frames, %d players%s",
                  (int) COOP_DESYNC_PERIOD, (int) sPlayers, (sDumpBytes != NULL) ? "" : ", no memory for dumps");
}

static uint8_t Byte(s32 value) {
    return ((value < 0) || (value > 255)) ? 255 : (uint8_t) value;
}

void CoopDesyncGame_Frame(uint32_t frame, uint32_t nowMs) {
    CoopStateHash hash;
    uint8_t state[4];

    if (!sOn) {
        return;
    }
    if (sDumpBytes != NULL) {
        CoopStateHash_ComputeDump(&hash, &sDumps[frame % COOP_DESYNC_RING]);
    } else {
        CoopStateHash_Compute(&hash);
    }
    state[0] = Byte(gGameState);
    state[1] = Byte(gCurrentLevel);
    state[2] = Byte(gLevelMode);
    state[3] = Byte(gPlayState);
    CoopDesync_Record(&sDesync, frame, &hash, state, nowMs);
    Log_Found();
    if ((frame != 0) && (frame % LOG_CHECKED_FRAMES == 0)) {
        CoopLog_Write(COOP_LOG_DESYNC, false, "frame %u: %u frames compared with a peer, %u unchecked",
                      (unsigned) frame, (unsigned) sDesync.checked, (unsigned) sDesync.unchecked);
    }
}

bool CoopDesyncGame_Receive(int peer, uint16_t msgId, const uint8_t* data, uint16_t size, uint32_t nowMs) {
    bool ok;

    if (!sOn) {
        return true; // off, or not in a level: what a peer sends about hashes is nobody's business here
    }
    CoopLog_Write(COOP_LOG_DESYNC, true, "%s from peer %d, %u bytes",
                  (msgId == COOP_MSG_SYNC_HASH) ? "hash totals" : "desync hashes", peer, (unsigned) size);
    ok = CoopDesync_Receive(&sDesync, peer, msgId, data, size, nowMs);
    Log_Found();
    return ok;
}

// The link of the player whose hash differed from ours, -1 if that is nobody we talk to.
static int Peer_Of(int32_t player) {
    if (player < 0) {
        return -1;
    }
    return sDesync.config.host ? (player - 1) : 0;
}

// The report, once per session, with what is known by now.
static void Report_Write(void) {
    CoopDesyncReportInfo info;
    const CoopStateDump* dump = NULL;
    char folder[PATH_SIZE];
    int32_t otherPlayer;
    int peer;

    if (sReported || !sDesync.found) {
        return;
    }
    sReported = true;
    otherPlayer = (sDesync.pair[0] == sDesync.config.self)   ? sDesync.pair[1]
                  : (sDesync.pair[1] == sDesync.config.self) ? sDesync.pair[0]
                                                             : sDesync.foundFrom;
    peer = Peer_Of(otherPlayer);
    CoopAppFolder_Path(folder, sizeof(folder));
    if ((sDumpBytes != NULL) && sDesync.mine.held) {
        dump = &sDumps[sDesync.frame % COOP_DESYNC_RING];
    }
    memset(&info, 0, sizeof(info));
    info.folder = folder;
    info.self = sSelfIdentity;
    info.other = ((peer >= 0) && (peer < COOP_NET_MAX_PEERS)) ? sPeerIdentity[peer] : "unknown";
    info.players = sPlayers;
    info.inputDelay = sInputDelay;
    CoopDesyncReport_Write(&sDesync, dump, &info, sReportPath, sizeof(sReportPath));
}

bool CoopDesyncGame_Check(uint32_t nowMs, char* text, size_t cap) {
    char regions[160];
    char name[32];

    if (!sOn || !CoopDesync_Done(&sDesync, nowMs)) {
        return false;
    }
    Report_Write();
    CoopDesyncReport_Regions(&sDesync, regions, sizeof(regions));
    CoopLog_Write(COOP_LOG_DESYNC, false, "desync at frame %u, %s (%s): players %d and %d differ in %s; report: %s",
                  (unsigned) sDesync.frame, CoopDesyncReport_LevelName(sDesync.mine.state[1], name, sizeof(name)),
                  CoopDesyncReport_ModeName(sDesync.mine.state[2]), (int) sDesync.pair[0] + 1,
                  (int) sDesync.pair[1] + 1, regions, (sReportPath[0] != '\0') ? sReportPath : "none");
    snprintf(text, cap,
             "The games went out of step at frame %u of %s (%s): player %d's game and player %d's game differ in %s. "
             "The session is over, and the level goes on here alone. %s%s",
             (unsigned) sDesync.frame, CoopDesyncReport_LevelName(sDesync.mine.state[1], name, sizeof(name)),
             CoopDesyncReport_ModeName(sDesync.mine.state[2]), (int) sDesync.pair[0] + 1, (int) sDesync.pair[1] + 1,
             regions, (sReportPath[0] != '\0') ? "A report for the developers is in " : "No report could be written",
             sReportPath);
    sOn = false; // one report per session
    return true;
}

void CoopDesyncGame_End(void) {
    // A session that ends for another reason, a player gone, while a desync is found: the report still gets written,
    // with what came in so far.
    if (sOn) {
        Report_Write();
        CoopLog_Write(COOP_LOG_DESYNC, false, "desync check off: %u frames compared, %u unchecked%s%s",
                      (unsigned) sDesync.checked, (unsigned) sDesync.unchecked,
                      (sReportPath[0] != '\0') ? "; report: " : "", sReportPath);
    }
    sOn = false;
    Dumps_Free();
}

#else

void CoopDesyncGame_Init(void) {
}

void CoopDesyncGame_Begin(const CoopDesyncGameLink* link, int32_t self, bool host, uint32_t present,
                          uint32_t inputDelay) {
    (void) link;
    (void) self;
    (void) host;
    (void) present;
    (void) inputDelay;
}

void CoopDesyncGame_Frame(uint32_t frame, uint32_t nowMs) {
    (void) frame;
    (void) nowMs;
}

bool CoopDesyncGame_Receive(int peer, uint16_t msgId, const uint8_t* data, uint16_t size, uint32_t nowMs) {
    (void) peer;
    (void) msgId;
    (void) data;
    (void) size;
    (void) nowMs;
    return true;
}

bool CoopDesyncGame_Check(uint32_t nowMs, char* text, size_t cap) {
    (void) nowMs;
    (void) text;
    (void) cap;
    return false;
}

void CoopDesyncGame_End(void) {
}

#endif
