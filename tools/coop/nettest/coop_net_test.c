// Test program for the co-op transport (WP-20). tools/coop/net_test.py drives it; see docs/coop/QA.md.
//
//   coop_net_test sim [--seeds N]
//       Runs the core over a seeded virtual network: no sockets, no real time. Prints one line and exits 0 on PASS.
//   coop_net_test host --port-file F [--port P] [--peers N] [options]
//   coop_net_test client --connect HOST:PORT [options]
//       Real processes over UDP. Each side sends a known pattern, checks what arrives, writes a JSON result.
//
// Options: --reliable N --unreliable N --rate PER_SECOND --timeout-ms T --max-seconds S --out FILE
//          --hold (stay connected until the peer goes) --close-after-ms T (close on purpose after connecting)

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/coop/net/net_core.h"
#include "port/coop/net/protocol.h"
#include "port/coop/net/transport.h"

// ---- Test traffic ----

typedef struct {
    uint32_t reliableSent;
    uint32_t unreliableSent;
    uint32_t reliableReceived;
    uint32_t unreliableReceived;
    uint32_t outOfOrder;
    uint32_t corrupt;
    uint32_t unreliableDuplicates;
    uint8_t* seen; // one flag per unreliable index
    uint32_t seenSize;
    bool doneSent;
    bool doneReceived;
    bool connected;
    bool ended;
    CoopNetEnd end;
} Flow;

static uint16_t Pattern_Size(uint32_t index) {
    // Every 97th message is as large as the transport allows.
    if (index % 97 == 96) {
        return COOP_NET_MAX_MESSAGE;
    }
    return (uint16_t) (8 + (index * 37) % 400);
}

static void Pattern_Fill(uint32_t index, uint8_t* out, uint16_t size) {
    uint32_t x = index * 2654435761u + 1;
    uint16_t i;

    out[0] = (uint8_t) index;
    out[1] = (uint8_t) (index >> 8);
    out[2] = (uint8_t) (index >> 16);
    out[3] = (uint8_t) (index >> 24);
    for (i = 4; i < size; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        out[i] = (uint8_t) x;
    }
}

static bool Pattern_Check(const uint8_t* data, uint16_t size, uint32_t* index) {
    uint8_t expected[COOP_NET_MAX_MESSAGE];

    if (size < 4) {
        return false;
    }
    *index = data[0] | (data[1] << 8) | (data[2] << 16) | ((uint32_t) data[3] << 24);
    if (Pattern_Size(*index) != size) {
        return false;
    }
    Pattern_Fill(*index, expected, size);
    return memcmp(expected, data, size) == 0;
}

static void Flow_Init(Flow* f, uint32_t unreliableCount) {
    memset(f, 0, sizeof(*f));
    f->seenSize = unreliableCount;
    f->seen = calloc(unreliableCount + 1, 1);
}

static void Flow_Free(Flow* f) {
    free(f->seen);
    f->seen = NULL;
}

static void Flow_OnMessage(Flow* f, const CoopNetEvent* ev) {
    uint32_t index = 0;

    if (ev->msgId == COOP_MSG_TEST_DONE) {
        f->doneReceived = true;
        return;
    }
    if (!Pattern_Check(ev->data, ev->size, &index)) {
        f->corrupt++;
        return;
    }
    if (ev->msgId == COOP_MSG_TEST_RELIABLE) {
        if (index != f->reliableReceived) {
            f->outOfOrder++;
        }
        f->reliableReceived++;
    } else if (ev->msgId == COOP_MSG_TEST_UNRELIABLE) {
        if (index >= f->seenSize) {
            f->corrupt++;
        } else if (f->seen[index]) {
            f->unreliableDuplicates++;
        } else {
            f->seen[index] = 1;
            f->unreliableReceived++;
        }
    } else {
        f->corrupt++;
    }
}

// ---- Virtual network ----

#define SIM_MAX_PACKETS 4096
#define SIM_HOST 1
#define SIM_CLIENT 2

typedef struct {
    uint32_t due;
    CoopNetAddr from;
    CoopNetAddr to;
    uint16_t size;
    uint8_t data[COOP_NET_MAX_DATAGRAM];
} SimPacket;

typedef struct {
    uint32_t lossPercent;
    uint32_t burstPercent;
    uint32_t latencyMs;
    uint32_t jitterMs;
    uint32_t duplicatePercent;
    uint32_t corruptPercent;
    uint32_t reorderPercent;
    uint32_t blackoutStart;
    uint32_t blackoutEnd;
} SimLink;

typedef struct {
    SimPacket* packets;
    uint32_t count;
    SimLink link;
    uint32_t now;
    uint64_t rng;
    bool burst;
    uint32_t dropped;
    uint32_t duplicated;
    uint32_t corrupted;
    uint32_t overflowed;
} SimNet;

static SimNet sNet;

