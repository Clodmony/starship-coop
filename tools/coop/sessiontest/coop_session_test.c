// Test program for the co-op session handshake (WP-21). tools/coop/session_test.py drives it; see docs/coop/QA.md.
//
//   coop_session_test sim
//       The session over the real transport core in a seeded virtual network: no sockets, no real time. Every
//       mismatch, a match, lossy links, two clients, broken and silent peers, raw peers that push game messages
//       before the welcome. Then the session on a scripted transport: every failure and time-out on demand, to the
//       millisecond. Prints one line, exits 0 on PASS.
//   coop_session_test host --port-file F [--clients N] [identity options] [--out FILE] [--max-seconds S] [--eager N]
//   coop_session_test client --connect HOST:PORT [identity options] [--out FILE] [--max-seconds S] [--eager N]
//       Real processes over UDP. Each writes what happened as JSON. --eager sends N game messages straight into the
//       transport, past CoopSession_Send, as soon as it has a connection; the session on the other side must drop
//       them.
//
// Identity options: --build TEXT --platform TEXT --rom HEX (repeat for several archives) --cvar NAME=VALUE (repeat).
// Defaults: build "test-build", this machine's platform, ROM US 1.0, every CVar at its default.

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/coop/net/net_core.h"
#include "port/coop/net/protocol.h"
#include "port/coop/net/transport.h"
#include "port/coop/session/identity.h"
#include "port/coop/session/session.h"
#include "port/coop/session/sim_cvars.h"

#define ROM_US10 0x76924CA9u
#define ROM_OTHER 0x94F1D5A7u
#define TEST_PAYLOAD "session test"

static void Fail(const char* scenario, const char* what) {
    printf("sim: FAIL %s: %s\n", scenario, what);
    exit(1);
}

#define EXPECT(cond, scenario, what) \
    do {                             \
        if (!(cond)) {               \
            Fail(scenario, what);    \
        }                            \
    } while (0)

// ---- Identities ----

static int32_t Default_Int(const char* name, int32_t defaultValue) {
    (void) name;
    return defaultValue;
}

static float Default_Float(const char* name, float defaultValue) {
    (void) name;
    return defaultValue;
}

static void Id_Default(CoopIdentity* id) {
    memset(id, 0, sizeof(*id));
    snprintf(id->build, sizeof(id->build), "test-build");
    CoopIdentity_Platform(id->platform, sizeof(id->platform));
    id->romCount = 1;
    id->roms[0] = ROM_US10;
    id->cvarTable = CoopSimCvars_TableHash();
    id->cvarCount = CoopSimCvars_Count();
    CoopSimCvars_Read(id->cvars, Default_Int, Default_Float);
}

static bool Id_SetCvar(CoopIdentity* id, const char* name, int32_t value) {
    char each[64];
    uint16_t i;

    for (i = 0; i < id->cvarCount; i++) {
        CoopSimCvars_Name(i, each, sizeof(each));
        if (strcmp(each, name) == 0) {
            id->cvars[i] = value;
            return true;
        }
    }
    return false;
}

// ---- Virtual network: the real transport core, a fake clock, a lossy queue ----

#define SIM_HOST 1
#define SIM_NODES 6
#define SIM_MAX_PACKETS 8192
#define SIM_STEP_MS 5

typedef struct {
    uint32_t due;
    CoopNetAddr from;
    CoopNetAddr to;
    uint16_t size;
    uint8_t data[COOP_NET_MAX_DATAGRAM];
} SimPacket;

static struct {
    SimPacket* packets;
    uint32_t count;
    uint32_t now;
    uint64_t rng;
    uint32_t lossPercent;
    uint32_t latencyMs;
    uint32_t dropped;
    CoopNetCore* cores[SIM_NODES];
} sNet;

static uint32_t Sim_Random(void) {
    // SplitMix64
    uint64_t z = (sNet.rng += 0x9E3779B97F4A7C15ull);

    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return (uint32_t) ((z ^ (z >> 31)) >> 32);
}

static void Sim_Tx(void* user, CoopNetAddr to, const uint8_t* data, uint16_t size) {
    SimPacket* p;

    if (Sim_Random() % 100 < sNet.lossPercent) {
        sNet.dropped++;
        return;
    }
    if (sNet.count == SIM_MAX_PACKETS) {
        Fail("network", "the virtual network ran out of room");
    }
    p = &sNet.packets[sNet.count++];
    p->due = sNet.now + sNet.latencyMs + (sNet.latencyMs ? Sim_Random() % (sNet.latencyMs + 1) : 0);
    p->from = (CoopNetAddr) (uintptr_t) user;
    p->to = to;
    p->size = size;
    memcpy(p->data, data, size);
}

typedef struct {
    CoopTransport base;
    CoopNetCore* core;
    int addr;
} SimTransport;

static bool SimT_Send(CoopTransport* t, int peer, uint16_t msgId, const void* data, uint16_t size, CoopNetLane lane) {
    return CoopNetCore_Send(((SimTransport*) t)->core, peer, msgId, data, size, lane, sNet.now);
}

static bool SimT_Poll(CoopTransport* t, CoopNetEvent* ev) {
    return CoopNetCore_Poll(((SimTransport*) t)->core, ev);
}

static void SimT_Disconnect(CoopTransport* t, int peer) {
    CoopNetCore_Disconnect(((SimTransport*) t)->core, peer);
}

static bool SimT_Stats(CoopTransport* t, int peer, CoopNetStats* out) {
    return CoopNetCore_Stats(((SimTransport*) t)->core, peer, out);
}

static void SimT_Close(CoopTransport* t) {
    SimTransport* st = (SimTransport*) t;

    CoopNetCore_Destroy(st->core);
    sNet.cores[st->addr] = NULL;
    free(st);
}

static const CoopTransportOps sSimOps = { SimT_Send, SimT_Poll, SimT_Disconnect, SimT_Stats, SimT_Close };

static CoopTransport* Sim_Transport(int addr) {
    CoopNetConfig config = { 0 };
    SimTransport* st = calloc(1, sizeof(*st));

    st->base.ops = &sSimOps;
    st->addr = addr;
    if (addr == SIM_HOST) {
        st->core = CoopNetCore_CreateHost(&config, Sim_Tx, (void*) (uintptr_t) addr);
    } else {
        st->core = CoopNetCore_CreateClient(&config, Sim_Tx, (void*) (uintptr_t) addr, SIM_HOST,
                                            (uint32_t) (addr * 7919u + Sim_Random()), sNet.now);
    }
    sNet.cores[addr] = st->core;
    return &st->base;
}

static void Sim_Reset(uint64_t seed, uint32_t lossPercent, uint32_t latencyMs) {
    if (sNet.packets == NULL) {
        sNet.packets = malloc(sizeof(SimPacket) * SIM_MAX_PACKETS);
    }
    sNet.count = 0;
    sNet.now = (uint32_t) (seed * 1000003u); // also wraps the 32-bit clock for some seeds
    sNet.rng = seed;
    sNet.lossPercent = lossPercent;
    sNet.latencyMs = latencyMs;
    sNet.dropped = 0;
    memset(sNet.cores, 0, sizeof(sNet.cores));
}

static void Sim_Step(void) {
    uint32_t i = 0;
    int n;

    sNet.now += SIM_STEP_MS;
    while (i < sNet.count) {
        SimPacket* p = &sNet.packets[i];

        if ((int32_t) (sNet.now - p->due) < 0) {
            i++;
            continue;
        }
        if (p->to < SIM_NODES && sNet.cores[p->to] != NULL) {
            CoopNetCore_Receive(sNet.cores[p->to], p->from, p->data, p->size, sNet.now);
        }
        *p = sNet.packets[--sNet.count];
    }
    for (n = 0; n < SIM_NODES; n++) {
        if (sNet.cores[n] != NULL) {
            CoopNetCore_Update(sNet.cores[n], sNet.now);
        }
    }
}

// ---- What one side saw ----

typedef struct {
    uint32_t events;
    uint32_t joined;
    uint32_t refused;
    uint32_t ended;
    uint32_t left;
    uint32_t diff;
    CoopSessionEnd end;
    CoopSessionEnd refusedEnd; // of the REFUSED event; on a client ENDED follows with an end of its own
    uint8_t playerIndex;
    uint32_t messages;      // TEST_SESSION with its payload
    uint32_t otherMessages; // anything else above the session range
    char text[COOP_SESSION_TEXT];
    char endText[COOP_SESSION_TEXT]; // of the last LEFT or ENDED
} Side;

static void Side_Record(Side* side, const CoopSessionEvent* ev) {
    EXPECT(ev->text != NULL, "events", "an event has no text");
    EXPECT(ev->type == COOP_SESSION_EV_JOINED || ev->playerIndex == 0, "events", "a stale field in an event");
    EXPECT(ev->type == COOP_SESSION_EV_MESSAGE || (ev->msgId == 0 && ev->size == 0 && ev->data == NULL), "events",
           "a stale field in an event");
    side->events++;
    switch (ev->type) {
        case COOP_SESSION_EV_JOINED:
            side->joined++;
            side->playerIndex = ev->playerIndex;
            side->diff = ev->diff;
            snprintf(side->text, sizeof(side->text), "%s", ev->text);
            break;
        case COOP_SESSION_EV_REFUSED:
            side->refused++;
            side->diff = ev->diff;
            side->end = ev->end;
            side->refusedEnd = ev->end;
            snprintf(side->text, sizeof(side->text), "%s", ev->text);
            break;
        case COOP_SESSION_EV_LEFT:
            side->left++;
            side->end = ev->end;
            snprintf(side->endText, sizeof(side->endText), "%s", ev->text);
            break;
        case COOP_SESSION_EV_ENDED:
            side->ended++;
            side->end = ev->end;
            snprintf(side->endText, sizeof(side->endText), "%s", ev->text);
            if (!side->refused) {
                snprintf(side->text, sizeof(side->text), "%s", ev->text);
            }
            break;
        case COOP_SESSION_EV_MESSAGE:
            if (ev->msgId == COOP_MSG_TEST_SESSION && ev->size == sizeof(TEST_PAYLOAD) &&
                memcmp(ev->data, TEST_PAYLOAD, sizeof(TEST_PAYLOAD)) == 0) {
                side->messages++;
            } else {
                side->otherMessages++;
            }
            break;
    }
}

// One poll of a session: every event goes to the side of its peer. A session that never stops talking is a failure,
// not a hang.
static uint32_t Drain(CoopSession* s, uint32_t now, Side* sides) {
    CoopSessionEvent ev;
    uint32_t n = 0;

    while (CoopSession_Poll(s, now, &ev)) {
        EXPECT(++n < 16, "events", "the session keeps producing events");
        Side_Record(&sides[ev.peer < 0 ? 0 : ev.peer], &ev);
    }
    return n;
}

// ---- Scenarios with sessions on both ends ----

#define MAX_CLIENTS 3

typedef struct {
    const CoopIdentity* host;
    const CoopIdentity* clients[MAX_CLIENTS];
    int clientCount;
    uint32_t lossPercent;
    uint32_t latencyMs;
    uint64_t seed;
    bool eager; // both ends push game messages into the transport without waiting for the handshake
} Setup;

typedef struct {
    Side host[COOP_NET_MAX_PEERS];
    Side client[MAX_CLIENTS];
    uint32_t hostIgnored;
    uint32_t clientIgnored[MAX_CLIENTS];
    bool lossy;
} Outcome;

// Enough to have one in flight before the hello and others behind it.
#define EAGER_MESSAGES 3

// Straight into the transport, past CoopSession_Send: what a broken or changed peer could do. Failing is normal while
// there is no connection yet.
static void Sim_Eager(CoopNetCore* core, int peer, uint32_t* sent) {
    if (core != NULL && *sent < EAGER_MESSAGES &&
        CoopNetCore_Send(core, peer, COOP_MSG_TEST_SESSION, TEST_PAYLOAD, sizeof(TEST_PAYLOAD), COOP_NET_RELIABLE,
                         sNet.now)) {
        (*sent)++;
    }
}

