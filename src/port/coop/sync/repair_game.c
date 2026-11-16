// Repair of a desync in place, see repair_game.h.

#include "repair_game.h"

#ifdef USE_NETWORKING

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/coop/log/coop_log.h"
#include "port/coop/net/protocol.h"
#include "desync.h"
#include "desync_dump.h"
#include "desync_game.h"
#include "state_hash.h"
#include "port/coop/test/switches.h"

#define STATE_PARTS_PER_HOLD 16
#define REPAIR_SIZE 8
#define REPAIRED_SIZE 21 // result, hash, fingerprint of the pointers, barrier
#define TEXT_SIZE 256

typedef struct {
    bool used;            // a client of this repair
    CoopDesyncDumpOut out; // what goes to it
    bool answered;
    bool ok;
    uint64_t total;
    uint64_t print;
} Target;

static struct {
    bool on; // a repair is under way: offered (host) or heard of (client)
    CoopRepairLink link;
    bool off;           // SF64_COOP_TEST_NO_REPAIR
    bool refuse;        // SF64_COOP_TEST_REPAIR_REFUSE: a client that cannot write the state it is sent
    bool badPrint;      // SF64_COOP_TEST_REPAIR_PRINT: a client whose object pointers differ after the write
    uint32_t barrier;   // the frame every game holds at
    uint32_t found;     // the frame the desync check named
    uint32_t sinceMs;   // when the repair began, for the time limit
    uint32_t lastFrame; // the barrier of the last repair of this run, 0 for none
    uint32_t count;     // repairs of this run
    // Host: what goes out.
    bool prepared;
    uint8_t* stream;
    uint32_t size;
    uint64_t total;
    uint64_t print; // the host's PointerPrint at the barrier
    Target targets[COOP_NET_MAX_PEERS];
    // Client: what comes in, and the wait for an offer.
    CoopDesyncDumpIn in;
    bool offerWait;
    uint32_t offerMs;
} sR;

static void Put32(uint8_t* out, uint32_t value) {
    out[0] = (uint8_t) value;
    out[1] = (uint8_t) (value >> 8);
    out[2] = (uint8_t) (value >> 16);
    out[3] = (uint8_t) (value >> 24);
}

static uint32_t Get32(const uint8_t* in) {
    return (uint32_t) in[0] | ((uint32_t) in[1] << 8) | ((uint32_t) in[2] << 16) | ((uint32_t) in[3] << 24);
}

static void Put64(uint8_t* out, uint64_t value) {
    Put32(out, (uint32_t) value);
    Put32(out + 4, (uint32_t) (value >> 32));
}

static uint64_t Get64(const uint8_t* in) {
    return (uint64_t) Get32(in) | ((uint64_t) Get32(in + 4) << 32);
}

static void Targets_Free(void) {
    free(sR.stream);
    sR.stream = NULL;
    sR.size = 0;
    sR.prepared = false;
    memset(sR.targets, 0, sizeof(sR.targets));
}

static void Repair_Clear(void) {
    Targets_Free();
    CoopDesyncDump_InFree(&sR.in);
    memset(&sR.in, 0, sizeof(sR.in));
    sR.on = false;
    sR.offerWait = false;
}

static void Say(const char* text) {
    CoopLog_Write(COOP_LOG_DESYNC, false, "%s", text);
    if (sR.link.notice != NULL) {
        sR.link.notice(text);
    }
}

void CoopRepair_Begin(const CoopRepairLink* link) {
    const char* off = getenv("SF64_COOP_TEST_NO_REPAIR");

    Repair_Clear();
    sR.link = *link;
    sR.off = (off != NULL) && (off[0] != '\0');
    sR.badPrint = (getenv("SF64_COOP_TEST_REPAIR_PRINT") != NULL) && (getenv("SF64_COOP_TEST_REPAIR_PRINT")[0] != '\0');
    sR.refuse = (getenv("SF64_COOP_TEST_REPAIR_REFUSE") != NULL) && (getenv("SF64_COOP_TEST_REPAIR_REFUSE")[0] != '\0');
    sR.lastFrame = 0;
    sR.count = 0;
}

void CoopRepair_End(void) {
    Repair_Clear();
}

