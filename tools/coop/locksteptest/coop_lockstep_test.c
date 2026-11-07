// Tests src/port/coop/sync/lockstep.c with no game and no sockets (WP-22): host and clients in a virtual network with
// its own clock, where messages get lost, late, reordered and doubled. Every player's input is a function of player and
// frame, so every frame on every client can be checked against what it must be. tools/coop/lockstep_test.py runs this.
//
// Prints one line per scenario and exits 0 only when all passed.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/coop/sync/lockstep.h"

#define NODES 4
#define QUEUE 8192
#define FRAME_MS 33
#define MESSAGE_MAX 2048 // room for what a test sends on purpose

typedef struct {
    uint32_t atMs;
    int from;
    int to;
    uint16_t size;
    uint8_t data[COOP_NET_MAX_MESSAGE];
} Packet;

typedef struct {
    uint32_t latencyMs;
    uint32_t jitterMs;
    uint32_t lossPercent;
    uint32_t duplicatePercent;
} Network;

typedef struct {
    CoopLockstep lockstep;
    bool up;          // sends and runs frames
    bool dead;        // says nothing any more
    uint32_t startMs; // gets to the level at this time
    uint32_t nextFrameMs;
    uint32_t frameMs;
    uint32_t started;
} Node;

static Packet sQueue[QUEUE];
static int sQueued;
static Network sNet;
static Node sNodes[NODES];
static int sCount;
static uint32_t sNow;
static uint32_t sRandom;
static uint32_t sFailures;
static uint32_t sChecks;
static uint32_t sDropped;
static uint32_t sLastFrame;

static uint32_t Random(uint32_t below) {
    sRandom = sRandom * 1664525u + 1013904223u;
    return (sRandom >> 8) % below;
}

static void Fail(const char* text, uint32_t a, uint32_t b) {
    if (sFailures++ < 8) {
        printf("  FAILED: %s (%u, %u)\n", text, (unsigned) a, (unsigned) b);
    }
}

static void Check(bool ok, const char* text, uint32_t a, uint32_t b) {
    sChecks++;
    if (!ok) {
        Fail(text, a, b);
    }
}

// What `player` does when it reads its controller on `frame`.
static void Input_Of(int32_t player, uint32_t frame, CoopInputFrame* out) {
    uint32_t mix = (frame + 1) * 2654435761u + (uint32_t) player * 40503u;

    memset(out, 0, sizeof(*out));
    out->hold.button = (uint16_t) mix;
    out->hold.stick_x = (int8_t) (mix >> 8);
    out->hold.stick_y = (int8_t) (mix >> 16);
    out->press.button = (uint16_t) (mix >> 11);
    out->press.stick_x = (int8_t) (mix >> 19);
    out->press.stick_y = (int8_t) (mix >> 23);
}

static bool Same_Pad(const OSContPad* a, const OSContPad* b) {
    return (a->button == b->button) && (a->stick_x == b->stick_x) && (a->stick_y == b->stick_y);
}

// A transport peer index on `from` to the node it reaches: a client has the host at 0, a host has player n at n - 1.
static int Peer_Node(int from, int peer) {
    return (from == 0) ? peer + 1 : 0;
}

static int Node_Peer(int from, int to) {
    return (to == 0) ? from - 1 : 0;
}

static void Queue_Add(int from, int to, const void* data, uint16_t size) {
    Packet* p;

    if (sQueued == QUEUE) {
        sDropped++;
        return;
    }
    p = &sQueue[sQueued++];
    p->atMs = sNow + sNet.latencyMs + ((sNet.jitterMs != 0) ? Random(sNet.jitterMs) : 0);
    p->from = from;
    p->to = to;
    p->size = size;
    memcpy(p->data, data, size);
}

static bool Send(void* user, int peer, const void* data, uint16_t size) {
    int from = (int) (intptr_t) user;
    int to = Peer_Node(from, peer);

    Check(size <= COOP_NET_MAX_MESSAGE, "a message too long for a datagram", size, 0);
    if (sNodes[from].dead || (to >= sCount)) {
        return false;
    }
    if (Random(100) < sNet.lossPercent) {
        sDropped++;
        return true;
    }
    Queue_Add(from, to, data, size);
    if (Random(100) < sNet.duplicatePercent) {
        Queue_Add(from, to, data, size);
    }
    return true;
}