static void Sim_Run(const Setup* in, Outcome* out) {
    CoopSession* host;
    CoopSession* clients[MAX_CLIENTS];
    bool done[MAX_CLIENTS];
    uint32_t hostEager[MAX_CLIENTS] = { 0 };
    uint32_t clientEager[MAX_CLIENTS] = { 0 };
    uint32_t start;
    uint32_t quietSince = 0;
    int i;

    memset(out, 0, sizeof(*out));
    out->lossy = in->lossPercent > 0;
    Sim_Reset(in->seed, in->lossPercent, in->latencyMs);
    start = sNet.now;
    host = CoopSession_Host(Sim_Transport(SIM_HOST), in->host);
    for (i = 0; i < in->clientCount; i++) {
        clients[i] = CoopSession_Join(Sim_Transport(2 + i), in->clients[i], sNet.now);
        done[i] = false;
    }

    // 60 s of virtual time is far past every timeout in the session and the transport.
    while (sNet.now - start < 60000) {
        CoopSessionEvent ev;
        bool allDone = true;
        uint32_t burst = 0;

        Sim_Step();
        // Before the polls: the first message goes out before the session has a chance to say hello.
        for (i = 0; in->eager && i < in->clientCount; i++) {
            Sim_Eager(sNet.cores[SIM_HOST], i, &hostEager[i]);
            Sim_Eager(sNet.cores[2 + i], 0, &clientEager[i]);
        }
        while (CoopSession_Poll(host, sNet.now, &ev)) {
            EXPECT(++burst < 16, "events", "the host keeps producing events");
            Side_Record(&out->host[ev.peer], &ev);
            if (ev.type == COOP_SESSION_EV_JOINED) {
                CoopSession_Send(host, ev.peer, COOP_MSG_TEST_SESSION, TEST_PAYLOAD, sizeof(TEST_PAYLOAD),
                                 COOP_NET_RELIABLE);
            }
        }
        for (i = 0; i < in->clientCount; i++) {
            Side* side = &out->client[i];

            if (done[i]) {
                continue;
            }
            while (!done[i] && CoopSession_Poll(clients[i], sNet.now, &ev)) {
                Side_Record(side, &ev);
                if (ev.type == COOP_SESSION_EV_JOINED) {
                    CoopSession_Send(clients[i], 0, COOP_MSG_TEST_SESSION, TEST_PAYLOAD, sizeof(TEST_PAYLOAD),
                                     COOP_NET_RELIABLE);
                }
                if (side->ended) {
                    done[i] = true;
                }
            }
            // A client that got in leaves once its test message is answered and its own one has arrived.
            if (!done[i] && side->joined && side->messages > 0) {
                CoopNetStats stats;

                if (CoopNetCore_Stats(sNet.cores[2 + i], 0, &stats) && stats.pending == 0) {
                    CoopSession_Close(clients[i]);
                    clients[i] = NULL;
                    done[i] = true;
                }
            }
            allDone = allDone && done[i];
        }
        if (allDone) {
            // Give the host time to see each client go and to drop the refused ones.
            if (quietSince == 0) {
                quietSince = sNet.now | 1;
            } else if (sNet.now - quietSince > COOP_SESSION_REFUSE_LINGER_MS + 8000) {
                break;
            }
        }
    }
    out->hostIgnored = CoopSession_Ignored(host);
    for (i = 0; i < in->clientCount; i++) {
        if (clients[i] != NULL) {
            out->clientIgnored[i] = CoopSession_Ignored(clients[i]);
            CoopSession_Close(clients[i]);
        }
    }
    CoopSession_Close(host);
}

static void Run_Pair(const char* name, const CoopIdentity* host, const CoopIdentity* client, uint32_t loss,
                     uint64_t seed, Outcome* out) {
    Setup s;

    (void) name;
    memset(&s, 0, sizeof(s));
    s.host = host;
    s.clients[0] = client;
    s.clientCount = 1;
    s.lossPercent = loss;
    s.latencyMs = loss ? 40 : 5;
    s.seed = seed;
    // In a run that ends in a refusal, nothing may get through at all. A match may let the last ones in.
    s.eager = (CoopIdentity_Compare(host, client) & COOP_ID_DIFF_REFUSE) != 0;
    Sim_Run(&s, out);
}

// Both sides refused, with the same text naming what differs, and no game message got through.
static void Check_Refused(const char* name, const Outcome* o, uint32_t diff, const char* needle) {
    const Side* h = &o->host[0];
    const Side* c = &o->client[0];

    EXPECT(h->refused && !h->joined, name, "host did not refuse");
    EXPECT(c->refused && !c->joined, name, "client was not told it was refused");
    EXPECT(c->ended && c->end == COOP_SESSION_END_REFUSED, name, "client did not end with the refusal");
    EXPECT(h->end == COOP_SESSION_END_REFUSED, name, "host event does not say it refused");
    EXPECT(h->diff == diff && c->diff == diff, name, "wrong reasons");
    EXPECT(strcmp(h->text, c->text) == 0, name, "host and client got different texts");
    EXPECT(strstr(c->text, needle) != NULL, name, "the text does not say what differs");
    EXPECT(h->messages == 0 && c->messages == 0, name, "a game message passed a refused handshake");
    // Both ends pushed game messages. On a clean link each session must have dropped some, or the line above proves
    // nothing. On a lossy one the other side can be gone before they arrive.
    EXPECT(o->lossy || (o->hostIgnored > 0 && o->clientIgnored[0] > 0), name, "nothing was held back");
    EXPECT(!h->left, name, "a refused client counted as one that left");
}

// On a lossy link all three goodbyes can be lost, so the host may only see the client leave by timeout.
static void Check_Joined(const char* name, const Outcome* o, uint32_t diff, bool lossy) {
    const Side* h = &o->host[0];
    const Side* c = &o->client[0];

    EXPECT(h->joined && c->joined, name, "the handshake did not let a matching client in");
    EXPECT(!h->refused && !c->refused, name, "a matching client was refused");
    EXPECT(h->diff == diff && c->diff == diff, name, "wrong warnings");
    EXPECT(c->playerIndex == 1, name, "the first client is not player 1");
    EXPECT(h->messages == 1 && c->messages == 1, name, "game messages did not pass after the welcome");
    EXPECT(h->left && (h->end == COOP_SESSION_END_CLOSED || (lossy && h->end == COOP_SESSION_END_TIMEOUT)), name,
           "host did not see the client leave");
    EXPECT(strcmp(h->text, c->text) == 0, name, "host and client got different texts");
}

static void Sim_Mismatches(void) {
    CoopIdentity a, b;
    Outcome o;
    uint16_t i;

    Id_Default(&a);

    b = a;
    Run_Pair("match", &a, &b, 0, 1, &o);
    Check_Joined("match", &o, 0, false);
    EXPECT(o.client[0].text[0] == '\0', "match", "a match produced a message");

    b = a;
    snprintf(b.build, sizeof(b.build), "other-build");
    Run_Pair("build", &a, &b, 0, 2, &o);
    Check_Refused("build", &o, COOP_ID_DIFF_BUILD,
                  "Different game builds: host test-build, joining player other-build");

    b = a;
    b.roms[0] = ROM_OTHER;
    Run_Pair("rom", &a, &b, 0, 3, &o);
    Check_Refused("rom", &o, COOP_ID_DIFF_ROM, "host US 1.0 (76924CA9), joining player US (94F1D5A7)");

    b = a;
    b.romCount = 2;
    b.roms[1] = 0x12345678;
    Run_Pair("rom_extra", &a, &b, 0, 4, &o);
    Check_Refused("rom_extra", &o, COOP_ID_DIFF_ROM, "unknown ROM (12345678)");

    b = a;
    EXPECT(Id_SetCvar(&b, "gInvincible", 1), "cvar", "gInvincible is not in the table");
    Run_Pair("cvar", &a, &b, 0, 5, &o);
    Check_Refused("cvar", &o, COOP_ID_DIFF_CVARS, "Setting \"Invincible\" (gInvincible): host 0, joining player 1.");

    b = a;
    EXPECT(Id_SetCvar(&b, "gSkipBriefing", 1), "cvar_skip_briefing", "gSkipBriefing is not in the table");
    Run_Pair("cvar_skip_briefing", &a, &b, 0, 11, &o);
    Check_Refused("cvar_skip_briefing", &o, COOP_ID_DIFF_CVARS,
                  "Setting \"Skip briefing\" (gSkipBriefing): host 0, joining player 1.");

    b = a;
    EXPECT(Id_SetCvar(&b, "gCoopDebugPlayerCount", 2), "cvar_player_count",
           "gCoopDebugPlayerCount is not in the table");
    Run_Pair("cvar_player_count", &a, &b, 0, 12, &o);
    Check_Refused("cvar_player_count", &o, COOP_ID_DIFF_CVARS,
                  "Setting \"Debug: player count\" (gCoopDebugPlayerCount): host 1, joining player 2.");

    b = a;
    for (i = 0; i < 9; i++) {
        b.cvars[i] ^= 1;
    }
    Run_Pair("many_cvars", &a, &b, 0, 6, &o);
    Check_Refused("many_cvars", &o, COOP_ID_DIFF_CVARS, "And 3 more settings differ.");

    b = a;
    b.cvarTable ^= 1;
    Run_Pair("cvar_table", &a, &b, 0, 7, &o);
    Check_Refused("cvar_table", &o, COOP_ID_DIFF_CVARS, "Different lists of game settings");

    b = a;
    EXPECT(Id_SetCvar(&b, "gCheckpoint.0.Set", 1), "checkpoint", "checkpoints are not in the table");
    Run_Pair("checkpoint", &a, &b, 0, 8, &o);
    Check_Refused("checkpoint", &o, COOP_ID_DIFF_CVARS, "Setting \"Checkpoint\" (gCheckpoint.0.Set)");

    b = a;
    snprintf(b.platform, sizeof(b.platform), "Linux x86-64 GCC 13.2");
    snprintf(a.platform, sizeof(a.platform), "Windows x86-64 MSVC 1944");
    Run_Pair("platform", &a, &b, 0, 9, &o);
    Check_Joined("platform", &o, COOP_ID_DIFF_PLATFORM, false);
    EXPECT(strstr(o.client[0].text, "Warning: host runs Windows x86-64 MSVC 1944, joining player runs Linux") != NULL,
           "platform", "no platform warning");

    snprintf(b.build, sizeof(b.build), "other-build");
    Run_Pair("platform_build", &a, &b, 0, 10, &o);
    Check_Refused("platform_build", &o, COOP_ID_DIFF_BUILD | COOP_ID_DIFF_PLATFORM, "Warning: host runs");
}

// The outcome must not depend on the link: a refusal arrives through loss too, and so does a welcome. The one limit
// is the linger time: a refusal gets about five tries in it, and a bad enough link can lose them all. Then the client
// only sees the host go, which still lets nothing through.
static uint32_t Sim_Lossy(uint32_t seeds) {
    CoopIdentity a, b;
    Outcome o;
    uint32_t seed;
    uint32_t dropped = 0;
    uint32_t unheard = 0;

    Id_Default(&a);
    for (seed = 1; seed <= seeds; seed++) {
        uint32_t loss = 10 + seed % 31; // 10 to 40 percent

        b = a;
        Run_Pair("lossy_match", &a, &b, loss, 1000 + seed, &o);
        Check_Joined("lossy_match", &o, 0, true);
        dropped += sNet.dropped;

        snprintf(b.build, sizeof(b.build), "other-build");
        Run_Pair("lossy_refuse", &a, &b, loss, 2000 + seed, &o);
        if (o.client[0].refused == 0 && o.client[0].end == COOP_SESSION_END_CLOSED) {
            EXPECT(o.host[0].refused == 1 && o.client[0].joined == 0, "lossy_refuse", "a client got past a refusal");
            EXPECT(o.host[0].messages == 0 && o.client[0].messages == 0, "lossy_refuse", "a game message got through");
            unheard++;
        } else {
            Check_Refused("lossy_refuse", &o, COOP_ID_DIFF_BUILD, "Different game builds");
        }
        dropped += sNet.dropped;
    }
    EXPECT(dropped > 0, "lossy", "the virtual network dropped nothing, so this run proves nothing");
    EXPECT(unheard * 10 <= seeds, "lossy", "too many refusals never arrived");
    return dropped;
}