bool CoopRepair_IsMessage(uint16_t msgId) {
    return (msgId == COOP_MSG_SYNC_REPAIR) || (msgId == COOP_MSG_SYNC_STATE) || (msgId == COOP_MSG_SYNC_REPAIRED);
}

bool CoopRepair_Busy(void) {
    return sR.on || sR.offerWait;
}

uint32_t CoopRepair_Count(void) {
    return sR.count;
}

static int Peer_Of(int32_t player) {
    return sR.link.host ? (int) (player - 1) : 0;
}

bool CoopRepair_Found(uint32_t frame, uint32_t now, uint32_t nowMs, const char* text) {
    uint8_t message[REPAIR_SIZE];
    int32_t player;

    if (sR.off) {
        return false;
    }
    if (sR.on) {
        return true; // the host's offer came before this game found the desync itself
    }
    if (!sR.link.host) {
        // The host says whether it repairs; this game waits for the offer, and goes on as before if none comes.
        sR.offerWait = true;
        sR.offerMs = nowMs;
        CoopLog_Write(COOP_LOG_DESYNC, false, "desync at frame %u: waiting for the host's repair", (unsigned) frame);
        return true;
    }
    if ((now < COOP_REPAIR_MIN_FRAME) || (sR.count >= COOP_REPAIR_MAX) ||
        ((sR.lastFrame != 0) && (now < sR.lastFrame + COOP_REPAIR_SPACING))) {
        CoopLog_Write(COOP_LOG_DESYNC, false, "no repair of the desync at frame %u: %s", (unsigned) frame,
                      (now < COOP_REPAIR_MIN_FRAME) ? "too early in the level"
                      : (sR.count >= COOP_REPAIR_MAX) ? "repaired too often in this level"
                                                      : "the last repair is too near");
        return false;
    }
    sR.barrier = now + sR.link.delay + 2 * COOP_DESYNC_PERIOD + COOP_REPAIR_LEAD;
    sR.found = frame;
    Put32(&message[0], sR.barrier);
    Put32(&message[4], frame);
    memset(sR.targets, 0, sizeof(sR.targets));
    for (player = 1; player < COOP_PLAYERS_MAX; player++) {
        if ((sR.link.present & (1u << player)) != 0) {
            if (!sR.link.send(Peer_Of(player), COOP_MSG_SYNC_REPAIR, message, sizeof(message), COOP_NET_RELIABLE)) {
                CoopLog_Write(COOP_LOG_DESYNC, false, "no repair of the desync at frame %u: player %d cannot be reached",
                              (unsigned) frame, (int) player + 1);
                Targets_Free();
                return false;
            }
            sR.targets[Peer_Of(player)].used = true;
        }
    }
    sR.on = true;
    sR.sinceMs = nowMs;
    (void) text;
    {
        char line[TEXT_SIZE];

        snprintf(line, sizeof(line), "The games parted at frame %u. Syncing them again at frame %u.", (unsigned) frame,
                 (unsigned) sR.barrier);
        Say(line);
    }
    return true;
}

bool CoopRepair_OfferExpired(uint32_t nowMs) {
    if (!sR.offerWait || sR.on || ((nowMs - sR.offerMs) < COOP_REPAIR_OFFER_MS)) {
        return false;
    }
    sR.offerWait = false;
    CoopLog_Write(COOP_LOG_DESYNC, false, "the host made no repair offer within %d ms", (int) COOP_REPAIR_OFFER_MS);
    return true;
}

