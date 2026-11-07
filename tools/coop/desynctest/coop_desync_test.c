// Tests src/port/coop/sync/desync.c with no game and no sockets (WP-23): host and clients in a virtual network with
// its own clock and lag, where the reliable lane delivers in order. Every game's state hash is a function of the
// frame, and a poke makes one game's differ from a frame on, so what every game must find is known: the frame, the
// pair, the regions. tools/coop/lockstep_test.py runs this as `desync_sim`.
//
// Prints one line per scenario and exits 0 only when all passed.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/coop/net/protocol.h"
#include "port/coop/sync/desync.h"

#define NODES 4
#define QUEUE 65536
#define FRAME_MS 33
#define NO_POKE 0xFFFFFFFFu

typedef struct {
    uint32_t atMs;
    int from;
    int to;
    uint16_t msgId;
    uint16_t size;
    uint8_t data[COOP_NET_MAX_MESSAGE];
} Packet;

typedef struct {
    CoopDesync desync;
    bool dead;        // sends nothing any more
    bool muteDesync;  // sends no SYNC_DESYNC: a game that goes away between its batch and its answer
    uint32_t extraMs; // more lag on everything to and from this node
    bool stuck;       // runs no more frames
    uint32_t frame;
    uint32_t nextFrameMs;
    uint32_t pokeFrame; // this game's hash differs from here on
    uint32_t pokeRegion;
    bool pokeOnce;       // only on that frame
    uint32_t doneMs;     // when Done first held, 0 if never
    bool answeredAtDone; // whether the other side had answered by then
    uint32_t sent;
} Node;

static Packet sQueue[QUEUE];
static int sQueued;
static Node sNodes[NODES];
static int sCount;
static uint32_t sNow;
static uint32_t sLatencyMs;
static uint32_t sDelay; // a node runs a frame only once every other node is within this many frames
static uint32_t sFailures;
static uint32_t sChecks;

static void Fail(const char* text, uint32_t a, uint32_t b) {
    if (sFailures++ < 10) {
        printf("  FAILED: %s (%u, %u)\n", text, (unsigned) a, (unsigned) b);
    }
}

static void Check(bool ok, const char* text, uint32_t a, uint32_t b) {
    sChecks++;
    if (!ok) {
        Fail(text, a, b);
    }
}

// Host is node 0 and talks to client k at link k - 1; a client talks to the host at link 0.
static int Node_Of(int from, int link) {
    return (from == 0) ? link + 1 : 0;
}

static int Link_To(int from, int to) {
    return (from == 0) ? to - 1 : 0;
}

static bool Send(void* user, int peer, uint16_t msgId, const void* data, uint16_t size) {
    int from = (int) (intptr_t) user;
    Packet* p;

    sNodes[from].sent++;
    if (sNodes[from].dead || (sNodes[from].muteDesync && (msgId == COOP_MSG_SYNC_DESYNC)) || (sQueued >= QUEUE)) {
        return true;
    }
    p = &sQueue[sQueued++];
    p->from = from;
    p->to = Node_Of(from, peer);
    p->atMs = sNow + sLatencyMs + sNodes[from].extraMs + sNodes[p->to].extraMs;
    p->msgId = msgId;
    p->size = size;
    memcpy(p->data, data, size);
    return true;
}

static void Hash_Of(int node, uint32_t frame, CoopStateHash* out) {
    Node* n = &sNodes[node];
    int i;

    for (i = 0; i < COOP_HASH_REGION_COUNT; i++) {
        out->region[i] = 0x9E3779B97F4A7C15ULL * (frame + 1) + 0x100000001B3ULL * (uint64_t) (i + 1);
    }
    if ((frame >= n->pokeFrame) && (!n->pokeOnce || (frame == n->pokeFrame))) {
        out->region[n->pokeRegion] ^= 1;
    }
    out->total = 0xCBF29CE484222325ULL;
    for (i = 0; i < COOP_HASH_REGION_COUNT; i++) {
        out->total ^= out->region[i];
        out->total *= 0x100000001B3ULL;
    }
}