// One client matches and one does not: the host takes the one and refuses the other.
static void Sim_TwoClients(void) {
    CoopIdentity a, good, bad;
    Setup s;
    Outcome o;
    int i;
    int joined = 0, refused = 0;

    Id_Default(&a);
    good = a;
    bad = a;
    Id_SetCvar(&bad, "gRapidFire", 1);
    memset(&s, 0, sizeof(s));
    s.host = &a;
    s.clients[0] = &bad;
    s.clients[1] = &good;
    s.clientCount = 2;
    s.latencyMs = 10;
    s.seed = 77;
    Sim_Run(&s, &o);
    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        joined += o.host[i].joined;
        refused += o.host[i].refused;
    }
    EXPECT(joined == 1 && refused == 1, "two_clients", "host did not take one client and refuse the other");
    EXPECT(o.client[0].refused && strstr(o.client[0].text, "Rapid fire") != NULL, "two_clients",
           "the client with other settings was not refused");
    EXPECT(o.client[1].joined && o.client[1].messages == 1, "two_clients", "the matching client did not get in");
}

// ---- Scenarios with a raw transport on one end ----

// Runs the virtual network until cond or limit ms; polls the session each step and records into side.
typedef bool (*CondFn)(void* user);

static void Pump(CoopSession* s, Side* sides, uint32_t limitMs, CondFn cond, void* user) {
    uint32_t start = sNet.now;

    while (sNet.now - start < limitMs && !(cond && cond(user))) {
        Sim_Step();
        if (s != NULL) {
            Drain(s, sNet.now, sides);
        }
    }
}

// Waits for one event on a raw transport. Returns false on timeout.
static bool Raw_Wait(CoopTransport* t, CoopSession* s, Side* sides, CoopNetEventType type, uint16_t msgId,
                     CoopNetEvent* out, uint32_t limitMs) {
    uint32_t start = sNet.now;

    while (sNet.now - start < limitMs) {
        Sim_Step();
        if (s != NULL) {
            Drain(s, sNet.now, sides);
        }
        while (t->ops->poll(t, out)) {
            if (out->type == type && (type != COOP_NET_EVENT_MESSAGE || out->msgId == msgId)) {
                return true;
            }
        }
    }
    return false;
}

// A hello that cannot be read is refused as malformed; a game message before the hello is ignored.
static void Sim_BrokenClients(void) {
    CoopIdentity a;
    CoopSession* host;
    CoopTransport* raw;
    CoopNetEvent net;
    Side sides[COOP_NET_MAX_PEERS];
    uint8_t junk[40];
    uint32_t reasons;

    Id_Default(&a);
    Sim_Reset(5, 0, 5);
    memset(sides, 0, sizeof(sides));
    host = CoopSession_Host(Sim_Transport(SIM_HOST), &a);
    raw = Sim_Transport(2);
    EXPECT(Raw_Wait(raw, host, sides, COOP_NET_EVENT_CONNECTED, 0, &net, 3000), "malformed", "no connection");
    EXPECT(raw->ops->send(raw, 0, COOP_MSG_TEST_SESSION, TEST_PAYLOAD, sizeof(TEST_PAYLOAD), COOP_NET_RELIABLE),
           "malformed", "send failed");
    memset(junk, 0xEE, sizeof(junk));
    EXPECT(raw->ops->send(raw, 0, COOP_MSG_SESSION_HELLO, junk, sizeof(junk), COOP_NET_RELIABLE), "malformed",
           "send failed");
    EXPECT(Raw_Wait(raw, host, sides, COOP_NET_EVENT_MESSAGE, COOP_MSG_SESSION_REFUSE, &net, 3000), "malformed",
           "no refusal for a broken hello");
    reasons = (uint32_t) net.data[0] | ((uint32_t) net.data[1] << 8);
    EXPECT(reasons == COOP_SESSION_REFUSE_MALFORMED, "malformed", "refusal does not say the hello was broken");
    EXPECT(sides[0].refused && strstr(sides[0].text, "cannot read") != NULL, "malformed", "host text is wrong");
    EXPECT(sides[0].messages == 0 && CoopSession_Ignored(host) == 1, "malformed",
           "a game message before the hello was not ignored");
    // The host lets go of the refused client by itself.
    EXPECT(Raw_Wait(raw, host, sides, COOP_NET_EVENT_DISCONNECTED, 0, &net, COOP_SESSION_REFUSE_LINGER_MS + 6000),
           "malformed", "host kept the refused client");
    raw->ops->close(raw);
    CoopSession_Close(host);

    // A client that never says hello is refused after the handshake time.
    Sim_Reset(6, 0, 5);
    memset(sides, 0, sizeof(sides));
    host = CoopSession_Host(Sim_Transport(SIM_HOST), &a);
    raw = Sim_Transport(2);
    EXPECT(Raw_Wait(raw, host, sides, COOP_NET_EVENT_CONNECTED, 0, &net, 3000), "silent_client", "no connection");
    EXPECT(!Raw_Wait(raw, host, sides, COOP_NET_EVENT_MESSAGE, COOP_MSG_SESSION_REFUSE, &net,
                     COOP_SESSION_HANDSHAKE_MS - 500),
           "silent_client", "refused before the handshake time");
    EXPECT(Raw_Wait(raw, host, sides, COOP_NET_EVENT_MESSAGE, COOP_MSG_SESSION_REFUSE, &net, 2000), "silent_client",
           "a silent client was not refused");
    EXPECT(sides[0].refused && strstr(sides[0].text, "did not finish in time") != NULL, "silent_client",
           "host text is wrong");
    raw->ops->close(raw);
    CoopSession_Close(host);
}

// A host that accepts the connection but never answers the hello: the client gives up with a clear reason.
static void Sim_SilentHost(void) {
    CoopIdentity a;
    CoopSession* client;
    CoopTransport* raw;
    CoopNetEvent net;
    Side sides[1];

    Id_Default(&a);
    Sim_Reset(7, 0, 5);
    memset(sides, 0, sizeof(sides));
    raw = Sim_Transport(SIM_HOST);
    client = CoopSession_Join(Sim_Transport(2), &a, sNet.now);
    EXPECT(Raw_Wait(raw, client, sides, COOP_NET_EVENT_MESSAGE, COOP_MSG_SESSION_HELLO, &net, 3000), "silent_host",
           "the client sent no hello");
    Pump(client, sides, COOP_SESSION_HANDSHAKE_MS + 1000, NULL, NULL);
    EXPECT(sides[0].ended && sides[0].end == COOP_SESSION_END_NO_HANDSHAKE, "silent_host",
           "client did not give up on a silent host");
    EXPECT(strstr(sides[0].text, "older build") != NULL, "silent_host", "client text is wrong");
    CoopSession_Close(client);
    raw->ops->close(raw);
}

// ---- Game messages before the welcome: a raw peer on the real transport core ----

// The same game message is sent before and after the welcome, so a count that stays at 0 before and goes to 1 after
// shows the session held it back, and not that it went missing.
static void Raw_Send(CoopTransport* raw, const char* name, uint16_t msgId, const void* data, uint16_t size) {
    EXPECT(raw->ops->send(raw, 0, msgId, data, size, COOP_NET_RELIABLE), name, "send failed");
}

static void Raw_Game(CoopTransport* raw, const char* name) {
    Raw_Send(raw, name, COOP_MSG_TEST_SESSION, TEST_PAYLOAD, sizeof(TEST_PAYLOAD));
}

// The handshake messages as a peer would send them. Return the size.
static uint16_t Wire_Welcome(const CoopIdentity* id, uint8_t player, uint8_t* out) {
    out[0] = player;
    return (uint16_t) (1 + CoopIdentity_Write(id, out + 1, COOP_NET_MAX_MESSAGE - 1));
}

static uint16_t Wire_Refuse(const CoopIdentity* id, uint32_t reasons, uint8_t* out) {
    out[0] = (uint8_t) reasons;
    out[1] = (uint8_t) (reasons >> 8);
    out[2] = (uint8_t) (reasons >> 16);
    out[3] = (uint8_t) (reasons >> 24);
    return (uint16_t) (4 + CoopIdentity_Write(id, out + 4, COOP_NET_MAX_MESSAGE - 4));
}

// A client session against a raw host, up to the point where the client has said hello.
static CoopSession* Raw_HostUp(CoopTransport** raw, const CoopIdentity* id, uint64_t seed, Side* side,
                               const char* name) {
    CoopSession* client;
    CoopNetEvent net;

    Sim_Reset(seed, 0, 5);
    memset(side, 0, sizeof(*side));
    *raw = Sim_Transport(SIM_HOST);
    client = CoopSession_Join(Sim_Transport(2), id, sNet.now);
    EXPECT(Raw_Wait(*raw, client, side, COOP_NET_EVENT_MESSAGE, COOP_MSG_SESSION_HELLO, &net, 3000), name,
           "the client sent no hello");
    return client;
}

// The client holds back game messages until the welcome, and for good after a refusal.
static void Sim_RawHost(void) {
    const char* name = "raw_host";
    CoopIdentity a, other;
    CoopSession* client;
    CoopTransport* raw;
    CoopNetEvent net;
    Side side;
    uint8_t buf[COOP_NET_MAX_MESSAGE];
    uint16_t size;

    Id_Default(&a);
    client = Raw_HostUp(&raw, &a, 8, &side, name);
    EXPECT(!CoopSession_Send(client, 0, COOP_MSG_TEST_SESSION, TEST_PAYLOAD, sizeof(TEST_PAYLOAD), COOP_NET_RELIABLE),
           name, "the client could send before the welcome");
    Raw_Game(raw, name);
    Pump(client, &side, 300, NULL, NULL);
    EXPECT(side.messages == 0, name, "a game message reached a client before the welcome");
    EXPECT(CoopSession_Ignored(client) == 1, name, "the client did not count the message it dropped");

    size = Wire_Welcome(&a, 1, buf);
    Raw_Send(raw, name, COOP_MSG_SESSION_WELCOME, buf, size);
    Pump(client, &side, 300, NULL, NULL);
    EXPECT(side.joined == 1 && side.playerIndex == 1, name, "the client did not take the welcome");
    Raw_Game(raw, name);
    Pump(client, &side, 300, NULL, NULL);
    EXPECT(side.messages == 1 && CoopSession_Ignored(client) == 1, name, "the game message did not pass the welcome");
    EXPECT(CoopSession_Send(client, 0, COOP_MSG_TEST_SESSION, TEST_PAYLOAD, sizeof(TEST_PAYLOAD), COOP_NET_RELIABLE),
           name, "the client could not send after the welcome");
    EXPECT(Raw_Wait(raw, client, &side, COOP_NET_EVENT_MESSAGE, COOP_MSG_TEST_SESSION, &net, 3000), name,
           "the client's game message did not arrive");
    CoopSession_Close(client);
    raw->ops->close(raw);

    // A message either side of the refusal: neither gets in.
    other = a;
    snprintf(other.build, sizeof(other.build), "other-build");
    client = Raw_HostUp(&raw, &a, 9, &side, name);
    Raw_Game(raw, name);
    size = Wire_Refuse(&other, COOP_ID_DIFF_BUILD, buf);
    Raw_Send(raw, name, COOP_MSG_SESSION_REFUSE, buf, size);
    Raw_Game(raw, name);
    Pump(client, &side, 300, NULL, NULL);
    EXPECT(side.refused == 1 && side.ended == 1 && !side.joined, name, "the client did not take the refusal");
    EXPECT(side.messages == 0 && side.otherMessages == 0, name, "a game message reached a refused client");
    EXPECT(CoopSession_Ignored(client) == 1, name, "the message before the refusal was not dropped");
    CoopSession_Close(client);
    raw->ops->close(raw);
}

