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
#include "desync_dump.h"
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

// #189: the dump of the last desync, on its way to the other game of the pair, and the other game's on its way here.
// It outlives the lockstep run it came from: the level starts again while it crosses.
static struct {
    bool on;
    bool said;          // the first-difference line is written, or why there is none
    int peer;           // the other game of the pair
    int32_t players[2]; // the pair, lower index first
    int32_t self;
    int32_t other;
    uint32_t frame;
    uint32_t sinceMs;
    uint8_t* stream; // our own, NULL if this game held no dump of the frame
    uint32_t size;
    CoopDesyncDumpOut out;
    CoopDesyncDumpIn in;
    bool (*send)(int peer, uint16_t msgId, const void* data, uint16_t size, CoopNetLane lane);
    char report[PATH_SIZE];
} sDump;
// Parts sent in one pump: the window is 128, and the session's own messages need room in it.
#define DUMP_PARTS_PER_PUMP 16

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
    // sReportPath stays: the report of a desync earlier in this session is still the one to hand over when the session
    // ends (CoopDesyncGame_TakeReport), until a later desync writes another.
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

static const char* Dump_RegionName(uint8_t region) {
    return (region < COOP_HASH_REGION_COUNT) ? CoopStateHash_RegionName((CoopHashRegion) region) : NULL;
}

// One line into the co-op log and behind the report, as a comment: tools/coop/desync_report.py skips it.
static void Dump_Say(const char* what) {
    FILE* f;

    sDump.said = true;
    CoopLog_Write(COOP_LOG_DESYNC, false, "first difference at frame %u, players %d and %d: %s", (unsigned) sDump.frame,
                  (int) sDump.players[0] + 1, (int) sDump.players[1] + 1, what);
    if (sDump.report[0] == '\0') {
        return;
    }
    f = fopen(sDump.report, "a");
    if (f != NULL) {
        fprintf(f, "# first difference at frame %u, players %d and %d: %s\n", (unsigned) sDump.frame,
                (int) sDump.players[0] + 1, (int) sDump.players[1] + 1, what);
        fclose(f);
    }
}

void CoopDesyncGame_DropDump(void) {
    if (sDump.on && !sDump.said) {
        Dump_Say("unknown: the session went before the other game's dump came");
    }
    free(sDump.stream);
    CoopDesyncDump_InFree(&sDump.in);
    memset(&sDump, 0, sizeof(sDump));
}

// The desync is complete: if this game is one of the pair, its dump of the frame starts for the other one.
static void Dump_Begin(uint32_t nowMs) {
    int32_t self = sDesync.config.self;
    int32_t other;
    const CoopStateDump* dump = NULL;

    CoopDesyncGame_DropDump();
    if ((sDesync.pair[0] != self) && (sDesync.pair[1] != self)) {
        return; // a third game: it compares nothing, and reads the line in the logs of the two
    }
    other = (sDesync.pair[0] == self) ? sDesync.pair[1] : sDesync.pair[0];
    if ((Peer_Of(other) < 0) || (Peer_Of(other) >= COOP_NET_MAX_PEERS) || (sLink.sendRaw == NULL)) {
        return;
    }
    sDump.on = true;
    sDump.peer = Peer_Of(other);
    sDump.self = self;
    sDump.other = other;
    sDump.players[0] = (self < other) ? self : other;
    sDump.players[1] = (self < other) ? other : self;
    sDump.frame = sDesync.frame;
    sDump.sinceMs = nowMs;
    sDump.send = sLink.sendRaw;
    snprintf(sDump.report, sizeof(sDump.report), "%s", sReportPath);
    if ((sDumpBytes != NULL) && sDesync.mine.held) {
        dump = &sDumps[sDesync.frame % COOP_DESYNC_RING];
    }
    if ((dump != NULL) && !dump->overflow) {
        size_t size = CoopDesyncDump_StreamSize(dump);

        sDump.stream = ((size != 0) && (size <= COOP_DESYNC_DUMP_MAX)) ? malloc(size) : NULL;
        if (sDump.stream != NULL) {
            sDump.size = (uint32_t) CoopDesyncDump_Stream(dump, sDump.stream, size);
        }
    }
    CoopDesyncDump_OutBegin(&sDump.out, sLink.run, sDump.frame, (uint8_t) self, sDump.stream, sDump.size);
    CoopLog_Write(COOP_LOG_DESYNC, false, "dump of frame %u: %u bytes in %u parts go to player %d",
                  (unsigned) sDump.frame, (unsigned) sDump.size, (unsigned) sDump.out.parts, (int) other + 1);
}