static uint32_t Sim_Random(void) {
    // SplitMix64
    uint64_t z = (sNet.rng += 0x9E3779B97F4A7C15ull);

    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return (uint32_t) ((z ^ (z >> 31)) >> 32);
}

static bool Sim_Chance(uint32_t percent) {
    return Sim_Random() % 100 < percent;
}

static void Sim_Queue(CoopNetAddr from, CoopNetAddr to, const uint8_t* data, uint16_t size, uint32_t delay) {
    SimPacket* p;

    if (sNet.count == SIM_MAX_PACKETS) {
        sNet.overflowed++;
        return;
    }
    p = &sNet.packets[sNet.count++];
    p->due = sNet.now + delay;
    p->from = from;
    p->to = to;
    p->size = size;
    memcpy(p->data, data, size);
}

static uint32_t Sim_Delay(void) {
    if (Sim_Chance(sNet.link.reorderPercent)) {
        return 0;
    }
    return sNet.link.latencyMs + (sNet.link.jitterMs ? Sim_Random() % (sNet.link.jitterMs + 1) : 0);
}

static void Sim_Tx(void* user, CoopNetAddr to, const uint8_t* data, uint16_t size) {
    CoopNetAddr from = (CoopNetAddr) (uintptr_t) user;
    uint8_t copy[COOP_NET_MAX_DATAGRAM];
    uint32_t elapsed = sNet.now - sNet.link.blackoutStart;

    if (sNet.link.blackoutEnd != sNet.link.blackoutStart && elapsed < sNet.link.blackoutEnd - sNet.link.blackoutStart) {
        sNet.dropped++;
        return;
    }
    if (sNet.burst ? Sim_Chance(30) : Sim_Chance(sNet.link.burstPercent)) {
        sNet.burst = !sNet.burst;
    }
    if (Sim_Chance(sNet.burst ? 60 : sNet.link.lossPercent)) {
        sNet.dropped++;
        return;
    }
    memcpy(copy, data, size);
    if (Sim_Chance(sNet.link.corruptPercent)) {
        copy[Sim_Random() % size] ^= (uint8_t) (1 << (Sim_Random() % 8));
        sNet.corrupted++;
    }
    Sim_Queue(from, to, copy, size, Sim_Delay());
    if (Sim_Chance(sNet.link.duplicatePercent)) {
        Sim_Queue(from, to, copy, size, Sim_Delay());
        sNet.duplicated++;
    }
}

static void Sim_Deliver(CoopNetCore* host, CoopNetCore* client) {
    uint32_t i = 0;

    while (i < sNet.count) {
        SimPacket* p = &sNet.packets[i];

        if ((int32_t) (sNet.now - p->due) < 0) {
            i++;
            continue;
        }
        CoopNetCore_Receive(p->to == SIM_HOST ? host : client, p->from, p->data, p->size, sNet.now);
        *p = sNet.packets[--sNet.count];
    }
}

static void Sim_Reset(uint64_t seed, const SimLink* link, uint32_t startTime) {
    if (sNet.packets == NULL) {
        sNet.packets = malloc(sizeof(SimPacket) * SIM_MAX_PACKETS);
    }
    sNet.count = 0;
    sNet.link = *link;
    sNet.now = startTime;
    sNet.rng = seed;
    sNet.burst = false;
    sNet.dropped = 0;
    sNet.duplicated = 0;
    sNet.corrupted = 0;
    sNet.overflowed = 0;
}

static void Sim_Fail(uint64_t seed, const char* what) {
    printf("sim: FAIL seed %llu: %s\n", (unsigned long long) seed, what);
    exit(1);
}

// Sends the next pattern message on each lane, if one is left. A full reliable window just means trying again later.
static void Flow_SendNext(CoopNetCore* core, int peer, Flow* f, uint32_t reliableCount, uint32_t unreliableCount,
                          uint32_t now) {
    uint8_t buffer[COOP_NET_MAX_MESSAGE];
    uint16_t size;

    if (f->reliableSent < reliableCount) {
        size = Pattern_Size(f->reliableSent);
        Pattern_Fill(f->reliableSent, buffer, size);
        if (CoopNetCore_Send(core, peer, COOP_MSG_TEST_RELIABLE, buffer, size, COOP_NET_RELIABLE, now)) {
            f->reliableSent++;
        }
    } else if (!f->doneSent) {
        f->doneSent = CoopNetCore_Send(core, peer, COOP_MSG_TEST_DONE, NULL, 0, COOP_NET_RELIABLE, now);
    }
    if (f->unreliableSent < unreliableCount) {
        size = Pattern_Size(f->unreliableSent);
        Pattern_Fill(f->unreliableSent, buffer, size);
        if (CoopNetCore_Send(core, peer, COOP_MSG_TEST_UNRELIABLE, buffer, size, COOP_NET_UNRELIABLE, now)) {
            f->unreliableSent++;
        }
    }
}