// A host session against a raw client, up to the connection.
static CoopSession* Raw_ClientUp(CoopTransport** raw, const CoopIdentity* id, uint64_t seed, Side* sides,
                                 const char* name) {
    CoopSession* host;
    CoopNetEvent net;

    Sim_Reset(seed, 0, 5);
    memset(sides, 0, sizeof(Side) * COOP_NET_MAX_PEERS);
    host = CoopSession_Host(Sim_Transport(SIM_HOST), id);
    *raw = Sim_Transport(2);
    EXPECT(Raw_Wait(*raw, host, sides, COOP_NET_EVENT_CONNECTED, 0, &net, 3000), name, "no connection");
    return host;
}

// The host holds back game messages until the hello matched, and from a refused client for good.
static void Sim_RawClient(void) {
    const char* name = "raw_client";
    CoopIdentity a, other;
    CoopSession* host;
    CoopTransport* raw;
    CoopNetEvent net;
    Side sides[COOP_NET_MAX_PEERS];
    uint8_t buf[COOP_NET_MAX_MESSAGE];
    uint16_t size;

    Id_Default(&a);
    size = CoopIdentity_Write(&a, buf, sizeof(buf));
    host = Raw_ClientUp(&raw, &a, 10, sides, name);
    EXPECT(!CoopSession_Send(host, 0, COOP_MSG_TEST_SESSION, TEST_PAYLOAD, sizeof(TEST_PAYLOAD), COOP_NET_RELIABLE),
           name, "the host could send before the hello");
    Raw_Game(raw, name);
    Pump(host, sides, 300, NULL, NULL);
    EXPECT(sides[0].messages == 0 && CoopSession_Ignored(host) == 1, name, "a game message reached the host early");

    Raw_Send(raw, name, COOP_MSG_SESSION_HELLO, buf, size);
    EXPECT(Raw_Wait(raw, host, sides, COOP_NET_EVENT_MESSAGE, COOP_MSG_SESSION_WELCOME, &net, 3000), name,
           "no welcome for a matching hello");
    EXPECT(sides[0].joined == 1 && net.data[0] == 1, name, "the host did not take the client as player 1");
    Raw_Game(raw, name);
    Pump(host, sides, 300, NULL, NULL);
    EXPECT(sides[0].messages == 1 && CoopSession_Ignored(host) == 1, name, "the game message did not pass the hello");
    CoopSession_Close(host);
    raw->ops->close(raw);

    // A message behind a hello that gets refused does not get in, and the host stays silent towards this client.
    other = a;
    snprintf(other.build, sizeof(other.build), "other-build");
    size = CoopIdentity_Write(&other, buf, sizeof(buf));
    host = Raw_ClientUp(&raw, &a, 11, sides, name);
    Raw_Send(raw, name, COOP_MSG_SESSION_HELLO, buf, size);
    Raw_Game(raw, name);
    EXPECT(Raw_Wait(raw, host, sides, COOP_NET_EVENT_MESSAGE, COOP_MSG_SESSION_REFUSE, &net, 3000), name,
           "no refusal for another build");
    Pump(host, sides, 300, NULL, NULL);
    EXPECT(sides[0].refused == 1 && !sides[0].joined, name, "the host did not refuse");
    EXPECT(sides[0].messages == 0 && CoopSession_Ignored(host) == 1, name, "a game message got past a refusal");
    EXPECT(!CoopSession_Send(host, 0, COOP_MSG_TEST_SESSION, TEST_PAYLOAD, sizeof(TEST_PAYLOAD), COOP_NET_RELIABLE),
           name, "the host could send to a refused client");
    CoopSession_Close(host);
    raw->ops->close(raw);
}

// ---- A scripted transport: any event, any failure, and a record of every call ----

#define SCRIPT_T0 0xFFFFFC00u // scripted clocks start here, so the time-outs run across the 32-bit wrap
#define SCRIPT_EVENTS 8
#define SCRIPT_SENDS 8

typedef struct {
    int peer;
    uint16_t msgId;
    uint16_t size;
    CoopNetLane lane;
    uint8_t data[COOP_NET_MAX_MESSAGE];
} ScriptSent;

typedef struct {
    CoopTransport base;
    CoopNetEvent events[SCRIPT_EVENTS];
    uint32_t eventCount;
    uint32_t eventNext;
    uint8_t* given;     // payload of the last event, gone at the next poll as transport.h allows
    uint32_t failSends; // this many sends fail, starting with the next one
    ScriptSent sent[SCRIPT_SENDS];
    uint32_t sentCount;
    uint32_t disconnects[COOP_NET_MAX_PEERS];
    uint32_t pending; // what stats reports
    uint32_t closes;
} Script;

static bool Script_Send(CoopTransport* transport, int peer, uint16_t msgId, const void* data, uint16_t size,
                        CoopNetLane lane) {
    Script* t = (Script*) transport;
    ScriptSent* m;

    if (t->failSends > 0) {
        t->failSends--;
        return false;
    }
    EXPECT(t->sentCount < SCRIPT_SENDS, "script", "more sends than the script has room for");
    m = &t->sent[t->sentCount++];
    m->peer = peer;
    m->msgId = msgId;
    m->size = size;
    m->lane = lane;
    if (size != 0) {
        memcpy(m->data, data, size);
    }
    return true;
}

static bool Script_Poll(CoopTransport* transport, CoopNetEvent* ev) {
    Script* t = (Script*) transport;

    free(t->given);
    t->given = NULL;
    if (t->eventNext == t->eventCount) {
        memset(ev, 0, sizeof(*ev));
        return false;
    }
    *ev = t->events[t->eventNext++];
    t->given = (uint8_t*) ev->data;
    return true;
}

static void Script_Disconnect(CoopTransport* transport, int peer) {
    Script* t = (Script*) transport;

    EXPECT(peer >= 0 && peer < COOP_NET_MAX_PEERS, "script", "disconnect of a peer that cannot exist");
    t->disconnects[peer]++;
}

static bool Script_Stats(CoopTransport* transport, int peer, CoopNetStats* out) {
    (void) peer;
    memset(out, 0, sizeof(*out));
    out->pending = ((Script*) transport)->pending;
    return true;
}

static void Script_Close(CoopTransport* transport) {
    Script* t = (Script*) transport;

    free(t->given);
    t->given = NULL;
    while (t->eventNext < t->eventCount) {
        free((void*) t->events[t->eventNext++].data);
    }
    t->closes++;
}

static const CoopTransportOps sScriptOps = { Script_Send, Script_Poll, Script_Disconnect, Script_Stats, Script_Close };

static void Script_Init(Script* t) {
    memset(t, 0, sizeof(*t));
    t->base.ops = &sScriptOps;
}

// Each payload gets a block of exactly its size and an empty one none at all, so a read past the end shows: at once
// for an empty payload, in the sanitizer run for the others.
static void Script_Add(Script* t, CoopNetEventType type, int peer, CoopNetEnd end, uint16_t msgId, const void* data,
                       uint16_t size) {
    CoopNetEvent* ev;
    uint8_t* copy = NULL;

    if (t->eventNext == t->eventCount) {
        t->eventNext = t->eventCount = 0;
    }
    EXPECT(t->eventCount < SCRIPT_EVENTS, "script", "more events than the script has room for");
    if (size != 0) {
        copy = malloc(size);
        memcpy(copy, data, size);
    }
    ev = &t->events[t->eventCount++];
    memset(ev, 0, sizeof(*ev));
    ev->type = type;
    ev->peer = peer;
    ev->end = end;
    ev->msgId = msgId;
    ev->size = size;
    ev->data = copy;
}

static void Script_Connected(Script* t, int peer) {
    Script_Add(t, COOP_NET_EVENT_CONNECTED, peer, COOP_NET_END_NONE, 0, NULL, 0);
}

static void Script_Gone(Script* t, int peer, CoopNetEnd end) {
    Script_Add(t, COOP_NET_EVENT_DISCONNECTED, peer, end, 0, NULL, 0);
}

static void Script_Message(Script* t, int peer, uint16_t msgId, const void* data, uint16_t size) {
    Script_Add(t, COOP_NET_EVENT_MESSAGE, peer, COOP_NET_END_NONE, msgId, data, size);
}

static void Script_Game(Script* t, int peer) {
    Script_Message(t, peer, COOP_MSG_TEST_SESSION, TEST_PAYLOAD, sizeof(TEST_PAYLOAD));
}

static void Script_Hello(Script* t, int peer, const CoopIdentity* id) {
    uint8_t buf[COOP_NET_MAX_MESSAGE];

    Script_Message(t, peer, COOP_MSG_SESSION_HELLO, buf, CoopIdentity_Write(id, buf, sizeof(buf)));
}

static void Script_Welcome(Script* t, const CoopIdentity* id, uint8_t player) {
    uint8_t buf[COOP_NET_MAX_MESSAGE];

    Script_Message(t, 0, COOP_MSG_SESSION_WELCOME, buf, Wire_Welcome(id, player, buf));
}

static void Script_Refuse(Script* t, const CoopIdentity* id, uint32_t reasons) {
    uint8_t buf[COOP_NET_MAX_MESSAGE];

    Script_Message(t, 0, COOP_MSG_SESSION_REFUSE, buf, Wire_Refuse(id, reasons, buf));
}

// The last thing sent: msgId to peer on the reliable lane, with the identity id behind the first skip bytes.
static void Expect_Sent(const char* name, const Script* t, int peer, uint16_t msgId, uint16_t skip,
                        const CoopIdentity* id) {
    const ScriptSent* m;
    CoopIdentity got;

    EXPECT(t->sentCount > 0, name, "nothing was sent");
    m = &t->sent[t->sentCount - 1];
    EXPECT(m->peer == peer && m->msgId == msgId && m->lane == COOP_NET_RELIABLE, name, "the wrong message was sent");
    EXPECT(m->size > skip, name, "a handshake message without an identity");
    EXPECT(CoopIdentity_Read(&got, m->data + skip, (uint16_t) (m->size - skip)) == m->size - skip, name,
           "the identity sent cannot be read");
    EXPECT(CoopIdentity_Compare(&got, id) == 0, name, "the wrong identity was sent");
}

static bool Session_CanSend(CoopSession* s, int peer) {
    return CoopSession_Send(s, peer, COOP_MSG_TEST_SESSION, TEST_PAYLOAD, sizeof(TEST_PAYLOAD), COOP_NET_RELIABLE);
}

// An identity with another build, so a hello with it gets refused.
static void Id_Other(CoopIdentity* id) {
    Id_Default(id);
    snprintf(id->build, sizeof(id->build), "other-build");
}

static CoopSession* Script_Host(Script* t, Side* sides, const CoopIdentity* id) {
    Script_Init(t);
    memset(sides, 0, sizeof(Side) * COOP_NET_MAX_PEERS);
    return CoopSession_Host(&t->base, id);
}

// A client session at SCRIPT_T0: connected, hello sent.
static CoopSession* Script_Client(Script* t, Side* side, const CoopIdentity* id) {
    CoopSession* s;

    Script_Init(t);
    memset(side, 0, sizeof(*side));
    s = CoopSession_Join(&t->base, id, SCRIPT_T0);
    Script_Connected(t, 0);
    Drain(s, SCRIPT_T0, side);
    return s;
}

static void Script_Lifetime(void) {
    const char* name = "lifetime";
    CoopIdentity a;
    Script t;
    Side sides[COOP_NET_MAX_PEERS];
    CoopSession* s;

    Id_Default(&a);
    EXPECT(CoopSession_Host(NULL, &a) == NULL && CoopSession_Join(NULL, &a, 0) == NULL, name,
           "a session without a transport");
    CoopSession_Close(NULL);
    s = Script_Host(&t, sides, &a);
    CoopSession_Close(s);
    EXPECT(t.closes == 1, name, "the session did not close its transport");
}