static void Net_Deliver(void) {
    int i = 0;

    while (i < sQueued) {
        Packet* p = &sQueue[i];

        if (p->atMs <= sNow) {
            Packet packet = *p;

            *p = sQueue[--sQueued];
            if (sNodes[packet.to].started && !sNodes[packet.to].dead) {
                bool ok = CoopLockstep_Receive(&sNodes[packet.to].lockstep, Node_Peer(packet.from, packet.to),
                                               packet.data, packet.size, sNow);

                Check(ok, "a good message refused", (uint32_t) packet.from, (uint32_t) packet.to);
            }
        } else {
            i++;
        }
    }
}

// Every player's input on this frame is what it read `delay` frames ago, on every client.
static void Node_CheckFrame(const Node* node, uint32_t delay) {
    uint32_t frame = node->lockstep.frame;
    int32_t player;

    for (player = 0; player < COOP_PLAYERS_MAX; player++) {
        CoopInputFrame got;
        CoopInputFrame want;

        memset(&want, 0, sizeof(want));
        if ((player < sCount) && (frame >= delay)) {
            Input_Of(player, frame - delay, &want);
        }
        CoopLockstep_Read(&node->lockstep, frame, player, &got);
        Check(Same_Pad(&got.hold, &want.hold) && Same_Pad(&got.press, &want.press), "an input is not what it must be",
              frame, (uint32_t) player);
    }
}

static void World_Reset(int count, const Network* net, uint32_t seed) {
    int i;

    memset(sNodes, 0, sizeof(sNodes));
    sCount = count;
    sNet = *net;
    sQueued = 0;
    sNow = 0;
    sRandom = seed;
    sDropped = 0;
    sLastFrame = 0;
    for (i = 0; i < count; i++) {
        sNodes[i].up = true;
        sNodes[i].frameMs = FRAME_MS;
    }
}

static void Node_Start(int i, uint32_t delay) {
    CoopLockstepConfig config;

    memset(&config, 0, sizeof(config));
    config.send = Send;
    config.user = (void*) (intptr_t) i;
    config.self = i;
    config.host = i == 0;
    config.present = (1u << sCount) - 1;
    config.delay = delay;
    CoopLockstep_Start(&sNodes[i].lockstep, &config, sNow);
    sNodes[i].started = 1;
}

// One ms of the world. Returns the lowest frame anybody who is up has reached.
static uint32_t World_Step(uint32_t delay) {
    uint32_t lowest = UINT32_MAX;
    uint32_t highest = 0;
    int i;

    sNow++;
    Net_Deliver();
    for (i = 0; i < sCount; i++) {
        Node* node = &sNodes[i];

        if (!node->up) {
            continue;
        }
        if (!node->started) {
            if (sNow >= node->startMs) {
                Node_Start(i, delay);
            }
            lowest = 0;
            continue;
        }
        CoopLockstep_Tick(&node->lockstep, sNow);
        if ((sNow >= node->nextFrameMs) && (CoopLockstep_Waiting(&node->lockstep) < 0)) {
            CoopInputFrame local;

            Node_CheckFrame(node, delay);
            Input_Of(i, node->lockstep.frame, &local);
            CoopLockstep_Advance(&node->lockstep, &local, sNow);
            node->nextFrameMs = sNow + node->frameMs;
        }
        if (node->lockstep.frame < lowest) {
            lowest = node->lockstep.frame;
        }
        if (node->lockstep.frame > highest) {
            highest = node->lockstep.frame;
        }
    }
    if (highest > lowest) {
        Check((highest - lowest) <= delay, "a client ran more than the delay ahead of another", highest, lowest);
    }
    return lowest;
}

static bool World_Run(uint32_t frames, uint32_t delay, uint32_t limitMs) {
    while (sNow < limitMs) {
        sLastFrame = World_Step(delay);
        if (sLastFrame >= frames) {
            return true;
        }
    }
    return false;
}

// ---- Scenarios ----