static void Sim_Drain(uint64_t seed, CoopNetCore* core, Flow* f) {
    CoopNetEvent ev;

    while (CoopNetCore_Poll(core, &ev)) {
        if (ev.type == COOP_NET_EVENT_CONNECTED) {
            if (f->connected) {
                Sim_Fail(seed, "connected twice");
            }
            f->connected = true;
        } else if (ev.type == COOP_NET_EVENT_DISCONNECTED) {
            f->ended = true;
            f->end = ev.end;
        } else if (ev.type == COOP_NET_EVENT_MESSAGE) {
            Flow_OnMessage(f, &ev);
        }
    }
}

static void Sim_CheckFlow(uint64_t seed, const Flow* f, const Flow* sender, uint32_t reliableCount) {
    if (f->outOfOrder != 0) {
        Sim_Fail(seed, "reliable message out of order");
    }
    if (f->corrupt != 0) {
        Sim_Fail(seed, "corrupt message delivered");
    }
    if (f->unreliableDuplicates != 0) {
        Sim_Fail(seed, "unreliable message delivered twice");
    }
    if (f->reliableReceived != reliableCount) {
        Sim_Fail(seed, "reliable message lost or delivered twice");
    }
    if (f->unreliableReceived > sender->unreliableSent) {
        Sim_Fail(seed, "more unreliable messages than were sent");
    }
}

// Both sides send everything; every promise of the lanes is checked at the end.
static void Sim_Exchange(uint64_t seed, const SimLink* link, uint32_t startTime, uint32_t reliableCount,
                         uint32_t unreliableCount, uint32_t stepMs, uint32_t sendEverySteps) {
    CoopNetConfig config = { 0 };
    CoopNetCore* host;
    CoopNetCore* client;
    Flow hostFlow;
    Flow clientFlow;
    uint32_t steps = 0;
    uint32_t limit = 400000 + (reliableCount + unreliableCount) * 40;

    Sim_Reset(seed, link, startTime);
    config.timeoutMs = 8000;
    Flow_Init(&hostFlow, unreliableCount);
    Flow_Init(&clientFlow, unreliableCount);
    host = CoopNetCore_CreateHost(&config, Sim_Tx, (void*) (uintptr_t) SIM_HOST);
    client = CoopNetCore_CreateClient(&config, Sim_Tx, (void*) (uintptr_t) SIM_CLIENT, SIM_HOST,
                                      (uint32_t) seed * 2 + 1, sNet.now);

    for (;;) {
        CoopNetStats hostStats = { 0 };
        CoopNetStats clientStats = { 0 };

        Sim_Deliver(host, client);
        CoopNetCore_Update(host, sNet.now);
        CoopNetCore_Update(client, sNet.now);
        Sim_Drain(seed, host, &hostFlow);
        Sim_Drain(seed, client, &clientFlow);
        if (hostFlow.ended || clientFlow.ended) {
            Sim_Fail(seed, "connection ended during the exchange");
        }
        if (steps % sendEverySteps == 0) {
            if (hostFlow.connected) {
                Flow_SendNext(host, 0, &hostFlow, reliableCount, unreliableCount, sNet.now);
            }
            if (clientFlow.connected) {
                Flow_SendNext(client, 0, &clientFlow, reliableCount, unreliableCount, sNet.now);
            }
        }
        CoopNetCore_Stats(host, 0, &hostStats);
        CoopNetCore_Stats(client, 0, &clientStats);
        if (hostFlow.doneReceived && clientFlow.doneReceived && hostFlow.doneSent && clientFlow.doneSent &&
            hostFlow.unreliableSent == unreliableCount && clientFlow.unreliableSent == unreliableCount &&
            hostStats.pending == 0 && clientStats.pending == 0) {
            break;
        }
        sNet.now += stepMs;
        if (++steps > limit) {
            Sim_Fail(seed, "exchange did not finish");
        }
    }

    Sim_CheckFlow(seed, &hostFlow, &clientFlow, reliableCount);
    Sim_CheckFlow(seed, &clientFlow, &hostFlow, reliableCount);
    Flow_Free(&hostFlow);
    Flow_Free(&clientFlow);
    CoopNetCore_Destroy(host);
    CoopNetCore_Destroy(client);
}