static void Reset(int count, uint32_t latencyMs, uint32_t delay) {
    int i;

    memset(sNodes, 0, sizeof(sNodes));
    sQueued = 0;
    sCount = count;
    sNow = 0;
    sLatencyMs = latencyMs;
    sDelay = delay;
    for (i = 0; i < count; i++) {
        CoopDesyncConfig config;

        memset(&config, 0, sizeof(config));
        config.send = Send;
        config.user = (void*) (intptr_t) i;
        config.self = i;
        config.host = (i == 0);
        config.present = (1u << count) - 1;
        config.period = COOP_DESYNC_PERIOD;
        CoopDesync_Start(&sNodes[i].desync, &config);
        sNodes[i].pokeFrame = NO_POKE;
        sNodes[i].nextFrameMs = FRAME_MS;
    }
}

static void Deliver(void) {
    int i = 0;

    while (i < sQueued) {
        Packet* p = &sQueue[i];

        if (p->atMs <= sNow) {
            Node* to = &sNodes[p->to];

            Check(CoopDesync_Receive(&to->desync, Link_To(p->to, p->from), p->msgId, p->data, p->size, sNow),
                  "a message of the sim was refused", p->to, p->msgId);
            memmove(&sQueue[i], &sQueue[i + 1], (sQueued - i - 1) * sizeof(Packet));
            sQueued--;
        } else {
            i++;
        }
    }
}

static bool Can_Run(int node) {
    int i;

    for (i = 0; i < sCount; i++) {
        if ((i != node) && !sNodes[i].stuck && (sNodes[node].frame > sNodes[i].frame + sDelay)) {
            return false;
        }
    }
    return true;
}

static void Run_Frame(int node) {
    Node* n = &sNodes[node];
    CoopStateHash hash;
    uint8_t state[4] = { 7, 0, (uint8_t) (n->frame > 1500), 1 };

    Hash_Of(node, n->frame, &hash);
    CoopDesync_Record(&n->desync, n->frame, &hash, state, sNow);
    n->frame++;
}

// Runs the sim until every node has `frames` or has been done for a while. Returns the time it took.
static void Run(uint32_t frames) {
    uint32_t idle = 0;

    while (idle < 3000) {
        int i;
        bool moved = false;

        sNow++;
        Deliver();
        for (i = 0; i < sCount; i++) {
            Node* n = &sNodes[i];

            if ((n->doneMs == 0) && CoopDesync_Done(&n->desync, sNow)) {
                n->doneMs = sNow;
                n->answeredAtDone = n->desync.answered;
            }
            if (!n->stuck && (n->frame < frames) && (sNow >= n->nextFrameMs) && Can_Run(i)) {
                Run_Frame(i);
                n->nextFrameMs = sNow + FRAME_MS;
                moved = true;
            }
        }
        idle = moved ? 0 : idle + 1;
        if (!moved && (sQueued == 0)) {
            bool waiting = false;

            for (i = 0; i < sCount; i++) {
                if (sNodes[i].desync.found && (sNodes[i].doneMs == 0)) {
                    waiting = true; // the grace runs
                }
            }
            if (!waiting) {
                int done = 0;

                for (i = 0; i < sCount; i++) {
                    if (sNodes[i].frame >= frames) {
                        done++;
                    }
                }
                if (done == sCount) {
                    return;
                }
            }
        }
    }
}