static void Script_EndTexts(void) {
    static const struct {
        CoopSessionEnd end;
        const char* needle;
    } cases[] = {
        { COOP_SESSION_END_REFUSED, "refused this game" },
        { COOP_SESSION_END_CLOSED, "closed the connection" },
        { COOP_SESSION_END_TIMEOUT, "timed out" },
        { COOP_SESSION_END_HOST_FULL, "host is full" },
        { COOP_SESSION_END_PROTOCOL, "another version of the co-op protocol" },
        { COOP_SESSION_END_NO_HANDSHAKE, "older build" },
        { COOP_SESSION_END_BAD_MESSAGE, "cannot read" },
    };
    static const struct {
        CoopNetEnd net;
        CoopSessionEnd session;
    } ends[] = {
        { COOP_NET_END_REFUSED_VERSION, COOP_SESSION_END_PROTOCOL },
        { COOP_NET_END_REFUSED_FULL, COOP_SESSION_END_HOST_FULL },
        { COOP_NET_END_TIMEOUT, COOP_SESSION_END_TIMEOUT },
        { COOP_NET_END_CLOSED, COOP_SESSION_END_CLOSED },
    };
    CoopIdentity a;
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        EXPECT(strstr(CoopSession_EndText(cases[i].end), cases[i].needle) != NULL, "end_text", "wrong text");
    }
    EXPECT(CoopSession_EndText(COOP_SESSION_END_NONE)[0] == '\0', "end_text", "text for no ending");

    // A transport ending before the handshake, on a client; the same ending for a joined peer, on a host.
    Id_Default(&a);
    for (i = 0; i < sizeof(ends) / sizeof(ends[0]); i++) {
        Script t;
        Side side;
        Side sides[COOP_NET_MAX_PEERS];
        CoopSession* s;

        s = Script_Client(&t, &side, &a);
        Script_Gone(&t, 0, ends[i].net);
        Drain(s, SCRIPT_T0 + 1, &side);
        EXPECT(side.ended == 1 && side.end == ends[i].session, "transport_end", "a transport ending was not passed on");
        EXPECT(strcmp(side.endText, CoopSession_EndText(ends[i].session)) == 0, "transport_end", "wrong text");
        EXPECT(Drain(s, SCRIPT_T0 + 2, &side) == 0, "transport_end", "an event after the end");
        CoopSession_Close(s);

        s = Script_Host(&t, sides, &a);
        Script_Connected(&t, 0);
        Script_Hello(&t, 0, &a);
        Script_Gone(&t, 0, ends[i].net);
        Drain(s, SCRIPT_T0, sides);
        EXPECT(sides[0].left == 1 && sides[0].end == ends[i].session, "host_left",
               "a transport ending was not passed on");
        EXPECT(strcmp(sides[0].endText, CoopSession_EndText(ends[i].session)) == 0, "host_left", "wrong text");
        CoopSession_Close(s);
    }
}

// A silent client gets HANDSHAKE_MS to the millisecond, and its refusal REFUSE_LINGER_MS to arrive.
static void Script_HostSilentClient(void) {
    const char* name = "host_silent_client";
    uint32_t refusedAt = SCRIPT_T0 + COOP_SESSION_HANDSHAKE_MS;
    CoopIdentity a;
    Script t;
    Side sides[COOP_NET_MAX_PEERS];
    CoopSession* s;

    Id_Default(&a);
    s = Script_Host(&t, sides, &a);
    Script_Connected(&t, 0);
    Drain(s, SCRIPT_T0, sides);
    Drain(s, SCRIPT_T0 + 1, sides);
    Drain(s, refusedAt - 1, sides);
    EXPECT(sides[0].events == 0 && t.sentCount == 0, name, "refused before the handshake time");
    t.pending = 1;
    Drain(s, refusedAt, sides);
    EXPECT(sides[0].refused == 1 && sides[0].diff == 0, name, "no refusal at the handshake time");
    EXPECT(sides[0].end == COOP_SESSION_END_REFUSED, name, "host event does not say it refused");
    EXPECT(strstr(sides[0].text, "did not finish in time") != NULL, name, "host text is wrong");
    Expect_Sent(name, &t, 0, COOP_MSG_SESSION_REFUSE, 4, &a);
    EXPECT(memcmp(t.sent[0].data, "\0\0\0\0", 4) == 0, name, "a time-out has no reasons");

    Drain(s, refusedAt + COOP_SESSION_REFUSE_LINGER_MS - 1, sides);
    EXPECT(t.disconnects[0] == 0, name, "dropped before the refusal could arrive");
    Drain(s, refusedAt + COOP_SESSION_REFUSE_LINGER_MS, sides);
    EXPECT(t.disconnects[0] == 1, name, "kept past the linger time");
    Drain(s, refusedAt + COOP_SESSION_REFUSE_LINGER_MS + 1, sides);
    EXPECT(t.disconnects[0] == 1 && sides[0].events == 1 && t.sentCount == 1, name, "dropped twice");
    CoopSession_Close(s);
}

// A refused peer stays until the refusal is acknowledged, the client goes or the linger time from the refusal is up.
static void Script_HostRefusal(void) {
    const char* name = "host_refusal";
    uint32_t late = SCRIPT_T0 + COOP_SESSION_HANDSHAKE_MS - 1000;
    CoopIdentity a, other;
    Script t;
    Side sides[COOP_NET_MAX_PEERS];
    CoopSession* s;

    Id_Default(&a);
    Id_Other(&other);

    // Acknowledged.
    s = Script_Host(&t, sides, &a);
    t.pending = 1;
    Script_Connected(&t, 0);
    Script_Hello(&t, 0, &other);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(sides[0].refused == 1 && sides[0].end == COOP_SESSION_END_REFUSED, name, "another build was not refused");
    Expect_Sent(name, &t, 0, COOP_MSG_SESSION_REFUSE, 4, &a);
    EXPECT(t.sent[0].data[0] == COOP_ID_DIFF_BUILD, name, "the refusal does not give the reason");
    Drain(s, SCRIPT_T0 + 1, sides);
    Drain(s, SCRIPT_T0 + COOP_SESSION_REFUSE_LINGER_MS - 1, sides);
    EXPECT(t.disconnects[0] == 0, name, "dropped with the refusal still on its way");
    t.pending = 0;
    Drain(s, SCRIPT_T0 + COOP_SESSION_REFUSE_LINGER_MS - 1, sides);
    EXPECT(t.disconnects[0] == 1, name, "not dropped once the refusal had arrived");
    CoopSession_Close(s);

    // Linger time from the refusal, not from the connection.
    s = Script_Host(&t, sides, &a);
    t.pending = 1;
    Script_Connected(&t, 0);
    Drain(s, SCRIPT_T0, sides);
    Script_Hello(&t, 0, &other);
    Drain(s, late, sides);
    EXPECT(sides[0].refused == 1 && t.disconnects[0] == 0, name, "a late hello was not refused");
    Drain(s, late + 1, sides);
    Drain(s, late + COOP_SESSION_REFUSE_LINGER_MS - 1, sides);
    EXPECT(t.disconnects[0] == 0, name, "the linger time ran from the connection");
    Drain(s, late + COOP_SESSION_REFUSE_LINGER_MS, sides);
    EXPECT(t.disconnects[0] == 1 && sides[0].refused == 1, name, "kept past the linger time");
    CoopSession_Close(s);

    // The client goes first: no goodbye of ours, no "left" for someone who never joined.
    s = Script_Host(&t, sides, &a);
    t.pending = 1;
    Script_Connected(&t, 0);
    Script_Hello(&t, 0, &other);
    Drain(s, SCRIPT_T0, sides);
    Script_Gone(&t, 0, COOP_NET_END_CLOSED);
    Drain(s, SCRIPT_T0 + 1, sides);
    Drain(s, SCRIPT_T0 + COOP_SESSION_REFUSE_LINGER_MS + 1, sides);
    EXPECT(sides[0].left == 0 && t.disconnects[0] == 0, name, "a refused client that went was treated as joined");
    CoopSession_Close(s);

    // A client that goes before its hello is forgotten too.
    s = Script_Host(&t, sides, &a);
    Script_Connected(&t, 0);
    Script_Gone(&t, 0, COOP_NET_END_CLOSED);
    Drain(s, SCRIPT_T0, sides);
    Drain(s, SCRIPT_T0 + COOP_SESSION_HANDSHAKE_MS + 1, sides);
    EXPECT(sides[0].events == 0 && t.sentCount == 0, name, "a client that went was refused all the same");
    CoopSession_Close(s);
}

static void Expect_BadHello(const char* name, const uint8_t* data, uint16_t size) {
    CoopIdentity a;
    Script t;
    Side sides[COOP_NET_MAX_PEERS];
    CoopSession* s;

    Id_Default(&a);
    s = Script_Host(&t, sides, &a);
    Script_Connected(&t, 0);
    Script_Message(&t, 0, COOP_MSG_SESSION_HELLO, data, size);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(sides[0].refused == 1 && sides[0].joined == 0, name, "a hello that cannot be read was not refused");
    EXPECT(sides[0].diff == COOP_SESSION_REFUSE_MALFORMED, name, "the refusal does not say the hello was broken");
    EXPECT(strstr(sides[0].text, "cannot read") != NULL, name, "host text is wrong");
    Expect_Sent(name, &t, 0, COOP_MSG_SESSION_REFUSE, 4, &a);
    EXPECT(memcmp(t.sent[0].data, "\0\1\0\0", 4) == 0, name, "the reason bytes are wrong");
    CoopSession_Close(s);
}

static void Script_HostBadHellos(void) {
    CoopIdentity a;
    uint8_t buf[COOP_NET_MAX_MESSAGE] = { 0 };
    uint16_t size;

    Id_Default(&a);
    size = CoopIdentity_Write(&a, buf, sizeof(buf));
    Expect_BadHello("hello_empty", NULL, 0);
    Expect_BadHello("hello_cut", buf, (uint16_t) (size - 1));
    Expect_BadHello("hello_trailing", buf, (uint16_t) (size + 1));
}

// The transport can refuse a send at any moment; a peer must not be left half in.
static void Script_HostSendFails(void) {
    const char* name = "host_send_fails";
    CoopIdentity a, other;
    Script t;
    Side sides[COOP_NET_MAX_PEERS];
    CoopSession* s;

    Id_Default(&a);
    Id_Other(&other);

    // The refusal cannot go out: the peer is dropped at once and nothing further happens.
    s = Script_Host(&t, sides, &a);
    t.failSends = 1;
    Script_Connected(&t, 0);
    Script_Hello(&t, 0, &other);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(sides[0].refused == 1 && t.disconnects[0] == 1, name, "a refusal that failed kept the peer");
    Drain(s, SCRIPT_T0 + COOP_SESSION_HANDSHAKE_MS + 1, sides);
    EXPECT(sides[0].events == 1 && t.disconnects[0] == 1, name, "a dropped peer still had a state");
    CoopSession_Close(s);

    // The welcome cannot go out: no join; the refusal that takes its place can.
    s = Script_Host(&t, sides, &a);
    t.failSends = 1;
    t.pending = 1;
    Script_Connected(&t, 0);
    Script_Hello(&t, 0, &a);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(sides[0].refused == 1 && sides[0].joined == 0, name, "joined although the welcome failed");
    EXPECT(sides[0].diff == 0 && t.disconnects[0] == 0, name, "wrong refusal after a welcome that failed");
    Expect_Sent(name, &t, 0, COOP_MSG_SESSION_REFUSE, 4, &a);
    EXPECT(CoopSession_PeerIdentity(s, 0) == NULL && !Session_CanSend(s, 0), name, "a refused peer counts as joined");
    CoopSession_Close(s);

    // Neither can go out.
    s = Script_Host(&t, sides, &a);
    t.failSends = 2;
    Script_Connected(&t, 0);
    Script_Hello(&t, 0, &a);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(sides[0].refused == 1 && t.disconnects[0] == 1 && t.sentCount == 0, name, "a peer stayed with nothing sent");
    CoopSession_Close(s);

    // The host's own identity does not fit the wire form: no refusal without one.
    other = a;
    other.cvarCount = COOP_SIM_CVARS_MAX + 1;
    s = Script_Host(&t, sides, &other);
    Script_Connected(&t, 0);
    Script_Hello(&t, 0, &a);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(sides[0].refused == 1 && t.sentCount == 0 && t.disconnects[0] == 1, name, "a refusal without an identity");
    CoopSession_Close(s);
}