// One-way traffic: the acks have no data to ride on, so they must go out on their own. If they waited for the next
// ping, every message would be resent on a clean link.
static void Sim_OneWayAcks(void) {
    CoopNetConfig config = { 0 };
    SimLink clean = { 0 };
    CoopNetCore* host;
    CoopNetCore* client;
    Flow hostFlow;
    Flow clientFlow;
    CoopNetStats stats = { 0 };
    uint32_t steps;

    clean.latencyMs = 5;
    Sim_Reset(2, &clean, 0);
    Flow_Init(&hostFlow, 0);
    Flow_Init(&clientFlow, 0);
    host = CoopNetCore_CreateHost(&config, Sim_Tx, (void*) (uintptr_t) SIM_HOST);
    client = CoopNetCore_CreateClient(&config, Sim_Tx, (void*) (uintptr_t) SIM_CLIENT, SIM_HOST, 80, sNet.now);

    for (steps = 0; steps < 20000; steps++) {
        Sim_Deliver(host, client);
        CoopNetCore_Update(host, sNet.now);
        CoopNetCore_Update(client, sNet.now);
        Sim_Drain(2, host, &hostFlow);
        Sim_Drain(2, client, &clientFlow);
        if (clientFlow.connected) {
            Flow_SendNext(client, 0, &clientFlow, 300, 0, sNet.now);
        }
        CoopNetCore_Stats(client, 0, &stats);
        if (clientFlow.doneSent && stats.pending == 0) {
            break;
        }
        sNet.now += 2;
    }
    if (hostFlow.reliableReceived != 300 || !hostFlow.doneReceived) {
        Sim_Fail(2, "one-way traffic did not arrive");
    }
    if (stats.resent != 0) {
        Sim_Fail(2, "messages were resent on a clean link, so acks did not go out on their own");
    }
    Flow_Free(&hostFlow);
    Flow_Free(&clientFlow);
    CoopNetCore_Destroy(host);
    CoopNetCore_Destroy(client);
}

// Captures datagrams instead of delivering them, for the tests that look at single messages.
static uint8_t sCaptured[COOP_NET_MAX_DATAGRAM];
static uint16_t sCapturedSize;

static void Capture_Tx(void* user, CoopNetAddr to, const uint8_t* data, uint16_t size) {
    (void) user;
    (void) to;
    memcpy(sCaptured, data, size);
    sCapturedSize = size;
}

static void Datagram_Seal(uint8_t* data, uint16_t size) {
    uint32_t crc = CoopNetCore_Crc32(data + 4, size - 4);

    data[0] = (uint8_t) crc;
    data[1] = (uint8_t) (crc >> 8);
    data[2] = (uint8_t) (crc >> 16);
    data[3] = (uint8_t) (crc >> 24);
}

// A client of another protocol version is refused, and that client reports why.
static void Sim_VersionMismatch(void) {
    CoopNetConfig config = { 0 };
    CoopNetCore* host = CoopNetCore_CreateHost(&config, Capture_Tx, NULL);
    CoopNetCore* client = CoopNetCore_CreateClient(&config, Capture_Tx, NULL, SIM_HOST, 77, 0);
    CoopNetEvent ev;
    uint8_t request[COOP_NET_MAX_DATAGRAM];
    uint16_t size;

    CoopNetCore_Update(client, 0);
    size = sCapturedSize;
    if (size < COOP_NET_HEADER_SIZE) {
        Sim_Fail(0, "client sent no connect request");
    }
    memcpy(request, sCaptured, size);
    request[6] = COOP_PROTOCOL_VERSION + 1;
    Datagram_Seal(request, size);

    sCapturedSize = 0;
    CoopNetCore_Receive(host, SIM_CLIENT, request, size, 1);
    if (sCapturedSize == 0) {
        Sim_Fail(0, "host did not answer a client of another version");
    }
    if (CoopNetCore_Poll(host, &ev)) {
        Sim_Fail(0, "host accepted a client of another version");
    }
    // The refusal has to be readable by the other version too.
    sCaptured[6] = COOP_PROTOCOL_VERSION + 1;
    Datagram_Seal(sCaptured, sCapturedSize);
    CoopNetCore_Receive(client, SIM_HOST, sCaptured, sCapturedSize, 2);
    if (!CoopNetCore_Poll(client, &ev) || ev.type != COOP_NET_EVENT_DISCONNECTED ||
        ev.end != COOP_NET_END_REFUSED_VERSION) {
        Sim_Fail(0, "client did not report the version refusal");
    }
    CoopNetCore_Destroy(host);
    CoopNetCore_Destroy(client);
}