bool CoopRepair_Receive(int peer, uint16_t msgId, const uint8_t* data, uint16_t size, uint32_t nowMs) {
    if (msgId == COOP_MSG_SYNC_REPAIR) {
        if (sR.link.host || (peer != 0) || (size != REPAIR_SIZE)) {
            return false;
        }
        if (sR.on) {
            return true; // a second offer while one is under way: the first stands
        }
        Repair_Clear();
        sR.barrier = Get32(&data[0]);
        sR.found = Get32(&data[4]);
        sR.sinceMs = nowMs;
        sR.on = true;
        CoopLog_Write(COOP_LOG_DESYNC, false, "the host repairs the desync of frame %u: all games hold at frame %u",
                      (unsigned) sR.found, (unsigned) sR.barrier);
        if ((getenv("SF64_COOP_TEST_REPAIR_FREEZE") != NULL) && (getenv("SF64_COOP_TEST_REPAIR_FREEZE")[0] != '\0')) {
            // Test only (#262): this client is lost while the repair is under way, as a connection cut then.
            CoopLog_Write(COOP_LOG_DESYNC, false, "stopped dead at the repair offer (SF64_COOP_TEST_REPAIR_FREEZE)");
            CoopTestSwitches_FreezeNow();
        }
        return true;
    }
    if (msgId == COOP_MSG_SYNC_REPAIRED) {
        Target* t;

        if (!sR.link.host || (size != REPAIRED_SIZE) || (peer < 0) || (peer >= COOP_NET_MAX_PEERS)) {
            return false;
        }
        t = &sR.targets[peer];
        if (!sR.on || !t->used || (Get32(&data[17]) != sR.barrier)) {
            return true; // not of this repair
        }
        t->answered = true;
        t->ok = data[0] == 0;
        t->total = Get64(&data[1]);
        t->print = Get64(&data[9]);
        return true;
    }
    return true;
}

void CoopRepair_ReceiveState(int peer, const uint8_t* data, uint16_t size) {
    if (!sR.on || sR.link.host || (peer != 0)) {
        return; // nothing this game waits for
    }
    CoopDesyncDump_InReceive(&sR.in, sR.link.run, sR.barrier, 0, data, size);
}

static int Fail(char* text, size_t cap, const char* format, ...) {
    va_list args;

    va_start(args, format);
    vsnprintf(text, cap, format, args);
    va_end(args);
    CoopLog_Write(COOP_LOG_DESYNC, false, "repair failed: %s", text);
    Repair_Clear();
    return -1;
}

// The host at the barrier: the state goes out to every client and the answers come back.
static int Hold_Host(uint32_t nowMs, char* text, size_t cap) {
    int p;
    bool all = true;

    if (!sR.prepared) {
        CoopStateDump dump;
        CoopStateHash hash;
        size_t bytes = CoopStateHash_DumpBytes();
        size_t size;

        memset(&dump, 0, sizeof(dump));
        dump.bytes = (uint8_t*) malloc(bytes);
        dump.cap = bytes;
        dump.chunks = (CoopStateChunk*) malloc(sizeof(CoopStateChunk) * COOP_STATE_DUMP_CHUNKS);
        dump.chunkCap = COOP_STATE_DUMP_CHUNKS;
        if ((dump.bytes == NULL) || (dump.chunks == NULL)) {
            free(dump.bytes);
            free(dump.chunks);
            return Fail(text, cap, "No memory to send the state for the repair.");
        }
        CoopStateHash_ComputeDump(&hash, &dump);
        size = dump.overflow ? 0 : CoopDesyncDump_StreamSize(&dump);
        if ((size != 0) && (size <= COOP_DESYNC_DUMP_MAX)) {
            sR.stream = (uint8_t*) malloc(size);
            if (sR.stream != NULL) {
                sR.size = (uint32_t) CoopDesyncDump_Stream(&dump, sR.stream, size);
            }
        }
        free(dump.bytes);
        free(dump.chunks);
        if ((sR.stream == NULL) || (sR.size == 0)) {
            return Fail(text, cap, "The state of the game did not fit the repair.");
        }
        sR.total = hash.total;
        sR.print = CoopStateHash_PointerPrint();
        for (p = 0; p < COOP_NET_MAX_PEERS; p++) {
            if (sR.targets[p].used) {
                CoopDesyncDump_OutBegin(&sR.targets[p].out, sR.link.run, sR.barrier, (uint8_t) sR.link.self, sR.stream,
                                        sR.size);
            }
        }
        sR.prepared = true;
        CoopLog_Write(COOP_LOG_DESYNC, false, "repair at frame %u: %u bytes of state go to every client (hash %016llx)",
                      (unsigned) sR.barrier, (unsigned) sR.size, (unsigned long long) sR.total);
    }
    for (p = 0; p < COOP_NET_MAX_PEERS; p++) {
        Target* t = &sR.targets[p];
        int parts;

        if (!t->used) {
            continue;
        }
        if (!t->answered) {
            uint8_t message[COOP_DESYNC_DUMP_MESSAGE_MAX];

            all = false;
            for (parts = 0; parts < STATE_PARTS_PER_HOLD; parts++) {
                uint16_t size = CoopDesyncDump_OutNext(&t->out, message);

                if ((size == 0) || !sR.link.sendRaw(p, COOP_MSG_SYNC_STATE, message, size, COOP_NET_RELIABLE)) {
                    break;
                }
                CoopDesyncDump_OutSent(&t->out);
            }
            continue;
        }
        if (!t->ok) {
            return Fail(text, cap, "Player %d's game could not take the state of the host, so the level starts again.",
                        p + 2);
        }
        if ((t->total == sR.total) && (t->print != sR.print)) {
            return Fail(text, cap,
                        "Player %d's game holds other pointers for its objects after the repair, so the level starts "
                        "again.",
                        p + 2);
        }
        if (t->total != sR.total) {
            return Fail(text, cap,
                        "Player %d's game still differs after the repair (hash %016llx, the host's %016llx), so the "
                        "level starts again.",
                        p + 2, (unsigned long long) t->total, (unsigned long long) sR.total);
        }
    }
    if (!all) {
        if ((nowMs - sR.sinceMs) >= COOP_REPAIR_MS) {
            return Fail(text, cap, "The repair took more than %d seconds, so the level starts again.",
                        (int) (COOP_REPAIR_MS / 1000));
        }
        return 1;
    }
    return 0;
}