// What may reach the game on a host, and what may leave it, in each state of a peer.
static void Script_HostHoldBack(void) {
    const char* name = "host_hold_back";
    CoopIdentity a, other;
    const CoopIdentity* seen;
    Script t;
    Side sides[COOP_NET_MAX_PEERS];
    CoopSession* s;
    uint32_t sent;

    Id_Default(&a);
    Id_Other(&other);
    other.cvars[0] = 7; // still refused, but a read identity must be the one that was sent

    // Waiting for the hello.
    s = Script_Host(&t, sides, &a);
    EXPECT(CoopSession_PeerIdentity(s, 0) == NULL && !Session_CanSend(s, 0), name, "a free slot counts as joined");
    Script_Connected(&t, 0);
    Script_Game(&t, 0);
    Script_Message(&t, 0, COOP_MSG_SESSION_WELCOME, NULL, 0);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(sides[0].events == 0 && CoopSession_Ignored(s) == 2, name, "a message passed before the hello");
    EXPECT(CoopSession_PeerIdentity(s, 0) == NULL, name, "an identity before the hello");
    EXPECT(!Session_CanSend(s, 0) && t.sentCount == 0, name, "a message left before the hello");

    // Joined: game messages pass in both directions, from the first id above the session range; session ids do not.
    Script_Hello(&t, 0, &a);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(sides[0].joined == 1 && sides[0].events == 1, name, "a matching hello was not taken");
    seen = CoopSession_PeerIdentity(s, 0);
    EXPECT(seen != NULL && CoopIdentity_Compare(seen, &a) == 0, name, "the joined peer's identity is wrong");
    Script_Hello(&t, 0, &a);
    Script_Message(&t, 0, COOP_MSG_SESSION_END - 1, TEST_PAYLOAD, sizeof(TEST_PAYLOAD));
    Drain(s, SCRIPT_T0, sides);
    EXPECT(sides[0].joined == 1 && sides[0].otherMessages == 0 && CoopSession_Ignored(s) == 4, name,
           "a session message reached the game");
    Script_Message(&t, 0, COOP_MSG_SESSION_END, TEST_PAYLOAD, sizeof(TEST_PAYLOAD));
    Script_Game(&t, 0);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(sides[0].otherMessages == 1 && sides[0].messages == 1 && CoopSession_Ignored(s) == 4, name,
           "a game message did not pass");

    sent = t.sentCount;
    EXPECT(!CoopSession_Send(s, 0, COOP_MSG_SESSION_END - 1, TEST_PAYLOAD, sizeof(TEST_PAYLOAD), COOP_NET_RELIABLE) &&
               t.sentCount == sent,
           name, "the game could send a session message");
    EXPECT(CoopSession_Send(s, 0, COOP_MSG_SESSION_END, TEST_PAYLOAD, sizeof(TEST_PAYLOAD), COOP_NET_UNRELIABLE), name,
           "the first game id could not be sent");
    EXPECT(t.sentCount == sent + 1 && t.sent[sent].peer == 0 && t.sent[sent].msgId == COOP_MSG_SESSION_END &&
               t.sent[sent].size == sizeof(TEST_PAYLOAD) && t.sent[sent].lane == COOP_NET_UNRELIABLE &&
               memcmp(t.sent[sent].data, TEST_PAYLOAD, sizeof(TEST_PAYLOAD)) == 0,
           name, "the message sent is not the one asked for");
    t.failSends = 1;
    EXPECT(!Session_CanSend(s, 0), name, "a failed send was reported as sent");
    EXPECT(!Session_CanSend(s, 1) && !Session_CanSend(s, -1) && !Session_CanSend(s, COOP_NET_MAX_PEERS), name,
           "a send to a peer that is not there");
    EXPECT(CoopSession_PeerIdentity(s, 1) == NULL, name, "an identity for a free slot");

    // The handshake time is over once joined.
    Drain(s, SCRIPT_T0 + COOP_SESSION_HANDSHAKE_MS + 1, sides);
    EXPECT(sides[0].refused == 0 && t.disconnects[0] == 0, name, "the handshake time ended a joined client");
    CoopSession_Close(s);

    // Refused: nothing comes in, nothing goes out, and a good hello is too late.
    s = Script_Host(&t, sides, &a);
    t.pending = 1;
    Script_Connected(&t, 0);
    Script_Hello(&t, 0, &other);
    Drain(s, SCRIPT_T0, sides);
    sent = t.sentCount;
    Script_Game(&t, 0);
    Script_Hello(&t, 0, &a);
    Drain(s, SCRIPT_T0 + 1, sides);
    EXPECT(sides[0].events == 1 && CoopSession_Ignored(s) == 2, name, "a refused peer got through");
    EXPECT(CoopSession_PeerIdentity(s, 0) == NULL, name, "an identity for a refused peer");
    EXPECT(!Session_CanSend(s, 0) && t.sentCount == sent, name, "a message left for a refused peer");
    CoopSession_Close(s);
}

// Slots: each peer its own player, and only peers that can exist.
static void Script_HostSlots(void) {
    const char* name = "host_slots";
    CoopIdentity a, other;
    Script t;
    Side sides[COOP_NET_MAX_PEERS];
    CoopSession* s;
    int peer;

    Id_Default(&a);
    Id_Other(&other);
    s = Script_Host(&t, sides, &a);
    for (peer = 0; peer < COOP_NET_MAX_PEERS; peer++) {
        Script_Connected(&t, peer);
        Script_Hello(&t, peer, &a);
        Drain(s, SCRIPT_T0, sides);
        EXPECT(sides[peer].joined == 1 && sides[peer].playerIndex == peer + 1, name,
               "the host event has the wrong slot");
        Expect_Sent(name, &t, peer, COOP_MSG_SESSION_WELCOME, 1, &a);
        EXPECT(t.sent[t.sentCount - 1].data[0] == peer + 1, name, "the welcome has the wrong slot");
    }

    // Events for a peer that cannot exist change nothing, not even a counter.
    Script_Connected(&t, -1);
    Script_Game(&t, -1);
    Script_Connected(&t, COOP_NET_MAX_PEERS);
    Script_Game(&t, COOP_NET_MAX_PEERS);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(CoopSession_Ignored(s) == 0, name, "an event for a peer that cannot exist was looked at");
    EXPECT(CoopSession_PeerIdentity(s, -1) == NULL && CoopSession_PeerIdentity(s, COOP_NET_MAX_PEERS) == NULL, name,
           "an identity for a peer that cannot exist");

    // Everything is told about the right peer.
    Script_Game(&t, 1);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(sides[1].messages == 1 && sides[0].messages == 0, name, "a message came from the wrong peer");
    Script_Gone(&t, 1, COOP_NET_END_CLOSED);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(sides[1].left == 1 && sides[0].left == 0, name, "the wrong peer left");

    // The welcomes left their bytes in the session's buffer; a refusal must write all of its reason bytes.
    t.pending = 1;
    Script_Connected(&t, 1);
    Script_Hello(&t, 1, &other);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(sides[1].refused == 1 && sides[0].refused == 0, name, "the wrong peer was refused");
    Expect_Sent(name, &t, 1, COOP_MSG_SESSION_REFUSE, 4, &a);
    EXPECT(memcmp(t.sent[t.sentCount - 1].data, "\1\0\0\0", 4) == 0, name, "the reason bytes are wrong");
    t.pending = 0;
    Drain(s, SCRIPT_T0 + 1, sides);
    EXPECT(t.disconnects[1] == 1 && t.disconnects[0] == 0, name, "the wrong peer was dropped");
    CoopSession_Close(s);
}

static void Script_HostLeft(void) {
    const char* name = "host_left";
    uint32_t back = SCRIPT_T0 + 5000;
    CoopIdentity a;
    Script t;
    Side sides[COOP_NET_MAX_PEERS];
    CoopSession* s;

    Id_Default(&a);
    s = Script_Host(&t, sides, &a);
    Script_Connected(&t, 0);
    Script_Hello(&t, 0, &a);
    Drain(s, SCRIPT_T0, sides);
    Script_Gone(&t, 0, COOP_NET_END_CLOSED);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(sides[0].left == 1, name, "the host did not see a joined client go");
    EXPECT(CoopSession_PeerIdentity(s, 0) == NULL && !Session_CanSend(s, 0), name, "a client that left still counts");

    // The slot starts over: its handshake time runs from the new connection.
    Script_Connected(&t, 0);
    Drain(s, back, sides);
    Drain(s, back + COOP_SESSION_HANDSHAKE_MS - 1, sides);
    EXPECT(sides[0].refused == 0, name, "the handshake time ran from the old connection");
    Drain(s, back + COOP_SESSION_HANDSHAKE_MS, sides);
    EXPECT(sides[0].refused == 1, name, "the new connection had no handshake time");
    CoopSession_Close(s);
}

static void Script_ClientHello(void) {
    const char* name = "client_hello";
    CoopIdentity a;
    Script t;
    Side side;
    CoopSession* s;

    Id_Default(&a);
    s = Script_Client(&t, &side, &a);
    EXPECT(side.events == 0 && t.sentCount == 1, name, "the client did not say hello once");
    Expect_Sent(name, &t, 0, COOP_MSG_SESSION_HELLO, 0, &a);
    CoopSession_Close(s);

    // The hello cannot go out.
    Script_Init(&t);
    memset(&side, 0, sizeof(side));
    t.failSends = 1;
    s = CoopSession_Join(&t.base, &a, SCRIPT_T0);
    Script_Connected(&t, 0);
    Drain(s, SCRIPT_T0, &side);
    EXPECT(side.ended == 1 && side.end == COOP_SESSION_END_BAD_MESSAGE, name, "a hello that failed went unnoticed");
    EXPECT(t.disconnects[0] == 1, name, "the client stayed after its hello failed");
    CoopSession_Close(s);

    // This game's own identity does not fit the wire form.
    Script_Init(&t);
    memset(&side, 0, sizeof(side));
    a.cvarCount = COOP_SIM_CVARS_MAX + 1;
    s = CoopSession_Join(&t.base, &a, SCRIPT_T0);
    Script_Connected(&t, 0);
    Drain(s, SCRIPT_T0, &side);
    EXPECT(side.ended == 1 && side.end == COOP_SESSION_END_BAD_MESSAGE && t.sentCount == 0, name,
           "an identity that cannot be written went out");
    EXPECT(t.disconnects[0] == 1, name, "the client stayed without a hello");
    CoopSession_Close(s);
}

static void Script_ClientTimes(void) {
    const char* name = "client_times";
    uint32_t connectedAt = SCRIPT_T0 + 60000;
    CoopIdentity a;
    Script t;
    Side side;
    CoopSession* s;

    Id_Default(&a);

    // While connecting only the transport's timeout counts; the wait for the answer starts at the connection.
    Script_Init(&t);
    memset(&side, 0, sizeof(side));
    s = CoopSession_Join(&t.base, &a, SCRIPT_T0);
    Drain(s, connectedAt - 1, &side);
    EXPECT(side.events == 0 && t.disconnects[0] == 0, name, "the session timed out a connection attempt");
    Script_Connected(&t, 0);
    Drain(s, connectedAt, &side);
    Drain(s, connectedAt + COOP_SESSION_HANDSHAKE_MS - 1, &side);
    EXPECT(side.events == 0 && t.disconnects[0] == 0, name, "gave up on the host before the handshake time");
    Drain(s, connectedAt + COOP_SESSION_HANDSHAKE_MS, &side);
    EXPECT(side.ended == 1 && side.end == COOP_SESSION_END_NO_HANDSHAKE, name, "no end at the handshake time");
    EXPECT(strcmp(side.text, CoopSession_EndText(COOP_SESSION_END_NO_HANDSHAKE)) == 0, name, "wrong text");
    EXPECT(t.disconnects[0] == 1, name, "the client did not leave a silent host");
    EXPECT(Drain(s, connectedAt + COOP_SESSION_HANDSHAKE_MS + 1, &side) == 0 && t.disconnects[0] == 1, name,
           "an event after the end");
    EXPECT(CoopSession_PeerIdentity(s, 0) == NULL, name, "an identity after the end");
    CoopSession_Close(s);

    // Across the wrap of the clock.
    s = Script_Client(&t, &side, &a);
    Drain(s, SCRIPT_T0 + 1, &side);
    Drain(s, SCRIPT_T0 + COOP_SESSION_HANDSHAKE_MS - 1, &side);
    EXPECT(side.events == 0, name, "gave up early when the clock wrapped");
    Drain(s, SCRIPT_T0 + COOP_SESSION_HANDSHAKE_MS, &side);
    EXPECT(side.ended == 1, name, "never gave up when the clock wrapped");
    CoopSession_Close(s);
}