// A client the host no longer holds learns of it from its next datagram: the host answers a datagram of a connection
// it does not hold with a goodbye for that connection, and the client ends as closed, at once, not by its time-out.
// So a client that was dropped while it could not hear can come back (the session survives). A goodbye that is itself
// stray gets no answer.
static void Sim_StrayGoodbye(void) {
    CoopNetConfig config = { 0 };
    CoopNetCore* host = CoopNetCore_CreateHost(&config, Capture_Tx, NULL);
    CoopNetCore* client = CoopNetCore_CreateClient(&config, Capture_Tx, NULL, SIM_HOST, 91, 0);
    CoopNetEvent ev;
    CoopNetStats stats;
    uint8_t datagram[COOP_NET_MAX_DATAGRAM];
    uint16_t size;

    CoopNetCore_Update(client, 0);
    size = sCapturedSize;
    memcpy(datagram, sCaptured, size);
    sCapturedSize = 0;
    CoopNetCore_Receive(host, SIM_CLIENT, datagram, size, 1);
    size = sCapturedSize;
    memcpy(datagram, sCaptured, size);
    CoopNetCore_Receive(client, SIM_HOST, datagram, size, 2);
    if (!CoopNetCore_Poll(host, &ev) || ev.type != COOP_NET_EVENT_CONNECTED || !CoopNetCore_Poll(client, &ev) ||
        ev.type != COOP_NET_EVENT_CONNECTED) {
        Sim_Fail(0, "stray goodbye: no connection to start from");
    }

    // The host drops the client, and none of its goodbyes arrives.
    CoopNetCore_Disconnect(host, 0);
    sCapturedSize = 0;
    CoopNetCore_Update(client, 400); // a ping is due
    size = sCapturedSize;
    if (size < COOP_NET_HEADER_SIZE) {
        Sim_Fail(0, "stray goodbye: the client sent nothing to answer");
    }
    memcpy(datagram, sCaptured, size);
    sCapturedSize = 0;
    CoopNetCore_Receive(host, SIM_CLIENT, datagram, size, 401);
    if (sCapturedSize == 0) {
        Sim_Fail(0, "the host did not answer a datagram of a connection it had dropped");
    }
    if (!CoopNetCore_Stats(host, 0, &stats) || stats.strayDatagrams != 1) {
        Sim_Fail(0, "the host did not count the stray datagram");
    }
    if (CoopNetCore_Poll(host, &ev)) {
        Sim_Fail(0, "a stray datagram reached the host's owner");
    }
    size = sCapturedSize;
    memcpy(datagram, sCaptured, size);
    CoopNetCore_Receive(client, SIM_HOST, datagram, size, 402);
    if (!CoopNetCore_Poll(client, &ev) || ev.type != COOP_NET_EVENT_DISCONNECTED || ev.end != COOP_NET_END_CLOSED) {
        Sim_Fail(0, "the client did not end as closed on the host's answer to its stray datagram");
    }

    // That answer is a goodbye. Sent to the host from an address it does not know, it is stray too, and gets nothing.
    sCapturedSize = 0;
    CoopNetCore_Receive(host, SIM_CLIENT + 5, datagram, size, 403);
    if (sCapturedSize != 0) {
        Sim_Fail(0, "the host answered a stray goodbye");
    }
    if (!CoopNetCore_Stats(host, 0, &stats) || stats.strayDatagrams != 2) {
        Sim_Fail(0, "the host did not count the stray goodbye");
    }
    CoopNetCore_Destroy(host);
    CoopNetCore_Destroy(client);
}

// The checksum protects every byte of a datagram, and a bad one has no effect at all.
static void Sim_Checksum(void) {
    CoopNetConfig config = { 0 };
    CoopNetCore* host = CoopNetCore_CreateHost(&config, Capture_Tx, NULL);
    CoopNetCore* client = CoopNetCore_CreateClient(&config, Capture_Tx, NULL, SIM_HOST, 78, 0);
    CoopNetEvent ev;
    CoopNetStats stats = { 0 };
    uint8_t request[COOP_NET_MAX_DATAGRAM];
    uint16_t size;
    uint16_t bit;

    CoopNetCore_Update(client, 0);
    size = sCapturedSize;
    memcpy(request, sCaptured, size);
    for (bit = 0; bit < size * 8; bit++) {
        request[bit / 8] ^= (uint8_t) (1 << (bit % 8));
        CoopNetCore_Receive(host, SIM_CLIENT, request, size, 1);
        request[bit / 8] ^= (uint8_t) (1 << (bit % 8));
    }
    if (CoopNetCore_Poll(host, &ev)) {
        Sim_Fail(0, "a damaged datagram had an effect");
    }
    CoopNetCore_Receive(host, SIM_CLIENT, request, size, 1);
    if (!CoopNetCore_Poll(host, &ev) || ev.type != COOP_NET_EVENT_CONNECTED) {
        Sim_Fail(0, "the undamaged datagram was not accepted");
    }
    CoopNetCore_Stats(host, ev.peer, &stats);
    if (stats.badDatagrams != (uint32_t) size * 8) {
        Sim_Fail(0, "not every damaged datagram was counted");
    }
    CoopNetCore_Destroy(host);
    CoopNetCore_Destroy(client);
}