static void Expect_Found(int node, uint32_t frame, int32_t a, int32_t b, int32_t from, uint32_t regions) {
    const CoopDesync* d = &sNodes[node].desync;
    bool inPair = (node == a) || (node == b);

    Check(d->found, "a desync was not found", node, frame);
    if (!d->found) {
        return;
    }
    Check(d->frame == frame, "the first differing frame", node, d->frame);
    Check((d->pair[0] == a) && (d->pair[1] == b), "the pair", node, (uint32_t) (d->pair[0] * 10 + d->pair[1]));
    Check(d->foundFrom == from, "who showed it", node, (uint32_t) d->foundFrom);
    Check(d->sent, "a SYNC_DESYNC went out", node, 0);
    Check(d->answered, "the other side answered", node, 0);
    Check(d->mine.held, "this game held the frame", node, frame);
    Check(d->theirs.held, "the other side held the frame", node, frame);
    Check(CoopDesync_Regions(d) == regions, "the regions", node, CoopDesync_Regions(d));
    Check(sNodes[node].doneMs != 0, "done", node, 0);
    if (inPair) {
        Check(sNodes[node].doneMs < d->foundMs + COOP_DESYNC_GRACE_MS, "done before the grace was over", node,
              sNodes[node].doneMs - d->foundMs);
        Check(sNodes[node].answeredAtDone, "done only once the other side had answered", node, 0);
    } else {
        Check(sNodes[node].doneMs >= d->foundMs + COOP_DESYNC_GRACE_MS, "a game outside the pair waits the grace", node,
              sNodes[node].doneMs - d->foundMs);
    }
}

static void Expect_Clean(int node, uint32_t frames) {
    const CoopDesync* d = &sNodes[node].desync;

    Check(!d->found, "a desync was found in a clean run", node, d->frame);
    Check(d->unchecked == 0, "frames went unchecked", node, d->unchecked);
    // Everyone this game talks to sent totals for every frame but the last partial batch.
    Check(d->checked + COOP_DESYNC_PERIOD >= (frames / COOP_DESYNC_PERIOD) * COOP_DESYNC_PERIOD,
          "too few frames checked", node, d->checked);
    Check(sNodes[node].doneMs == 0, "done without a desync", node, 0);
}

static void Scenario(const char* name) {
    printf("%s\n", name);
    fflush(stdout);
}

static void sc_clean(void) {
    Scenario("clean: two players, 3000 frames, 60 ms of lag");
    Reset(2, 60, 3);
    Run(3000);
    Expect_Clean(0, 3000);
    Expect_Clean(1, 3000);
    Check(sNodes[0].sent == 3000 / COOP_DESYNC_PERIOD, "the host sent one batch per period", sNodes[0].sent, 0);
}

static void sc_poke(void) {
    Scenario("poke: the client's region 3 differs from frame 777 on");
    Reset(2, 60, 3);
    sNodes[1].pokeFrame = 777;
    sNodes[1].pokeRegion = 3;
    Run(1500);
    Expect_Found(0, 777, 0, 1, 1, 1u << 3);
    Expect_Found(1, 777, 0, 1, 0, 1u << 3);
    Check(sNodes[0].desync.haveTheirTotal && sNodes[1].desync.haveTheirTotal, "the batch showed the total", 0, 0);
}

static void sc_poke_once(void) {
    Scenario("poke once: one frame differs and the next is equal again");
    Reset(2, 10, 3);
    sNodes[0].pokeFrame = 1234;
    sNodes[0].pokeRegion = 10;
    sNodes[0].pokeOnce = true;
    Run(1500);
    Expect_Found(0, 1234, 0, 1, 1, 1u << 10);
    Expect_Found(1, 1234, 0, 1, 0, 1u << 10);
}

static void sc_first_frame(void) {
    Scenario("first frame: the host differs from frame 0");
    Reset(2, 10, 3);
    sNodes[0].pokeFrame = 0;
    sNodes[0].pokeRegion = 0;
    Run(300);
    Expect_Found(0, 0, 0, 1, 1, 1u << 0);
    Expect_Found(1, 0, 0, 1, 0, 1u << 0);
}

static void sc_three(void) {
    Scenario("three players: client 2 differs, and client 1 hears it from the host");
    Reset(3, 40, 3);
    sNodes[2].pokeFrame = 500;
    sNodes[2].pokeRegion = 1;
    Run(900);
    Expect_Found(0, 500, 0, 2, 2, 1u << 1);
    Expect_Found(2, 500, 0, 2, 0, 1u << 1);
    Expect_Found(1, 500, 0, 2, 0, 0); // equal to the host's, so no region differs from what it was told
}

