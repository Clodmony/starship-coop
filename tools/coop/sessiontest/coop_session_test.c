// Test program for the co-op session handshake (WP-21). tools/coop/session_test.py drives it; see docs/coop/QA.md.
//
//   coop_session_test sim
//       The session over the real transport core in a seeded virtual network: no sockets, no real time. Every
//       mismatch, a match, lossy links, two clients, broken and silent peers. Prints one line, exits 0 on PASS.
//   coop_session_test host --port-file F [--clients N] [identity options] [--out FILE] [--max-seconds S]
//   coop_session_test client --connect HOST:PORT [identity options] [--out FILE] [--max-seconds S]
//       Real processes over UDP. Each writes what happened as JSON.
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
    bool joined;
    bool refused;
    bool ended;
    bool left;
    uint32_t diff;
    CoopSessionEnd end;
    uint8_t playerIndex;
    uint32_t messages;
    char text[COOP_SESSION_TEXT];
} Side;

static void Side_Record(Side* side, const CoopSessionEvent* ev) {
    switch (ev->type) {
        case COOP_SESSION_EV_JOINED:
            side->joined = true;
            side->playerIndex = ev->playerIndex;
            side->diff = ev->diff;
            snprintf(side->text, sizeof(side->text), "%s", ev->text);
            break;
        case COOP_SESSION_EV_REFUSED:
            side->refused = true;
            side->diff = ev->diff;
            snprintf(side->text, sizeof(side->text), "%s", ev->text);
            break;
        case COOP_SESSION_EV_LEFT:
            side->left = true;
            side->end = ev->end;
            break;
        case COOP_SESSION_EV_ENDED:
            side->ended = true;
            side->end = ev->end;
            if (!side->refused) {
                snprintf(side->text, sizeof(side->text), "%s", ev->text);
            }
            break;
        case COOP_SESSION_EV_MESSAGE:
            if (ev->msgId == COOP_MSG_TEST_SESSION && ev->size == sizeof(TEST_PAYLOAD) &&
                memcmp(ev->data, TEST_PAYLOAD, sizeof(TEST_PAYLOAD)) == 0) {
                side->messages++;
            }
            break;
    }
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
} Setup;

typedef struct {
    Side host[COOP_NET_MAX_PEERS];
    Side client[MAX_CLIENTS];
    uint32_t hostIgnored;
} Outcome;