// A client at the barrier: wait for the state, write it, answer with the hash of the result.
static int Hold_Client(uint32_t nowMs, char* text, size_t cap) {
    uint8_t answer[REPAIRED_SIZE];
    CoopStateHash hash;
    char why[TEXT_SIZE];
    uint32_t written = 0;

    if (sR.in.refused) {
        memset(answer, 0, sizeof(answer));
        answer[0] = 1;
        Put32(&answer[17], sR.barrier);
        sR.link.send(0, COOP_MSG_SYNC_REPAIRED, answer, sizeof(answer), COOP_NET_RELIABLE);
        return Fail(text, cap, "The state of the host could not be read, so the level starts again.");
    }
    if (!sR.in.done || sR.in.none) {
        if (sR.in.done && sR.in.none) {
            return Fail(text, cap, "The host had no state to send, so the level starts again.");
        }
        if ((nowMs - sR.sinceMs) >= COOP_REPAIR_MS) {
            return Fail(text, cap, "The state of the host did not come within %d seconds, so the level starts again.",
                        (int) (COOP_REPAIR_MS / 1000));
        }
        return 1;
    }
    if (sR.refuse) {
        snprintf(why, sizeof(why), "refused by SF64_COOP_TEST_REPAIR_REFUSE");
    }
    if (sR.refuse || !CoopStateHash_Restore(sR.in.stream, sR.in.size, why, sizeof(why), &written)) {
        memset(answer, 0, sizeof(answer));
        answer[0] = 1;
        Put32(&answer[17], sR.barrier);
        sR.link.send(0, COOP_MSG_SYNC_REPAIRED, answer, sizeof(answer), COOP_NET_RELIABLE);
        CoopLog_Write(COOP_LOG_DESYNC, false, "repair at frame %u refused: %s", (unsigned) sR.barrier, why);
        return Fail(text, cap, "The state of the host does not fit this game (%s), so the level starts again.", why);
    }
    CoopStateHash_Compute(&hash);
    answer[0] = 0;
    Put64(&answer[1], hash.total);
    Put64(&answer[9], CoopStateHash_PointerPrint() ^ (sR.badPrint ? 1u : 0u));
    Put32(&answer[17], sR.barrier);
    if (!sR.link.send(0, COOP_MSG_SYNC_REPAIRED, answer, sizeof(answer), COOP_NET_RELIABLE)) {
        return Fail(text, cap, "The answer to the host could not be sent, so the level starts again.");
    }
    CoopLog_Write(COOP_LOG_DESYNC, false, "repaired at frame %u: %u chunks written (%s), hash now %016llx",
                  (unsigned) sR.barrier, (unsigned) written, why, (unsigned long long) hash.total);
    return 0;
}