static void Sc_Network(int players, const Network* net, uint32_t frames, uint32_t limitMs, uint32_t seed) {
    int i;

    World_Reset(players, net, seed);
    Check(World_Run(frames, COOP_LOCKSTEP_INPUT_DELAY, limitMs), "the frames were not reached", sLastFrame, sNow);
    for (i = 0; i < players; i++) {
        Check(CoopLockstep_StalledOn(&sNodes[i].lockstep, sNow) < 0, "a stall on a working network", (uint32_t) i,
              sNow);
    }
}

static void Sc_Clean(void) {
    static const Network clean = { 1, 0, 0, 0 };
    int players;

    for (players = 2; players <= 4; players++) {
        Sc_Network(players, &clean, 3000, 200000, 1);
    }
}

static void Sc_Lossy(void) {
    static const Network bad = { 35, 25, 5, 2 };
    int players;
    uint32_t seed;

    for (players = 2; players <= 4; players++) {
        for (seed = 1; seed <= 5; seed++) {
            Sc_Network(players, &bad, 2000, 600000, seed);
        }
    }
}

static void Sc_Hell(void) {
    static const Network hell = { 200, 150, 30, 10 };

    Sc_Network(2, &hell, 300, 600000, 7);
    Sc_Network(4, &hell, 300, 600000, 8);
}

// Slow clients set the pace: nobody runs ahead, and a slow client is no stall.
static void Sc_Slow(void) {
    static const Network net = { 10, 5, 0, 0 };

    World_Reset(3, &net, 3);
    sNodes[2].frameMs = 200;
    Check(World_Run(300, COOP_LOCKSTEP_INPUT_DELAY, 200000), "the frames were not reached", sLastFrame, sNow);
    Check(CoopLockstep_StalledOn(&sNodes[0].lockstep, sNow) < 0, "a slow client taken for a stall", 0, sNow);
}

// A game that needs long for the level holds the others at the start, for up to COOP_LOCKSTEP_START_MS.
static void Sc_LateStart(void) {
    static const Network net = { 10, 5, 0, 0 };

    World_Reset(2, &net, 4);
    sNodes[1].startMs = 30000;
    while (sNow < 29000) {
        World_Step(COOP_LOCKSTEP_INPUT_DELAY);
    }
    Check(sNodes[0].lockstep.frame == 0, "the host ran without the other player", sNodes[0].lockstep.frame, 0);
    Check(CoopLockstep_StalledOn(&sNodes[0].lockstep, sNow) < 0, "a slow start taken for a stall", 0, sNow);
    Check(World_Run(200, COOP_LOCKSTEP_INPUT_DELAY, 60000), "no frames after the late start", sLastFrame, sNow);
}

static void Sc_NeverStarts(void) {
    static const Network net = { 10, 5, 0, 0 };

    World_Reset(2, &net, 5);
    sNodes[1].startMs = UINT32_MAX;
    while (sNow < COOP_LOCKSTEP_START_MS - 100) {
        World_Step(COOP_LOCKSTEP_INPUT_DELAY);
    }
    Check(CoopLockstep_StalledOn(&sNodes[0].lockstep, sNow) < 0, "a stall before its time", 0, sNow);
    sNow += 200;
    Check(CoopLockstep_StalledOn(&sNodes[0].lockstep, sNow) == 1, "no stall when a client never came", 0, sNow);
}

// A client that goes quiet in the middle: everyone stops, and names it once the time is up.
static void Sc_Stall(void) {
    static const Network net = { 10, 5, 0, 0 };
    uint32_t reached;
    uint32_t until;

    World_Reset(3, &net, 6);
    Check(World_Run(100, COOP_LOCKSTEP_INPUT_DELAY, 100000), "the frames were not reached", sLastFrame, sNow);
    sNodes[2].dead = true;
    sNodes[2].up = false;
    until = sNow + COOP_LOCKSTEP_STALL_MS - 200;
    while (sNow < until) {
        World_Step(COOP_LOCKSTEP_INPUT_DELAY);
    }
    reached = sNodes[0].lockstep.frame;
    Check(CoopLockstep_StalledOn(&sNodes[0].lockstep, sNow) < 0, "a stall before its time", 0, sNow);
    Check(CoopLockstep_Waiting(&sNodes[0].lockstep) == 2, "waiting for the wrong player", reached, 0);
    Check(reached <= 100 + 2 * COOP_LOCKSTEP_INPUT_DELAY + 4, "the host ran on without the player", reached, 0);
    until = sNow + 500;
    while (sNow < until) {
        World_Step(COOP_LOCKSTEP_INPUT_DELAY);
    }
    Check(CoopLockstep_StalledOn(&sNodes[0].lockstep, sNow) == 2, "the host names no one", 0, sNow);
    Check(CoopLockstep_StalledOn(&sNodes[1].lockstep, sNow) == 2, "a client names no one", 0, sNow);
}