static void sc_four(void) {
    Scenario("four players: two clients differ in different regions, the lower pair wins");
    Reset(4, 40, 3);
    sNodes[1].pokeFrame = 400;
    sNodes[1].pokeRegion = 2;
    sNodes[3].pokeFrame = 400;
    sNodes[3].pokeRegion = 5;
    Run(800);
    // The host compares client 1 first: its batch arrives first, as everything is in order.
    Expect_Found(0, 400, 0, 1, 1, 1u << 2);
    Expect_Found(1, 400, 0, 1, 0, 1u << 2);
    Check(sNodes[2].desync.found && (sNodes[2].desync.frame == 400), "client 2 heard of frame 400", 2,
          sNodes[2].desync.frame);
    Check(sNodes[3].desync.found && (sNodes[3].desync.frame == 400), "client 3 found frame 400", 3,
          sNodes[3].desync.frame);
}

static void sc_slow_pair(void) {
    Scenario("slow pair: the differing client is far away, so client 1's hash reaches the host before its answer");
    Reset(3, 20, 3);
    sNodes[2].pokeFrame = 500;
    sNodes[2].pokeRegion = 6;
    sNodes[2].extraMs = 300;
    Run(900);
    Expect_Found(0, 500, 0, 2, 2, 1u << 6);
    Expect_Found(2, 500, 0, 2, 0, 1u << 6);
    Expect_Found(1, 500, 0, 2, 0, 0);
    Check(sNodes[0].desync.theirPlayer == 2, "the host's `theirs` is the pair's", 0,
          (uint32_t) sNodes[0].desync.theirPlayer);
}

static void sc_other_frame(void) {
    uint8_t message[COOP_DESYNC_MESSAGE_SIZE];
    CoopStateHash hash;
    CoopDesync* host;
    int i;

    Scenario("other frame: the pair's answer names another frame, so no region can be called different");
    Reset(2, 10, 3);
    sNodes[1].pokeFrame = 600;
    sNodes[1].pokeRegion = 4;
    sNodes[1].muteDesync = true;
    Run(640);
    host = &sNodes[0].desync;
    Check(host->found && (host->frame == 600) && !host->answered, "the host found 600 and waits", 0, host->frame);
    // The client's hash of frame 601, as its answer, with the frame it is of.
    Hash_Of(1, 601, &hash);
    memset(message, 0, sizeof(message));
    message[0] = (uint8_t) 601;
    message[1] = (uint8_t) (601 >> 8);
    message[4] = 1;
    message[5] = 0;
    message[6] = 1;
    message[7] = 1;
    memcpy(&message[12], &hash.total, 8);
    for (i = 0; i < COOP_HASH_REGION_COUNT; i++) {
        memcpy(&message[20 + 8 * i], &hash.region[i], 8);
    }
    Check(CoopDesync_Receive(host, 0, COOP_MSG_SYNC_DESYNC, message, sizeof(message), sNow), "the answer is taken", 0,
          0);
    Check(host->answered && (host->theirFrame == 601) && (host->frame == 600), "their frame is kept apart", 0,
          host->theirFrame);
    Check(CoopDesync_Regions(host) == 0, "regions of two different frames", 0, CoopDesync_Regions(host));
}

static void sc_ahead(void) {
    Scenario("ahead: the client runs 16 frames ahead, so batches arrive for frames the host has not run");
    Reset(2, 200, 16);
    sNodes[1].nextFrameMs = 1; // the client gets going first
    sNodes[1].pokeFrame = 1000;
    sNodes[1].pokeRegion = 7;
    Run(1300);
    Expect_Found(0, 1000, 0, 1, 1, 1u << 7);
    Expect_Found(1, 1000, 0, 1, 0, 1u << 7);
    Check(sNodes[0].desync.unchecked == 0, "the host left frames unchecked", sNodes[0].desync.unchecked, 0);
}