// Both dumps are here, or it is known that one never will be: the line.
static void Dump_Compare(void) {
    char line[COOP_DESYNC_DUMP_LINE];
    const uint8_t* a;
    const uint8_t* b;
    size_t aSize, bSize;

    if (sDump.said || !sDump.in.done) {
        return;
    }
    if (sDump.in.none || (sDump.stream == NULL)) {
        snprintf(line, sizeof(line), "unknown: player %d held no dump of that frame",
                 (int) (sDump.in.none ? sDump.other : sDump.self) + 1);
        Dump_Say(line);
        return;
    }
    if (sDump.self < sDump.other) {
        a = sDump.stream;
        aSize = sDump.size;
        b = sDump.in.stream;
        bSize = sDump.in.size;
    } else {
        a = sDump.in.stream;
        aSize = sDump.in.size;
        b = sDump.stream;
        bSize = sDump.size;
    }
    CoopDesyncDump_FirstDifference(a, aSize, b, bSize, Dump_RegionName, line, sizeof(line));
    Dump_Say(line);
}

void CoopDesyncGame_Pump(uint32_t nowMs) {
    uint8_t message[COOP_DESYNC_DUMP_MESSAGE_MAX];
    int parts;

    if (!sDump.on) {
        return;
    }
    for (parts = 0; parts < DUMP_PARTS_PER_PUMP; parts++) {
        uint16_t size = CoopDesyncDump_OutNext(&sDump.out, message);

        if ((size == 0) || !sDump.send(sDump.peer, COOP_MSG_SYNC_DUMP, message, size, COOP_NET_RELIABLE)) {
            break;
        }
        CoopDesyncDump_OutSent(&sDump.out);
    }
    Dump_Compare();
    if (!sDump.said && (nowMs - sDump.sinceMs >= COOP_DESYNC_DUMP_MS)) {
        char line[COOP_DESYNC_DUMP_LINE];

        snprintf(line, sizeof(line), "unknown: player %d's dump did not come within %d ms", (int) sDump.other + 1,
                 (int) COOP_DESYNC_DUMP_MS);
        Dump_Say(line);
    }
}

void CoopDesyncGame_ReceiveDump(int peer, const uint8_t* data, uint16_t size, uint32_t nowMs) {
    (void) nowMs;
    if (!sDump.on || (peer != sDump.peer) || sDump.said) {
        return; // nothing this game waits for
    }
    if (!CoopDesyncDump_InReceive(&sDump.in, sDump.out.run, sDump.frame, (uint8_t) sDump.other, data, size)) {
        if (sDump.in.refused) {
            Dump_Say("unknown: the other game's dump could not be read");
        }
        return;
    }
    Dump_Compare();
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
             "%s%s",
             (unsigned) sDesync.frame, CoopDesyncReport_LevelName(sDesync.mine.state[1], name, sizeof(name)),
             CoopDesyncReport_ModeName(sDesync.mine.state[2]), (int) sDesync.pair[0] + 1, (int) sDesync.pair[1] + 1,
             regions, (sReportPath[0] != '\0') ? "A report for the developers is in " : "No report could be written",
             sReportPath);
    Dump_Begin(nowMs);
    sOn = false; // one report per lockstep run
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

bool CoopDesyncGame_TakeReport(char* out, size_t cap) {
    if (sOn) {
        Report_Write();
    }
    snprintf(out, cap, "%s", sReportPath);
    sReportPath[0] = '\0';
    return out[0] != '\0';
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

void CoopDesyncGame_Pump(uint32_t nowMs) {
    (void) nowMs;
}

void CoopDesyncGame_ReceiveDump(int peer, const uint8_t* data, uint16_t size, uint32_t nowMs) {
    (void) peer;
    (void) data;
    (void) size;
    (void) nowMs;
}

void CoopDesyncGame_DropDump(void) {
}

bool CoopDesyncGame_TakeReport(char* out, size_t cap) {
    if (cap != 0) {
        out[0] = '\0';
    }
    return false;
}

#endif