// Another delay needs nothing but the number, and the two ends may even differ: a stamp says what a frame is for.
static void Sc_Delays(void) {
    static const Network net = { 20, 10, 2, 0 };
    uint32_t delay;

    for (delay = 1; delay <= COOP_LOCKSTEP_MAX_DELAY; delay *= 2) {
        World_Reset(3, &net, delay);
        Check(World_Run(500, delay, 300000), "the frames were not reached", delay, sLastFrame);
    }
}

static uint16_t Put_Header(uint8_t* out, uint8_t blocks) {
    memset(out, 0, 4 * COOP_PLAYERS_MAX);
    out[4 * COOP_PLAYERS_MAX] = blocks;
    return 4 * COOP_PLAYERS_MAX + 1;
}

static uint16_t Put_Block(uint8_t* out, uint16_t at, uint8_t player, uint32_t first, uint8_t count) {
    out[at] = player;
    out[at + 1] = (uint8_t) first;
    out[at + 2] = (uint8_t) (first >> 8);
    out[at + 3] = (uint8_t) (first >> 16);
    out[at + 4] = (uint8_t) (first >> 24);
    out[at + 5] = count;
    memset(&out[at + 6], 0x11, (size_t) count * COOP_LOCKSTEP_FRAME_SIZE);
    return (uint16_t) (at + 6 + count * COOP_LOCKSTEP_FRAME_SIZE);
}