static void Sim_Run(const Setup* in, Outcome* out) {
    CoopSession* host;
    CoopSession* clients[MAX_CLIENTS];
    bool done[MAX_CLIENTS];
    uint32_t start;
    uint32_t quietSince = 0;
    int i;

    memset(out, 0, sizeof(*out));
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

        Sim_Step();
        while (CoopSession_Poll(host, sNet.now, &ev)) {
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
    Sim_Run(&s, out);
}

// Both sides refused, with the same text naming what differs, and no game message got through.
static void Check_Refused(const char* name, const Outcome* o, uint32_t diff, const char* needle) {
    const Side* h = &o->host[0];
    const Side* c = &o->client[0];

    EXPECT(h->refused && !h->joined, name, "host did not refuse");
    EXPECT(c->refused && !c->joined, name, "client was not told it was refused");
    EXPECT(c->ended && c->end == COOP_SESSION_END_REFUSED, name, "client did not end with the refusal");
    EXPECT(h->diff == diff && c->diff == diff, name, "wrong reasons");
    EXPECT(strcmp(h->text, c->text) == 0, name, "host and client got different texts");
    EXPECT(strstr(c->text, needle) != NULL, name, "the text does not say what differs");
    EXPECT(h->messages == 0 && c->messages == 0, name, "a game message passed a refused handshake");
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

// The outcome must not depend on the link: a refusal arrives through loss too, and so does a welcome.
static uint32_t Sim_Lossy(uint32_t seeds) {
    CoopIdentity a, b;
    Outcome o;
    uint32_t seed;
    uint32_t dropped = 0;

    Id_Default(&a);
    for (seed = 1; seed <= seeds; seed++) {
        uint32_t loss = 10 + seed % 31; // 10 to 40 percent

        b = a;
        Run_Pair("lossy_match", &a, &b, loss, 1000 + seed, &o);
        Check_Joined("lossy_match", &o, 0, true);
        dropped += sNet.dropped;

        snprintf(b.build, sizeof(b.build), "other-build");
        Run_Pair("lossy_refuse", &a, &b, loss, 2000 + seed, &o);
        Check_Refused("lossy_refuse", &o, COOP_ID_DIFF_BUILD, "Different game builds");
        dropped += sNet.dropped;
    }
    EXPECT(dropped > 0, "lossy", "the virtual network dropped nothing, so this run proves nothing");
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
        CoopSessionEvent ev;

        Sim_Step();
        while (s != NULL && CoopSession_Poll(s, sNet.now, &ev)) {
            Side_Record(&sides[ev.peer < 0 ? 0 : ev.peer], &ev);
        }
    }
}

// Waits for one event on a raw transport. Returns false on timeout.
static bool Raw_Wait(CoopTransport* t, CoopSession* s, Side* sides, CoopNetEventType type, uint16_t msgId,
                     CoopNetEvent* out, uint32_t limitMs) {
    uint32_t start = sNet.now;

    while (sNet.now - start < limitMs) {
        CoopSessionEvent ev;

        Sim_Step();
        if (s != NULL) {
            while (CoopSession_Poll(s, sNet.now, &ev)) {
                Side_Record(&sides[ev.peer < 0 ? 0 : ev.peer], &ev);
            }
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

// ---- Transport endings, through a scripted transport ----

typedef struct {
    CoopTransport base;
    CoopNetEnd end;
    bool given;
} FakeTransport;

static bool Fake_Send(CoopTransport* t, int peer, uint16_t msgId, const void* data, uint16_t size, CoopNetLane lane) {
    (void) t, (void) peer, (void) msgId, (void) data, (void) size, (void) lane;
    return false;
}

static bool Fake_Poll(CoopTransport* t, CoopNetEvent* ev) {
    FakeTransport* f = (FakeTransport*) t;

    if (f->given) {
        return false;
    }
    f->given = true;
    memset(ev, 0, sizeof(*ev));
    ev->type = COOP_NET_EVENT_DISCONNECTED;
    ev->end = f->end;
    return true;
}

static void Fake_Disconnect(CoopTransport* t, int peer) {
    (void) t, (void) peer;
}

static bool Fake_Stats(CoopTransport* t, int peer, CoopNetStats* out) {
    (void) t, (void) peer, (void) out;
    return false;
}

static void Fake_Close(CoopTransport* t) {
    free(t);
}

static const CoopTransportOps sFakeOps = { Fake_Send, Fake_Poll, Fake_Disconnect, Fake_Stats, Fake_Close };

static void Sim_TransportEnds(void) {
    static const struct {
        CoopNetEnd net;
        CoopSessionEnd session;
        const char* needle;
    } cases[] = {
        { COOP_NET_END_REFUSED_VERSION, COOP_SESSION_END_PROTOCOL, "another version of the co-op protocol" },
        { COOP_NET_END_REFUSED_FULL, COOP_SESSION_END_HOST_FULL, "host is full" },
        { COOP_NET_END_TIMEOUT, COOP_SESSION_END_TIMEOUT, "timed out" },
        { COOP_NET_END_CLOSED, COOP_SESSION_END_CLOSED, "closed the connection" },
    };
    CoopIdentity a;
    size_t i;

    Id_Default(&a);
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        FakeTransport* f = calloc(1, sizeof(*f));
        CoopSession* s;
        CoopSessionEvent ev;

        f->base.ops = &sFakeOps;
        f->end = cases[i].net;
        s = CoopSession_Join(&f->base, &a, 0);
        EXPECT(CoopSession_Poll(s, 1, &ev) && ev.type == COOP_SESSION_EV_ENDED && ev.end == cases[i].session,
               "transport_end", "a transport ending was not passed on");
        EXPECT(strstr(ev.text, cases[i].needle) != NULL, "transport_end", "the text for a transport ending is wrong");
        EXPECT(!CoopSession_Poll(s, 2, &ev), "transport_end", "an event after the end");
        CoopSession_Close(s);
    }
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
    Sim_TransportEnds();
    Sim_Mismatches();
    Sim_TwoClients();
    Sim_BrokenClients();
    Sim_SilentHost();
    dropped = Sim_Lossy(40);
    printf("sim: PASS identity, transport endings, 10 mismatch cases, two clients, broken and silent peers, 80 lossy "
           "runs (dropped %u datagrams)\n",
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