int CoopRepair_Check(uint32_t frame, uint32_t nowMs, char* text, size_t cap) {
    if (!sR.on) {
        return 0;
    }
    if (frame > sR.barrier) {
        if (!sR.link.host) {
            uint8_t answer[REPAIRED_SIZE];

            memset(answer, 0, sizeof(answer));
            answer[0] = 1;
            Put32(&answer[17], sR.barrier);
            sR.link.send(0, COOP_MSG_SYNC_REPAIRED, answer, sizeof(answer), COOP_NET_RELIABLE);
        }
        return Fail(text, cap, "This game was past the frame of the repair (%u, at %u), so the level starts again.",
                    (unsigned) sR.barrier, (unsigned) frame);
    }
    if ((frame < sR.barrier) && ((nowMs - sR.sinceMs) >= COOP_REPAIR_MS * 2)) {
        return Fail(text, cap, "The repair did not get to its frame in time, so the level starts again.");
    }
    return 0;
}

int CoopRepair_Hold(uint32_t frame, uint32_t nowMs, char* text, size_t cap) {
    int result;

    if (!sR.on) {
        return 0;
    }
    if (frame < sR.barrier) {
        if ((nowMs - sR.sinceMs) >= COOP_REPAIR_MS * 2) {
            return Fail(text, cap, "The repair did not get to its frame in time, so the level starts again.");
        }
        return 0;
    }
    if (frame > sR.barrier) {
        if (!sR.link.host) {
            uint8_t answer[REPAIRED_SIZE];

            memset(answer, 0, sizeof(answer));
            answer[0] = 1;
            Put32(&answer[17], sR.barrier);
            sR.link.send(0, COOP_MSG_SYNC_REPAIRED, answer, sizeof(answer), COOP_NET_RELIABLE);
        }
        return Fail(text, cap, "This game was past the frame of the repair (%u, at %u), so the level starts again.",
                    (unsigned) sR.barrier, (unsigned) frame);
    }
    result = sR.link.host ? Hold_Host(nowMs, text, cap) : Hold_Client(nowMs, text, cap);
    if (result != 0) {
        return result;
    }
    // Done: the check starts over from here, on this state.
    sR.lastFrame = sR.barrier;
    sR.count++;
    CoopDesyncGame_Resync(sR.barrier);
    Say(sR.link.host ? "The games are in step again." : "The games are in step again (the host's state).");
    Repair_Clear();
    return 0;
}

#else

void CoopRepair_Begin(const CoopRepairLink* link) {
    (void) link;
}

void CoopRepair_End(void) {
}

bool CoopRepair_IsMessage(uint16_t msgId) {
    (void) msgId;
    return false;
}

bool CoopRepair_Found(uint32_t frame, uint32_t now, uint32_t nowMs, const char* text) {
    (void) frame;
    (void) now;
    (void) nowMs;
    (void) text;
    return false;
}

bool CoopRepair_OfferExpired(uint32_t nowMs) {
    (void) nowMs;
    return false;
}

int CoopRepair_Check(uint32_t frame, uint32_t nowMs, char* text, size_t cap) {
    (void) frame;
    (void) nowMs;
    (void) text;
    (void) cap;
    return 0;
}

int CoopRepair_Hold(uint32_t frame, uint32_t nowMs, char* text, size_t cap) {
    (void) frame;
    (void) nowMs;
    (void) text;
    (void) cap;
    return 0;
}

bool CoopRepair_Receive(int peer, uint16_t msgId, const uint8_t* data, uint16_t size, uint32_t nowMs) {
    (void) peer;
    (void) msgId;
    (void) data;
    (void) size;
    (void) nowMs;
    return true;
}

void CoopRepair_ReceiveState(int peer, const uint8_t* data, uint16_t size) {
    (void) peer;
    (void) data;
    (void) size;
}

bool CoopRepair_Busy(void) {
    return false;
}

uint32_t CoopRepair_Count(void) {
    return 0;
}

#endif