static void Expect_BadAnswer(const char* name, uint16_t msgId, const uint8_t* data, uint16_t size) {
    CoopIdentity a;
    Script t;
    Side side;
    CoopSession* s;

    Id_Default(&a);
    s = Script_Client(&t, &side, &a);
    Script_Message(&t, 0, msgId, data, size);
    Drain(s, SCRIPT_T0 + 1, &side);
    EXPECT(side.ended == 1 && side.end == COOP_SESSION_END_BAD_MESSAGE, name,
           "a broken answer did not end the session");
    EXPECT(side.joined == 0 && side.refused == 0, name, "a broken answer was taken for an answer");
    EXPECT(strcmp(side.text, CoopSession_EndText(COOP_SESSION_END_BAD_MESSAGE)) == 0, name, "wrong text");
    EXPECT(t.disconnects[0] == 1, name, "the client did not leave once");
    EXPECT(CoopSession_PeerIdentity(s, 0) == NULL && !Session_CanSend(s, 0), name, "a broken answer let the client in");
    CoopSession_Close(s);
}

static void Script_ClientBadAnswers(void) {
    CoopIdentity a;
    uint8_t buf[COOP_NET_MAX_MESSAGE] = { 0 };
    uint16_t size;

    Id_Default(&a);
    size = Wire_Welcome(&a, 1, buf);
    Expect_BadAnswer("welcome_empty", COOP_MSG_SESSION_WELCOME, NULL, 0);
    Expect_BadAnswer("welcome_no_identity", COOP_MSG_SESSION_WELCOME, buf, 1);
    Expect_BadAnswer("welcome_cut", COOP_MSG_SESSION_WELCOME, buf, (uint16_t) (size - 1));
    Expect_BadAnswer("welcome_trailing", COOP_MSG_SESSION_WELCOME, buf, (uint16_t) (size + 1));
    buf[0] = 0;
    Expect_BadAnswer("welcome_player_0", COOP_MSG_SESSION_WELCOME, buf, size);
    buf[0] = COOP_NET_MAX_PEERS + 1;
    Expect_BadAnswer("welcome_player_high", COOP_MSG_SESSION_WELCOME, buf, size);

    memset(buf, 0, sizeof(buf));
    size = Wire_Refuse(&a, COOP_ID_DIFF_BUILD, buf);
    Expect_BadAnswer("refuse_empty", COOP_MSG_SESSION_REFUSE, NULL, 0);
    Expect_BadAnswer("refuse_short", COOP_MSG_SESSION_REFUSE, buf, 3);
    Expect_BadAnswer("refuse_no_identity", COOP_MSG_SESSION_REFUSE, buf, 4);
    Expect_BadAnswer("refuse_cut", COOP_MSG_SESSION_REFUSE, buf, (uint16_t) (size - 1));
    Expect_BadAnswer("refuse_trailing", COOP_MSG_SESSION_REFUSE, buf, (uint16_t) (size + 1));
}

static void Script_ClientRefused(void) {
    static const struct {
        uint32_t reasons;
        const char* needle;
    } cases[] = {
        { COOP_ID_DIFF_BUILD, "Different game builds: host other-build, joining player test-build" },
        { COOP_SESSION_REFUSE_MALFORMED, "could not read this game's handshake" },
        { 0, "did not finish in time" },
        // All four bytes count: bit 2 is a reason, and the others are not ours to drop.
        { 0x04030204u, "These settings change the game" },
    };
    const char* name = "client_refused";
    CoopIdentity a, other;
    size_t i;

    Id_Default(&a);
    Id_Other(&other);
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Script t;
        Side side;
        CoopSession* s = Script_Client(&t, &side, &a);

        Script_Refuse(&t, &other, cases[i].reasons);
        // What the transport has left after the refusal must not count any more.
        Script_Game(&t, 0);
        Script_Welcome(&t, &a, 1);
        Script_Gone(&t, 0, COOP_NET_END_CLOSED);
        Drain(s, SCRIPT_T0 + 1, &side);
        EXPECT(side.refused == 1 && side.ended == 1 && side.events == 2, name, "a refusal is one REFUSED, one ENDED");
        EXPECT(side.diff == cases[i].reasons, name, "the reasons were not passed on");
        EXPECT(side.end == COOP_SESSION_END_REFUSED, name, "the client did not end with the refusal");
        EXPECT(side.refusedEnd == COOP_SESSION_END_REFUSED, name, "the refused event has no end");
        EXPECT(strstr(side.text, cases[i].needle) != NULL, name, "the text does not say why");
        EXPECT(strcmp(side.endText, CoopSession_EndText(COOP_SESSION_END_REFUSED)) == 0, name, "wrong ending text");
        EXPECT(t.disconnects[0] == 1, name, "the client did not leave once");
        EXPECT(Drain(s, SCRIPT_T0 + COOP_SESSION_HANDSHAKE_MS, &side) == 0, name, "an event after the end");
        EXPECT(CoopSession_PeerIdentity(s, 0) == NULL && !Session_CanSend(s, 0), name,
               "a refused client counts as joined");
        CoopSession_Close(s);
    }
}

// What may reach the game on a client before the host's answer.
static void Script_ClientHoldBack(void) {
    const char* name = "client_hold_back";
    CoopIdentity a;
    Script t;
    Side side;
    CoopSession* s;

    Id_Default(&a);

    // Still connecting: not even an answer counts.
    Script_Init(&t);
    memset(&side, 0, sizeof(side));
    s = CoopSession_Join(&t.base, &a, SCRIPT_T0);
    Script_Game(&t, 0);
    Script_Welcome(&t, &a, 1);
    Drain(s, SCRIPT_T0, &side);
    EXPECT(side.events == 0 && CoopSession_Ignored(s) == 2, name, "a message counted before the connection");
    EXPECT(CoopSession_PeerIdentity(s, 0) == NULL && !Session_CanSend(s, 0), name, "joined before the connection");
    CoopSession_Close(s);

    // Waiting for the answer: anything but the two answers is dropped, and the wait goes on.
    s = Script_Client(&t, &side, &a);
    Script_Game(&t, 0);
    Script_Message(&t, 0, COOP_MSG_SESSION_HELLO, NULL, 0);
    Drain(s, SCRIPT_T0 + 1, &side);
    EXPECT(side.events == 0 && CoopSession_Ignored(s) == 2 && t.disconnects[0] == 0, name,
           "a message was taken for the answer");
    EXPECT(CoopSession_PeerIdentity(s, 0) == NULL && !Session_CanSend(s, 0), name, "joined without a welcome");
    Script_Welcome(&t, &a, 1);
    Drain(s, SCRIPT_T0 + 1, &side);
    EXPECT(side.joined == 1, name, "the welcome after the dropped messages was not taken");
    CoopSession_Close(s);
}

// After the welcome.
static void Script_ClientJoined(void) {
    const char* name = "client_joined";
    CoopIdentity a, host;
    const CoopIdentity* seen;
    Script t;
    Side side;
    CoopSession* s;
    uint32_t sent;
    uint8_t player;

    Id_Default(&a);
    host = a;
    snprintf(host.platform, sizeof(host.platform), "Other platform");
    for (player = 1; player <= COOP_NET_MAX_PEERS; player++) {
        s = Script_Client(&t, &side, &a);
        Script_Welcome(&t, &host, player);
        Drain(s, SCRIPT_T0 + 1, &side);
        EXPECT(side.joined == 1 && side.playerIndex == player, name, "the client did not take its player index");
        EXPECT(side.diff == COOP_ID_DIFF_PLATFORM, name, "the client did not compare the host's identity");
        EXPECT(strstr(side.text, "Warning: host runs Other platform, joining player runs") != NULL, name,
               "wrong warning");
        seen = CoopSession_PeerIdentity(s, 0);
        EXPECT(seen != NULL && CoopIdentity_Compare(seen, &host) == 0, name, "the host's identity is wrong");
        EXPECT(CoopSession_PeerIdentity(s, 1) == NULL, name, "a client has one peer");
        EXPECT(CoopSession_PeerIdentity(s, -1) == NULL && CoopSession_PeerIdentity(s, COOP_NET_MAX_PEERS) == NULL, name,
               "an identity for a peer that cannot exist");
        CoopSession_Close(s);
    }

    s = Script_Client(&t, &side, &a);
    Script_Welcome(&t, &a, 1);
    Drain(s, SCRIPT_T0 + 1, &side);
    EXPECT(side.joined == 1 && side.diff == 0 && side.text[0] == '\0', name, "a match produced a message");

    // The handshake time is over once joined.
    Drain(s, SCRIPT_T0 + COOP_SESSION_HANDSHAKE_MS + 1, &side);
    EXPECT(side.events == 1 && t.disconnects[0] == 0, name, "the handshake time ended a joined client");

    // Another answer is out of place, session ids do not reach the game, game ids from the first one up do.
    Script_Welcome(&t, &a, 2);
    Script_Refuse(&t, &a, COOP_ID_DIFF_BUILD);
    Script_Message(&t, 0, COOP_MSG_SESSION_END - 1, TEST_PAYLOAD, sizeof(TEST_PAYLOAD));
    Drain(s, SCRIPT_T0 + 2, &side);
    EXPECT(side.events == 1 && CoopSession_Ignored(s) == 3 && t.disconnects[0] == 0, name,
           "a session message counted after the welcome");
    Script_Message(&t, 0, COOP_MSG_SESSION_END, TEST_PAYLOAD, sizeof(TEST_PAYLOAD));
    Script_Game(&t, 0);
    Drain(s, SCRIPT_T0 + 2, &side);
    EXPECT(side.otherMessages == 1 && side.messages == 1 && CoopSession_Ignored(s) == 3, name,
           "a game message did not pass");

    sent = t.sentCount;
    EXPECT(!CoopSession_Send(s, 0, COOP_MSG_SESSION_END - 1, TEST_PAYLOAD, sizeof(TEST_PAYLOAD), COOP_NET_RELIABLE) &&
               t.sentCount == sent,
           name, "the game could send a session message");
    EXPECT(Session_CanSend(s, 0) && t.sentCount == sent + 1, name, "the client could not send");
    EXPECT(!Session_CanSend(s, 1) && t.sentCount == sent + 1, name, "a client has one peer to send to");

    // The end.
    Script_Gone(&t, 0, COOP_NET_END_TIMEOUT);
    Drain(s, SCRIPT_T0 + 3, &side);
    EXPECT(side.ended == 1 && side.end == COOP_SESSION_END_TIMEOUT, name, "the end of a joined client got lost");
    EXPECT(CoopSession_PeerIdentity(s, 0) == NULL && !Session_CanSend(s, 0), name, "an ended client counts as joined");
    CoopSession_Close(s);
}

// ---- Identity wire form ----