// Valid checksums over random content: the parser may not crash or read out of bounds. Run under a sanitizer.
static void Sim_Fuzz(uint64_t seed) {
    CoopNetConfig config = { 0 };
    CoopNetCore* host = CoopNetCore_CreateHost(&config, Capture_Tx, NULL);
    CoopNetCore* client = CoopNetCore_CreateClient(&config, Capture_Tx, NULL, SIM_HOST, 79, 0);
    CoopNetEvent ev;
    uint8_t data[COOP_NET_MAX_DATAGRAM + 64];
    uint32_t i;

    sNet.rng = seed;
    CoopNetCore_Update(client, 0);
    CoopNetCore_Receive(host, SIM_CLIENT, sCaptured, sCapturedSize, 1);
    CoopNetCore_Receive(client, SIM_HOST, sCaptured, sCapturedSize, 1);
    for (i = 0; i < 20000; i++) {
        uint32_t size = Sim_Random() % sizeof(data);
        uint32_t j;

        memcpy(data, sCaptured, sCapturedSize);
        for (j = sCapturedSize; j < size; j++) {
            data[j] = (uint8_t) Sim_Random();
        }
        for (j = 0; j < 1 + Sim_Random() % 6; j++) {
            data[4 + Sim_Random() % (COOP_NET_HEADER_SIZE - 4)] = (uint8_t) Sim_Random();
        }
        if (size >= 4 && Sim_Chance(95)) {
            uint32_t crc = CoopNetCore_Crc32(data + 4, size - 4);

            data[0] = (uint8_t) crc;
            data[1] = (uint8_t) (crc >> 8);
            data[2] = (uint8_t) (crc >> 16);
            data[3] = (uint8_t) (crc >> 24);
        }
        CoopNetCore_Receive(Sim_Chance(50) ? host : client, Sim_Chance(50) ? SIM_CLIENT : SIM_HOST, data, size, i);
        CoopNetCore_Update(host, i);
        CoopNetCore_Update(client, i);
        while (CoopNetCore_Poll(host, &ev) || CoopNetCore_Poll(client, &ev)) {}
    }
    CoopNetCore_Destroy(host);
    CoopNetCore_Destroy(client);
}

static int Sim_Main(uint32_t seeds) {
    uint64_t totalDropped = 0;
    uint64_t totalDuplicated = 0;
    uint64_t totalCorrupted = 0;
    uint32_t overflowed = 0;
    uint32_t seed;
    SimLink clean = { 0 };

    Sim_VersionMismatch();
    Sim_StrayGoodbye();
    Sim_Checksum();
    Sim_OneWayAcks();

    // More messages than the 16-bit sequence numbers can count, on both lanes.
    clean.latencyMs = 5;
    Sim_Exchange(1, &clean, 0, 70000, 70000, 1, 1);
    totalDropped += sNet.dropped;

    for (seed = 1; seed <= seeds; seed++) {
        SimLink link = { 0 };
        uint32_t startTime;

        sNet.rng = seed * 0x100000001B3ull;
        link.lossPercent = Sim_Random() % 25;
        link.burstPercent = Sim_Random() % 6;
        link.latencyMs = 1 + Sim_Random() % 150;
        link.jitterMs = Sim_Random() % 100;
        link.duplicatePercent = Sim_Random() % 12;
        link.corruptPercent = Sim_Random() % 6;
        link.reorderPercent = Sim_Random() % 15;
        // Every fourth run starts just before the 32-bit millisecond clock wraps.
        startTime = (seed % 4 == 0) ? 0xFFFFFFFFu - Sim_Random() % 4000 : Sim_Random() % 100000;
        if (seed % 3 == 0) {
            link.blackoutStart = startTime + 1000 + Sim_Random() % 1000;
            link.blackoutEnd = link.blackoutStart + 500 + Sim_Random() % 3000;
        }
        // Every other run polls once per game frame, as the game will. The rest poll far more often than that.
        Sim_Exchange(seed, &link, startTime, 300, 300, (seed % 2 == 0) ? 33 : 2, 1 + seed % 8);
        totalDropped += sNet.dropped;
        totalDuplicated += sNet.duplicated;
        totalCorrupted += sNet.corrupted;
        overflowed += sNet.overflowed;
        Sim_Fuzz(seed);
    }

    if (totalDropped == 0 || totalDuplicated == 0 || totalCorrupted == 0) {
        Sim_Fail(0, "the virtual network did nothing, so this run proves nothing");
    }
    if (overflowed != 0) {
        Sim_Fail(0, "the virtual network ran out of room");
    }
    printf("sim: PASS %u seeds, dropped %llu, duplicated %llu, corrupted %llu\n", seeds,
           (unsigned long long) totalDropped, (unsigned long long) totalDuplicated,
           (unsigned long long) totalCorrupted);
    return 0;
}

// ---- Real processes ----

typedef struct {
    const char* connect;
    const char* portFile;
    const char* out;
    uint32_t port;
    uint32_t peers;
    uint32_t reliable;
    uint32_t unreliable;
    uint32_t rate;
    uint32_t timeoutMs;
    uint32_t maxSeconds;
    uint32_t closeAfterMs;
    bool hold;
} BotOptions;

static const char* End_Name(CoopNetEnd end) {
    switch (end) {
        case COOP_NET_END_CLOSED:
            return "closed";
        case COOP_NET_END_TIMEOUT:
            return "timeout";
        case COOP_NET_END_REFUSED_FULL:
            return "refused_full";
        case COOP_NET_END_REFUSED_VERSION:
            return "refused_version";
        default:
            return "none";
    }
}

