// Test program for the co-op session handshake (WP-21). tools/coop/session_test.py drives it; see docs/coop/QA.md.
//
//   coop_session_test sim
//       The session over the real transport core in a seeded virtual network: no sockets, no real time. Every
//       mismatch, a match, lossy links, two clients, broken and silent peers, raw peers that push game messages
//       before the welcome, three clients picking characters and starting a level. Then the session on a scripted
//       transport: every failure and time-out on demand, to the millisecond. Prints one line, exits 0 on PASS.
//   coop_session_test host --port-file F [--clients N] [--players N] [identity options] [--out FILE] [--max-seconds S]
//                          [--eager N] [--start-level N] [--start-with N]
//   coop_session_test client --connect HOST:PORT [--pick NAME]... [--players N] [identity options] [--out FILE]
//                            [--max-seconds S] [--eager N] [--pick-delay-ms N] [--wait-level 1]
//       Real processes over UDP. Each writes what happened as JSON. --eager sends N game messages straight into the
//       transport, past CoopSession_Send, as soon as it has a connection; the session on the other side must drop
//       them. --pick names a character (falco, slippy, peppy, fox) to ask for after the welcome; several go in order,
//       the next one after each refusal. With --players a client stays until the first N players all hold a
//       character, and the JSON has that roster. --pick-delay-ms waits that long before each pick. --start-level has
//       the host try to start that level on every loop once --start-with clients (default: all of them) have joined,
//       and count the tries that fail. --wait-level has a client stay until a level has started.
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

#define NO_PLAYER COOP_CHARACTER_NO_PLAYER
#define UNPICKED COOP_CHARACTER_UNPICKED
#define FOX COOP_CHARACTER_FOX
#define FALCO COOP_CHARACTER_FALCO
#define SLIPPY COOP_CHARACTER_SLIPPY
#define PEPPY COOP_CHARACTER_PEPPY

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
    uint32_t rosters;       // ROSTER events. Not in `events`: they come along with JOINED and LEFT.
    uint32_t pickRefused;
    uint8_t refusedCharacter;
    uint32_t levelStarts;
    CoopLevelStart levelStart; // of the last LEVEL_START
    char text[COOP_SESSION_TEXT];
    char endText[COOP_SESSION_TEXT]; // of the last LEFT or ENDED
    // Of the last PICK_REFUSED.
    char pickText[COOP_SESSION_TEXT];
} Side;