static void Sim_Identity(void) {
    CoopIdentity a, b, big;
    uint8_t buf[COOP_NET_MAX_MESSAGE];
    char names[COOP_SIM_CVARS_MAX][64];
    uint16_t size, n, i, j;
    uint32_t round;

    EXPECT(CoopSimCvars_Count() <= COOP_SIM_CVARS_MAX, "identity", "the CVar table is larger than its room");
    for (i = 0; i < CoopSimCvars_Count(); i++) {
        CoopSimCvars_Name(i, names[i], sizeof(names[i]));
        for (j = 0; j < i; j++) {
            EXPECT(strcmp(names[i], names[j]) != 0, "identity", "a CVar is in the table twice");
        }
    }

    // The largest identity still fits a refusal (4 bytes of reasons in front).
    memset(&big, 'x', sizeof(big));
    big.build[COOP_ID_TEXT - 1] = '\0';
    big.platform[COOP_ID_TEXT - 1] = '\0';
    big.romCount = COOP_ID_MAX_ROMS;
    big.cvarCount = COOP_SIM_CVARS_MAX;
    EXPECT(CoopIdentity_Write(&big, buf, COOP_NET_MAX_MESSAGE - 4) != 0, "identity",
           "the largest identity does not fit a message");

    Id_Default(&a);
    Id_SetCvar(&a, "gCheckpoint.3.Set", 1);
    Id_SetCvar(&a, "gLaserRangeMult", 250);
    size = CoopIdentity_Write(&a, buf, sizeof(buf));
    EXPECT(size != 0 && CoopIdentity_Read(&b, buf, size) == size, "identity", "round trip failed");
    EXPECT(CoopIdentity_Compare(&a, &b) == 0, "identity", "round trip changed the identity");
    for (n = 0; n < size; n++) {
        EXPECT(CoopIdentity_Read(&b, buf, n) == 0, "identity", "a cut identity was accepted");
    }

    // Damaged bytes: never a crash, never text the player cannot read.
    sNet.rng = 99;
    for (round = 0; round < 20000; round++) {
        uint8_t copy[COOP_NET_MAX_MESSAGE];
        uint16_t got;

        memcpy(copy, buf, size);
        copy[Sim_Random() % size] ^= (uint8_t) (1 << (Sim_Random() % 8));
        if (round % 3 == 0) {
            for (n = 0; n < size; n++) {
                copy[n] = (uint8_t) Sim_Random();
            }
        }
        got = CoopIdentity_Read(&b, copy, size);
        if (got != 0) {
            for (n = 0; b.build[n]; n++) {
                EXPECT(b.build[n] >= 0x20 && b.build[n] <= 0x7E, "identity", "unprintable text accepted");
            }
            EXPECT(b.romCount <= COOP_ID_MAX_ROMS && b.cvarCount <= COOP_SIM_CVARS_MAX, "identity",
                   "counts past the arrays accepted");
        }
    }
}

static int Sim_Main(void) {
    uint32_t dropped;

    Sim_Identity();
    Sim_Mismatches();
    Sim_TwoClients();
    Sim_BrokenClients();
    Sim_SilentHost();
    Sim_RawHost();
    Sim_RawClient();
    Script_Lifetime();
    Script_EndTexts();
    Script_HostSilentClient();
    Script_HostRefusal();
    Script_HostBadHellos();
    Script_HostSendFails();
    Script_HostHoldBack();
    Script_HostSlots();
    Script_HostLeft();
    Script_ClientHello();
    Script_ClientTimes();
    Script_ClientBadAnswers();
    Script_ClientRefused();
    Script_ClientHoldBack();
    Script_ClientJoined();
    dropped = Sim_Lossy(40);
    printf("sim: PASS identity, 12 mismatch cases, two clients, broken, silent and raw peers, scripted transport, "
           "80 lossy runs (dropped %u datagrams)\n",
           (unsigned) dropped);
    return 0;
}

// ---- Real processes ----

typedef struct {
    const char* connect;
    const char* portFile;
    const char* out;
    uint32_t clients;
    uint32_t maxSeconds;
    uint32_t eager;
    CoopIdentity id;
} BotOptions;

typedef struct {
    CoopSessionEventType type;
    int peer;
    uint32_t diff;
    CoopSessionEnd end;
    uint8_t playerIndex;
    char text[COOP_SESSION_TEXT];
} BotEvent;

static const char* Event_Name(CoopSessionEventType type) {
    switch (type) {
        case COOP_SESSION_EV_JOINED:
            return "joined";
        case COOP_SESSION_EV_REFUSED:
            return "refused";
        case COOP_SESSION_EV_LEFT:
            return "left";
        case COOP_SESSION_EV_ENDED:
            return "ended";
        default:
            return "message";
    }
}

static void Json_String(FILE* f, const char* s) {
    fputc('"', f);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') {
            fprintf(f, "\\%c", *s);
        } else if (*s == '\n') {
            fputs("\\n", f);
        } else if ((unsigned char) *s < 0x20) {
            fprintf(f, "\\u%04x", (unsigned char) *s);
        } else {
            fputc(*s, f);
        }
    }
    fputc('"', f);
}

static int Bot_Main(const BotOptions* o) {
    CoopNetConfig config = { 0 };
    CoopTransport* t;
    CoopSession* s;
    bool isHost = o->connect == NULL;
    uint32_t start = SDL_GetTicks();
    uint32_t outcomes = 0;
    uint32_t joinedLive = 0;
    uint32_t received = 0;
    uint32_t finishedAt = 0;
    uint32_t eager = o->eager;
    bool joined = false;
    bool finished = false;
    FILE* f;
    static BotEvent events[32];
    uint32_t eventCount = 0;
    uint32_t i;

    if (isHost) {
        config.maxPeers = (uint8_t) o->clients;
        t = CoopNetSdl_Host(0, &config);
        if (t == NULL) {
            fprintf(stderr, "host failed: %s\n", SDL_GetError());
            return 2;
        }
        f = fopen(o->portFile, "w");
        if (f == NULL) {
            return 2;
        }
        fprintf(f, "%u\n", (unsigned) CoopNetSdl_Port(t));
        fclose(f);
        s = CoopSession_Host(t, &o->id);
    } else {
        char host[256];
        const char* colon = strrchr(o->connect, ':');
        size_t n = colon ? (size_t) (colon - o->connect) : 0;

        if (colon == NULL || n >= sizeof(host)) {
            return 2;
        }
        memcpy(host, o->connect, n);
        host[n] = '\0';
        t = CoopNetSdl_Connect(host, (uint16_t) atoi(colon + 1), &config);
        if (t == NULL) {
            fprintf(stderr, "connect failed: %s\n", SDL_GetError());
            return 2;
        }
        s = CoopSession_Join(t, &o->id, SDL_GetTicks());
    }

    while (!finished && SDL_GetTicks() - start < o->maxSeconds * 1000u) {
        CoopSessionEvent ev;
        uint32_t now = SDL_GetTicks();

        // Past the session on purpose; send fails until there is a connection.
        if (eager > 0 &&
            t->ops->send(t, 0, COOP_MSG_TEST_SESSION, TEST_PAYLOAD, sizeof(TEST_PAYLOAD), COOP_NET_RELIABLE)) {
            eager--;
        }
        while (s != NULL && CoopSession_Poll(s, now, &ev)) {
            if (ev.type == COOP_SESSION_EV_MESSAGE) {
                received++;
                continue;
            }
            if (eventCount < 32) {
                BotEvent* e = &events[eventCount++];

                e->type = ev.type;
                e->peer = ev.peer;
                e->diff = ev.diff;
                e->end = ev.end;
                e->playerIndex = ev.playerIndex;
                snprintf(e->text, sizeof(e->text), "%s", ev.text);
            }
            printf("%s peer %d: %s\n", Event_Name(ev.type), ev.peer, ev.text[0] ? ev.text : "-");
            fflush(stdout);
            if (ev.type == COOP_SESSION_EV_JOINED) {
                joined = true;
                CoopSession_Send(s, ev.peer, COOP_MSG_TEST_SESSION, TEST_PAYLOAD, sizeof(TEST_PAYLOAD),
                                 COOP_NET_RELIABLE);
                if (isHost) {
                    outcomes++;
                    joinedLive++;
                }
            } else if (ev.type == COOP_SESSION_EV_REFUSED) {
                outcomes += isHost;
            } else if (ev.type == COOP_SESSION_EV_LEFT) {
                joinedLive--;
            } else if (ev.type == COOP_SESSION_EV_ENDED) {
                finished = true;
            }
        }
        if (isHost) {
            // Done when every client got its answer and the joined ones have left again; then a moment for the
            // refused ones to be dropped.
            if (outcomes >= o->clients && joinedLive == 0) {
                if (finishedAt == 0) {
                    finishedAt = now | 1;
                } else if (now - finishedAt > 500) {
                    finished = true;
                }
            }
        } else if (joined && received > 0) {
            CoopNetStats stats;

            if (t->ops->stats(t, 0, &stats) && stats.pending == 0) {
                finished = true;
            }
        }
        SDL_Delay(2);
    }

    f = o->out ? fopen(o->out, "w") : stdout;
    if (f == NULL) {
        return 2;
    }
    fprintf(f, "{\"role\": \"%s\", \"finished\": %s, \"received\": %u, \"ignored\": %u, \"events\": [",
            isHost ? "host" : "client", finished ? "true" : "false", (unsigned) received,
            (unsigned) CoopSession_Ignored(s));
    for (i = 0; i < eventCount; i++) {
        const BotEvent* e = &events[i];

        fprintf(f, "%s{\"type\": \"%s\", \"peer\": %d, \"diff\": %u, \"end\": %u, \"player\": %u, \"text\": ",
                i ? ", " : "", Event_Name(e->type), e->peer, (unsigned) e->diff, (unsigned) e->end,
                (unsigned) e->playerIndex);
        Json_String(f, e->text);
        fputc('}', f);
    }
    fprintf(f, "]}\n");
    if (f != stdout) {
        fclose(f);
    }
    CoopSession_Close(s);
    return finished ? 0 : 1;
}

int main(int argc, char** argv) {
    BotOptions o;
    bool romsGiven = false;
    int i;

    memset(&o, 0, sizeof(o));
    o.clients = 1;
    o.maxSeconds = 60;
    Id_Default(&o.id);

    if (argc < 2) {
        fprintf(stderr, "usage: coop_session_test sim|host|client [options], see the top of coop_session_test.c\n");
        return 2;
    }
    for (i = 2; i + 1 < argc; i += 2) {
        const char* key = argv[i];
        const char* value = argv[i + 1];

        if (strcmp(key, "--connect") == 0) {
            o.connect = value;
        } else if (strcmp(key, "--port-file") == 0) {
            o.portFile = value;
        } else if (strcmp(key, "--out") == 0) {
            o.out = value;
        } else if (strcmp(key, "--clients") == 0) {
            o.clients = (uint32_t) atoi(value);
        } else if (strcmp(key, "--max-seconds") == 0) {
            o.maxSeconds = (uint32_t) atoi(value);
        } else if (strcmp(key, "--eager") == 0) {
            o.eager = (uint32_t) atoi(value);
        } else if (strcmp(key, "--build") == 0) {
            snprintf(o.id.build, sizeof(o.id.build), "%s", value);
        } else if (strcmp(key, "--platform") == 0) {
            snprintf(o.id.platform, sizeof(o.id.platform), "%s", value);
        } else if (strcmp(key, "--rom") == 0) {
            if (!romsGiven) {
                o.id.romCount = 0;
                romsGiven = true;
            }
            if (o.id.romCount < COOP_ID_MAX_ROMS) {
                o.id.roms[o.id.romCount++] = (uint32_t) strtoul(value, NULL, 16);
            }
            CoopIdentity_SortRoms(&o.id);
        } else if (strcmp(key, "--cvar") == 0) {
            char name[64];
            const char* eq = strchr(value, '=');
            size_t n = eq ? (size_t) (eq - value) : 0;

            if (eq == NULL || n >= sizeof(name)) {
                fprintf(stderr, "bad --cvar %s\n", value);
                return 2;
            }
            memcpy(name, value, n);
            name[n] = '\0';
            if (!Id_SetCvar(&o.id, name, atoi(eq + 1))) {
                fprintf(stderr, "%s is not a simulation CVar\n", name);
                return 2;
            }
        } else {
            fprintf(stderr, "unknown option %s\n", key);
            return 2;
        }
    }
    if (i < argc) {
        fprintf(stderr, "option %s has no value\n", argv[i]);
        return 2;
    }

    if (strcmp(argv[1], "sim") == 0) {
        return Sim_Main();
    }
    if (strcmp(argv[1], "host") == 0 && o.portFile != NULL && o.clients >= 1 && o.clients <= COOP_NET_MAX_PEERS) {
        return Bot_Main(&o);
    }
    if (strcmp(argv[1], "client") == 0 && o.connect != NULL) {
        return Bot_Main(&o);
    }
    fprintf(stderr, "bad arguments, see the top of coop_session_test.c\n");
    return 2;
}