static void sc_no_answer(void) {
    Scenario("no answer: the differing client sends its batch and never its hash, and the host ends after the grace");
    Reset(2, 10, 3);
    sNodes[1].pokeFrame = 600;
    sNodes[1].pokeRegion = 4;
    sNodes[1].muteDesync = true;
    Run(900);
    Check(sNodes[0].desync.found && (sNodes[0].desync.frame == 600), "the host found frame 600", 0,
          sNodes[0].desync.frame);
    Check(sNodes[0].desync.sent && !sNodes[0].desync.answered, "no answer came", 0, 0);
    Check(sNodes[0].desync.haveTheirTotal, "the total from the batch is kept", 0, 0);
    Check(sNodes[0].doneMs != 0, "done after the grace", 0, 0);
    Check(sNodes[0].doneMs >= sNodes[0].desync.foundMs + COOP_DESYNC_GRACE_MS, "done only after the grace", 0,
          sNodes[0].doneMs - sNodes[0].desync.foundMs);
    Check(CoopDesync_Regions(&sNodes[0].desync) == 0, "regions without an answer", 0,
          CoopDesync_Regions(&sNodes[0].desync));
}

static void sc_too_old(void) {
    Scenario("too old: a batch for frames beyond the ring is unchecked, not a desync");
    Reset(2, 10, 3);
    Run(200);
    {
        uint8_t message[COOP_DESYNC_HASH_HEADER + 8 * COOP_DESYNC_PERIOD];
        uint32_t i;

        memset(message, 0, sizeof(message));
        message[0] = 5; // frames 5 to 14 of 200: long gone from the ring, with totals that are all wrong
        message[4] = COOP_DESYNC_PERIOD;
        for (i = 0; i < COOP_DESYNC_PERIOD; i++) {
            message[COOP_DESYNC_HASH_HEADER + 8 * i] = 0xEE;
        }
        Check(CoopDesync_Receive(&sNodes[0].desync, 0, COOP_MSG_SYNC_HASH, message, sizeof(message), sNow),
              "an old batch is taken", 0, 0);
        Check(!sNodes[0].desync.found, "an old batch found a desync", 0, sNodes[0].desync.frame);
        Check(sNodes[0].desync.unchecked == COOP_DESYNC_PERIOD, "unchecked frames counted", sNodes[0].desync.unchecked,
              0);
    }
}

static void sc_pending_full(void) {
    Scenario("pending full: a peer more than four batches ahead loses the oldest to unchecked");
    Reset(2, 10, 3);
    Run(50);
    {
        uint8_t message[COOP_DESYNC_HASH_HEADER + 8 * COOP_DESYNC_PERIOD];
        uint32_t b;

        for (b = 0; b < COOP_DESYNC_PENDING + 1; b++) {
            uint32_t first = 100 + b * COOP_DESYNC_PERIOD;

            memset(message, 0, sizeof(message));
            memcpy(&message[0], &first, 4);
            message[4] = COOP_DESYNC_PERIOD;
            Check(CoopDesync_Receive(&sNodes[0].desync, 0, COOP_MSG_SYNC_HASH, message, sizeof(message), sNow),
                  "a future batch is taken", b, 0);
        }
        Check(sNodes[0].desync.links[0].pendingCount == COOP_DESYNC_PENDING, "pending batches kept",
              sNodes[0].desync.links[0].pendingCount, 0);
        Check(sNodes[0].desync.unchecked == COOP_DESYNC_PERIOD, "the oldest went unchecked", sNodes[0].desync.unchecked,
              0);
        Check(!sNodes[0].desync.found, "a future batch found a desync", 0, 0);
    }
}