static void Bot_WriteResult(const BotOptions* o, const char* role, const Flow* flows, const CoopNetStats* stats,
                            uint32_t count, uint32_t elapsedMs, bool finished) {
    FILE* f = o->out ? fopen(o->out, "w") : stdout;
    uint32_t i;

    if (f == NULL) {
        return;
    }
    fprintf(f, "{\"role\":\"%s\",\"elapsed_ms\":%u,\"finished\":%s,\"peers\":[", role, elapsedMs,
            finished ? "true" : "false");
    for (i = 0; i < count; i++) {
        const Flow* fl = &flows[i];
        const CoopNetStats* s = &stats[i];

        fprintf(f,
                "%s{\"connected\":%s,\"end\":\"%s\",\"reliable_sent\":%u,\"reliable_received\":%u,"
                "\"unreliable_sent\":%u,\"unreliable_received\":%u,\"out_of_order\":%u,\"corrupt\":%u,"
                "\"unreliable_duplicates\":%u,\"done_received\":%s,\"sent\":%u,\"received\":%u,\"resent\":%u,"
                "\"pending\":%u,\"duplicates\":%u,\"overflow\":%u,\"rtt_ms\":%u,\"rtt_min_ms\":%u,\"rtt_max_ms\":%u,"
                "\"bad_datagrams\":%u,\"stray_datagrams\":%u}",
                i ? "," : "", fl->connected ? "true" : "false", End_Name(fl->end), fl->reliableSent,
                fl->reliableReceived, fl->unreliableSent, fl->unreliableReceived, fl->outOfOrder, fl->corrupt,
                fl->unreliableDuplicates, fl->doneReceived ? "true" : "false", s->sent, s->received, s->resent,
                s->pending, s->duplicates, s->overflow, s->rttMs, s->rttMinMs, s->rttMaxMs, s->badDatagrams,
                s->strayDatagrams);
    }
    fprintf(f, "]}\n");
    if (o->out) {
        fclose(f);
    }
}

static void Bot_Send(CoopTransport* t, int peer, Flow* f, const BotOptions* o) {
    uint8_t buffer[COOP_NET_MAX_MESSAGE];
    uint16_t size;

    if (f->reliableSent < o->reliable) {
        size = Pattern_Size(f->reliableSent);
        Pattern_Fill(f->reliableSent, buffer, size);
        if (t->ops->send(t, peer, COOP_MSG_TEST_RELIABLE, buffer, size, COOP_NET_RELIABLE)) {
            f->reliableSent++;
        }
    } else if (!f->doneSent) {
        f->doneSent = t->ops->send(t, peer, COOP_MSG_TEST_DONE, NULL, 0, COOP_NET_RELIABLE);
    }
    if (f->unreliableSent < o->unreliable) {
        size = Pattern_Size(f->unreliableSent);
        Pattern_Fill(f->unreliableSent, buffer, size);
        if (t->ops->send(t, peer, COOP_MSG_TEST_UNRELIABLE, buffer, size, COOP_NET_UNRELIABLE)) {
            f->unreliableSent++;
        }
    }
}