// Every received byte is untrusted (decision 0004). A host of three players hears client 1 at peer 0.
static void Sc_Malformed(void) {
    static const Network net = { 1, 0, 0, 0 };
    CoopLockstep* host;
    uint8_t m[MESSAGE_MAX];
    uint16_t n;

    World_Reset(3, &net, 9);
    Node_Start(0, 3);
    Node_Start(1, 3);
    host = &sNodes[0].lockstep;

    n = Put_Header(m, 0);
    Check(CoopLockstep_Receive(host, 0, m, n, 0), "an empty message refused", 0, 0);
    Check(!CoopLockstep_Receive(host, 0, m, n - 1, 0), "a short header taken", 0, 0);
    Check(!CoopLockstep_Receive(host, 3, m, n, 0), "a peer that does not exist taken", 0, 0);
    Check(!CoopLockstep_Receive(host, 2, m, n, 0), "a peer nobody plays taken", 0, 0);

    n = Put_Header(m, 1);
    Check(!CoopLockstep_Receive(host, 0, m, n, 0), "a missing block taken", 0, 0);
    n = Put_Block(m, n, 1, 0, 2);
    Check(CoopLockstep_Receive(host, 0, m, n, 0), "a good block refused", 0, 0);
    Check(host->next[1] == 2, "the good block went nowhere", host->next[1], 0);
    Check(!CoopLockstep_Receive(host, 0, m, n - 1, 0), "a cut block taken", 0, 0);
    m[n] = 0;
    Check(!CoopLockstep_Receive(host, 0, m, n + 1, 0), "a byte too many taken", 0, 0);

    n = Put_Block(m, Put_Header(m, 1), 2, 0, 1);
    Check(!CoopLockstep_Receive(host, 0, m, n, 0), "a client speaking for another taken", 0, 0);
    n = Put_Block(m, Put_Header(m, 1), 0, 0, 1);
    Check(!CoopLockstep_Receive(host, 0, m, n, 0), "a client speaking for the host taken", 0, 0);
    n = Put_Block(m, Put_Header(m, 1), 3, 0, 1);
    Check(!CoopLockstep_Receive(host, 0, m, n, 0), "a player who is not in the level taken", 0, 0);
    n = Put_Block(m, Put_Header(m, 1), 9, 0, 1);
    Check(!CoopLockstep_Receive(host, 0, m, n, 0), "a player that does not exist taken", 0, 0);
    n = Put_Block(m, Put_Header(m, 1), 1, 2, 0);
    Check(!CoopLockstep_Receive(host, 0, m, n, 0), "an empty block taken", 0, 0);
    n = Put_Block(m, Put_Header(m, 1), 1, 2, COOP_LOCKSTEP_BLOCK_FRAMES + 1);
    Check(!CoopLockstep_Receive(host, 0, m, n, 0), "a block too long taken", 0, 0);
    n = Put_Block(m, Put_Header(m, 1), 1, 5, 1);
    Check(!CoopLockstep_Receive(host, 0, m, n, 0), "a gap taken", 0, 0);
    n = Put_Block(m, Put_Header(m, 1), 1, 0xFFFFFFFFu, 2);
    Check(!CoopLockstep_Receive(host, 0, m, n, 0), "a frame that wraps taken", 0, 0);

    // Up to the end of the ring, and no further: what is still to run must not be written over.
    n = Put_Block(m, Put_Header(m, 1), 1, 2, 32);
    Check(CoopLockstep_Receive(host, 0, m, n, 0), "frames in the ring refused", host->next[1], 0);
    n = Put_Block(m, Put_Header(m, 1), 1, 34, 30);
    Check(CoopLockstep_Receive(host, 0, m, n, 0), "frames up to the end of the ring refused", host->next[1], 0);
    n = Put_Block(m, Put_Header(m, 1), 1, 64, 1);
    Check(!CoopLockstep_Receive(host, 0, m, n, 0), "a frame past the ring taken", host->next[1], 0);

    // The header of a client, which holds no other player's frames for a host to refuse.
    n = Put_Block(m, Put_Header(m, 1), 1, 0, 1);
    Check(!CoopLockstep_Receive(&sNodes[1].lockstep, 0, m, n, 0), "a client taking its own input from outside", 0, 0);
    n = Put_Block(m, Put_Header(m, 1), 2, 0, 1);
    Check(CoopLockstep_Receive(&sNodes[1].lockstep, 0, m, n, 0), "a client refusing another's input from the host", 0,
          0);
    n = Put_Block(m, Put_Header(m, 1), 3, 0, 1);
    Check(!CoopLockstep_Receive(&sNodes[1].lockstep, 0, m, n, 0), "a client taking a player who is not there", 0, 0);
    n = Put_Block(m, Put_Header(m, 1), 200, 0, 1);
    Check(!CoopLockstep_Receive(&sNodes[1].lockstep, 0, m, n, 0), "a client taking a player that cannot be", 0, 0);
}

typedef struct {
    const char* name;
    void (*run)(void);
} Scenario;

static const Scenario sScenarios[] = {
    { "clean", Sc_Clean }, { "lossy", Sc_Lossy },          { "hell", Sc_Hell },
    { "slow", Sc_Slow },   { "late_start", Sc_LateStart }, { "never_starts", Sc_NeverStarts },
    { "stall", Sc_Stall }, { "delays", Sc_Delays },        { "malformed", Sc_Malformed },
};

int main(int argc, char** argv) {
    size_t i;
    uint32_t failed = 0;
    size_t ran = 0;

    for (i = 0; i < sizeof(sScenarios) / sizeof(sScenarios[0]); i++) {
        uint32_t before = sChecks;

        if ((argc > 1) && (strcmp(argv[1], sScenarios[i].name) != 0)) {
            continue;
        }
        sFailures = 0;
        sScenarios[i].run();
        printf("%-7s %s: %u checks\n", (sFailures == 0) ? "ok" : "FAILED", sScenarios[i].name,
               (unsigned) (sChecks - before));
        failed += (sFailures != 0);
        ran++;
    }
    printf("RESULT: %u/%u scenarios passed, %u checks\n", (unsigned) (ran - failed), (unsigned) ran,
           (unsigned) sChecks);
    return (failed == 0) ? 0 : 1;
}