static void sc_malformed(void) {
    uint8_t message[COOP_DESYNC_MESSAGE_SIZE + 8];
    CoopDesync* host;
    CoopDesync* client;

    Scenario("malformed: what a receiver refuses, and what it lets through");
    Reset(3, 10, 3);
    Run(100);
    host = &sNodes[0].desync;
    client = &sNodes[1].desync;
    memset(message, 0, sizeof(message));
    Check(CoopDesync_Receive(host, 0, COOP_MSG_SYNC_INPUT, message, 3, sNow), "another id is left alone", 0, 0);
    Check(!CoopDesync_Receive(host, 0, COOP_MSG_SYNC_HASH, message, 3, sNow), "a short hash message", 0, 0);
    message[4] = 0;
    Check(!CoopDesync_Receive(host, 0, COOP_MSG_SYNC_HASH, message, COOP_DESYNC_HASH_HEADER, sNow), "count 0", 0, 0);
    message[4] = COOP_DESYNC_MAX_BATCH + 1;
    Check(!CoopDesync_Receive(host, 0, COOP_MSG_SYNC_HASH, message, COOP_DESYNC_HASH_HEADER + 8, sNow),
          "count too large", 0, 0);
    message[4] = 2;
    Check(!CoopDesync_Receive(host, 0, COOP_MSG_SYNC_HASH, message, COOP_DESYNC_HASH_HEADER + 8, sNow),
          "a hash message of the wrong size", 0, 0);
    message[4] = 1;
    Check(!CoopDesync_Receive(host, 0, COOP_MSG_SYNC_HASH, message, COOP_DESYNC_HASH_HEADER + 16, sNow),
          "a hash message with bytes to spare", 0, 0);
    message[4] = 2; // a good message from here on, on links nobody is on
    Check(!CoopDesync_Receive(host, 2, COOP_MSG_SYNC_HASH, message, COOP_DESYNC_HASH_HEADER + 16, sNow),
          "a link nobody is on", 0, 0);
    Check(!CoopDesync_Receive(client, 1, COOP_MSG_SYNC_HASH, message, COOP_DESYNC_HASH_HEADER + 16, sNow),
          "a client's second link", 0, 0);
    Check(!CoopDesync_Receive(host, -1, COOP_MSG_SYNC_HASH, message, COOP_DESYNC_HASH_HEADER + 16, sNow),
          "a negative link", 0, 0);
    // SYNC_DESYNC: frame 50, sender, pair.
    memset(message, 0, sizeof(message));
    message[0] = 50;
    message[4] = 1; // sender: client 1, at the host's link 0
    message[5] = 0;
    message[6] = 1;
    message[7] = 1;
    Check(!CoopDesync_Receive(host, 0, COOP_MSG_SYNC_DESYNC, message, COOP_DESYNC_MESSAGE_SIZE - 1, sNow),
          "a short desync message", 0, 0);
    Check(!CoopDesync_Receive(host, 0, COOP_MSG_SYNC_DESYNC, message, COOP_DESYNC_MESSAGE_SIZE + 8, sNow),
          "a long desync message", 0, 0);
    message[4] = 2;
    Check(!CoopDesync_Receive(host, 0, COOP_MSG_SYNC_DESYNC, message, COOP_DESYNC_MESSAGE_SIZE, sNow),
          "a client speaking for another", 0, 0);
    message[4] = 1;
    message[6] = 0;
    Check(!CoopDesync_Receive(host, 0, COOP_MSG_SYNC_DESYNC, message, COOP_DESYNC_MESSAGE_SIZE, sNow),
          "a pair of one player", 0, 0);
    message[6] = 3;
    Check(!CoopDesync_Receive(host, 0, COOP_MSG_SYNC_DESYNC, message, COOP_DESYNC_MESSAGE_SIZE, sNow),
          "a player who is not in the level", 0, 0);
    Check(!host->found, "a refused message found a desync", 0, 0);
    message[6] = 1;
    Check(CoopDesync_Receive(host, 0, COOP_MSG_SYNC_DESYNC, message, COOP_DESYNC_MESSAGE_SIZE, sNow),
          "a good desync message", 0, 0);
    Check(host->found && (host->frame == 50) && host->answered && host->sent, "the host took it", 0, host->frame);
    Check(CoopDesync_Done(host, sNow), "the host is done at once", 0, 0);
}

int main(void) {
    sc_clean();
    sc_poke();
    sc_poke_once();
    sc_first_frame();
    sc_three();
    sc_four();
    sc_ahead();
    sc_slow_pair();
    sc_other_frame();
    sc_no_answer();
    sc_too_old();
    sc_pending_full();
    sc_malformed();
    printf("desync sim: %u checks, %u failed\n", (unsigned) sChecks, (unsigned) sFailures);
    return (sFailures == 0) ? 0 : 1;
}