static int Bot_Main(const BotOptions* o) {
    CoopNetConfig config = { 0 };
    CoopTransport* t;
    Flow flows[COOP_NET_MAX_PEERS];
    CoopNetStats stats[COOP_NET_MAX_PEERS];
    bool isHost = o->connect == NULL;
    uint32_t count = isHost ? o->peers : 1;
    uint32_t start = SDL_GetTicks();
    uint32_t connectedAt = 0;
    uint32_t finishedAt = 0;
    uint32_t nextSend = start;
    uint32_t interval = 1000 / (o->rate ? o->rate : 60);
    bool finished = false;
    uint32_t i;

    config.timeoutMs = o->timeoutMs;
    config.maxPeers = (uint8_t) o->peers;
    memset(stats, 0, sizeof(stats));
    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        Flow_Init(&flows[i], o->unreliable);
    }

    if (isHost) {
        FILE* f;

        t = CoopNetSdl_Host((uint16_t) o->port, &config);
        if (t == NULL) {
            fprintf(stderr, "host failed: %s\n", SDL_GetError());
            return 2;
        }
        f = fopen(o->portFile, "w");
        if (f == NULL) {
            return 2;
        }
        fprintf(f, "%u\n", CoopNetSdl_Port(t));
        fclose(f);
    } else {
        char hostName[128];
        const char* colon = strrchr(o->connect, ':');

        if (colon == NULL || (size_t) (colon - o->connect) >= sizeof(hostName)) {
            return 2;
        }
        memcpy(hostName, o->connect, colon - o->connect);
        hostName[colon - o->connect] = '\0';
        t = CoopNetSdl_Connect(hostName, (uint16_t) atoi(colon + 1), &config);
        if (t == NULL) {
            fprintf(stderr, "connect failed: %s\n", SDL_GetError());
            return 2;
        }
    }

    for (;;) {
        CoopNetEvent ev;
        uint32_t now = SDL_GetTicks();
        uint32_t ended = 0;
        uint32_t done = 0;

        while (t->ops->poll(t, &ev)) {
            Flow* f = &flows[ev.peer];

            if (ev.type == COOP_NET_EVENT_CONNECTED) {
                f->connected = true;
                connectedAt = now;
            } else if (ev.type == COOP_NET_EVENT_DISCONNECTED) {
                t->ops->stats(t, ev.peer, &stats[ev.peer]);
                f->ended = true;
                f->end = ev.end;
            } else if (ev.type == COOP_NET_EVENT_MESSAGE) {
                Flow_OnMessage(f, &ev);
            }
        }

        if ((int32_t) (now - nextSend) >= 0) {
            nextSend = now + interval;
            for (i = 0; i < count; i++) {
                if (flows[i].connected && !flows[i].ended && !o->hold) {
                    Bot_Send(t, (int) i, &flows[i], o);
                }
            }
        }

        for (i = 0; i < count; i++) {
            if (flows[i].ended) {
                ended++;
                continue;
            }
            if (flows[i].connected) {
                t->ops->stats(t, (int) i, &stats[i]);
                if (flows[i].doneSent && flows[i].doneReceived && stats[i].pending == 0 &&
                    flows[i].unreliableSent == o->unreliable) {
                    done++;
                }
            }
        }

        if (ended == count) {
            finished = true;
            break;
        }
        if (!o->hold && ended + done == count && finishedAt == 0) {
            finishedAt = now;
        }
        // Stay a while after finishing, so the last acks reach the peer.
        if (finishedAt != 0 && now - finishedAt > 1000) {
            finished = true;
            break;
        }
        if (o->closeAfterMs != 0 && connectedAt != 0 && now - connectedAt > o->closeAfterMs) {
            finished = true;
            break;
        }
        if (now - start > o->maxSeconds * 1000) {
            break;
        }
        SDL_Delay(1);
    }

    for (i = 0; i < count; i++) {
        if (!flows[i].ended) {
            t->ops->stats(t, (int) i, &stats[i]);
        }
    }
    Bot_WriteResult(o, isHost ? "host" : "client", flows, stats, count, SDL_GetTicks() - start, finished);
    t->ops->close(t);
    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        Flow_Free(&flows[i]);
    }
    return 0;
}

int main(int argc, char** argv) {
    BotOptions o;
    uint32_t seeds = 200;
    int i;

    memset(&o, 0, sizeof(o));
    o.peers = 1;
    o.rate = 60;
    o.maxSeconds = 120;

    if (argc < 2) {
        fprintf(stderr, "usage: coop_net_test sim|host|client [options], see the top of coop_net_test.c\n");
        return 2;
    }
    for (i = 2; i < argc; i++) {
        const char* key = argv[i];
        const char* value = (i + 1 < argc) ? argv[i + 1] : "";

        if (strcmp(key, "--hold") == 0) {
            o.hold = true;
            continue;
        }
        i++;
        if (strcmp(key, "--seeds") == 0) {
            seeds = (uint32_t) atoi(value);
        } else if (strcmp(key, "--connect") == 0) {
            o.connect = value;
        } else if (strcmp(key, "--port-file") == 0) {
            o.portFile = value;
        } else if (strcmp(key, "--out") == 0) {
            o.out = value;
        } else if (strcmp(key, "--port") == 0) {
            o.port = (uint32_t) atoi(value);
        } else if (strcmp(key, "--peers") == 0) {
            o.peers = (uint32_t) atoi(value);
        } else if (strcmp(key, "--reliable") == 0) {
            o.reliable = (uint32_t) atoi(value);
        } else if (strcmp(key, "--unreliable") == 0) {
            o.unreliable = (uint32_t) atoi(value);
        } else if (strcmp(key, "--rate") == 0) {
            o.rate = (uint32_t) atoi(value);
        } else if (strcmp(key, "--timeout-ms") == 0) {
            o.timeoutMs = (uint32_t) atoi(value);
        } else if (strcmp(key, "--max-seconds") == 0) {
            o.maxSeconds = (uint32_t) atoi(value);
        } else if (strcmp(key, "--close-after-ms") == 0) {
            o.closeAfterMs = (uint32_t) atoi(value);
        } else {
            fprintf(stderr, "unknown option %s\n", key);
            return 2;
        }
    }

    if (strcmp(argv[1], "sim") == 0) {
        return Sim_Main(seeds);
    }
    if (strcmp(argv[1], "host") == 0 && o.portFile != NULL && o.peers >= 1 && o.peers <= COOP_NET_MAX_PEERS) {
        return Bot_Main(&o);
    }
    if (strcmp(argv[1], "client") == 0 && o.connect != NULL) {
        o.peers = 1;
        return Bot_Main(&o);
    }
    fprintf(stderr, "bad arguments, see the top of coop_net_test.c\n");
    return 2;
}