static void Side_Record(Side* side, const CoopSessionEvent* ev) {
    EXPECT(ev->text != NULL, "events", "an event has no text");
    EXPECT(ev->type == COOP_SESSION_EV_JOINED || ev->playerIndex == 0, "events", "a stale field in an event");
    EXPECT(ev->type == COOP_SESSION_EV_MESSAGE || (ev->msgId == 0 && ev->size == 0 && ev->data == NULL), "events",
           "a stale field in an event");
    EXPECT(ev->type == COOP_SESSION_EV_PICK_REFUSED || ev->character == 0, "events", "a stale field in an event");
    EXPECT((ev->type == COOP_SESSION_EV_LEVEL_START) == (ev->levelStart != NULL), "events",
           "a level start without its level, or a stale field in an event");
    if (ev->type == COOP_SESSION_EV_ROSTER) {
        side->rosters++;
        return;
    }
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
        case COOP_SESSION_EV_PICK_REFUSED:
            side->pickRefused++;
            side->refusedCharacter = ev->character;
            snprintf(side->pickText, sizeof(side->pickText), "%s", ev->text);
            break;
        case COOP_SESSION_EV_LEVEL_START:
            side->levelStarts++;
            side->levelStart = *ev->levelStart;
            break;
        case COOP_SESSION_EV_ROSTER:
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

// What Run_Pair expects the host to refuse, apart from the session's own idea of it (decision 0010: not the platform).
#define REFUSES (COOP_ID_DIFF_BUILD | COOP_ID_DIFF_ROM | COOP_ID_DIFF_CVARS)

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
    s.eager = (CoopIdentity_Compare(host, client) & REFUSES) != 0;
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

    // A difference in platform alone gets in, with a warning both players read (decision 0010). With a build
    // difference on top it refuses, and the refusal names the platform too.
    b = a;
    snprintf(b.platform, sizeof(b.platform), "Linux x86-64 GCC 13.2");
    snprintf(a.platform, sizeof(a.platform), "Windows x86-64 MSVC 1944");
    Run_Pair("platform", &a, &b, 0, 9, &o);
    Check_Joined("platform", &o, COOP_ID_DIFF_PLATFORM, false);
    EXPECT(strstr(o.client[0].text, "Different platforms: host Windows x86-64 MSVC 1944, joining player Linux x86-64 "
                                    "GCC 13.2.") != NULL,
           "platform", "both players should read what differs");

    snprintf(b.build, sizeof(b.build), "other-build");
    Run_Pair("platform_build", &a, &b, 0, 10, &o);
    Check_Refused("platform_build", &o, COOP_ID_DIFF_BUILD | COOP_ID_DIFF_PLATFORM, "Different platforms: host");
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
#define SCRIPT_SENDS 64

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

static const ScriptSent* Last_Sent(const Script* t, uint16_t msgId) {
    uint32_t i;

    for (i = t->sentCount; i > 0; i--) {
        if (t->sent[i - 1].msgId == msgId) {
            return &t->sent[i - 1];
        }
    }
    return NULL;
}

// The last msgId sent: to peer on the reliable lane, with the identity id behind the first skip bytes.
static void Expect_Sent(const char* name, const Script* t, int peer, uint16_t msgId, uint16_t skip,
                        const CoopIdentity* id) {
    const ScriptSent* m = Last_Sent(t, msgId);
    CoopIdentity got;

    EXPECT(m != NULL, name, "nothing was sent");
    EXPECT(m->peer == peer && m->lane == COOP_NET_RELIABLE, name, "the wrong message was sent");
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

// The counters are for peers that have joined, and they are the transport's own.
static void Script_PeerStats(void) {
    const char* name = "peer_stats";
    CoopIdentity a;
    Script t;
    Side sides[COOP_NET_MAX_PEERS];
    CoopNetStats stats;
    CoopSession* s;

    Id_Default(&a);
    s = Script_Host(&t, sides, &a);
    EXPECT(!CoopSession_Stats(s, 0, &stats), name, "counters for a peer that has not joined");
    Script_Connected(&t, 0);
    Script_Hello(&t, 0, &a);
    Drain(s, SCRIPT_T0, sides);
    t.pending = 7;
    memset(&stats, 0, sizeof(stats));
    EXPECT(CoopSession_Stats(s, 0, &stats) && stats.pending == 7, name, "not the counters of a joined peer");
    EXPECT(!CoopSession_Stats(s, 1, &stats), name, "counters for a free slot");
    EXPECT(!CoopSession_Stats(s, COOP_NET_MAX_PEERS, &stats), name, "counters for a peer that cannot exist");
    CoopSession_Close(s);
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
        EXPECT(Last_Sent(&t, COOP_MSG_SESSION_WELCOME)->data[0] == peer + 1, name, "the welcome has the wrong slot");
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
        { COOP_ID_DIFF_PLATFORM, "Different platforms: host " },
        { COOP_SESSION_REFUSE_MALFORMED, "could not read this game's handshake" },
        { 0, "did not finish in time" },
        // All four bytes count: bit 2 is a reason, and the others are not ours to drop.
        { 0x04030404u, "These settings change the game" },
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
        EXPECT(strstr(side.text, "Different platforms: host Other platform, joining player ") != NULL, name,
               "wrong text");
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

// ---- Characters on a scripted transport ----

static void Expect_Roster(const char* name, const CoopRoster* r, uint8_t p0, uint8_t p1, uint8_t p2, uint8_t p3) {
    const uint8_t want[COOP_SESSION_PLAYERS] = { p0, p1, p2, p3 };

    EXPECT(r != NULL, name, "no roster");
    EXPECT(memcmp(r->character, want, sizeof(want)) == 0, name, "the roster is wrong");
}

static uint32_t Sent_Since(const Script* t, uint32_t from, uint16_t msgId) {
    uint32_t n = 0;

    for (; from < t->sentCount; from++) {
        n += t->sent[from].msgId == msgId;
    }
    return n;
}

// Since from, peer got the roster exactly once: on the reliable lane, with the bytes of want.
static void Expect_RosterTo(const char* name, const Script* t, uint32_t from, int peer, const CoopRoster* want) {
    uint32_t n = 0;

    for (; from < t->sentCount; from++) {
        const ScriptSent* m = &t->sent[from];

        if (m->peer == peer && m->msgId == COOP_MSG_SESSION_ROSTER) {
            n++;
            EXPECT(m->lane == COOP_NET_RELIABLE, name, "a roster on the unreliable lane");
            EXPECT(m->size == sizeof(want->character) && memcmp(m->data, want->character, m->size) == 0, name,
                   "the roster sent is not the host's");
        }
    }
    EXPECT(n == 1, name, "a peer did not get the roster exactly once");
}

static void Script_Pick(Script* t, int peer, uint8_t character) {
    Script_Message(t, peer, COOP_MSG_SESSION_PICK, &character, 1);
}

static void Script_RosterMsg(Script* t, uint8_t p0, uint8_t p1, uint8_t p2, uint8_t p3) {
    const uint8_t bytes[COOP_SESSION_PLAYERS] = { p0, p1, p2, p3 };

    Script_Message(t, 0, COOP_MSG_SESSION_ROSTER, bytes, sizeof(bytes));
}

// A host with joined clients at peers 0 to clients - 1.
static CoopSession* Script_HostWith(Script* t, Side* sides, const CoopIdentity* id, int clients) {
    CoopSession* s = Script_Host(t, sides, id);
    int peer;

    for (peer = 0; peer < clients; peer++) {
        Script_Connected(t, peer);
        Script_Hello(t, peer, id);
        Drain(s, SCRIPT_T0, sides);
    }
    return s;
}

// A client that the host welcomed as player.
static CoopSession* Script_Joined(Script* t, Side* side, const CoopIdentity* id, uint8_t player) {
    CoopSession* s = Script_Client(t, side, id);

    Script_Welcome(t, id, player);
    Drain(s, SCRIPT_T0 + 1, side);
    return s;
}

static void Script_CharacterNames(void) {
    static const struct {
        uint8_t character;
        const char* text;
    } cases[] = {
        { FOX, "Fox" },    { FALCO, "Falco" }, { SLIPPY, "Slippy" },         { PEPPY, "Peppy" },
        { NO_PLAYER, "" }, { UNPICKED, "" },   { COOP_CHARACTER_COUNT, "" }, { 0xFF, "" },
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        EXPECT(strcmp(CoopSession_CharacterName(cases[i].character), cases[i].text) == 0, "character_names",
               "wrong name");
    }
}

// Who is in the roster: the host from the start, a client from its welcome until it goes.
static void Script_HostRoster(void) {
    const char* name = "host_roster";
    CoopIdentity a, other;
    Script t;
    Side sides[COOP_NET_MAX_PEERS];
    CoopSession* s;
    const CoopRoster* r;
    uint32_t sent;

    Id_Default(&a);
    Id_Other(&other);
    s = Script_Host(&t, sides, &a);
    r = CoopSession_Roster(s);
    Expect_Roster(name, r, FOX, NO_PLAYER, NO_PLAYER, NO_PLAYER);

    // Not yet at the connection.
    Script_Connected(&t, 0);
    Drain(s, SCRIPT_T0, sides);
    Expect_Roster(name, r, FOX, NO_PLAYER, NO_PLAYER, NO_PLAYER);
    EXPECT(sides[0].rosters == 0 && t.sentCount == 0, name, "a roster for a client that has not joined");

    // Joined: in the roster without a character, and the client hears of it after its welcome.
    Script_Hello(&t, 0, &a);
    Drain(s, SCRIPT_T0, sides);
    Expect_Roster(name, r, FOX, UNPICKED, NO_PLAYER, NO_PLAYER);
    EXPECT(sides[0].rosters == 1, name, "the host's game did not hear of the roster");
    EXPECT(t.sentCount == 2 && t.sent[0].msgId == COOP_MSG_SESSION_WELCOME, name, "no roster right after the welcome");
    Expect_RosterTo(name, &t, 0, 0, r);

    // By slot, and everyone who joined hears of it.
    sent = t.sentCount;
    Script_Connected(&t, 2);
    Script_Hello(&t, 2, &a);
    Drain(s, SCRIPT_T0, sides);
    Expect_Roster(name, r, FOX, UNPICKED, NO_PLAYER, UNPICKED);
    EXPECT(sides[0].rosters == 2, name, "the host's game did not hear of the second client");
    Expect_RosterTo(name, &t, sent, 0, r);
    Expect_RosterTo(name, &t, sent, 2, r);

    // A refused client never was a player, so nothing changes when it comes or goes.
    sent = t.sentCount;
    t.pending = 1;
    Script_Connected(&t, 1);
    Script_Hello(&t, 1, &other);
    Drain(s, SCRIPT_T0, sides);
    Script_Gone(&t, 1, COOP_NET_END_CLOSED);
    Drain(s, SCRIPT_T0, sides);
    Expect_Roster(name, r, FOX, UNPICKED, NO_PLAYER, UNPICKED);
    EXPECT(sides[0].rosters == 2 && Sent_Since(&t, sent, COOP_MSG_SESSION_ROSTER) == 0, name,
           "a refused client changed the roster");
    CoopSession_Close(s);
}

// Each character once: what the host takes, what it refuses and who hears of which.
static void Script_HostPicks(void) {
    const char* name = "host_picks";
    CoopIdentity a;
    Script t;
    Side sides[COOP_NET_MAX_PEERS];
    CoopSession* s;
    const CoopRoster* r;
    const ScriptSent* m;
    uint32_t sent;
    uint32_t rosters;
    int peer;

    Id_Default(&a);
    s = Script_HostWith(&t, sides, &a, COOP_NET_MAX_PEERS);
    r = CoopSession_Roster(s);
    Expect_Roster(name, r, FOX, UNPICKED, UNPICKED, UNPICKED);

    // Free: the client gets it and everyone hears of it.
    sent = t.sentCount;
    rosters = sides[0].rosters;
    Script_Pick(&t, 0, FALCO);
    Drain(s, SCRIPT_T0, sides);
    Expect_Roster(name, r, FOX, FALCO, UNPICKED, UNPICKED);
    for (peer = 0; peer < COOP_NET_MAX_PEERS; peer++) {
        Expect_RosterTo(name, &t, sent, peer, r);
    }
    EXPECT(Sent_Since(&t, sent, COOP_MSG_SESSION_PICK_REFUSE) == 0, name, "a free character was refused");
    EXPECT(sides[0].rosters == rosters + 1 && sides[0].pickRefused == 0, name, "the host's game heard the wrong thing");

    // Taken: only the one who asked hears of it, and it stays.
    sent = t.sentCount;
    rosters = sides[0].rosters;
    Script_Pick(&t, 1, FALCO);
    Drain(s, SCRIPT_T0, sides);
    Expect_Roster(name, r, FOX, FALCO, UNPICKED, UNPICKED);
    EXPECT(Sent_Since(&t, sent, COOP_MSG_SESSION_PICK_REFUSE) == 1, name, "a taken character was not refused once");
    EXPECT(Sent_Since(&t, sent, COOP_MSG_SESSION_ROSTER) == 0 && sides[0].rosters == rosters, name,
           "a refusal changed the roster");
    m = Last_Sent(&t, COOP_MSG_SESSION_PICK_REFUSE);
    EXPECT(m->peer == 1 && m->lane == COOP_NET_RELIABLE && m->size == 1 && m->data[0] == FALCO, name,
           "the refusal sent is wrong");
    EXPECT(sides[1].pickRefused == 1 && sides[1].refusedCharacter == FALCO && sides[0].pickRefused == 0, name,
           "the host's event has the wrong peer or character");
    EXPECT(strcmp(sides[1].pickText, "Falco is already taken.") == 0, name, "wrong text");
    EXPECT(sides[1].left == 0 && t.disconnects[1] == 0 && Session_CanSend(s, 1), name,
           "a refused pick ended the client");

    // Fox is player 0's, whatever it asks.
    sent = t.sentCount;
    Script_Pick(&t, 1, FOX);
    Drain(s, SCRIPT_T0, sides);
    Expect_Roster(name, r, FOX, FALCO, UNPICKED, UNPICKED);
    EXPECT(Sent_Since(&t, sent, COOP_MSG_SESSION_PICK_REFUSE) == 1 && sides[1].refusedCharacter == FOX, name,
           "Fox was not refused");

    // A refused client may pick again.
    Script_Pick(&t, 1, PEPPY);
    Drain(s, SCRIPT_T0, sides);
    Expect_Roster(name, r, FOX, FALCO, PEPPY, UNPICKED);

    // What it already holds: not refused, nothing to tell.
    sent = t.sentCount;
    rosters = sides[0].rosters;
    Script_Pick(&t, 1, PEPPY);
    Drain(s, SCRIPT_T0, sides);
    Expect_Roster(name, r, FOX, FALCO, PEPPY, UNPICKED);
    EXPECT(t.sentCount == sent && sides[0].rosters == rosters && sides[1].pickRefused == 2, name,
           "picking your own character did something");

    // Another pick frees the old one at once.
    Script_Pick(&t, 0, SLIPPY);
    Script_Pick(&t, 2, FALCO);
    Drain(s, SCRIPT_T0, sides);
    Expect_Roster(name, r, FOX, SLIPPY, PEPPY, FALCO);
    EXPECT(sides[2].pickRefused == 0, name, "the character given up was still taken");

    // A refused pick keeps what the client holds.
    Script_Pick(&t, 0, FALCO);
    Drain(s, SCRIPT_T0, sides);
    Expect_Roster(name, r, FOX, SLIPPY, PEPPY, FALCO);
    EXPECT(sides[0].pickRefused == 1, name, "the last taken character was not refused");

    // Gone: the character is free at once, and only those still here hear of it.
    sent = t.sentCount;
    rosters = sides[0].rosters;
    Script_Gone(&t, 2, COOP_NET_END_CLOSED);
    Drain(s, SCRIPT_T0, sides);
    Expect_Roster(name, r, FOX, SLIPPY, PEPPY, NO_PLAYER);
    EXPECT(sides[2].left == 1 && sides[0].rosters == rosters + 1, name, "the host's game did not hear of the change");
    Expect_RosterTo(name, &t, sent, 0, r);
    Expect_RosterTo(name, &t, sent, 1, r);
    EXPECT(Sent_Since(&t, sent, COOP_MSG_SESSION_ROSTER) == 2, name, "a roster went to a client that left");
    Script_Pick(&t, 0, FALCO);
    Drain(s, SCRIPT_T0, sides);
    Expect_Roster(name, r, FOX, FALCO, PEPPY, NO_PLAYER);
    EXPECT(sides[0].pickRefused == 1, name, "the character of a client that left was still taken");

    // Whoever comes next into the slot starts with nothing.
    Script_Connected(&t, 2);
    Script_Hello(&t, 2, &a);
    Drain(s, SCRIPT_T0, sides);
    Expect_Roster(name, r, FOX, FALCO, PEPPY, UNPICKED);
    CoopSession_Close(s);
}

// Two picks of one character in the same frame: the first to arrive has it, and the other client is told.
static void Script_HostRace(void) {
    const char* name = "host_race";
    CoopIdentity a;
    Script t;
    Side sides[COOP_NET_MAX_PEERS];
    CoopSession* s;
    const CoopRoster* r;
    uint32_t sent;

    Id_Default(&a);
    s = Script_HostWith(&t, sides, &a, 2);
    r = CoopSession_Roster(s);
    sent = t.sentCount;
    Script_Pick(&t, 1, SLIPPY);
    Script_Pick(&t, 0, SLIPPY);
    Drain(s, SCRIPT_T0, sides);
    Expect_Roster(name, r, FOX, UNPICKED, SLIPPY, NO_PLAYER);
    EXPECT(sides[0].pickRefused == 1 && sides[1].pickRefused == 0, name, "the second pick was not the one refused");
    EXPECT(Last_Sent(&t, COOP_MSG_SESSION_PICK_REFUSE)->peer == 0, name, "the refusal went to the wrong client");
    Expect_RosterTo(name, &t, sent, 0, r);
    Expect_RosterTo(name, &t, sent, 1, r);
    CoopSession_Close(s);
}

// A pick that cannot be read is a broken peer, as a hello that cannot be read is: it goes, and its character with it.
static void Expect_BadPick(const char* name, const uint8_t* data, uint16_t size) {
    CoopIdentity a;
    Script t;
    Side sides[COOP_NET_MAX_PEERS];
    CoopSession* s;
    const CoopRoster* r;
    uint32_t sent;
    uint32_t rosters;

    Id_Default(&a);
    s = Script_HostWith(&t, sides, &a, 2);
    r = CoopSession_Roster(s);
    Script_Pick(&t, 0, FALCO);
    Script_Pick(&t, 1, PEPPY);
    Drain(s, SCRIPT_T0, sides);
    Expect_Roster(name, r, FOX, FALCO, PEPPY, NO_PLAYER);

    sent = t.sentCount;
    rosters = sides[0].rosters;
    Script_Message(&t, 1, COOP_MSG_SESSION_PICK, data, size);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(sides[1].left == 1 && sides[1].end == COOP_SESSION_END_BAD_MESSAGE, name,
           "a pick that cannot be read did not end the client");
    EXPECT(strcmp(sides[1].endText, CoopSession_EndText(COOP_SESSION_END_BAD_MESSAGE)) == 0, name, "wrong text");
    EXPECT(t.disconnects[1] == 1 && t.disconnects[0] == 0, name, "the wrong peer was dropped");
    EXPECT(sides[1].pickRefused == 0 && Sent_Since(&t, sent, COOP_MSG_SESSION_PICK_REFUSE) == 0, name,
           "a pick that cannot be read counted as a pick");
    Expect_Roster(name, r, FOX, FALCO, NO_PLAYER, NO_PLAYER);
    EXPECT(sides[0].rosters == rosters + 1, name, "the host's game did not hear of the change");
    Expect_RosterTo(name, &t, sent, 0, r);
    EXPECT(Sent_Since(&t, sent, COOP_MSG_SESSION_ROSTER) == 1, name, "a roster went to the dropped client");
    EXPECT(CoopSession_PeerIdentity(s, 1) == NULL && !Session_CanSend(s, 1), name, "a dropped client counts as joined");
    EXPECT(Drain(s, SCRIPT_T0 + COOP_SESSION_HANDSHAKE_MS + 1, sides) == 0 && t.disconnects[1] == 1, name,
           "a dropped client still had a state");
    CoopSession_Close(s);
}

static void Script_HostBadPicks(void) {
    static const uint8_t noCharacter[] = { NO_PLAYER, UNPICKED, COOP_CHARACTER_COUNT, 0xFF };
    const uint8_t two[2] = { SLIPPY, SLIPPY };
    size_t i;

    Expect_BadPick("pick_empty", NULL, 0);
    Expect_BadPick("pick_trailing", two, sizeof(two));
    for (i = 0; i < sizeof(noCharacter); i++) {
        Expect_BadPick("pick_no_character", &noCharacter[i], 1);
    }
}

// Picks from someone who is no player, and the host's own messages from a client: dropped and counted, nothing else.
static void Script_HostPicksOutOfPlace(void) {
    const char* name = "host_picks_out_of_place";
    const uint8_t falco = FALCO;
    const uint8_t roster[COOP_SESSION_PLAYERS] = { FOX, FALCO, NO_PLAYER, NO_PLAYER };
    CoopIdentity a, other;
    Script t;
    Side sides[COOP_NET_MAX_PEERS];
    CoopSession* s;
    const CoopRoster* r;
    uint32_t sent;

    Id_Default(&a);
    Id_Other(&other);

    // Before the hello; it does not count later either.
    s = Script_Host(&t, sides, &a);
    r = CoopSession_Roster(s);
    Script_Connected(&t, 0);
    Script_Pick(&t, 0, FALCO);
    Drain(s, SCRIPT_T0, sides);
    Expect_Roster(name, r, FOX, NO_PLAYER, NO_PLAYER, NO_PLAYER);
    EXPECT(CoopSession_Ignored(s) == 1 && t.sentCount == 0 && sides[0].events == 0, name,
           "a pick before the hello counted");
    Script_Hello(&t, 0, &a);
    Drain(s, SCRIPT_T0, sides);
    Expect_Roster(name, r, FOX, UNPICKED, NO_PLAYER, NO_PLAYER);

    // A client has no say in the roster.
    sent = t.sentCount;
    Script_Message(&t, 0, COOP_MSG_SESSION_ROSTER, roster, sizeof(roster));
    Script_Message(&t, 0, COOP_MSG_SESSION_PICK_REFUSE, &falco, 1);
    Drain(s, SCRIPT_T0, sides);
    Expect_Roster(name, r, FOX, UNPICKED, NO_PLAYER, NO_PLAYER);
    EXPECT(CoopSession_Ignored(s) == 3 && t.sentCount == sent && sides[0].left == 0 && t.disconnects[0] == 0, name,
           "a host's message from a client counted");
    CoopSession_Close(s);

    // After a refusal.
    s = Script_Host(&t, sides, &a);
    r = CoopSession_Roster(s);
    t.pending = 1;
    Script_Connected(&t, 0);
    Script_Hello(&t, 0, &other);
    Drain(s, SCRIPT_T0, sides);
    sent = t.sentCount;
    Script_Pick(&t, 0, FALCO);
    Drain(s, SCRIPT_T0, sides);
    Expect_Roster(name, r, FOX, NO_PLAYER, NO_PLAYER, NO_PLAYER);
    EXPECT(CoopSession_Ignored(s) == 1 && t.sentCount == sent, name, "a pick from a refused client counted");
    CoopSession_Close(s);
}

// The transport refuses a send while its window is full. All clients have to end up with the same roster anyway.
static void Script_HostFullWindow(void) {
    const char* name = "host_full_window";
    CoopIdentity a;
    Script t;
    Side sides[COOP_NET_MAX_PEERS];
    CoopSession* s;
    const CoopRoster* r;
    uint32_t sent;

    Id_Default(&a);
    s = Script_HostWith(&t, sides, &a, 2);
    r = CoopSession_Roster(s);

    // One peer's roster fails: that one is sent again on the next poll, the other one is not.
    sent = t.sentCount;
    t.failSends = 1;
    Script_Pick(&t, 0, FALCO);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(Sent_Since(&t, sent, COOP_MSG_SESSION_ROSTER) == 1, name, "the peer with room did not get the roster");
    Drain(s, SCRIPT_T0, sides);
    Expect_RosterTo(name, &t, sent, 0, r);
    Expect_RosterTo(name, &t, sent, 1, r);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(Sent_Since(&t, sent, COOP_MSG_SESSION_ROSTER) == 2, name, "a roster was sent again after it went out");

    // Several changes behind a full window: what goes out is the last roster.
    sent = t.sentCount;
    t.failSends = 100;
    Script_Pick(&t, 0, SLIPPY);
    Script_Pick(&t, 1, PEPPY);
    Drain(s, SCRIPT_T0, sides);
    t.failSends = 0;
    Drain(s, SCRIPT_T0, sides);
    Expect_Roster(name, r, FOX, SLIPPY, PEPPY, NO_PLAYER);
    Expect_RosterTo(name, &t, sent, 0, r);
    Expect_RosterTo(name, &t, sent, 1, r);

    // A refusal waits the same way, and it goes out once.
    sent = t.sentCount;
    t.failSends = 1;
    Script_Pick(&t, 0, PEPPY);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(Sent_Since(&t, sent, COOP_MSG_SESSION_PICK_REFUSE) == 0 && sides[0].pickRefused == 1, name,
           "the refusal should have failed");
    Drain(s, SCRIPT_T0, sides);
    EXPECT(Sent_Since(&t, sent, COOP_MSG_SESSION_PICK_REFUSE) == 1, name, "a refusal that failed was not sent again");
    EXPECT(Last_Sent(&t, COOP_MSG_SESSION_PICK_REFUSE)->peer == 0 &&
               Last_Sent(&t, COOP_MSG_SESSION_PICK_REFUSE)->data[0] == PEPPY,
           name, "the wrong refusal was sent");
    Drain(s, SCRIPT_T0, sides);
    EXPECT(Sent_Since(&t, sent, COOP_MSG_SESSION_PICK_REFUSE) == 1, name, "a refusal was sent twice");

    // Whoever goes meanwhile gets nothing, and the slot's next client owes nothing from the last one.
    sent = t.sentCount;
    t.failSends = 100;
    Script_Pick(&t, 1, FOX);
    Script_Pick(&t, 0, FALCO);
    Script_Gone(&t, 1, COOP_NET_END_CLOSED);
    Drain(s, SCRIPT_T0, sides);
    t.failSends = 0;
    Drain(s, SCRIPT_T0, sides);
    Expect_Roster(name, r, FOX, FALCO, NO_PLAYER, NO_PLAYER);
    EXPECT(Sent_Since(&t, sent, COOP_MSG_SESSION_PICK_REFUSE) == 0, name, "a refusal went to a client that left");
    EXPECT(Sent_Since(&t, sent, COOP_MSG_SESSION_ROSTER) == 1, name, "a roster went to a client that left");
    Expect_RosterTo(name, &t, sent, 0, r);
    sent = t.sentCount;
    Script_Connected(&t, 1);
    Script_Hello(&t, 1, &a);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(Sent_Since(&t, sent, COOP_MSG_SESSION_PICK_REFUSE) == 0, name, "a new client got the last one's refusal");
    CoopSession_Close(s);
}

static void Script_ClientPick(void) {
    static const uint8_t wingmen[] = { FALCO, SLIPPY, PEPPY };
    const char* name = "client_pick";
    CoopIdentity a;
    Script t;
    Side side;
    Side sides[COOP_NET_MAX_PEERS];
    CoopSession* s;
    uint32_t sent;
    size_t i;

    Id_Default(&a);

    // Not before the welcome: nothing but the hello goes out.
    s = Script_Client(&t, &side, &a);
    EXPECT(!CoopSession_Pick(s, FALCO) && t.sentCount == 1, name, "a pick before the welcome");
    Script_Welcome(&t, &a, 1);
    Drain(s, SCRIPT_T0 + 1, &side);

    // The wingmen, and nothing else.
    sent = t.sentCount;
    EXPECT(!CoopSession_Pick(s, NO_PLAYER) && !CoopSession_Pick(s, UNPICKED) && !CoopSession_Pick(s, FOX) &&
               !CoopSession_Pick(s, COOP_CHARACTER_COUNT) && t.sentCount == sent,
           name, "only a wingman can be picked");
    for (i = 0; i < sizeof(wingmen); i++) {
        const ScriptSent* m;

        EXPECT(CoopSession_Pick(s, (CoopCharacter) wingmen[i]), name, "a wingman could not be picked");
        m = &t.sent[t.sentCount - 1];
        EXPECT(m->peer == 0 && m->msgId == COOP_MSG_SESSION_PICK && m->lane == COOP_NET_RELIABLE && m->size == 1 &&
                   m->data[0] == wingmen[i],
               name, "the pick sent is wrong");
    }

    // The answer comes from the host, not from what this client saw last: a taken character still goes out.
    Script_RosterMsg(&t, FOX, FALCO, NO_PLAYER, NO_PLAYER);
    Drain(s, SCRIPT_T0 + 1, &side);
    sent = t.sentCount;
    EXPECT(CoopSession_Pick(s, FALCO) && t.sentCount == sent + 1, name, "the client decided a pick itself");

    t.failSends = 1;
    EXPECT(!CoopSession_Pick(s, FALCO), name, "a pick that failed was reported as sent");

    // Not after the end.
    Script_Gone(&t, 0, COOP_NET_END_CLOSED);
    Drain(s, SCRIPT_T0 + 2, &side);
    sent = t.sentCount;
    EXPECT(!CoopSession_Pick(s, FALCO) && t.sentCount == sent, name, "a pick after the end");
    CoopSession_Close(s);

    // Player 0 is Fox: a host has nothing to pick, whoever has joined.
    s = Script_HostWith(&t, sides, &a, 1);
    sent = t.sentCount;
    EXPECT(!CoopSession_Pick(s, FALCO) && t.sentCount == sent, name, "a host picked");
    Expect_Roster(name, CoopSession_Roster(s), FOX, UNPICKED, NO_PLAYER, NO_PLAYER);
    CoopSession_Close(s);
}

// What a client knows and when.
static void Script_ClientRoster(void) {
    const char* name = "client_roster";
    const uint8_t falco = FALCO;
    CoopIdentity a;
    Script t;
    Side side;
    CoopSession* s;

    Id_Default(&a);

    // Nothing before the welcome, and the wait for the answer goes on.
    Script_Init(&t);
    memset(&side, 0, sizeof(side));
    s = CoopSession_Join(&t.base, &a, SCRIPT_T0);
    Script_RosterMsg(&t, FOX, UNPICKED, NO_PLAYER, NO_PLAYER);
    Drain(s, SCRIPT_T0, &side);
    EXPECT(side.rosters == 0 && CoopSession_Ignored(s) == 1, name, "a roster before the connection counted");
    CoopSession_Close(s);

    s = Script_Client(&t, &side, &a);
    Script_RosterMsg(&t, FOX, UNPICKED, NO_PLAYER, NO_PLAYER);
    Script_Message(&t, 0, COOP_MSG_SESSION_PICK_REFUSE, &falco, 1);
    Drain(s, SCRIPT_T0 + 1, &side);
    EXPECT(side.rosters == 0 && side.pickRefused == 0 && side.events == 0, name,
           "an answer before the welcome counted");
    EXPECT(CoopSession_Ignored(s) == 2 && t.disconnects[0] == 0, name, "not dropped and counted");
    EXPECT(CoopSession_Roster(s) == NULL, name, "a roster before the welcome");

    // The welcome says nothing about the roster.
    Script_Welcome(&t, &a, 1);
    Drain(s, SCRIPT_T0 + 1, &side);
    EXPECT(side.joined == 1 && CoopSession_Roster(s) == NULL, name, "a roster nobody sent");

    // Every form a host can send: free slots, players without a character, each wingman.
    Script_RosterMsg(&t, FOX, UNPICKED, NO_PLAYER, NO_PLAYER);
    Drain(s, SCRIPT_T0 + 1, &side);
    EXPECT(side.rosters == 1, name, "a roster was not passed on");
    Expect_Roster(name, CoopSession_Roster(s), FOX, UNPICKED, NO_PLAYER, NO_PLAYER);
    Script_RosterMsg(&t, FOX, UNPICKED, UNPICKED, UNPICKED);
    Drain(s, SCRIPT_T0 + 1, &side);
    Expect_Roster(name, CoopSession_Roster(s), FOX, UNPICKED, UNPICKED, UNPICKED);
    Script_RosterMsg(&t, FOX, FALCO, NO_PLAYER, UNPICKED);
    Drain(s, SCRIPT_T0 + 1, &side);
    Expect_Roster(name, CoopSession_Roster(s), FOX, FALCO, NO_PLAYER, UNPICKED);
    Script_RosterMsg(&t, FOX, PEPPY, SLIPPY, FALCO);
    Drain(s, SCRIPT_T0 + 1, &side);
    Expect_Roster(name, CoopSession_Roster(s), FOX, PEPPY, SLIPPY, FALCO);
    EXPECT(side.rosters == 4 && CoopSession_Ignored(s) == 2 && t.disconnects[0] == 0, name,
           "a good roster was dropped");

    // A pick is the client's own message.
    Script_Pick(&t, 0, FALCO);
    Drain(s, SCRIPT_T0 + 1, &side);
    EXPECT(CoopSession_Ignored(s) == 3 && side.rosters == 4 && t.disconnects[0] == 0, name,
           "a pick from the host counted");

    // Gone with the session.
    Script_Gone(&t, 0, COOP_NET_END_CLOSED);
    Drain(s, SCRIPT_T0 + 2, &side);
    EXPECT(CoopSession_Roster(s) == NULL, name, "a roster after the end");
    CoopSession_Close(s);
}

static void Expect_BadRoster(const char* name, uint8_t player, const uint8_t* data, uint16_t size) {
    CoopIdentity a;
    Script t;
    Side side;
    CoopSession* s;

    Id_Default(&a);
    s = Script_Joined(&t, &side, &a, player);
    Script_Message(&t, 0, COOP_MSG_SESSION_ROSTER, data, size);
    Drain(s, SCRIPT_T0 + 2, &side);
    EXPECT(side.ended == 1 && side.end == COOP_SESSION_END_BAD_MESSAGE, name,
           "a broken roster did not end the session");
    EXPECT(side.rosters == 0, name, "a broken roster was taken for a roster");
    EXPECT(strcmp(side.endText, CoopSession_EndText(COOP_SESSION_END_BAD_MESSAGE)) == 0, name, "wrong text");
    EXPECT(t.disconnects[0] == 1, name, "the client did not leave once");
    EXPECT(CoopSession_Roster(s) == NULL && !Session_CanSend(s, 0), name, "a broken roster left something behind");
    CoopSession_Close(s);
}

static void Script_ClientBadRosters(void) {
    static const struct {
        const char* name;
        uint8_t player;
        uint8_t bytes[COOP_SESSION_PLAYERS];
    } cases[] = {
        { "roster_host_not_fox", 1, { UNPICKED, UNPICKED, NO_PLAYER, NO_PLAYER } },
        { "roster_host_wingman", 1, { FALCO, UNPICKED, NO_PLAYER, NO_PLAYER } },
        { "roster_host_missing", 1, { NO_PLAYER, UNPICKED, NO_PLAYER, NO_PLAYER } },
        { "roster_second_fox", 1, { FOX, FOX, NO_PLAYER, NO_PLAYER } },
        { "roster_last_fox", 1, { FOX, UNPICKED, NO_PLAYER, FOX } },
        { "roster_past_the_end", 1, { FOX, COOP_CHARACTER_COUNT, NO_PLAYER, NO_PLAYER } },
        { "roster_last_past_the_end", 1, { FOX, UNPICKED, NO_PLAYER, COOP_CHARACTER_COUNT } },
        { "roster_unknown", 1, { FOX, 0xFF, NO_PLAYER, NO_PLAYER } },
        { "roster_twice", 1, { FOX, FALCO, FALCO, NO_PLAYER } },
        { "roster_twice_apart", 1, { FOX, PEPPY, UNPICKED, PEPPY } },
        { "roster_twice_at_the_end", 1, { FOX, UNPICKED, SLIPPY, SLIPPY } },
        { "roster_without_us_1", 1, { FOX, NO_PLAYER, UNPICKED, NO_PLAYER } },
        { "roster_without_us_3", 3, { FOX, UNPICKED, UNPICKED, NO_PLAYER } },
    };
    const uint8_t good[COOP_SESSION_PLAYERS + 1] = { FOX, UNPICKED, NO_PLAYER, NO_PLAYER, NO_PLAYER };
    size_t i;

    Expect_BadRoster("roster_empty", 1, NULL, 0);
    Expect_BadRoster("roster_short", 1, good, COOP_SESSION_PLAYERS - 1);
    Expect_BadRoster("roster_trailing", 1, good, COOP_SESSION_PLAYERS + 1);
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Expect_BadRoster(cases[i].name, cases[i].player, cases[i].bytes, COOP_SESSION_PLAYERS);
    }
}

static void Expect_BadPickRefuse(const char* name, const uint8_t* data, uint16_t size) {
    CoopIdentity a;
    Script t;
    Side side;
    CoopSession* s;

    Id_Default(&a);
    s = Script_Joined(&t, &side, &a, 1);
    Script_Message(&t, 0, COOP_MSG_SESSION_PICK_REFUSE, data, size);
    Drain(s, SCRIPT_T0 + 2, &side);
    EXPECT(side.ended == 1 && side.end == COOP_SESSION_END_BAD_MESSAGE, name,
           "a broken refusal did not end the session");
    EXPECT(side.pickRefused == 0 && t.disconnects[0] == 1, name, "a broken refusal was taken for a refusal");
    CoopSession_Close(s);
}

static void Script_ClientPickRefused(void) {
    static const uint8_t noCharacter[] = { NO_PLAYER, UNPICKED, COOP_CHARACTER_COUNT, 0xFF };
    static const uint8_t refusable[] = { FOX, FALCO, SLIPPY, PEPPY };
    const char* name = "client_pick_refused";
    const uint8_t two[2] = { FALCO, FALCO };
    CoopIdentity a;
    Script t;
    Side side;
    CoopSession* s;
    size_t i;

    Id_Default(&a);

    // Told, still connected, roster untouched, free to go on.
    s = Script_Joined(&t, &side, &a, 2);
    Script_RosterMsg(&t, FOX, FALCO, UNPICKED, NO_PLAYER);
    Drain(s, SCRIPT_T0 + 2, &side);
    for (i = 0; i < sizeof(refusable); i++) {
        Script_Message(&t, 0, COOP_MSG_SESSION_PICK_REFUSE, &refusable[i], 1);
        Drain(s, SCRIPT_T0 + 2, &side);
        EXPECT(side.pickRefused == i + 1 && side.refusedCharacter == refusable[i], name,
               "the refusal was not passed on");
    }
    EXPECT(strcmp(side.pickText, "Peppy is already taken.") == 0, name, "wrong text");
    EXPECT(side.ended == 0 && t.disconnects[0] == 0 && side.rosters == 1, name, "a refused pick changed the session");
    Expect_Roster(name, CoopSession_Roster(s), FOX, FALCO, UNPICKED, NO_PLAYER);
    EXPECT(CoopSession_Pick(s, PEPPY) && Session_CanSend(s, 0), name, "a refused client could not go on");
    CoopSession_Close(s);

    Expect_BadPickRefuse("pick_refuse_empty", NULL, 0);
    Expect_BadPickRefuse("pick_refuse_trailing", two, sizeof(two));
    for (i = 0; i < sizeof(noCharacter); i++) {
        Expect_BadPickRefuse("pick_refuse_no_character", &noCharacter[i], 1);
    }
}

// ---- Level start on a scripted transport ----

#define LEVEL_BYTES (2 + COOP_SESSION_PLAYERS + 4 * COOP_LEVEL_START_VALUES + 1)
#define TEST_LEVEL 5
#define TEST_PHASE 1

// One value for each position, with all four bytes in use and negative ones among them, so that a lost or a swapped
// byte shows. session_test.py computes the same.
static int32_t Level_Value(int i) {
    return (int32_t) (0x9E3779B1u * (uint32_t) (i + 1));
}

static void Level_Fill(CoopLevelStart* start, uint8_t level, uint8_t phase) {
    int i;

    memset(start, 0xEE, sizeof(*start)); // the roster is the session's to fill in
    start->level = level;
    start->phase = phase;
    for (i = 0; i < COOP_LEVEL_START_VALUES; i++) {
        start->values[i] = Level_Value(i);
    }
    start->cleared = 1;
}

// The message as PROTOCOL.md gives it, written apart from the session's own code.
static void Level_Bytes(uint8_t* out, uint8_t level, uint8_t phase, uint8_t p0, uint8_t p1, uint8_t p2, uint8_t p3) {
    int i;

    out[0] = level;
    out[1] = phase;
    out[2] = p0;
    out[3] = p1;
    out[4] = p2;
    out[5] = p3;
    for (i = 0; i < COOP_LEVEL_START_VALUES; i++) {
        uint32_t v = (uint32_t) Level_Value(i);

        out[6 + 4 * i] = (uint8_t) v;
        out[7 + 4 * i] = (uint8_t) (v >> 8);
        out[8 + 4 * i] = (uint8_t) (v >> 16);
        out[9 + 4 * i] = (uint8_t) (v >> 24);
    }
    out[LEVEL_BYTES - 1] = 1; // cleared (#191)
}

static void Expect_Level(const char* name, const Side* side, uint8_t level, uint8_t phase, uint8_t p0, uint8_t p1,
                         uint8_t p2, uint8_t p3) {
    const CoopLevelStart* got = &side->levelStart;
    int i;

    EXPECT(side->levelStarts == 1, name, "not exactly one level start");
    EXPECT(got->level == level && got->phase == phase, name, "the wrong level or phase");
    EXPECT(got->roster[0] == p0 && got->roster[1] == p1 && got->roster[2] == p2 && got->roster[3] == p3, name,
           "the level start holds the wrong roster");
    for (i = 0; i < COOP_LEVEL_START_VALUES; i++) {
        EXPECT(got->values[i] == Level_Value(i), name, "a value did not arrive as it was");
    }
    EXPECT(got->cleared == 1, name, "the cleared flag did not arrive as it was");
}

// Since from, peer got the level start exactly once, on the reliable lane, with these bytes.
static void Expect_LevelTo(const char* name, const Script* t, uint32_t from, int peer, const uint8_t* want) {
    uint32_t n = 0;

    for (; from < t->sentCount; from++) {
        const ScriptSent* m = &t->sent[from];

        if (m->peer == peer && m->msgId == COOP_MSG_SESSION_LEVEL_START) {
            n++;
            EXPECT(m->lane == COOP_NET_RELIABLE, name, "a level start on the unreliable lane");
            EXPECT(m->size == LEVEL_BYTES && memcmp(m->data, want, LEVEL_BYTES) == 0, name,
                   "the level start sent is not what PROTOCOL.md says");
        }
    }
    EXPECT(n == 1, name, "a peer did not get the level start exactly once");
}

// Who may start, and when: everyone who joined has to hold a character.
static void Script_HostStartGuards(void) {
    const char* name = "host_start_guards";
    uint8_t bytes[LEVEL_BYTES];
    CoopIdentity a;
    CoopIdentity other;
    CoopLevelStart want;
    Script t;
    Side sides[COOP_NET_MAX_PEERS];
    Side clientSide;
    CoopSession* s;
    uint32_t sent;

    Id_Default(&a);
    Id_Other(&other);
    Level_Fill(&want, TEST_LEVEL, TEST_PHASE);

    // Each client that holds nothing holds the start up, whichever slot it has.
    s = Script_HostWith(&t, sides, &a, 3);
    sent = t.sentCount;
    EXPECT(!CoopSession_CanStart(s), name, "start offered with three clients without a character");
    EXPECT(!CoopSession_StartLevel(s, &want), name, "started with three clients without a character");
    Script_Pick(&t, 0, FALCO);
    Script_Pick(&t, 1, SLIPPY);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(!CoopSession_CanStart(s), name, "start offered with the last client without a character");
    EXPECT(!CoopSession_StartLevel(s, &want), name, "started with the last client without a character");
    Script_Pick(&t, 2, PEPPY);
    Script_Gone(&t, 0, COOP_NET_END_CLOSED);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(Sent_Since(&t, sent, COOP_MSG_SESSION_LEVEL_START) == 0 && sides[0].levelStarts == 0, name,
           "a start that was turned down went out");
    Script_Connected(&t, 0);
    Script_Hello(&t, 0, &a);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(!CoopSession_CanStart(s), name, "start offered with the first client without a character");
    EXPECT(!CoopSession_StartLevel(s, &want), name, "started with the first client without a character");
    CoopSession_Close(s);

    // A client in the handshake waits for it to end; one that was refused does not count.
    s = Script_HostWith(&t, sides, &a, 1);
    Script_Pick(&t, 0, FALCO);
    Script_Connected(&t, 1);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(!CoopSession_CanStart(s), name, "start offered while a client was in the handshake");
    EXPECT(!CoopSession_StartLevel(s, &want), name, "started while a client was in the handshake");
    t.pending = 1;
    Script_Hello(&t, 1, &other);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(sides[1].refused == 1, name, "the other build was not refused");
    sent = t.sentCount;
    EXPECT(CoopSession_CanStart(s), name, "a refused client held the offer up");
    EXPECT(CoopSession_StartLevel(s, &want), name, "a refused client held the start up");
    EXPECT(!CoopSession_CanStart(s), name, "start offered once a level has started");
    Drain(s, SCRIPT_T0, sides);
    Level_Bytes(bytes, TEST_LEVEL, TEST_PHASE, FOX, FALCO, NO_PLAYER, NO_PLAYER);
    Expect_LevelTo(name, &t, sent, 0, bytes);
    EXPECT(Sent_Since(&t, sent, COOP_MSG_SESSION_LEVEL_START) == 1, name, "a refused client got the level start");
    CoopSession_Close(s);

    // The level has to be one that can be started: the last one is, the next one is not.
    s = Script_Host(&t, sides, &a);
    Level_Fill(&want, COOP_LEVEL_START_LEVELS, 0);
    EXPECT(CoopSession_CanStart(s), name, "no start offered to a host alone");
    EXPECT(!CoopSession_StartLevel(s, &want), name, "started a level that cannot be started");
    Level_Fill(&want, COOP_LEVEL_START_LEVELS - 1, 0);
    EXPECT(CoopSession_StartLevel(s, &want), name, "the last level did not start, or a host alone cannot start");
    Drain(s, SCRIPT_T0, sides);
    EXPECT(sides[0].levelStarts == 1 && t.sentCount == 0, name, "a host alone did not start cleanly");
    CoopSession_Close(s);

    // Only a host starts.
    s = Script_Joined(&t, &clientSide, &a, 1);
    Script_RosterMsg(&t, FOX, FALCO, NO_PLAYER, NO_PLAYER);
    Drain(s, SCRIPT_T0 + 2, &clientSide);
    sent = t.sentCount;
    EXPECT(!CoopSession_CanStart(s), name, "start offered to a client");
    EXPECT(!CoopSession_StartLevel(s, &want), name, "a client started a level");
    Drain(s, SCRIPT_T0 + 2, &clientSide);
    EXPECT(t.sentCount == sent && clientSide.levelStarts == 0, name, "a client's start did something");
    CoopSession_Close(s);
}

// What goes out, once, and what is over after it.
static void Script_HostStart(void) {
    const char* name = "host_start";
    const uint8_t two[2] = { PEPPY, PEPPY };
    uint8_t bytes[LEVEL_BYTES];
    CoopIdentity a;
    CoopIdentity other;
    CoopLevelStart want;
    Script t;
    Side sides[COOP_NET_MAX_PEERS];
    CoopSession* s;
    const CoopRoster* r;
    uint32_t sent;
    uint32_t ignored;
    uint32_t rosters;

    Id_Default(&a);
    Id_Other(&other);
    Level_Fill(&want, TEST_LEVEL, TEST_PHASE);
    Level_Bytes(bytes, TEST_LEVEL, TEST_PHASE, FOX, FALCO, SLIPPY, NO_PLAYER);
    s = Script_HostWith(&t, sides, &a, 2);
    r = CoopSession_Roster(s);
    Script_Pick(&t, 0, FALCO);
    Script_Pick(&t, 1, SLIPPY);
    Drain(s, SCRIPT_T0, sides);
    sent = t.sentCount;
    EXPECT(CoopSession_StartLevel(s, &want), name, "everyone holds a character, and still no start");

    // The session keeps its own copy.
    memset(&want, 0, sizeof(want));
    Drain(s, SCRIPT_T0, sides);
    Expect_LevelTo(name, &t, sent, 0, bytes);
    Expect_LevelTo(name, &t, sent, 1, bytes);
    Expect_Level(name, &sides[0], TEST_LEVEL, TEST_PHASE, FOX, FALCO, SLIPPY, NO_PLAYER);
    EXPECT(sides[1].levelStarts == 0 && sides[2].levelStarts == 0, name, "the host's event has the wrong peer");

    // Once is once.
    Level_Fill(&want, TEST_LEVEL, TEST_PHASE);
    EXPECT(!CoopSession_StartLevel(s, &want), name, "a second level start was taken");
    Drain(s, SCRIPT_T0, sides);
    EXPECT(Sent_Since(&t, sent, COOP_MSG_SESSION_LEVEL_START) == 2 && sides[0].levelStarts == 1, name,
           "the level start went out again");

    // Picks are closed: free, taken or broken, the pick is dropped and counted, and the roster stays.
    sent = t.sentCount;
    ignored = CoopSession_Ignored(s);
    rosters = sides[0].rosters;
    Script_Pick(&t, 0, PEPPY);
    Script_Pick(&t, 1, FALCO);
    Script_Message(&t, 1, COOP_MSG_SESSION_PICK, two, sizeof(two));
    Drain(s, SCRIPT_T0, sides);
    EXPECT(CoopSession_Ignored(s) == ignored + 3, name, "a pick after the level start was not dropped");
    Expect_Roster(name, r, FOX, FALCO, SLIPPY, NO_PLAYER);
    EXPECT(t.sentCount == sent && sides[0].rosters == rosters && sides[0].pickRefused == 0 && sides[1].pickRefused == 0,
           name, "a pick after the level start was answered");
    EXPECT(sides[1].left == 0 && t.disconnects[1] == 0, name, "a pick after the level start ended a client");

    // Someone who goes still goes, and the others hear of it.
    sent = t.sentCount;
    Script_Gone(&t, 0, COOP_NET_END_CLOSED);
    Drain(s, SCRIPT_T0, sides);
    Expect_Roster(name, r, FOX, NO_PLAYER, SLIPPY, NO_PLAYER);
    Expect_RosterTo(name, &t, sent, 1, r);
    EXPECT(sides[0].left == 1, name, "a client that left after the level start was not reported");

    // The level goes on, so the slot stays empty: a new client is told why, even one the identity would refuse.
    sent = t.sentCount;
    Script_Connected(&t, 0);
    Script_Hello(&t, 0, &other);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(sides[0].diff == COOP_SESSION_REFUSE_STARTED, name, "the build came before the level start");
    Script_Gone(&t, 0, COOP_NET_END_CLOSED);
    Drain(s, SCRIPT_T0, sides);
    sides[0].refused = 0;
    Script_Connected(&t, 0);
    Script_Hello(&t, 0, &a);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(sides[0].refused == 1 && sides[0].joined == 1, name, "a client got in after the level start");
    EXPECT(sides[0].diff == COOP_SESSION_REFUSE_STARTED, name, "the wrong reason for a client after the level start");
    EXPECT(strstr(sides[0].text, "already started") != NULL, name, "the text does not say that the level started");
    EXPECT(Sent_Since(&t, sent, COOP_MSG_SESSION_WELCOME) == 0, name, "a welcome after the level start");
    Expect_Sent(name, &t, 0, COOP_MSG_SESSION_REFUSE, 4, &a);
    {
        const ScriptSent* m = Last_Sent(&t, COOP_MSG_SESSION_REFUSE);

        EXPECT(m->data[0] == 0 && m->data[1] == 2 && m->data[2] == 0 && m->data[3] == 0, name,
               "the refusal does not carry bit 9");
    }
    Expect_Roster(name, r, FOX, NO_PLAYER, SLIPPY, NO_PLAYER);
    CoopSession_Close(s);
}

// What a full window holds back stays owed, and the level start keeps the roster of the moment it was taken.
static void Script_HostStartOwed(void) {
    const char* name = "host_start_owed";
    uint8_t bytes[LEVEL_BYTES];
    CoopIdentity a;
    CoopLevelStart want;
    CoopSessionEvent ev;
    Script t;
    Side sides[COOP_NET_MAX_PEERS];
    CoopSession* s;
    uint32_t sent;

    Id_Default(&a);
    Level_Fill(&want, TEST_LEVEL, TEST_PHASE);
    Level_Bytes(bytes, TEST_LEVEL, TEST_PHASE, FOX, FALCO, SLIPPY, NO_PLAYER);

    // One peer's send fails: that one goes on the next poll, the other one does not go twice.
    s = Script_HostWith(&t, sides, &a, 2);
    Script_Pick(&t, 0, FALCO);
    Script_Pick(&t, 1, SLIPPY);
    Drain(s, SCRIPT_T0, sides);
    sent = t.sentCount;
    EXPECT(CoopSession_StartLevel(s, &want), name, "no start");
    t.failSends = 1;
    EXPECT(CoopSession_Poll(s, SCRIPT_T0, &ev) && ev.type == COOP_SESSION_EV_LEVEL_START, name,
           "the host waited for the window");
    Side_Record(&sides[0], &ev);
    EXPECT(Sent_Since(&t, sent, COOP_MSG_SESSION_LEVEL_START) == 1, name, "the peer with room did not get the level");
    Drain(s, SCRIPT_T0, sides);
    Expect_LevelTo(name, &t, sent, 0, bytes);
    Expect_LevelTo(name, &t, sent, 1, bytes);
    Drain(s, SCRIPT_T0, sides);
    EXPECT(Sent_Since(&t, sent, COOP_MSG_SESSION_LEVEL_START) == 2, name, "a level start was sent again");
    EXPECT(sides[0].levelStarts == 1, name, "the host's game heard of the level twice");
    CoopSession_Close(s);

    // Behind a full window a roster and the level start both stay owed.
    s = Script_HostWith(&t, sides, &a, 2);
    Script_Pick(&t, 0, FALCO);
    Script_Pick(&t, 1, SLIPPY);
    t.failSends = 100;
    Drain(s, SCRIPT_T0, sides);
    EXPECT(CoopSession_StartLevel(s, &want), name, "no start behind a full window");
    Drain(s, SCRIPT_T0, sides);
    sent = t.sentCount;
    t.failSends = 0;
    Drain(s, SCRIPT_T0, sides);
    Expect_RosterTo(name, &t, sent, 0, CoopSession_Roster(s));
    Expect_LevelTo(name, &t, sent, 0, bytes);
    Expect_LevelTo(name, &t, sent, 1, bytes);
    CoopSession_Close(s);

    // Whoever goes before it could be told gets nothing, but the others still start from the roster the host took:
    // everyone has to come to the same player count.
    s = Script_HostWith(&t, sides, &a, 2);
    Script_Pick(&t, 0, FALCO);
    Script_Pick(&t, 1, SLIPPY);
    Drain(s, SCRIPT_T0, sides);
    sent = t.sentCount;
    EXPECT(CoopSession_StartLevel(s, &want), name, "no start");
    Script_Gone(&t, 1, COOP_NET_END_CLOSED);
    t.failSends = 100;
    Drain(s, SCRIPT_T0, sides);
    Drain(s, SCRIPT_T0, sides);
    t.failSends = 0;
    Drain(s, SCRIPT_T0, sides);
    Expect_LevelTo(name, &t, sent, 0, bytes);
    EXPECT(Sent_Since(&t, sent, COOP_MSG_SESSION_LEVEL_START) == 1, name, "a client that left got the level start");
    Expect_Level(name, &sides[0], TEST_LEVEL, TEST_PHASE, FOX, FALCO, SLIPPY, NO_PLAYER);
    Expect_Roster(name, CoopSession_Roster(s), FOX, FALCO, NO_PLAYER, NO_PLAYER);
    CoopSession_Close(s);
}

// The wrong side, and the wrong moment.
static void Script_LevelOutOfPlace(void) {
    const char* name = "level_out_of_place";
    uint8_t bytes[LEVEL_BYTES];
    CoopIdentity a;
    Script t;
    Side sides[COOP_NET_MAX_PEERS];
    Side side;
    CoopSession* s;
    uint32_t sent;

    Id_Default(&a);
    Level_Bytes(bytes, TEST_LEVEL, TEST_PHASE, FOX, FALCO, NO_PLAYER, NO_PLAYER);

    // Clients do not tell a host to start: the first one that tries is a joined client, the second one is not.
    s = Script_HostWith(&t, sides, &a, 1);
    Script_Pick(&t, 0, FALCO);
    Drain(s, SCRIPT_T0, sides);
    sent = t.sentCount;
    Script_Message(&t, 0, COOP_MSG_SESSION_LEVEL_START, bytes, sizeof(bytes));
    Script_Connected(&t, 1);
    Script_Message(&t, 1, COOP_MSG_SESSION_LEVEL_START, bytes, sizeof(bytes));
    Drain(s, SCRIPT_T0, sides);
    EXPECT(CoopSession_Ignored(s) == 2, name, "a level start from a client was not dropped and counted");
    EXPECT(sides[0].levelStarts == 0 && sides[1].levelStarts == 0 && t.sentCount == sent, name,
           "a client started a level");
    CoopSession_Close(s);

    // Before the welcome nothing is real.
    s = Script_Client(&t, &side, &a);
    Script_Message(&t, 0, COOP_MSG_SESSION_LEVEL_START, bytes, sizeof(bytes));
    Drain(s, SCRIPT_T0, &side);
    EXPECT(CoopSession_Ignored(s) == 1 && side.levelStarts == 0 && side.ended == 0, name,
           "a level start before the welcome was not dropped and counted");
    CoopSession_Close(s);
}

static void Expect_BadLevel(const char* name, const uint8_t* data, uint16_t size, uint8_t self) {
    CoopIdentity a;
    Script t;
    Side side;
    CoopSession* s;

    Id_Default(&a);
    s = Script_Joined(&t, &side, &a, self);
    Script_Message(&t, 0, COOP_MSG_SESSION_LEVEL_START, data, size);
    Drain(s, SCRIPT_T0 + 2, &side);
    EXPECT(side.ended == 1 && side.end == COOP_SESSION_END_BAD_MESSAGE, name,
           "a bad level start did not end the client");
    EXPECT(side.levelStarts == 0, name, "a bad level start reached the game");
    EXPECT(t.disconnects[0] == 1, name, "a bad level start left the connection up");
    CoopSession_Close(s);
}

static void Expect_GoodLevel(const char* name, const uint8_t* data, uint8_t self) {
    CoopIdentity a;
    Script t;
    Side side;
    CoopSession* s;

    Id_Default(&a);
    s = Script_Joined(&t, &side, &a, self);
    Script_Message(&t, 0, COOP_MSG_SESSION_LEVEL_START, data, LEVEL_BYTES);
    Drain(s, SCRIPT_T0 + 2, &side);
    EXPECT(side.levelStarts == 1 && side.ended == 0, name, "a good level start was not taken");
    CoopSession_Close(s);
}

static void Script_ClientStart(void) {
    const char* name = "client_start";
    uint8_t bytes[LEVEL_BYTES];
    uint8_t longer[LEVEL_BYTES + 1];
    CoopIdentity a;
    Script t;
    Side side;
    CoopSession* s;
    uint32_t sent;

    Id_Default(&a);
    Level_Bytes(bytes, TEST_LEVEL, TEST_PHASE, FOX, FALCO, SLIPPY, NO_PLAYER);
    s = Script_Joined(&t, &side, &a, 2);
    Script_RosterMsg(&t, FOX, FALCO, SLIPPY, NO_PLAYER);
    Drain(s, SCRIPT_T0 + 2, &side);
    EXPECT(CoopSession_Pick(s, PEPPY), name, "a pick before the level start did not go out");
    sent = t.sentCount;

    Script_Message(&t, 0, COOP_MSG_SESSION_LEVEL_START, bytes, sizeof(bytes));
    Drain(s, SCRIPT_T0 + 2, &side);
    Expect_Level(name, &side, TEST_LEVEL, TEST_PHASE, FOX, FALCO, SLIPPY, NO_PLAYER);
    EXPECT(side.ended == 0 && t.disconnects[0] == 0 && Session_CanSend(s, 0), name, "a level start ended the session");
    sent = t.sentCount;
    EXPECT(!CoopSession_Pick(s, PEPPY), name, "a pick went out after the level start");
    EXPECT(t.sentCount == sent, name, "a pick after the level start was sent");

    // One level start per session: another one is out of place, not a new level.
    Script_Message(&t, 0, COOP_MSG_SESSION_LEVEL_START, bytes, sizeof(bytes));
    Drain(s, SCRIPT_T0 + 2, &side);
    EXPECT(side.levelStarts == 1 && CoopSession_Ignored(s) == 1 && side.ended == 0, name,
           "a second level start was not dropped and counted");

    // The roster lives on; what the level started from does not change with it.
    Script_RosterMsg(&t, FOX, NO_PLAYER, SLIPPY, NO_PLAYER);
    Drain(s, SCRIPT_T0 + 2, &side);
    Expect_Roster(name, CoopSession_Roster(s), FOX, NO_PLAYER, SLIPPY, NO_PLAYER);
    Expect_Level(name, &side, TEST_LEVEL, TEST_PHASE, FOX, FALCO, SLIPPY, NO_PLAYER);
    CoopSession_Close(s);

    // The refusal for a client that came too late: the same words on both sides.
    {
        Side hostSides[COOP_NET_MAX_PEERS];
        CoopLevelStart want;
        const ScriptSent* m;
        Script ht;
        CoopSession* host = Script_Host(&ht, hostSides, &a);

        Level_Fill(&want, TEST_LEVEL, TEST_PHASE);
        EXPECT(CoopSession_StartLevel(host, &want), name, "no start");
        Drain(host, SCRIPT_T0, hostSides);
        Script_Connected(&ht, 0);
        Script_Hello(&ht, 0, &a);
        Drain(host, SCRIPT_T0, hostSides);
        m = Last_Sent(&ht, COOP_MSG_SESSION_REFUSE);
        EXPECT(m != NULL, name, "no refusal");
        s = Script_Client(&t, &side, &a);
        Script_Message(&t, 0, COOP_MSG_SESSION_REFUSE, m->data, m->size);
        Drain(s, SCRIPT_T0, &side);
        Drain(s, SCRIPT_T0, &side);
        EXPECT(side.refused == 1 && side.ended == 1 && side.diff == COOP_SESSION_REFUSE_STARTED, name,
               "the client did not read the refusal");
        EXPECT(strcmp(side.text, hostSides[0].text) == 0, name, "host and client got different texts");
        CoopSession_Close(s);
        CoopSession_Close(host);
    }

    // What can be read: the last level, a free slot next to ours, and every wingman.
    Level_Bytes(bytes, COOP_LEVEL_START_LEVELS - 1, 0, FOX, NO_PLAYER, PEPPY, NO_PLAYER);
    Expect_GoodLevel(name, bytes, 2);
    Level_Bytes(bytes, 0, 255, FOX, FALCO, SLIPPY, PEPPY);
    Expect_GoodLevel(name, bytes, 3);

    // What cannot.
    Level_Bytes(bytes, TEST_LEVEL, TEST_PHASE, FOX, FALCO, NO_PLAYER, NO_PLAYER);
    Expect_BadLevel("level_empty", NULL, 0, 1);
    Expect_BadLevel("level_short", bytes, LEVEL_BYTES - 1, 1);
    memcpy(longer, bytes, LEVEL_BYTES);
    longer[LEVEL_BYTES] = 0;
    Expect_BadLevel("level_trailing", longer, sizeof(longer), 1);
    Level_Bytes(bytes, COOP_LEVEL_START_LEVELS, 0, FOX, FALCO, NO_PLAYER, NO_PLAYER);
    Expect_BadLevel("level_too_high", bytes, LEVEL_BYTES, 1);
    Level_Bytes(bytes, TEST_LEVEL, TEST_PHASE, FALCO, FALCO, NO_PLAYER, NO_PLAYER);
    Expect_BadLevel("level_no_fox", bytes, LEVEL_BYTES, 1);
    Level_Bytes(bytes, TEST_LEVEL, TEST_PHASE, FOX, FALCO, FALCO, NO_PLAYER);
    Expect_BadLevel("level_wingman_twice", bytes, LEVEL_BYTES, 1);
    Level_Bytes(bytes, TEST_LEVEL, TEST_PHASE, FOX, NO_PLAYER, FALCO, NO_PLAYER);
    Expect_BadLevel("level_own_slot_free", bytes, LEVEL_BYTES, 1);
    Level_Bytes(bytes, TEST_LEVEL, TEST_PHASE, FOX, UNPICKED, FALCO, NO_PLAYER);
    Expect_BadLevel("level_we_hold_nothing", bytes, LEVEL_BYTES, 1);
    Level_Bytes(bytes, TEST_LEVEL, TEST_PHASE, FOX, FALCO, NO_PLAYER, UNPICKED);
    Expect_BadLevel("level_last_holds_nothing", bytes, LEVEL_BYTES, 1);
    Level_Bytes(bytes, TEST_LEVEL, TEST_PHASE, FOX, FALCO, UNPICKED, NO_PLAYER);
    Expect_BadLevel("level_other_holds_nothing", bytes, LEVEL_BYTES, 1);
    Level_Bytes(bytes, TEST_LEVEL, TEST_PHASE, FOX, FALCO, NO_PLAYER, NO_PLAYER);
    bytes[LEVEL_BYTES - 1] = 2;
    Expect_BadLevel("level_cleared_not_a_flag", bytes, LEVEL_BYTES, 1);
}

// ---- Characters over the real transport core: three clients, each going down its list on every refusal ----

#define PARTY_CLIENTS (MAX_CLIENTS + 1) // one more than fit at once, for a client that comes after another left

typedef struct {
    CoopSession* host;
    CoopSession* clients[PARTY_CLIENTS];
    const uint8_t* picks[PARTY_CLIENTS]; // the next one to ask for; NO_PLAYER ends the list
    Side hostSides[COOP_NET_MAX_PEERS];
    Side sides[PARTY_CLIENTS];
} Party;

static void Party_Start(Party* p, uint64_t seed, uint32_t lossPercent, uint32_t latencyMs, const CoopIdentity* id) {
    memset(p, 0, sizeof(*p));
    Sim_Reset(seed, lossPercent, latencyMs);
    p->host = CoopSession_Host(Sim_Transport(SIM_HOST), id);
}

static void Party_Join(Party* p, int i, const CoopIdentity* id, const uint8_t* picks) {
    p->picks[i] = picks;
    p->clients[i] = CoopSession_Join(Sim_Transport(2 + i), id, sNet.now);
}

static void Party_Step(Party* p, const char* name) {
    int i;

    Sim_Step();
    Drain(p->host, sNet.now, p->hostSides);
    for (i = 0; i < PARTY_CLIENTS; i++) {
        CoopSessionEvent ev;

        while (p->clients[i] != NULL && CoopSession_Poll(p->clients[i], sNet.now, &ev)) {
            Side_Record(&p->sides[i], &ev);
            if ((ev.type == COOP_SESSION_EV_JOINED || ev.type == COOP_SESSION_EV_PICK_REFUSED) &&
                *p->picks[i] != NO_PLAYER) {
                EXPECT(CoopSession_Pick(p->clients[i], (CoopCharacter) *p->picks[i]), name, "a pick could not be sent");
                p->picks[i]++;
            }
        }
    }
}

// True when every client there has the host's roster, and that one has `held` clients with a character.
static bool Party_Agrees(const Party* p, int held) {
    const CoopRoster* r = CoopSession_Roster(p->host);
    int n = 0;
    int i;

    for (i = 1; i < COOP_SESSION_PLAYERS; i++) {
        n += r->character[i] >= COOP_CHARACTER_FOX;
    }
    if (n != held) {
        return false;
    }
    for (i = 0; i < PARTY_CLIENTS; i++) {
        const CoopRoster* seen = p->clients[i] != NULL ? CoopSession_Roster(p->clients[i]) : NULL;

        if (p->clients[i] != NULL && (seen == NULL || memcmp(seen, r, sizeof(*r)) != 0)) {
            return false;
        }
    }
    return true;
}

// Runs until everyone agrees, then a while longer: agreeing for a moment is not enough.
static void Party_Settle(Party* p, const char* name, int held) {
    uint32_t start = sNet.now;

    while (!Party_Agrees(p, held)) {
        EXPECT(sNet.now - start < 30000, name, "the clients and the host did not come to agree");
        Party_Step(p, name);
    }
    start = sNet.now;
    while (sNet.now - start < 1000) {
        Party_Step(p, name);
    }
    EXPECT(Party_Agrees(p, held), name, "the clients and the host did not stay in agreement");
}

static uint32_t Party_Refused(const Party* p, const char* name) {
    uint32_t onHost = 0;
    uint32_t onClients = 0;
    int i;

    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        onHost += p->hostSides[i].pickRefused;
    }
    for (i = 0; i < PARTY_CLIENTS; i++) {
        onClients += p->sides[i].pickRefused;
    }
    EXPECT(onHost == onClients, name, "the host refused other picks than the clients heard of");
    return onClients;
}

static void Party_Close(Party* p) {
    int i;

    for (i = 0; i < PARTY_CLIENTS; i++) {
        CoopSession_Close(p->clients[i]);
    }
    CoopSession_Close(p->host);
}

// Fox, then each wingman once.
static void Expect_Full(const char* name, const CoopRoster* r) {
    uint32_t seen = 0;
    int i;

    EXPECT(r->character[0] == FOX, name, "player 0 is not Fox");
    for (i = 1; i < COOP_SESSION_PLAYERS; i++) {
        EXPECT(r->character[i] > FOX && r->character[i] < COOP_CHARACTER_COUNT, name, "a client holds no wingman");
        seen |= 1u << r->character[i];
    }
    EXPECT(seen == ((1u << FALCO) | (1u << SLIPPY) | (1u << PEPPY)), name, "a wingman is held twice");
}

static const uint8_t sJustFalco[] = { FALCO, NO_PLAYER };
static const uint8_t sJustSlippy[] = { SLIPPY, NO_PLAYER };
static const uint8_t sJustPeppy[] = { PEPPY, NO_PLAYER };
static const uint8_t sAllWingmen[] = { FALCO, SLIPPY, PEPPY, NO_PLAYER };
static const uint8_t sNobody[] = { NO_PLAYER };

static void Sim_Characters(void) {
    const char* name = "characters";
    static const uint8_t lateComer[] = { SLIPPY, PEPPY, NO_PLAYER };
    const CoopRoster* r;
    CoopIdentity a;
    Party p;
    int i;

    Id_Default(&a);

    // Everyone gets their wish and everyone knows.
    Party_Start(&p, 21, 0, 5, &a);
    Party_Join(&p, 0, &a, sJustFalco);
    Party_Join(&p, 1, &a, sJustSlippy);
    Party_Join(&p, 2, &a, sJustPeppy);
    Party_Settle(&p, name, 3);
    r = CoopSession_Roster(p.host);
    Expect_Full(name, r);
    EXPECT(Party_Refused(&p, name) == 0, name, "a pick of a free character was refused");
    EXPECT(r->character[p.sides[0].playerIndex] == FALCO && r->character[p.sides[1].playerIndex] == SLIPPY &&
               r->character[p.sides[2].playerIndex] == PEPPY,
           name, "a client did not get what it picked");

    // The holder of Falco goes: free at once for those who stay, and the slot for whoever comes next.
    {
        int slot = p.sides[0].playerIndex;

        CoopSession_Close(p.clients[0]);
        p.clients[0] = NULL;
        Party_Settle(&p, name, 2);
        EXPECT(CoopSession_Roster(p.host)->character[slot] == NO_PLAYER, name,
               "the slot of a client that left is taken");
        EXPECT(CoopSession_Pick(p.clients[1], FALCO), name, "a pick could not be sent");
        while (CoopSession_Roster(p.host)->character[p.sides[1].playerIndex] != FALCO) {
            EXPECT(p.hostSides[p.sides[1].playerIndex - 1].pickRefused == 0, name, "the freed character was refused");
            Party_Step(&p, name);
        }
        Party_Settle(&p, name, 2);

        // Slippy is free again after that, Peppy is not: the new client's first wish goes through.
        Party_Join(&p, 3, &a, lateComer);
        Party_Settle(&p, name, 3);
        EXPECT(p.sides[3].playerIndex == slot, name, "the new client did not get the free slot");
        Expect_Full(name, CoopSession_Roster(p.host));
        EXPECT(CoopSession_Roster(p.host)->character[slot] == SLIPPY, name, "the new client did not get what was free");
    }
    Party_Close(&p);

    // All three want the same: one gets it, the others are told and go on, and the count of refusals is known.
    Party_Start(&p, 22, 0, 5, &a);
    for (i = 0; i < MAX_CLIENTS; i++) {
        Party_Join(&p, i, &a, sAllWingmen);
    }
    Party_Settle(&p, name, 3);
    Expect_Full(name, CoopSession_Roster(p.host));
    EXPECT(Party_Refused(&p, name) == 3, name, "not one refusal for each pick of a taken character");
    for (i = 0; i < MAX_CLIENTS; i++) {
        EXPECT(p.sides[i].left == 0 && p.sides[i].ended == 0, name, "a refused pick ended a client");
        EXPECT(strstr(p.sides[i].pickText, "already taken") != NULL || p.sides[i].pickRefused == 0, name, "wrong text");
    }
    Party_Close(&p);
}

// Rosters and refusals are reliable, so a bad link only takes longer.
static uint32_t Sim_CharactersLossy(uint32_t seeds) {
    const char* name = "characters_lossy";
    CoopIdentity a;
    Party p;
    uint32_t seed;
    uint32_t dropped = 0;
    int i;

    Id_Default(&a);
    for (seed = 1; seed <= seeds; seed++) {
        Party_Start(&p, 3000 + seed, 10 + seed % 31, 40, &a);
        for (i = 0; i < MAX_CLIENTS; i++) {
            Party_Join(&p, i, &a, sAllWingmen);
        }
        Party_Settle(&p, name, 3);
        Expect_Full(name, CoopSession_Roster(p.host));
        EXPECT(Party_Refused(&p, name) == 3, name, "not one refusal for each pick of a taken character");
        dropped += sNet.dropped;
        Party_Close(&p);
    }
    EXPECT(dropped > 0, name, "the virtual network dropped nothing, so this run proves nothing");
    return dropped;
}

// Every client there saw exactly one level start, the host's. A level start is reliable, so a bad link only takes
// longer.
static void Party_LevelArrives(Party* p, const char* name, uint8_t p1, uint8_t p2, uint8_t p3) {
    uint32_t start = sNet.now;
    int i;

    for (;;) {
        bool all = p->hostSides[0].levelStarts == 1;

        for (i = 0; i < PARTY_CLIENTS; i++) {
            all = all && (p->clients[i] == NULL || p->sides[i].levelStarts == 1);
        }
        if (all) {
            break;
        }
        EXPECT(sNet.now - start < 30000, name, "the level start did not reach everyone");
        Party_Step(p, name);
    }
    start = sNet.now;
    while (sNet.now - start < 1000) {
        Party_Step(p, name);
    }
    Expect_Level(name, &p->hostSides[0], TEST_LEVEL, TEST_PHASE, FOX, p1, p2, p3);
    for (i = 0; i < PARTY_CLIENTS; i++) {
        if (p->clients[i] != NULL) {
            Expect_Level(name, &p->sides[i], TEST_LEVEL, TEST_PHASE, FOX, p1, p2, p3);
            EXPECT(p->sides[i].ended == 0, name, "a level start ended a client");
        }
    }
}

static void Sim_LevelStart(void) {
    const char* name = "level_start";
    const CoopRoster* r;
    CoopIdentity a;
    CoopLevelStart want;
    Party p;
    int i;

    Id_Default(&a);
    Level_Fill(&want, TEST_LEVEL, TEST_PHASE);

    // Three clients walk down one list, and the level starts from what came of it.
    Party_Start(&p, 23, 0, 5, &a);
    for (i = 0; i < MAX_CLIENTS; i++) {
        Party_Join(&p, i, &a, sAllWingmen);
    }
    Party_Settle(&p, name, 3);
    r = CoopSession_Roster(p.host);
    EXPECT(CoopSession_StartLevel(p.host, &want), name, "no start with everyone holding a character");
    Party_LevelArrives(&p, name, r->character[1], r->character[2], r->character[3]);
    Party_Close(&p);

    // A client changes its mind while the host starts: the host has the last word, and the client hears it.
    Party_Start(&p, 24, 0, 5, &a);
    Party_Join(&p, 0, &a, sJustFalco);
    Party_Settle(&p, name, 1);
    EXPECT(CoopSession_Pick(p.clients[0], PEPPY), name, "a pick could not be sent");
    EXPECT(CoopSession_StartLevel(p.host, &want), name, "no start");
    Party_LevelArrives(&p, name, FALCO, NO_PLAYER, NO_PLAYER);
    EXPECT(CoopSession_Ignored(p.host) == 1, name, "the late pick was not dropped and counted");
    Expect_Roster(name, CoopSession_Roster(p.host), FOX, FALCO, NO_PLAYER, NO_PLAYER);
    Expect_Roster(name, CoopSession_Roster(p.clients[0]), FOX, FALCO, NO_PLAYER, NO_PLAYER);
    Party_Close(&p);

    // Nobody starts past a client that holds nothing, and it can still pick and go on.
    Party_Start(&p, 25, 0, 5, &a);
    Party_Join(&p, 0, &a, sJustFalco);
    Party_Join(&p, 1, &a, sNobody);
    Party_Settle(&p, name, 1);
    EXPECT(!CoopSession_StartLevel(p.host, &want), name, "started with a client that holds nothing");
    EXPECT(CoopSession_Pick(p.clients[1], FALCO), name, "a pick could not be sent");
    while (p.sides[1].pickRefused == 0) {
        Party_Step(&p, name);
    }
    EXPECT(!CoopSession_StartLevel(p.host, &want), name, "a refused pick opened the start");
    EXPECT(CoopSession_Pick(p.clients[1], SLIPPY), name, "a pick could not be sent");
    Party_Settle(&p, name, 2);
    EXPECT(CoopSession_StartLevel(p.host, &want), name, "no start with everyone holding a character");
    Party_LevelArrives(&p, name, CoopSession_Roster(p.host)->character[1], CoopSession_Roster(p.host)->character[2],
                       NO_PLAYER);
    Party_Close(&p);

    // A client that comes late hears why on both ends, over the real transport.
    Party_Start(&p, 26, 0, 5, &a);
    Party_Join(&p, 0, &a, sJustFalco);
    Party_Settle(&p, name, 1);
    EXPECT(CoopSession_StartLevel(p.host, &want), name, "no start");
    Party_LevelArrives(&p, name, FALCO, NO_PLAYER, NO_PLAYER);
    Party_Join(&p, 1, &a, sNobody);
    while (p.sides[1].ended == 0) {
        Party_Step(&p, name);
    }
    EXPECT(p.sides[1].refused == 1 && p.sides[1].joined == 0, name, "a client got in after the level start");
    EXPECT(p.sides[1].diff == COOP_SESSION_REFUSE_STARTED, name, "the wrong reason on the client");
    EXPECT(p.hostSides[1].refused == 1 && strcmp(p.hostSides[1].text, p.sides[1].text) == 0, name,
           "the host's text is not the client's");
    Party_Close(&p);
}

static uint32_t Sim_LevelStartLossy(uint32_t seeds) {
    const char* name = "level_start_lossy";
    CoopIdentity a;
    CoopLevelStart want;
    Party p;
    uint32_t seed;
    uint32_t dropped = 0;
    int i;

    Id_Default(&a);
    Level_Fill(&want, TEST_LEVEL, TEST_PHASE);
    for (seed = 1; seed <= seeds; seed++) {
        const CoopRoster* r;

        Party_Start(&p, 4000 + seed, 10 + seed % 31, 40, &a);
        for (i = 0; i < MAX_CLIENTS; i++) {
            Party_Join(&p, i, &a, sAllWingmen);
        }
        Party_Settle(&p, name, 3);
        r = CoopSession_Roster(p.host);
        EXPECT(CoopSession_StartLevel(p.host, &want), name, "no start with everyone holding a character");
        Party_LevelArrives(&p, name, r->character[1], r->character[2], r->character[3]);
        dropped += sNet.dropped;
        Party_Close(&p);
    }
    EXPECT(dropped > 0, name, "the virtual network dropped nothing, so this run proves nothing");
    return dropped;
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
    Script_PeerStats();
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
    Script_CharacterNames();
    Script_HostRoster();
    Script_HostPicks();
    Script_HostRace();
    Script_HostBadPicks();
    Script_HostPicksOutOfPlace();
    Script_HostFullWindow();
    Script_ClientPick();
    Script_ClientRoster();
    Script_ClientBadRosters();
    Script_ClientPickRefused();
    Script_HostStartGuards();
    Script_HostStart();
    Script_HostStartOwed();
    Script_LevelOutOfPlace();
    Script_ClientStart();
    Sim_Characters();
    Sim_LevelStart();
    dropped = Sim_Lossy(40);
    dropped += Sim_CharactersLossy(20);
    dropped += Sim_LevelStartLossy(20);
    printf("sim: PASS identity, 12 mismatch cases, two clients, broken, silent and raw peers, scripted transport, "
           "characters, level start, 120 lossy runs (dropped %u datagrams)\n",
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
    uint32_t players;                    // stay until this many players hold a character; 0: do not wait for that
    uint8_t picks[COOP_CHARACTER_COUNT]; // what to ask for after the welcome, the next one after each refusal
    uint32_t pickCount;
    uint32_t pickDelayMs;
    int32_t startLevel; // host: the level to start, -1 for none
    uint32_t startWith; // host: try once this many clients have joined; 0 means all of them
    bool waitLevel;     // client: stay until a level has started
    CoopIdentity id;
} BotOptions;

typedef struct {
    CoopSessionEventType type;
    int peer;
    uint32_t diff;
    CoopSessionEnd end;
    uint8_t playerIndex;
    uint8_t character;
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
        case COOP_SESSION_EV_PICK_REFUSED:
            return "pick_refused";
        case COOP_SESSION_EV_LEVEL_START:
            return "level_start";
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

static bool Roster_Complete(const CoopRoster* r, uint32_t players) {
    uint32_t i;

    for (i = 0; i < players; i++) {
        if (r->character[i] < COOP_CHARACTER_FOX) {
            return false;
        }
    }
    return true;
}

static uint8_t Character_FromName(const char* name) {
    static const struct {
        const char* name;
        uint8_t character;
    } names[] = { { "fox", FOX }, { "falco", FALCO }, { "slippy", SLIPPY }, { "peppy", PEPPY } };
    size_t i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (strcmp(names[i].name, name) == 0) {
            return names[i].character;
        }
    }
    return NO_PLAYER;
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
    uint32_t nextPick = 0;
    uint32_t pickAt = 0;
    bool pickDue = false;
    uint32_t rosters = 0;
    uint32_t startRefused = 0;
    uint32_t startWith = o->startWith != 0 ? o->startWith : o->clients;
    bool startTaken = false;
    CoopLevelStart level;
    bool levelSeen = false;
    CoopRoster complete = { { 0 } };
    bool isComplete = false;
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
            if (ev.type == COOP_SESSION_EV_ROSTER) {
                const CoopRoster* r = CoopSession_Roster(s);

                rosters++;
                if (!isComplete && o->players > 0 && Roster_Complete(r, o->players)) {
                    complete = *r;
                    isComplete = true;
                }
                continue;
            }
            if (eventCount < 32) {
                BotEvent* e = &events[eventCount++];

                e->type = ev.type;
                e->peer = ev.peer;
                e->diff = ev.diff;
                e->end = ev.end;
                e->playerIndex = ev.playerIndex;
                e->character = ev.character;
                snprintf(e->text, sizeof(e->text), "%s", ev.text);
            }
            printf("%s peer %d: %s\n", Event_Name(ev.type), ev.peer, ev.text[0] ? ev.text : "-");
            fflush(stdout);
            if (!isHost && (ev.type == COOP_SESSION_EV_JOINED || ev.type == COOP_SESSION_EV_PICK_REFUSED) &&
                nextPick < o->pickCount) {
                pickDue = true;
                pickAt = now + o->pickDelayMs;
            }
            if (ev.type == COOP_SESSION_EV_LEVEL_START) {
                level = *ev.levelStart;
                levelSeen = true;
            }
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
        if (pickDue && (int32_t) (now - pickAt) >= 0) {
            pickDue = false;
            CoopSession_Pick(s, (CoopCharacter) o->picks[nextPick++]);
        }
        if (isHost && o->startLevel >= 0 && !startTaken && joinedLive >= startWith) {
            CoopLevelStart want;

            Level_Fill(&want, (uint8_t) o->startLevel, TEST_PHASE);
            startTaken = CoopSession_StartLevel(s, &want);
            startRefused += !startTaken;
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
        } else if (joined && received > 0 && (o->players == 0 || isComplete) && (!o->waitLevel || levelSeen)) {
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
    fprintf(f, "{\"role\": \"%s\", \"finished\": %s, \"received\": %u, \"ignored\": %u, \"rosters\": %u, \"roster\": [",
            isHost ? "host" : "client", finished ? "true" : "false", (unsigned) received,
            (unsigned) CoopSession_Ignored(s), (unsigned) rosters);
    for (i = 0; isComplete && i < COOP_SESSION_PLAYERS; i++) {
        fprintf(f, "%s\"%s\"", i ? ", " : "", CoopSession_CharacterName(complete.character[i]));
    }
    fprintf(f, "], \"start_refused\": %u, \"level_start\": ", (unsigned) startRefused);
    if (levelSeen) {
        fprintf(f, "{\"level\": %u, \"phase\": %u, \"roster\": [", level.level, level.phase);
        for (i = 0; i < COOP_SESSION_PLAYERS; i++) {
            fprintf(f, "%s\"%s\"", i ? ", " : "", CoopSession_CharacterName(level.roster[i]));
        }
        fprintf(f, "], \"values\": [");
        for (i = 0; i < COOP_LEVEL_START_VALUES; i++) {
            fprintf(f, "%s%d", i ? ", " : "", (int) level.values[i]);
        }
        fprintf(f, "]}");
    } else {
        fprintf(f, "null");
    }
    fprintf(f, ", \"events\": [");
    for (i = 0; i < eventCount; i++) {
        const BotEvent* e = &events[i];

        fprintf(f,
                "%s{\"type\": \"%s\", \"peer\": %d, \"diff\": %u, \"end\": %u, \"player\": %u, \"character\": "
                "\"%s\", \"text\": ",
                i ? ", " : "", Event_Name(e->type), e->peer, (unsigned) e->diff, (unsigned) e->end,
                (unsigned) e->playerIndex, CoopSession_CharacterName(e->character));
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
    o.startLevel = -1;
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
        } else if (strcmp(key, "--players") == 0) {
            o.players = (uint32_t) atoi(value);
        } else if (strcmp(key, "--pick-delay-ms") == 0) {
            o.pickDelayMs = (uint32_t) atoi(value);
        } else if (strcmp(key, "--start-level") == 0) {
            o.startLevel = atoi(value);
        } else if (strcmp(key, "--start-with") == 0) {
            o.startWith = (uint32_t) atoi(value);
        } else if (strcmp(key, "--wait-level") == 0) {
            o.waitLevel = atoi(value) != 0;
        } else if (strcmp(key, "--pick") == 0) {
            uint8_t character = Character_FromName(value);

            if (character == NO_PLAYER || o.pickCount == sizeof(o.picks)) {
                fprintf(stderr, "bad --pick %s\n", value);
                return 2;
            }
            o.picks[o.pickCount++] = character;
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
