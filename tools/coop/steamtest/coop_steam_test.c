// Test program for the Steam relay transport (WP-26). tools/coop/steam_test.py drives it; see docs/coop/QA.md.
//
//   coop_steam_test sim --scenario NAME [--seeds N] [link options]
//       The transport over the fake Steam of fake_steam.c: no SDK, no sockets, no real time. Prints a line starting
//       with "sim: PASS" or "sim: FAIL"; exchanges also print "stats: {json}". Exits 0 on PASS.
//   coop_steam_test open --library FILE
//       Loads a Steam library and starts it, then stops. Prints "open: OK <id>" or "open: ERROR <reason>".
//   coop_steam_test live host --library FILE [--id-file F] [--peers N] [options]
//   coop_steam_test live client --library FILE --connect STEAMID [options]
//       Real processes on a Steam library, real or mock. Each side sends a known pattern, checks what arrives, and
//       writes a JSON result.
//
// Link options, from tools/coop/chaos/profiles.json: --latency-ms --jitter-ms --loss --burst-enter --burst-exit
//                 --burst-loss --reorder --duplicate
// Live options: --reliable N --unreliable N --rate PER_SECOND --timeout-ms T --max-seconds S --out FILE
//               --hold (stay until the peer goes) --close-after-ms T (close on purpose after connecting)

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_steam.h"
#include "port/coop/net/protocol.h"
#include "port/coop/net/transport_steam.h"
#include "port/coop/session/identity.h"
#include "port/coop/session/session.h"
#include "port/coop/session/sim_cvars.h"

#define HOST_ID 76561190000000001ull
#define CLIENT_ID 76561190000000002ull
#define STRANGER_ID 76561190000000009ull
#define STEP_MS 16
#define HEADER 16 // the wire form is written out here on purpose: these tests must not share it with the transport
#define MAGIC 0x4353
#define ROM_US10 0x76924CA9u

static uint64_t sSeed;

static void Fail(const char* what) {
    printf("sim: FAIL seed %" PRIu64 ": %s\n", sSeed, what);
    exit(1);
}

#define EXPECT(condition, what) \
    do {                        \
        if (!(condition)) {     \
            Fail(what);         \
        }                       \
    } while (0)

// ---- Test traffic ----

typedef struct {
    uint32_t reliableSent;
    uint32_t unreliableSent;
    uint32_t reliableReceived;
    uint32_t unreliableReceived;
    uint32_t outOfOrder;
    uint32_t corrupt;
    uint32_t unreliableDuplicates;
    uint32_t connectedEvents;
    uint32_t endEvents;
    uint32_t beforeConnected; // messages that came before CONNECTED
    uint32_t afterEnd;        // messages that came after DISCONNECTED
    uint8_t* seen;            // one flag per unreliable index
    uint32_t seenSize;
    bool doneSent;
    bool doneReceived;
    bool connected;
    bool ended;
    CoopNetEnd end;
} Flow;

static uint16_t Pattern_Size(uint32_t index) {
    // Every 97th message is as large as the transport allows.
    return index % 97 == 96 ? COOP_NET_MAX_MESSAGE : (uint16_t) (8 + (index * 37) % 400);
}

static void Pattern_Fill(uint32_t index, uint8_t* out, uint16_t size) {
    uint32_t x = index * 2654435761u + 1;
    uint16_t i;

    memcpy(out, &index, 4);
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
    memcpy(index, data, 4);
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

static void Flow_OnMessage(Flow* f, const CoopNetEvent* ev) {
    uint32_t index = 0;

    if (!f->connected) {
        f->beforeConnected++;
    }
    if (f->ended) {
        f->afterEnd++;
    }
    if (ev->msgId == COOP_MSG_TEST_DONE) {
        f->doneReceived = true;
    } else if (!Pattern_Check(ev->data, ev->size, &index)) {
        f->corrupt++;
    } else if (ev->msgId == COOP_MSG_TEST_RELIABLE) {
        f->outOfOrder += index != f->reliableReceived;
        f->reliableReceived++;
    } else if (ev->msgId != COOP_MSG_TEST_UNRELIABLE || index >= f->seenSize) {
        f->corrupt++;
    } else if (f->seen[index]) {
        f->unreliableDuplicates++;
    } else {
        f->seen[index] = 1;
        f->unreliableReceived++;
    }
}

static void Flow_OnEvent(Flow* f, const CoopNetEvent* ev) {
    if (ev->type == COOP_NET_EVENT_CONNECTED) {
        f->connected = true;
        f->connectedEvents++;
    } else if (ev->type == COOP_NET_EVENT_DISCONNECTED) {
        f->ended = true;
        f->end = ev->end;
        f->endEvents++;
    } else if (ev->type == COOP_NET_EVENT_MESSAGE) {
        Flow_OnMessage(f, ev);
    }
}

static bool Flow_SendReliable(CoopTransport* t, int peer, Flow* f) {
    uint8_t buffer[COOP_NET_MAX_MESSAGE];
    uint16_t size = Pattern_Size(f->reliableSent);

    Pattern_Fill(f->reliableSent, buffer, size);
    if (!t->ops->send(t, peer, COOP_MSG_TEST_RELIABLE, buffer, size, COOP_NET_RELIABLE)) {
        return false;
    }
    f->reliableSent++;
    return true;
}

static bool Flow_SendUnreliable(CoopTransport* t, int peer, Flow* f) {
    uint8_t buffer[COOP_NET_MAX_MESSAGE];
    uint16_t size = Pattern_Size(f->unreliableSent);

    Pattern_Fill(f->unreliableSent, buffer, size);
    if (!t->ops->send(t, peer, COOP_MSG_TEST_UNRELIABLE, buffer, size, COOP_NET_UNRELIABLE)) {
        return false;
    }
    f->unreliableSent++;
    return true;
}

// The next message on each lane, if one is left. A full window just means trying again later.
static void Flow_SendNext(CoopTransport* t, int peer, Flow* f, uint32_t reliable, uint32_t unreliable) {
    if (f->reliableSent < reliable) {
        Flow_SendReliable(t, peer, f);
    } else if (!f->doneSent) {
        f->doneSent = t->ops->send(t, peer, COOP_MSG_TEST_DONE, NULL, 0, COOP_NET_RELIABLE);
    }
    if (f->unreliableSent < unreliable) {
        Flow_SendUnreliable(t, peer, f);
    }
}

// ---- Sides of a simulated connection ----

typedef struct {
    CoopTransport* t;
    CoopSteamBackend* backend;
    CoopSteamId id;
    Flow flows[COOP_NET_MAX_PEERS];
} Side;

static void Side_Init(Side* s, CoopSteamId id, uint32_t unreliable) {
    int i;

    memset(s, 0, sizeof(*s));
    s->id = id;
    s->backend = FakeNet_Join(id);
    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        Flow_Init(&s->flows[i], unreliable);
    }
}

static void Side_Host(Side* s, uint32_t unreliable, const CoopNetConfig* config) {
    Side_Init(s, HOST_ID, unreliable);
    s->t = CoopNetSteam_Host(s->backend, config);
    EXPECT(s->t != NULL, "no host transport");
}

static void Side_Client(Side* s, CoopSteamId id, uint32_t unreliable, const CoopNetConfig* config) {
    Side_Init(s, id, unreliable);
    s->t = CoopNetSteam_Connect(s->backend, HOST_ID, config);
    EXPECT(s->t != NULL, "no client transport");
}

static void Side_Poll(Side* s) {
    CoopNetEvent ev;

    while (s->t != NULL && s->t->ops->poll(s->t, &ev)) {
        Flow_OnEvent(&s->flows[ev.peer], &ev);
    }
}

static void Side_Close(Side* s) {
    int i;

    if (s->t != NULL) {
        s->t->ops->close(s->t);
        s->t = NULL;
    }
    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        free(s->flows[i].seen);
        s->flows[i].seen = NULL;
    }
}

static void Sim_Frames(Side* a, Side* b, uint32_t ms) {
    uint32_t elapsed;

    for (elapsed = 0; elapsed < ms; elapsed += STEP_MS) {
        FakeNet_Step(STEP_MS);
        Side_Poll(a);
        if (b != NULL) {
            Side_Poll(b);
        }
    }
}

// Runs until the client is connected, and returns how long it took.
static uint32_t Sim_Connect(Side* host, Side* client, uint32_t limitMs) {
    uint32_t elapsed = 0;

    while (!client->flows[0].connected) {
        Sim_Frames(host, client, STEP_MS);
        elapsed += STEP_MS;
        EXPECT(elapsed < limitMs, "the client did not connect");
        EXPECT(!client->flows[0].ended, "the client ended while connecting");
    }
    return elapsed;
}

// Every message that left the backend went back to it, so nothing is held or leaked.
static void Sim_CheckReleased(void) {
    const FakeCounters* c = FakeNet_Counters();

    EXPECT(c->received == c->released, "a message was not given back to the backend");
    EXPECT(c->oversize == 0, "a frame was larger than one datagram");
}

// ---- Exchanges: both sides send everything, with the promises of both lanes checked at the end ----

typedef struct {
    uint32_t reliable;
    uint32_t unreliable;
    uint32_t clients;
    uint32_t stepMs;
    uint32_t sendEvery; // steps between sends
    uint32_t timeoutMs;
    uint32_t outageAtMs;
    uint32_t outageMs;
    bool outageToHost;
    bool outageToClient;
} Exchange;

typedef struct {
    uint64_t dropped;
    uint64_t duplicated;
    uint64_t resent;
    uint64_t seen;
    uint64_t unreliableThrough;
    uint32_t rttMs;
    uint32_t busy;
} ExchangeResult;

static void Check_Flow(const Flow* f, uint32_t reliable, uint32_t unreliable) {
    EXPECT(f->connected && f->connectedEvents == 1, "not connected exactly once");
    EXPECT(f->endEvents == 0, "the connection ended during the exchange");
    EXPECT(f->beforeConnected == 0, "a message came before CONNECTED");
    EXPECT(f->corrupt == 0, "a corrupt message was delivered");
    EXPECT(f->outOfOrder == 0, "reliable messages out of order");
    EXPECT(f->reliableReceived == reliable, "reliable messages missing");
    EXPECT(f->doneReceived, "the final message never arrived");
    EXPECT(f->unreliableDuplicates == 0, "an unreliable message arrived twice");
    EXPECT(f->unreliableReceived <= unreliable, "more unreliable messages than were sent");
}

static void Run_Exchange(const FakeLink* link, uint32_t startTime, const Exchange* x, ExchangeResult* out) {
    CoopNetConfig config = { 0 };
    Side host;
    Side clients[COOP_NET_MAX_PEERS];
    uint32_t steps = 0;
    uint32_t elapsed = 0;
    uint32_t limit = 400000 + (x->reliable + x->unreliable) * 40;
    bool outageDone = x->outageMs == 0;
    const FakeCounters* c = FakeNet_Counters();
    uint32_t i;

    FakeNet_Reset(sSeed, link, startTime);
    config.timeoutMs = x->timeoutMs ? x->timeoutMs : 8000;
    config.maxPeers = (uint8_t) x->clients;
    Side_Host(&host, x->unreliable, &config);
    for (i = 0; i < x->clients; i++) {
        Side_Client(&clients[i], CLIENT_ID + i, x->unreliable, &config);
    }

    for (;;) {
        bool finished = true;

        FakeNet_Step(x->stepMs);
        elapsed += x->stepMs;
        Side_Poll(&host);
        for (i = 0; i < x->clients; i++) {
            Side_Poll(&clients[i]);
        }
        if (!outageDone && elapsed >= x->outageAtMs) {
            outageDone = true;
            for (i = 0; i < x->clients; i++) {
                if (x->outageToHost) {
                    FakeNet_Outage(clients[i].id, HOST_ID, x->outageMs);
                }
                if (x->outageToClient) {
                    FakeNet_Outage(HOST_ID, clients[i].id, x->outageMs);
                }
            }
        }
        for (i = 0; i < x->clients; i++) {
            Flow* h = &host.flows[i];
            Flow* f = &clients[i].flows[0];
            CoopNetStats hs = { 0 };
            CoopNetStats cs = { 0 };

            EXPECT(!h->ended && !f->ended, "a connection ended during the exchange");
            if (steps % x->sendEvery == 0) {
                if (h->connected) {
                    Flow_SendNext(host.t, (int) i, h, x->reliable, x->unreliable);
                }
                if (f->connected) {
                    Flow_SendNext(clients[i].t, 0, f, x->reliable, x->unreliable);
                }
            }
            host.t->ops->stats(host.t, (int) i, &hs);
            clients[i].t->ops->stats(clients[i].t, 0, &cs);
            finished = finished && h->doneReceived && f->doneReceived && h->doneSent && f->doneSent &&
                       h->unreliableSent == x->unreliable && f->unreliableSent == x->unreliable && hs.pending == 0 &&
                       cs.pending == 0;
            if (out != NULL && cs.rttMs != 0) {
                out->rttMs = cs.rttMs;
            }
        }
        if (finished) {
            break;
        }
        EXPECT(++steps <= limit, "the exchange did not finish");
    }

    for (i = 0; i < x->clients; i++) {
        Check_Flow(&host.flows[i], x->reliable, x->unreliable);
        Check_Flow(&clients[i].flows[0], x->reliable, x->unreliable);
        if (out != NULL) {
            out->unreliableThrough += host.flows[i].unreliableReceived + clients[i].flows[0].unreliableReceived;
        }
    }
    if (out != NULL) {
        out->dropped += c->unreliableDropped;
        out->duplicated += c->unreliableDuplicated;
        out->resent += c->reliableResent;
        out->seen += c->unreliableSent + c->reliableSent;
        out->busy += (uint32_t) c->busy;
    }
    Side_Close(&host);
    for (i = 0; i < x->clients; i++) {
        Side_Close(&clients[i]);
    }
    Sim_CheckReleased();
}

static void Print_Stats(const ExchangeResult* r) {
    printf("stats: {\"dropped\":%" PRIu64 ",\"duplicated\":%" PRIu64 ",\"resent\":%" PRIu64 ",\"seen\":%" PRIu64
           ",\"unreliable_through\":%" PRIu64 ",\"rtt_ms\":%u}\n",
           r->dropped, r->duplicated, r->resent, r->seen, r->unreliableThrough, r->rttMs);
}

// ---- Scenarios ----

typedef struct {
    uint32_t seeds;
    FakeLink link;
    const char* direction;
} Options;

static void Sc_Profile(const Options* o) {
    // This asks whether the lanes hold, not when a silent link ends: blackout_timeout does that. The hell profile can
    // really stay silent for 8 s (seed 550 of 1000 did), and ending then would be right.
    Exchange x = { 600, 600, 1, STEP_MS, 1, 30000 };
    ExchangeResult r = { 0 };

    for (sSeed = 1; sSeed <= o->seeds; sSeed++) {
        Run_Exchange(&o->link, (uint32_t) sSeed * 1000, &x, &r);
    }
    Print_Stats(&r);
    printf("sim: PASS %u seeds, %" PRIu64 " unreliable through\n", o->seeds, r.unreliableThrough);
}

// Random links, as coop_net_test's sim: every fourth run starts just before the 32-bit clock wraps.
static void Sc_Sim(const Options* o) {
    ExchangeResult r = { 0 };

    for (sSeed = 1; sSeed <= o->seeds; sSeed++) {
        FakeLink link = { 0 };
        Exchange x = { 300, 300, 1, 2, 1 };
        uint32_t startTime;

        FakeNet_Reset(sSeed * 0x100000001B3ull, &link, 0);
        link.loss = (FakeNet_Random() % 25) / 100.0;
        link.burstEnter = (FakeNet_Random() % 6) / 100.0;
        link.burstExit = 0.3;
        link.burstLoss = 0.6;
        link.latencyMs = 1 + FakeNet_Random() % 150;
        link.jitterMs = FakeNet_Random() % 100;
        link.duplicate = (FakeNet_Random() % 12) / 100.0;
        link.reorder = (FakeNet_Random() % 15) / 100.0;
        link.setupMs = FakeNet_Random() % 2000;
        startTime = (sSeed % 4 == 0) ? 0xFFFFFFFFu - FakeNet_Random() % 4000 : FakeNet_Random() % 100000;
        x.stepMs = sSeed % 2 == 0 ? 33 : 2;
        x.sendEvery = 1 + (uint32_t) (sSeed % 8);
        if (sSeed % 3 == 0) {
            x.outageAtMs = 1000 + FakeNet_Random() % 1000;
            x.outageMs = 500 + FakeNet_Random() % 3000;
            x.outageToHost = true;
            x.outageToClient = true;
        }
        Run_Exchange(&link, startTime, &x, &r);
    }
    EXPECT(r.dropped != 0 && r.duplicated != 0 && r.resent != 0, "the virtual network did nothing");
    Print_Stats(&r);
    printf("sim: PASS %u seeds, dropped %" PRIu64 ", duplicated %" PRIu64 ", resent %" PRIu64 "\n", o->seeds, r.dropped,
           r.duplicated, r.resent);
}

// More messages than the 16-bit sequence numbers can count, on both lanes.
static void Sc_Wrap(const Options* o) {
    FakeLink link = { 0 };
    Exchange x = { 70000, 70000, 1, 1, 1 };

    (void) o;
    sSeed = 1;
    link.latencyMs = 5;
    Run_Exchange(&link, 0, &x, NULL);
    printf("sim: PASS 70000 messages each way on each lane\n");
}

static void Sc_Outage(const Options* o) {
    Exchange x = { 600, 600, 1, STEP_MS, 1, 8000, 3000, 2000 };
    ExchangeResult r = { 0 };

    x.outageToHost = strcmp(o->direction, "s2c") != 0;
    x.outageToClient = strcmp(o->direction, "c2s") != 0;
    for (sSeed = 1; sSeed <= o->seeds; sSeed++) {
        Run_Exchange(&o->link, 1000, &x, &r);
    }
    EXPECT(r.dropped != 0, "the outage dropped nothing");
    printf("sim: PASS 2 s %s outage dropped %" PRIu64 " unreliable, all reliable messages arrived\n", o->direction,
           r.dropped);
}

static void Sc_Clients(const Options* o, uint32_t clients) {
    Exchange x = { 300, 300, clients, STEP_MS, 1 };
    ExchangeResult r = { 0 };

    for (sSeed = 1; sSeed <= o->seeds; sSeed++) {
        Run_Exchange(&o->link, 1000, &x, &r);
    }
    EXPECT(r.dropped != 0, "the network dropped nothing");
    printf("sim: PASS one host, %u clients, dropped %" PRIu64 "\n", clients, r.dropped);
}

static void Sc_TwoClients(const Options* o) {
    Sc_Clients(o, 2);
}

static void Sc_ThreeClients(const Options* o) {
    Sc_Clients(o, 3);
}

static void Sc_SendBuffer(const Options* o) {
    FakeLink link = { 0 };
    Exchange x = { 400, 0, 1, 2, 1 };
    ExchangeResult r = { 0 };

    (void) o;
    link.latencyMs = 80;
    link.sendBufferBytes = 3000;
    sSeed = 1;
    Run_Exchange(&link, 0, &x, &r);
    EXPECT(r.busy != 0, "Steam's send queue never filled, so this proves nothing");
    printf("sim: PASS send queue full %u times, all reliable messages arrived in order\n", r.busy);
}

static void Sc_BlackoutTimeout(const Options* o) {
    CoopNetConfig config = { 0 };
    FakeLink link = { 0 };
    Side host;
    Side client;
    uint32_t elapsed = 0;

    (void) o;
    sSeed = 1;
    config.timeoutMs = 2000;
    link.latencyMs = 10;
    FakeNet_Reset(sSeed, &link, 0);
    Side_Host(&host, 0, &config);
    Side_Client(&client, CLIENT_ID, 0, &config);
    Sim_Connect(&host, &client, 2000);
    Sim_Frames(&host, &client, 1000);
    FakeNet_Outage(HOST_ID, CLIENT_ID, 6000);
    FakeNet_Outage(CLIENT_ID, HOST_ID, 6000);
    while (!host.flows[0].ended || !client.flows[0].ended) {
        Sim_Frames(&host, &client, STEP_MS);
        elapsed += STEP_MS;
        EXPECT(elapsed < 3000, "the outage was not noticed in time");
    }
    EXPECT(host.flows[0].end == COOP_NET_END_TIMEOUT && client.flows[0].end == COOP_NET_END_TIMEOUT,
           "an outage must end in a timeout");
    EXPECT(elapsed >= 1500, "ended before the timeout could pass");
    Side_Close(&host);
    Side_Close(&client);
    printf("sim: PASS both sides reported a timeout %u ms after the link died (limit 2000 ms)\n", elapsed);
}

// Steam gives up before the transport would: its event ends the connection, and the next connection gets through.
static void Sc_SessionFailed(const Options* o) {
    CoopNetConfig config = { 0 };
    FakeLink link = { 0 };
    Side host;
    Side client;
    Side again;
    uint32_t elapsed = 0;

    (void) o;
    sSeed = 1;
    config.timeoutMs = 8000;
    config.pingMs = 100000; // no ping may find the broken session first: only Steam's event can tell
    link.latencyMs = 10;
    FakeNet_Reset(sSeed, &link, 0);
    Side_Host(&host, 0, &config);
    Side_Client(&client, CLIENT_ID, 0, &config);
    Sim_Connect(&host, &client, 2000);
    Sim_Frames(&host, &client, 500);
    FakeNet_Break(HOST_ID, CLIENT_ID);
    while (!host.flows[0].ended || !client.flows[0].ended) {
        Sim_Frames(&host, &client, STEP_MS);
        elapsed += STEP_MS;
        EXPECT(elapsed < 1000, "a failed session was not noticed at once");
    }
    EXPECT(host.flows[0].end == COOP_NET_END_TIMEOUT && client.flows[0].end == COOP_NET_END_TIMEOUT,
           "a failed session ends in a timeout");
    EXPECT(FakeNet_Counters()->sessionsClosed >= 2, "a broken session was not closed, so it stays broken");
    EXPECT(host.flows[0].endEvents == 1 && client.flows[0].endEvents == 1, "more than one end event");
    Side_Close(&client);

    Side_Client(&again, CLIENT_ID, 0, &config);
    host.flows[0].connected = false;
    host.flows[0].ended = false;
    Sim_Connect(&host, &again, 3000);
    Side_Close(&again);
    Side_Close(&host);
    printf("sim: PASS a failed session ended both sides after %u ms, and the client got back in\n", elapsed);
}

// "There is no notification when a peer proactively closes the session": only the next send can tell.
static void Sc_PeerClosed(const Options* o) {
    CoopNetConfig config = { 0 };
    FakeLink link = { 0 };
    Side host;
    Side client;
    uint32_t elapsed = 0;

    (void) o;
    sSeed = 1;
    config.timeoutMs = 8000;
    link.latencyMs = 10;
    FakeNet_Reset(sSeed, &link, 0);
    Side_Host(&host, 0, &config);
    Side_Client(&client, CLIENT_ID, 0, &config);
    Sim_Connect(&host, &client, 2000);
    Sim_Frames(&host, &client, 500);
    host.backend->ops->closeSession(host.backend, CLIENT_ID);
    while (!client.flows[0].ended) {
        Sim_Frames(&client, NULL, STEP_MS);
        elapsed += STEP_MS;
        EXPECT(elapsed < 1500, "a session closed by the peer was not noticed");
    }
    EXPECT(FakeNet_Counters()->broken != 0, "no send met the broken session, so this proves nothing");
    EXPECT(client.flows[0].end == COOP_NET_END_TIMEOUT, "a peer gone without a goodbye ends in a timeout");
    Side_Close(&client);
    Side_Close(&host);
    printf("sim: PASS a session closed without a goodbye was noticed after %u ms (timeout 8000 ms)\n", elapsed);
}

// Nobody has anything to say for far longer than the timeout: keep-alives alone hold the link, and measure it.
static void Sc_Quiet(const Options* o) {
    CoopNetConfig config = { 0 };
    FakeLink link = { 0 };
    CoopNetStats stats;
    Side host;
    Side client;

    (void) o;
    sSeed = 1;
    config.timeoutMs = 2000;
    link.latencyMs = 30;
    FakeNet_Reset(sSeed, &link, 0);
    Side_Host(&host, 0, &config);
    Side_Client(&client, CLIENT_ID, 0, &config);
    Sim_Connect(&host, &client, 2000);
    Sim_Frames(&host, &client, 20000);
    EXPECT(!host.flows[0].ended && !client.flows[0].ended, "a quiet link timed out");
    client.t->ops->stats(client.t, 0, &stats);
    EXPECT(stats.rttMs >= 60 && stats.rttMs <= 100, "the round trip was not measured");
    Side_Close(&host);
    Side_Close(&client);
    printf("sim: PASS 20 s without a message on a 2 s timeout, rtt %u ms\n", stats.rttMs);
}

static void Sc_KillClient(const Options* o) {
    CoopNetConfig config = { 0 };
    FakeLink link = { 0 };
    Side host;
    Side client;
    uint32_t elapsed = 0;

    (void) o;
    sSeed = 1;
    config.timeoutMs = 2000;
    link.latencyMs = 10;
    FakeNet_Reset(sSeed, &link, 0);
    Side_Host(&host, 0, &config);
    Side_Client(&client, CLIENT_ID, 0, &config);
    Sim_Connect(&host, &client, 2000);
    FakeNet_Kill(CLIENT_ID);
    while (!host.flows[0].ended) {
        Sim_Frames(&host, NULL, STEP_MS);
        elapsed += STEP_MS;
        EXPECT(elapsed < 3000, "a killed client was not dropped");
    }
    EXPECT(host.flows[0].connected && host.flows[0].end == COOP_NET_END_TIMEOUT, "the host must report a timeout");
    Side_Close(&client);
    Side_Close(&host);
    printf("sim: PASS host dropped a killed client after %u ms (limit 2000 ms)\n", elapsed);
}

// Everything sent before the goodbye arrives before it, and the end comes last.
static void Sc_CleanClose(const Options* o) {
    CoopNetConfig config = { 0 };
    Side host;
    Side client;
    uint32_t i;

    for (sSeed = 1; sSeed <= o->seeds; sSeed++) {
        uint32_t elapsed = 0;

        FakeNet_Reset(sSeed, &o->link, 0);
        Side_Host(&host, 0, &config);
        Side_Client(&client, CLIENT_ID, 0, &config);
        Sim_Connect(&host, &client, 5000);
        for (i = 0; i < 50; i++) {
            EXPECT(Flow_SendReliable(client.t, 0, &client.flows[0]), "a send failed");
        }
        Side_Close(&client);
        while (!host.flows[0].ended) {
            Sim_Frames(&host, NULL, STEP_MS);
            elapsed += STEP_MS;
            EXPECT(elapsed < 5000, "the host did not see the goodbye");
        }
        EXPECT(host.flows[0].end == COOP_NET_END_CLOSED, "a goodbye must end in closed");
        EXPECT(host.flows[0].reliableReceived == 50, "messages sent before the goodbye did not arrive first");
        EXPECT(host.flows[0].afterEnd == 0 && host.flows[0].endEvents == 1, "something came after the end");
        Side_Close(&host);
        Sim_CheckReleased();
    }
    printf("sim: PASS 50 messages, then the goodbye, then one end event, on %u seeds\n", o->seeds);
}

// Whether a goodbye survives closing Steam right after it is unknown. If it does not, the peer must still find out.
static void Sc_LostGoodbye(const Options* o) {
    CoopNetConfig config = { 0 };
    FakeLink link = { 0 };
    Side host;
    Side client;
    uint32_t elapsed = 0;

    (void) o;
    sSeed = 1;
    config.timeoutMs = 2000;
    link.latencyMs = 40;
    link.shutdownLosesData = true;
    FakeNet_Reset(sSeed, &link, 0);
    Side_Host(&host, 0, &config);
    Side_Client(&client, CLIENT_ID, 0, &config);
    Sim_Connect(&host, &client, 2000);
    Side_Close(&client);
    while (!host.flows[0].ended) {
        Sim_Frames(&host, NULL, STEP_MS);
        elapsed += STEP_MS;
        EXPECT(elapsed < 3000, "a lost goodbye left the host waiting");
    }
    EXPECT(host.flows[0].end == COOP_NET_END_TIMEOUT, "a lost goodbye ends in a timeout");
    Side_Close(&host);
    printf("sim: PASS host found out %u ms after a goodbye that was lost\n", elapsed);
}

static void Sc_HostFull(const Options* o) {
    CoopNetConfig config = { 0 };
    FakeLink link = { 0 };
    Side host;
    Side first;
    Side second;
    uint32_t elapsed = 0;

    (void) o;
    sSeed = 1;
    config.maxPeers = 1;
    link.latencyMs = 10;
    FakeNet_Reset(sSeed, &link, 0);
    Side_Host(&host, 0, &config);
    Side_Client(&first, CLIENT_ID, 0, &config);
    Sim_Connect(&host, &first, 2000);
    Side_Client(&second, CLIENT_ID + 1, 0, &config);
    while (!second.flows[0].ended) {
        Sim_Frames(&host, &first, STEP_MS);
        Side_Poll(&second);
        elapsed += STEP_MS;
        EXPECT(elapsed < 5000, "the second client got no answer");
    }
    EXPECT(!second.flows[0].connected && second.flows[0].end == COOP_NET_END_REFUSED_FULL, "not refused as full");
    EXPECT(!first.flows[0].ended && !host.flows[0].ended, "the first client was dropped");
    Side_Close(&second);
    Side_Close(&first);
    Side_Close(&host);
    printf("sim: PASS a second client on a full host was refused after %u ms, the first stayed\n", elapsed);
}

// ---- Raw peers: frames written by hand, from the layout in PROTOCOL.md ----

static void Put16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}

static void Put32(uint8_t* p, uint32_t v) {
    Put16(p, (uint16_t) v);
    Put16(p + 2, (uint16_t) (v >> 16));
}

static uint16_t Get16(const uint8_t* p) {
    return (uint16_t) (p[0] | (p[1] << 8));
}

static uint32_t Get32(const uint8_t* p) {
    return Get16(p) | ((uint32_t) Get16(p + 2) << 16);
}

static void Raw_Send(CoopSteamBackend* b, CoopSteamId to, uint8_t version, uint16_t msgId, uint32_t connId,
                     uint16_t seq, uint16_t ack, const void* payload, uint16_t size, CoopNetLane lane) {
    uint8_t frame[HEADER + COOP_NET_MAX_MESSAGE];

    Put16(frame, MAGIC);
    frame[2] = version;
    frame[3] = lane == COOP_NET_RELIABLE ? 1 : 0;
    Put16(frame + 4, msgId);
    Put16(frame + 6, size);
    Put32(frame + 8, connId);
    Put16(frame + 12, seq);
    Put16(frame + 14, ack);
    if (size != 0) {
        memcpy(frame + HEADER, payload, size);
    }
    b->ops->send(b, to, frame, (uint32_t) (HEADER + size), lane);
}

// The connection id of the first request that reached a raw host.
static uint32_t Raw_AwaitRequest(CoopSteamBackend* raw, Side* client) {
    CoopSteamMessage m;
    CoopSteamEvent e;
    uint32_t elapsed = 0;

    for (;;) {
        while (raw->ops->event(raw, &e)) {
            raw->ops->accept(raw, e.peer);
        }
        while (raw->ops->receive(raw, &m)) {
            uint16_t msgId = Get16(m.data + 4);
            uint32_t connId = Get32(m.data + 8);

            raw->ops->release(raw, &m);
            if (msgId == COOP_MSG_CONNECT_REQUEST) {
                return connId;
            }
        }
        Sim_Frames(client, NULL, STEP_MS);
        elapsed += STEP_MS;
        EXPECT(elapsed < 5000, "no connect request reached the raw host");
    }
}

static void Sc_Version(const Options* o) {
    CoopNetConfig config = { 0 };
    FakeLink link = { 0 };
    Side host;
    Side client;
    CoopSteamBackend* raw;
    CoopSteamMessage m;
    uint8_t reason = 2;
    uint32_t elapsed = 0;
    uint32_t connId;
    bool refused = false;

    (void) o;
    sSeed = 1;
    link.latencyMs = 10;
    FakeNet_Reset(sSeed, &link, 0);

    // A client of another version gets a refusal it can read.
    Side_Host(&host, 0, &config);
    raw = FakeNet_Join(STRANGER_ID);
    Raw_Send(raw, HOST_ID, COOP_PROTOCOL_VERSION + 1, COOP_MSG_CONNECT_REQUEST, 77, 0, 0, NULL, 0, COOP_NET_RELIABLE);
    while (!refused) {
        Sim_Frames(&host, NULL, STEP_MS);
        while (raw->ops->receive(raw, &m)) {
            refused = refused || (Get16(m.data + 4) == COOP_MSG_CONNECT_REFUSE && m.size == HEADER + 1 &&
                                  m.data[HEADER] == 2 && Get32(m.data + 8) == 77);
            raw->ops->release(raw, &m);
        }
        elapsed += STEP_MS;
        EXPECT(elapsed < 2000, "a client of another version got no refusal");
    }
    EXPECT(!host.flows[0].connected, "the host connected a client of another version");
    Side_Close(&host);

    // A host of another version refuses in its own version, and the client still reads it.
    raw = FakeNet_Join(HOST_ID);
    Side_Client(&client, CLIENT_ID, 0, &config);
    connId = Raw_AwaitRequest(raw, &client);
    Raw_Send(raw, CLIENT_ID, COOP_PROTOCOL_VERSION + 1, COOP_MSG_CONNECT_REFUSE, connId, 0, 0, &reason, 1,
             COOP_NET_RELIABLE);
    Sim_Frames(&client, NULL, 200);
    EXPECT(client.flows[0].ended && client.flows[0].end == COOP_NET_END_REFUSED_VERSION, "the refusal was not read");
    Side_Close(&client);
    printf("sim: PASS both versions refuse in a form the other can read\n");
}

// A real client against a raw host that has accepted it.
static CoopSteamBackend* Raw_Accepted(Side* client, uint32_t* connId) {
    CoopNetConfig config = { 0 };
    CoopSteamBackend* raw = FakeNet_Join(HOST_ID);

    config.timeoutMs = 8000;
    Side_Client(client, CLIENT_ID, 8, &config);
    *connId = Raw_AwaitRequest(raw, client);
    Raw_Send(raw, CLIENT_ID, COOP_PROTOCOL_VERSION, COOP_MSG_CONNECT_ACCEPT, *connId, 0, 0, NULL, 0, COOP_NET_RELIABLE);
    Sim_Frames(client, NULL, 200);
    EXPECT(client->flows[0].connected, "the client did not take the accept");
    return raw;
}

static void Raw_Pattern(CoopSteamBackend* raw, uint32_t connId, uint16_t msgId, uint32_t index, uint16_t seq,
                        CoopNetLane lane) {
    uint8_t buffer[COOP_NET_MAX_MESSAGE];
    uint16_t size = Pattern_Size(index);

    Pattern_Fill(index, buffer, size);
    Raw_Send(raw, CLIENT_ID, COOP_PROTOCOL_VERSION, msgId, connId, seq, 0, buffer, size, lane);
}

// A hole in the reliable numbers means a session broke and restarted under us. Handing out what follows would break
// the promise of the lane, so the connection ends instead.
static void Sc_Gap(const Options* o) {
    FakeLink link = { 0 };
    Side client;
    CoopSteamBackend* raw;
    uint32_t connId;

    (void) o;
    sSeed = 1;
    link.latencyMs = 10;
    FakeNet_Reset(sSeed, &link, 0);
    raw = Raw_Accepted(&client, &connId);
    Raw_Pattern(raw, connId, COOP_MSG_TEST_RELIABLE, 0, 0, COOP_NET_RELIABLE);
    Raw_Pattern(raw, connId, COOP_MSG_TEST_RELIABLE, 2, 2, COOP_NET_RELIABLE);
    Sim_Frames(&client, NULL, 200);
    EXPECT(client.flows[0].reliableReceived == 1, "a message after a hole was handed out");
    EXPECT(client.flows[0].ended && client.flows[0].end == COOP_NET_END_TIMEOUT, "a hole must end the connection");
    Side_Close(&client);
    printf("sim: PASS a hole in the reliable lane ended the connection, nothing after it was delivered\n");
}

// Steam may deliver an unreliable message twice, so the transport has to keep the promise of the lane by itself.
static void Sc_Duplicates(const Options* o) {
    FakeLink link = { 0 };
    Side client;
    CoopSteamBackend* raw;
    CoopNetStats stats;
    uint32_t connId;

    (void) o;
    sSeed = 1;
    link.latencyMs = 10;
    FakeNet_Reset(sSeed, &link, 0);
    raw = Raw_Accepted(&client, &connId);
    Raw_Pattern(raw, connId, COOP_MSG_TEST_RELIABLE, 0, 0, COOP_NET_RELIABLE);
    Raw_Pattern(raw, connId, COOP_MSG_TEST_RELIABLE, 0, 0, COOP_NET_RELIABLE);
    Raw_Pattern(raw, connId, COOP_MSG_TEST_RELIABLE, 1, 1, COOP_NET_RELIABLE);
    Sim_Frames(&client, NULL, 200);
    EXPECT(client.flows[0].reliableReceived == 2 && client.flows[0].outOfOrder == 0, "a reliable message came twice");
    EXPECT(!client.flows[0].ended, "a duplicate must not end the connection");
    Side_Close(&client);

    FakeNet_Reset(sSeed, &link, 0);
    raw = Raw_Accepted(&client, &connId);
    Raw_Pattern(raw, connId, COOP_MSG_TEST_UNRELIABLE, 5, 5, COOP_NET_UNRELIABLE);
    Sim_Frames(&client, NULL, 100);
    Raw_Pattern(raw, connId, COOP_MSG_TEST_UNRELIABLE, 5, 5, COOP_NET_UNRELIABLE);
    Raw_Pattern(raw, connId, COOP_MSG_TEST_UNRELIABLE, 4, 4, COOP_NET_UNRELIABLE); // late, but new
    Raw_Pattern(raw, connId, COOP_MSG_TEST_UNRELIABLE, 7, 70, COOP_NET_UNRELIABLE);
    Sim_Frames(&client, NULL, 100);
    Raw_Pattern(raw, connId, COOP_MSG_TEST_UNRELIABLE, 6, 6, COOP_NET_UNRELIABLE); // 64 behind: counts as a duplicate
    Sim_Frames(&client, NULL, 100);
    EXPECT(client.flows[0].unreliableDuplicates == 0, "an unreliable message came twice");
    EXPECT(client.flows[0].unreliableReceived == 3, "the wrong unreliable messages came through");
    client.t->ops->stats(client.t, 0, &stats);
    EXPECT(stats.duplicates == 2, "duplicates were not counted");
    Side_Close(&client);
    printf("sim: PASS each message delivered once, on both lanes\n");
}

// One malformed frame of each kind from a connected peer: counted, dropped, and the connection stays.
static void Sc_BadFrames(const Options* o) {
    FakeLink link = { 0 };
    Side client;
    CoopSteamBackend* raw;
    CoopNetStats stats;
    uint8_t frame[COOP_NET_MAX_DATAGRAM] = { 0 };
    uint32_t connId;
    uint32_t i;

    (void) o;
    sSeed = 1;
    link.latencyMs = 10;
    FakeNet_Reset(sSeed, &link, 0);
    raw = Raw_Accepted(&client, &connId);
    for (i = 0; i < 5; i++) {
        uint32_t size = HEADER + 8;

        Put16(frame, MAGIC);
        frame[2] = COOP_PROTOCOL_VERSION;
        Put16(frame + 4, COOP_MSG_TEST_UNRELIABLE);
        Put16(frame + 6, 8);
        Put32(frame + 8, connId);
        Put16(frame + 12, (uint16_t) i);
        if (i == 0) {
            Put16(frame, MAGIC + 1); // not ours
        } else if (i == 1) {
            size = HEADER - 1; // shorter than a header
        } else if (i == 2) {
            Put16(frame + 6, 9); // says more than there is
        } else if (i == 3) {
            Put16(frame + 6, 7); // says less than there is
        } else {
            size = HEADER + COOP_NET_MAX_MESSAGE + 1; // one byte over the limit, and it says so
            Put16(frame + 6, COOP_NET_MAX_MESSAGE + 1);
        }
        raw->ops->send(raw, CLIENT_ID, frame, size, COOP_NET_UNRELIABLE);
    }
    Sim_Frames(&client, NULL, 200);
    client.t->ops->stats(client.t, 0, &stats);
    EXPECT(stats.badDatagrams == 5, "a malformed frame was not counted");
    EXPECT(client.flows[0].corrupt == 0 && client.flows[0].unreliableReceived == 0, "a malformed frame was delivered");
    EXPECT(!client.flows[0].ended, "a malformed frame ended the connection");
    Side_Close(&client);
    printf("sim: PASS wrong magic, too short, both wrong sizes and one byte too many: all counted and dropped\n");
}

// What the peer says it got moves pending only forward, and only as far as what was sent.
static void Sc_Acks(const Options* o) {
    static const struct {
        uint16_t ack;
        uint32_t pending;
    } kSteps[] = { { 1000, 3 }, { 2, 1 }, { 1, 1 }, { 0xFFFF, 1 }, { 3, 0 } };
    FakeLink link = { 0 };
    Side client;
    CoopSteamBackend* raw;
    CoopNetStats stats;
    uint32_t connId;
    uint32_t i;

    (void) o;
    sSeed = 1;
    link.latencyMs = 10;
    FakeNet_Reset(sSeed, &link, 0);
    raw = Raw_Accepted(&client, &connId);
    for (i = 0; i < 3; i++) {
        EXPECT(Flow_SendReliable(client.t, 0, &client.flows[0]), "a send failed");
    }
    for (i = 0; i < 5; i++) {
        Raw_Send(raw, CLIENT_ID, COOP_PROTOCOL_VERSION, COOP_MSG_ACK, connId, 0, kSteps[i].ack, NULL, 0,
                 COOP_NET_UNRELIABLE);
        Sim_Frames(&client, NULL, 100);
        client.t->ops->stats(client.t, 0, &stats);
        EXPECT(stats.pending == kSteps[i].pending, "an ack moved pending the wrong way");
    }
    Side_Close(&client);
    printf("sim: PASS forged and stale acks left pending alone, real ones moved it\n");
}

// Steam's queue is full just when the accept should go. Nothing may overtake it, and it must still go out.
static void Sc_AcceptBusy(const Options* o) {
    CoopNetConfig config = { 0 };
    FakeLink link = { 0 };
    Side host;
    Side client;
    bool blocked = false;

    (void) o;
    sSeed = 1;
    link.latencyMs = 10;
    FakeNet_Reset(sSeed, &link, 0);
    Side_Host(&host, 0, &config);
    Side_Client(&client, CLIENT_ID, 0, &config);
    FakeNet_BusyNext(HOST_ID, 5);
    while (!host.flows[0].connected) {
        Sim_Frames(&host, &client, STEP_MS);
    }
    // The queue is free again and the accept still has not gone: only the transport can hold this message back.
    FakeNet_BusyNext(HOST_ID, 0);
    blocked = !host.t->ops->send(host.t, 0, COOP_MSG_TEST_DONE, NULL, 0, COOP_NET_RELIABLE);
    EXPECT(blocked, "a message went before the accept");
    Sim_Connect(&host, &client, 3000);
    Sim_Frames(&host, &client, 100);
    EXPECT(Flow_SendReliable(host.t, 0, &host.flows[0]), "no send after the accept went out");
    Sim_Frames(&host, &client, 200);
    EXPECT(client.flows[0].reliableReceived == 1 && client.flows[0].beforeConnected == 0, "the order broke");
    Side_Close(&host);
    Side_Close(&client);
    printf("sim: PASS an accept that met a full queue went out later, and first\n");
}

// Junk from a stranger and from a connected peer. Nothing may reach the caller, and nothing may crash (--asan).
static void Sc_Junk(const Options* o) {
    FakeLink link = { 0 };
    uint32_t bad = 0;

    link.latencyMs = 3;
    for (sSeed = 1; sSeed <= o->seeds; sSeed++) {
        CoopNetConfig config = { 0 };
        CoopNetStats stats;
        Side host;
        Side client;
        CoopSteamBackend* stranger;
        uint32_t i;

        config.timeoutMs = 8000;
        FakeNet_Reset(sSeed, &link, 0);
        Side_Host(&host, 8, &config);
        Side_Client(&client, CLIENT_ID, 8, &config);
        Sim_Connect(&host, &client, 2000);
        stranger = FakeNet_Join(STRANGER_ID);
        for (i = 0; i < 400; i++) {
            uint8_t junk[COOP_NET_MAX_DATAGRAM + 200];
            uint32_t size = FakeNet_Random() % sizeof(junk);
            uint32_t j;

            for (j = 0; j < size; j++) {
                junk[j] = (uint8_t) FakeNet_Random();
            }
            if (i % 2 == 0 && size >= HEADER) {
                // Well formed up to a point: the right magic and version, with sizes and ids at random.
                Put16(junk, MAGIC);
                junk[2] = COOP_PROTOCOL_VERSION;
                if (i % 4 == 0) {
                    Put16(junk + 6, (uint16_t) (size - HEADER));
                }
            }
            stranger->ops->send(stranger, i % 3 == 0 ? CLIENT_ID : HOST_ID, junk,
                                size > COOP_NET_MAX_DATAGRAM ? COOP_NET_MAX_DATAGRAM : size, COOP_NET_UNRELIABLE);
            if (i % 16 == 0) {
                Sim_Frames(&host, &client, STEP_MS);
            }
        }
        Sim_Frames(&host, &client, 300);
        EXPECT(host.flows[0].corrupt == 0 && client.flows[0].corrupt == 0, "junk was delivered");
        EXPECT(!host.flows[0].ended && !client.flows[0].ended, "a stranger's junk ended a connection");
        EXPECT(!host.flows[1].connected, "junk made a connection");
        host.t->ops->stats(host.t, 0, &stats);
        bad += stats.badDatagrams;
        EXPECT(Flow_SendReliable(client.t, 0, &client.flows[0]), "a send failed after the junk");
        Sim_Frames(&host, &client, 100);
        EXPECT(host.flows[0].reliableReceived == 1, "traffic stopped after the junk");
        Side_Close(&host);
        Side_Close(&client);
        Sim_CheckReleased();
    }
    EXPECT(bad != 0, "no junk was seen, so this proves nothing");
    printf("sim: PASS %u seeds, %u frames rejected, traffic unharmed\n", o->seeds, bad);
}

// A malicious peer that is already connected, with frames that are valid up to a point.
static void Sc_BadPeer(const Options* o) {
    FakeLink link = { 0 };
    uint32_t bad = 0;

    link.latencyMs = 3;
    for (sSeed = 1; sSeed <= o->seeds; sSeed++) {
        Side client;
        CoopSteamBackend* raw;
        CoopNetStats stats;
        uint32_t connId;
        uint32_t i;

        FakeNet_Reset(sSeed, &link, 0);
        raw = Raw_Accepted(&client, &connId);
        for (i = 0; i < 300; i++) {
            uint8_t frame[COOP_NET_MAX_DATAGRAM];
            uint32_t size = HEADER + FakeNet_Random() % (sizeof(frame) - HEADER);
            uint32_t j;

            for (j = 0; j < size; j++) {
                frame[j] = (uint8_t) FakeNet_Random();
            }
            Put16(frame, MAGIC);
            frame[2] = COOP_PROTOCOL_VERSION;
            frame[3] = 0;
            Put16(frame + 4, (uint16_t) (FakeNet_Random() % 8)); // a transport message, with any payload
            Put32(frame + 8, connId);
            if (i % 2 == 0) {
                Put16(frame + 6, (uint16_t) (size - HEADER));
            }
            raw->ops->send(raw, CLIENT_ID, frame, size, COOP_NET_UNRELIABLE);
            if (i % 16 == 0) {
                Sim_Frames(&client, NULL, STEP_MS);
            }
        }
        Sim_Frames(&client, NULL, 200);
        EXPECT(client.flows[0].corrupt == 0, "a bad frame was delivered");
        client.t->ops->stats(client.t, 0, &stats);
        bad += stats.badDatagrams;
        Side_Close(&client);
        Sim_CheckReleased();
    }
    EXPECT(bad != 0, "no bad frame was seen, so this proves nothing");
    printf("sim: PASS %u seeds, %u frames rejected\n", o->seeds, bad);
}

// The peer's last messages, then its session gone, all before we read a thing. Answering its ping finds the session
// broken, and what waits behind that ping must still come out, in order, before the end.
static void Sc_BrokenAfterData(const Options* o) {
    CoopNetConfig config = { 0 };
    FakeLink link = { 0 };
    Side host;
    CoopSteamBackend* raw;
    uint8_t stamp[4] = { 0 };
    uint32_t i;

    (void) o;
    sSeed = 1;
    config.timeoutMs = 8000;
    link.latencyMs = 10;
    FakeNet_Reset(sSeed, &link, 0);
    Side_Host(&host, 0, &config);
    raw = FakeNet_Join(CLIENT_ID);
    Raw_Send(raw, HOST_ID, COOP_PROTOCOL_VERSION, COOP_MSG_CONNECT_REQUEST, 5, 0, 0, NULL, 0, COOP_NET_RELIABLE);
    Sim_Frames(&host, NULL, 200);
    EXPECT(host.flows[0].connected, "the host did not take the request");

    Raw_Send(raw, HOST_ID, COOP_PROTOCOL_VERSION, COOP_MSG_PING, 5, 0, 0, stamp, 4, COOP_NET_UNRELIABLE);
    FakeNet_Step(100);
    for (i = 0; i < 3; i++) {
        uint8_t buffer[COOP_NET_MAX_MESSAGE];
        uint16_t size = Pattern_Size(i);

        Pattern_Fill(i, buffer, size);
        Raw_Send(raw, HOST_ID, COOP_PROTOCOL_VERSION, COOP_MSG_TEST_RELIABLE, 5, (uint16_t) i, 0, buffer, size,
                 COOP_NET_RELIABLE);
    }
    FakeNet_Step(100);
    raw->ops->closeSession(raw, HOST_ID);
    FakeNet_Step(100);
    Sim_Frames(&host, NULL, 100);
    EXPECT(FakeNet_Counters()->broken != 0, "no send met the broken session, so this proves nothing");
    EXPECT(host.flows[0].reliableReceived == 3, "messages that had arrived were lost with the session");
    EXPECT(host.flows[0].ended && host.flows[0].end == COOP_NET_END_TIMEOUT, "a broken session must end the link");
    EXPECT(host.flows[0].afterEnd == 0 && host.flows[0].endEvents == 1, "something came after the end");
    EXPECT(FakeNet_Counters()->sessionsClosed == 2, "the broken session was not closed"); // the raw peer's, and ours
    Side_Close(&host);
    printf("sim: PASS 3 messages behind a broken session came out first, then one end event\n");
}

// The same with Steam's event instead of a send, behind more transport frames than one poll reads: the event comes
// while a message still waits.
static void Sc_FailedAfterData(const Options* o) {
    CoopNetConfig config = { 0 };
    FakeLink link = { 0 };
    Side host;
    CoopSteamBackend* raw;
    uint8_t buffer[COOP_NET_MAX_MESSAGE];
    uint16_t size = Pattern_Size(0);
    uint32_t i;

    (void) o;
    sSeed = 1;
    config.timeoutMs = 8000;
    link.latencyMs = 10;
    FakeNet_Reset(sSeed, &link, 0);
    Side_Host(&host, 0, &config);
    raw = FakeNet_Join(CLIENT_ID);
    Raw_Send(raw, HOST_ID, COOP_PROTOCOL_VERSION, COOP_MSG_CONNECT_REQUEST, 5, 0, 0, NULL, 0, COOP_NET_RELIABLE);
    Sim_Frames(&host, NULL, 200);
    EXPECT(host.flows[0].connected, "the host did not take the request");
    for (i = 0; i < 300; i++) {
        Raw_Send(raw, HOST_ID, COOP_PROTOCOL_VERSION, COOP_MSG_ACK, 5, 0, 0, NULL, 0, COOP_NET_RELIABLE);
    }
    Pattern_Fill(0, buffer, size);
    Raw_Send(raw, HOST_ID, COOP_PROTOCOL_VERSION, COOP_MSG_TEST_RELIABLE, 5, 0, 0, buffer, size, COOP_NET_RELIABLE);
    FakeNet_Step(100);
    FakeNet_Break(HOST_ID, CLIENT_ID);
    Sim_Frames(&host, NULL, 100);
    EXPECT(host.flows[0].ended, "the failed session did not end the link");
    EXPECT(host.flows[0].reliableReceived == 1, "a message that had arrived was lost with the session");
    EXPECT(host.flows[0].afterEnd == 0, "a message came after the end");
    Side_Close(&host);
    printf("sim: PASS a failed session ended the link after the last message that had arrived\n");
}

// A stall longer than the timeout, with the peer's keep-alives waiting behind a flood from a stranger. Reading the
// flood takes more than one poll, and the peer must not time out while its frames are still in the queue.
static void Sc_Stall(const Options* o) {
    CoopNetConfig config = { 0 };
    FakeLink link = { 0 };
    Side host;
    Side client;
    CoopSteamBackend* stranger;
    CoopNetStats stats;
    uint32_t i;

    (void) o;
    sSeed = 1;
    config.timeoutMs = 2000;
    link.latencyMs = 10;
    FakeNet_Reset(sSeed, &link, 0);
    Side_Host(&host, 0, &config);
    Side_Client(&client, CLIENT_ID, 0, &config);
    Sim_Connect(&host, &client, 2000);
    stranger = FakeNet_Join(STRANGER_ID);
    Raw_Send(stranger, HOST_ID, COOP_PROTOCOL_VERSION, COOP_MSG_TEST_DONE, 1, 0, 0, NULL, 0, COOP_NET_RELIABLE);
    Sim_Frames(&host, &client, 100);
    Sim_Frames(&host, &client, 100); // the accept lets it through on the poll after
    for (i = 0; i < 600; i++) {
        Raw_Send(stranger, HOST_ID, COOP_PROTOCOL_VERSION, COOP_MSG_TEST_DONE, 1, 0, 0, NULL, 0, COOP_NET_RELIABLE);
    }
    // Only the client runs, so its keep-alives pile up behind the flood. Then the host is back, past its timeout.
    Sim_Frames(&client, NULL, 1800);
    FakeNet_Step(600);
    Sim_Frames(&host, NULL, 3 * STEP_MS);
    host.t->ops->stats(host.t, 0, &stats);
    EXPECT(stats.strayDatagrams >= 600, "the flood did not arrive, so this proves nothing");
    EXPECT(!host.flows[0].ended, "a peer timed out while its frames were still waiting to be read");
    Side_Close(&client);
    Side_Close(&host);
    printf("sim: PASS a 2.4 s stall behind 600 stray frames did not end a link with a 2 s timeout\n");
}

// A client accepts only its host. A host accepts anyone, but only a connect request gets anywhere.
static void Sc_Strangers(const Options* o) {
    CoopNetConfig config = { 0 };
    FakeLink link = { 0 };
    Side host;
    Side client;
    CoopSteamBackend* stranger;
    CoopNetStats stats;

    (void) o;
    sSeed = 1;
    link.latencyMs = 10;
    FakeNet_Reset(sSeed, &link, 0);
    Side_Host(&host, 8, &config);
    Side_Client(&client, CLIENT_ID, 8, &config);
    Sim_Connect(&host, &client, 2000);
    stranger = FakeNet_Join(STRANGER_ID);
    Raw_Send(stranger, CLIENT_ID, COOP_PROTOCOL_VERSION, COOP_MSG_TEST_RELIABLE, 1, 0, 0, "abcd", 4, COOP_NET_RELIABLE);
    Raw_Send(stranger, HOST_ID, COOP_PROTOCOL_VERSION, COOP_MSG_TEST_RELIABLE, 1, 0, 0, "abcd", 4, COOP_NET_RELIABLE);
    Sim_Frames(&host, &client, 300);
    EXPECT(FakeNet_Held(CLIENT_ID) == 1, "a client took a message from someone who is not its host");
    EXPECT(FakeNet_Held(HOST_ID) == 0, "a host left a stranger waiting, so a new client could never get in");
    host.t->ops->stats(host.t, 0, &stats);
    EXPECT(stats.strayDatagrams == 1, "the host did not count the stray message");
    EXPECT(host.flows[0].corrupt == 0 && !host.flows[1].connected, "a stranger got through");
    Side_Close(&host);
    Side_Close(&client);
    printf("sim: PASS a stranger reached nobody: held at the client, counted as stray at the host\n");
}

// The same user restarts: a new connection id, and the old link goes at once, without waiting for its timeout.
static void Sc_Restart(const Options* o) {
    CoopNetConfig config = { 0 };
    FakeLink link = { 0 };
    Side host;
    Side client;
    Side again;
    uint32_t elapsed = 0;

    (void) o;
    sSeed = 1;
    config.timeoutMs = 8000;
    link.latencyMs = 10;
    FakeNet_Reset(sSeed, &link, 0);
    Side_Host(&host, 0, &config);
    Side_Client(&client, CLIENT_ID, 0, &config);
    Sim_Connect(&host, &client, 2000);
    EXPECT(Flow_SendReliable(client.t, 0, &client.flows[0]), "a send failed");
    Sim_Frames(&host, &client, 200);
    FakeNet_Kill(CLIENT_ID);
    Side_Close(&client);
    Side_Client(&again, CLIENT_ID, 0, &config);
    while (!host.flows[0].ended) {
        Sim_Frames(&host, &again, STEP_MS);
        elapsed += STEP_MS;
        EXPECT(elapsed < 3000, "the old link stayed");
    }
    EXPECT(host.flows[0].end == COOP_NET_END_TIMEOUT, "a restart ends the old link in a timeout");
    host.flows[0].connected = false;
    host.flows[0].ended = false;
    host.flows[0].reliableReceived = 0;
    Sim_Connect(&host, &again, 3000);
    Sim_Frames(&host, &again, 100);
    EXPECT(host.flows[0].connected, "the host did not report the new link");
    EXPECT(Flow_SendReliable(again.t, 0, &again.flows[0]), "a send failed on the new link");
    Sim_Frames(&host, &again, 200);
    EXPECT(host.flows[0].reliableReceived == 1 && host.flows[0].outOfOrder == 0 && !host.flows[0].ended,
           "the new link does not count from the start");
    Side_Close(&again);
    Side_Close(&host);
    printf("sim: PASS a restarted client replaced its old link after %u ms (timeout 8000 ms)\n", elapsed);
}

// A host that sends the moment it sees CONNECTED. Steam will not resend what the client drops, and unreliable messages
// can overtake the accept, so CONNECTED must still come first and no reliable message may go missing.
static void Sc_EarlyTraffic(const Options* o) {
    uint32_t i;

    for (sSeed = 1; sSeed <= o->seeds; sSeed++) {
        CoopNetConfig config = { 0 };
        FakeLink link = o->link;
        Side host;
        Side client;
        uint32_t elapsed = 0;

        link.reorder = 0.5;
        config.timeoutMs = 8000;
        FakeNet_Reset(sSeed, &link, 0);
        Side_Host(&host, 20, &config);
        Side_Client(&client, CLIENT_ID, 20, &config);
        while (!host.flows[0].connected) {
            Sim_Frames(&host, &client, 1);
            elapsed++;
            EXPECT(elapsed < 8000, "the host never saw the client");
        }
        for (i = 0; i < 20; i++) {
            EXPECT(Flow_SendReliable(host.t, 0, &host.flows[0]) || elapsed < 8000, "no send");
            Flow_SendUnreliable(host.t, 0, &host.flows[0]);
        }
        while (client.flows[0].reliableReceived < host.flows[0].reliableSent) {
            Sim_Frames(&host, &client, STEP_MS);
            elapsed += STEP_MS;
            EXPECT(elapsed < 12000, "reliable messages sent right after the accept went missing");
        }
        EXPECT(host.flows[0].reliableSent != 0, "nothing was sent, so this proves nothing");
        EXPECT(client.flows[0].beforeConnected == 0, "a message came before CONNECTED");
        EXPECT(client.flows[0].outOfOrder == 0 && client.flows[0].connectedEvents == 1, "out of order");
        Side_Close(&host);
        Side_Close(&client);
    }
    printf("sim: PASS CONNECTED came first and nothing went missing on %u seeds\n", o->seeds);
}

// A peer that stops polling: 128 in flight, then send says no, and the rest follows once it polls again.
static void Sc_Window(const Options* o) {
    CoopNetConfig config = { 0 };
    FakeLink link = { 0 };
    CoopNetStats stats;
    Side host;
    Side client;
    uint32_t accepted = 0;
    uint32_t elapsed = 0;

    (void) o;
    sSeed = 1;
    config.timeoutMs = 60000;
    config.pingMs = 100000; // no ping to ride on: the acks must go out by themselves
    link.latencyMs = 10;
    FakeNet_Reset(sSeed, &link, 0);
    Side_Host(&host, 0, &config);
    Side_Client(&client, CLIENT_ID, 0, &config);
    Sim_Connect(&host, &client, 2000);
    Sim_Frames(&host, &client, 100);
    while (accepted < 500 && Flow_SendReliable(host.t, 0, &host.flows[0])) {
        accepted++;
    }
    EXPECT(accepted == 128, "the reliable window is not 128");
    host.t->ops->stats(host.t, 0, &stats);
    EXPECT(stats.pending == 128, "pending does not show the window");
    Sim_Frames(&host, NULL, 500);
    EXPECT(!Flow_SendReliable(host.t, 0, &host.flows[0]), "the window opened while the peer was not polling");
    do {
        Sim_Frames(&host, &client, STEP_MS);
        elapsed += STEP_MS;
        EXPECT(elapsed < 3000, "the window did not open");
        host.t->ops->stats(host.t, 0, &stats);
    } while (stats.pending != 0);
    EXPECT(client.flows[0].reliableReceived == 128 && client.flows[0].outOfOrder == 0, "messages went missing");
    EXPECT(Flow_SendReliable(host.t, 0, &host.flows[0]), "no send after the window opened");
    Side_Close(&host);
    Side_Close(&client);
    printf("sim: PASS window of 128, open again %u ms after the peer polled\n", elapsed);
}

// pending has to mean "not yet arrived": session.c drops a refused client once it reads 0.
static void Sc_Pending(const Options* o) {
    uint32_t i;

    for (sSeed = 1; sSeed <= o->seeds; sSeed++) {
        CoopNetConfig config = { 0 };
        FakeLink link = { 0 };
        CoopNetStats stats;
        Side host;
        Side client;
        uint32_t elapsed = 0;

        config.timeoutMs = 60000;
        link.latencyMs = 40;
        link.loss = 0.3; // acks travel unreliably
        FakeNet_Reset(sSeed, &link, 0);
        Side_Host(&host, 0, &config);
        Side_Client(&client, CLIENT_ID, 0, &config);
        Sim_Connect(&host, &client, 10000);
        for (i = 0; i < 10; i++) {
            EXPECT(Flow_SendReliable(host.t, 0, &host.flows[0]), "a send failed");
        }
        host.t->ops->stats(host.t, 0, &stats);
        EXPECT(stats.pending == 10, "pending must count what has not arrived");
        Sim_Frames(&host, NULL, 500);
        host.t->ops->stats(host.t, 0, &stats);
        EXPECT(stats.pending == 10, "pending fell although the peer has not read anything");
        do {
            Sim_Frames(&host, &client, STEP_MS);
            elapsed += STEP_MS;
            EXPECT(elapsed < 8000, "pending never reached 0: a lost ack must be repeated");
            host.t->ops->stats(host.t, 0, &stats);
        } while (stats.pending != 0);
        EXPECT(client.flows[0].reliableReceived == 10, "pending reached 0 before everything had arrived");
        Side_Close(&host);
        Side_Close(&client);
    }
    printf("sim: PASS pending follows what arrived, with 30 %% of the acks lost, on %u seeds\n", o->seeds);
}

static void Sc_Limits(const Options* o) {
    CoopNetConfig config = { 0 };
    FakeLink link = { 0 };
    CoopNetStats stats;
    Side host;
    Side client;
    uint8_t big[COOP_NET_MAX_MESSAGE + 1] = { 0 };

    (void) o;
    sSeed = 1;
    link.latencyMs = 10;
    FakeNet_Reset(sSeed, &link, 0);
    Side_Host(&host, 0, &config);
    Side_Client(&client, CLIENT_ID, 0, &config);
    EXPECT(!client.t->ops->send(client.t, 0, COOP_MSG_TEST_DONE, NULL, 0, COOP_NET_RELIABLE), "sent before connected");
    Sim_Connect(&host, &client, 2000);
    Sim_Frames(&host, &client, 100);
    EXPECT(client.t->ops->send(client.t, 0, COOP_MSG_TEST_DONE, big, COOP_NET_MAX_MESSAGE, COOP_NET_RELIABLE),
           "the largest message was refused");
    EXPECT(client.t->ops->send(client.t, 0, COOP_MSG_TEST_DONE, big, COOP_NET_MAX_MESSAGE, COOP_NET_UNRELIABLE),
           "the largest unreliable message was refused");
    EXPECT(!client.t->ops->send(client.t, 0, COOP_MSG_TEST_DONE, big, COOP_NET_MAX_MESSAGE + 1, COOP_NET_RELIABLE),
           "a message over the limit was sent");
    EXPECT(!client.t->ops->send(client.t, 0, COOP_MSG_PING, NULL, 0, COOP_NET_RELIABLE), "a transport id was sent");
    EXPECT(!client.t->ops->send(client.t, 1, COOP_MSG_TEST_DONE, NULL, 0, COOP_NET_RELIABLE), "sent to a bad peer");
    EXPECT(!client.t->ops->send(client.t, -1, COOP_MSG_TEST_DONE, NULL, 0, COOP_NET_RELIABLE), "sent to a bad peer");
    EXPECT(!host.t->ops->send(host.t, 2, COOP_MSG_TEST_DONE, NULL, 0, COOP_NET_RELIABLE), "sent to a free slot");
    EXPECT(!host.t->ops->stats(host.t, 3, &stats) && !host.t->ops->stats(host.t, -1, &stats), "stats of a bad peer");
    host.t->ops->disconnect(host.t, 9);
    host.t->ops->disconnect(host.t, -1);
    Sim_Frames(&host, &client, 200);
    EXPECT(host.flows[0].doneReceived && host.flows[0].corrupt == 0, "the largest message did not arrive");
    Side_Close(&host);
    Side_Close(&client);
    EXPECT(CoopNetSteam_Connect(NULL, HOST_ID, &config) == NULL && CoopNetSteam_Host(NULL, &config) == NULL,
           "a transport without a backend");
    EXPECT(CoopNetSteam_Connect(FakeNet_Join(STRANGER_ID), STRANGER_ID, &config) == NULL, "connected to ourselves");
    EXPECT(CoopNetSteam_Connect(FakeNet_Join(STRANGER_ID), 0, &config) == NULL, "connected to nobody");
    printf("sim: PASS limits as on UDP: largest message passes, the rest is refused\n");
}

// A local disconnect: no event here, one at the peer, and the peer can come back.
static void Sc_Disconnect(const Options* o) {
    CoopNetConfig config = { 0 };
    FakeLink link = { 0 };
    Side host;
    Side client;
    Side again;

    (void) o;
    sSeed = 1;
    link.latencyMs = 10;
    FakeNet_Reset(sSeed, &link, 0);
    Side_Host(&host, 0, &config);
    Side_Client(&client, CLIENT_ID, 0, &config);
    Sim_Connect(&host, &client, 2000);
    EXPECT(Flow_SendReliable(host.t, 0, &host.flows[0]), "a send failed");
    host.t->ops->disconnect(host.t, 0);
    EXPECT(!host.t->ops->send(host.t, 0, COOP_MSG_TEST_DONE, NULL, 0, COOP_NET_RELIABLE), "sent to a dropped peer");
    Sim_Frames(&host, &client, 500);
    EXPECT(host.flows[0].endEvents == 0, "an event followed a local disconnect");
    EXPECT(client.flows[0].end == COOP_NET_END_CLOSED && client.flows[0].endEvents == 1, "the goodbye did not arrive");
    EXPECT(client.flows[0].reliableReceived == 1, "the message before the goodbye did not arrive");
    Side_Close(&client);
    host.flows[0].connected = false;
    Side_Client(&again, CLIENT_ID, 0, &config);
    Sim_Connect(&host, &again, 3000);
    Side_Close(&again);
    Side_Close(&host);
    printf("sim: PASS a goodbye reached the peer, no event at home, and the peer got back in\n");
}

// data stays valid until the next poll, whatever else is called in between (--asan sees it if not).
static void Sc_Lifetime(const Options* o) {
    CoopNetConfig config = { 0 };
    FakeLink link = { 0 };
    CoopNetEvent ev;
    CoopNetStats stats;
    Side host;
    Side client;
    bool got = false;
    uint32_t elapsed = 0;

    (void) o;
    sSeed = 1;
    link.latencyMs = 10;
    FakeNet_Reset(sSeed, &link, 0);
    Side_Host(&host, 0, &config);
    Side_Client(&client, CLIENT_ID, 0, &config);
    Sim_Connect(&host, &client, 2000);
    Flow_SendReliable(client.t, 0, &client.flows[0]);
    while (!got) {
        FakeNet_Step(STEP_MS);
        elapsed += STEP_MS;
        EXPECT(elapsed < 2000, "the message did not arrive");
        while (host.t->ops->poll(host.t, &ev)) {
            if (ev.type == COOP_NET_EVENT_MESSAGE) {
                uint32_t index;

                EXPECT(Pattern_Check(ev.data, ev.size, &index), "the message was gone before the caller read it");
                host.t->ops->send(host.t, 0, COOP_MSG_TEST_DONE, NULL, 0, COOP_NET_RELIABLE);
                host.t->ops->stats(host.t, 0, &stats);
                EXPECT(Pattern_Check(ev.data, ev.size, &index), "the message changed before the next poll");
                got = true;
            }
        }
    }
    // Closing with a message still held must give it back.
    Flow_SendReliable(client.t, 0, &client.flows[0]);
    Sim_Frames(&client, NULL, 100);
    while (host.t->ops->poll(host.t, &ev) && ev.type != COOP_NET_EVENT_MESSAGE) {}
    EXPECT(ev.type == COOP_NET_EVENT_MESSAGE, "the second message did not arrive");
    Side_Close(&host);
    Side_Close(&client);
    Sim_CheckReleased();
    printf("sim: PASS a message stays valid until the next poll, and close gives back the last one\n");
}

static void Sc_ConnectTimeout(const Options* o) {
    static const struct {
        uint32_t configMs;
        uint32_t sessionMs;
        uint32_t lowMs;
        uint32_t highMs;
    } kCases[] = {
        { 3000, 60000, 2900, 3300 }, // nobody there, and Steam would wait longer: our limit
        { 0, 60000, 19900, 20300 },  // the same with the default
        { 0, 5000, 4900, 5300 },     // Steam gives up first
    };
    uint32_t i;

    (void) o;
    sSeed = 1;
    for (i = 0; i < 3; i++) {
        CoopNetConfig config = { 0 };
        FakeLink link = { 0 };
        Side client;
        uint32_t elapsed = 0;

        config.timeoutMs = kCases[i].configMs;
        link.latencyMs = 10;
        link.sessionTimeoutMs = kCases[i].sessionMs;
        FakeNet_Reset(sSeed, &link, 0);
        Side_Client(&client, CLIENT_ID, 0, &config);
        while (!client.flows[0].ended) {
            Sim_Frames(&client, NULL, STEP_MS);
            elapsed += STEP_MS;
            EXPECT(elapsed <= kCases[i].highMs, "connecting to nobody took too long");
        }
        EXPECT(elapsed >= kCases[i].lowMs, "gave up too early");
        EXPECT(!client.flows[0].connected && client.flows[0].end == COOP_NET_END_TIMEOUT, "must end in a timeout");
        Side_Close(&client);
    }
    printf("sim: PASS a host that is not there ends in a timeout, by our limit or by Steam's\n");
}

// Finding a route can take longer than the keep-alive timeout. It must not count against a connection attempt, and
// the requests repeated meanwhile must make one connection.
static void Sc_SlowSetup(const Options* o) {
    CoopNetConfig config = { 0 };
    FakeLink link = { 0 };
    Side host;
    Side client;
    uint32_t took;

    (void) o;
    sSeed = 1;
    link.latencyMs = 30;
    link.setupMs = 8000;
    FakeNet_Reset(sSeed, &link, 0);
    Side_Host(&host, 0, &config);
    Side_Client(&client, CLIENT_ID, 0, &config);
    took = Sim_Connect(&host, &client, 12000);
    EXPECT(took >= 7900, "connected before the session was up");
    Sim_Frames(&host, &client, 7000);
    EXPECT(host.flows[0].connectedEvents == 1 && client.flows[0].connectedEvents == 1, "connected more than once");
    EXPECT(!host.flows[1].connected, "repeated requests took a second slot");
    EXPECT(!host.flows[0].ended && !client.flows[0].ended, "the connection did not hold");
    Side_Close(&host);
    Side_Close(&client);
    printf("sim: PASS connected after %u ms of route finding, default timeouts, one connection\n", took);
}

// ---- The session on this transport ----

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

static void Sc_Session(const Options* o, bool match) {
    for (sSeed = 1; sSeed <= o->seeds; sSeed++) {
        CoopSteamBackend* hostBackend;
        CoopSteamBackend* clientBackend;
        CoopSession* host;
        CoopSession* client;
        CoopIdentity hostId;
        CoopIdentity clientId;
        CoopSessionEvent ev;
        uint32_t elapsed = 0;
        bool hostJoined = false;
        bool clientJoined = false;
        bool hostRefused = false;
        bool clientRefused = false;
        bool clientEnded = false;

        FakeNet_Reset(sSeed, &o->link, 0);
        Id_Default(&hostId);
        Id_Default(&clientId);
        if (!match) {
            snprintf(clientId.build, sizeof(clientId.build), "other-build");
        }
        hostBackend = FakeNet_Join(HOST_ID);
        clientBackend = FakeNet_Join(CLIENT_ID);
        host = CoopSession_Host(CoopNetSteam_Host(hostBackend, NULL), &hostId);
        client = CoopSession_Join(CoopNetSteam_Connect(clientBackend, HOST_ID, NULL), &clientId, FakeNet_Now());
        EXPECT(host != NULL && client != NULL, "no session");

        while (match ? !(hostJoined && clientJoined) : !(hostRefused && clientRefused && clientEnded)) {
            FakeNet_Step(STEP_MS);
            elapsed += STEP_MS;
            EXPECT(elapsed < 15000, "the handshake did not finish");
            while (CoopSession_Poll(host, FakeNet_Now(), &ev)) {
                hostJoined = hostJoined || ev.type == COOP_SESSION_EV_JOINED;
                hostRefused = hostRefused || ev.type == COOP_SESSION_EV_REFUSED;
            }
            while (CoopSession_Poll(client, FakeNet_Now(), &ev)) {
                clientJoined = clientJoined || ev.type == COOP_SESSION_EV_JOINED;
                if (ev.type == COOP_SESSION_EV_REFUSED) {
                    clientRefused = true;
                    EXPECT(ev.text != NULL && ev.text[0] != '\0', "a refusal without a text");
                    EXPECT(ev.end == COOP_SESSION_END_REFUSED, "refused for the wrong reason");
                }
                if (ev.type == COOP_SESSION_EV_ENDED) {
                    clientEnded = true;
                    EXPECT(match || ev.end == COOP_SESSION_END_REFUSED, "ended for the wrong reason");
                }
            }
        }
        EXPECT(!match || !clientEnded, "a matching client ended");
        CoopSession_Close(client);
        CoopSession_Close(host);
        Sim_CheckReleased();
    }
    printf("sim: PASS session %s through Steam on %u seeds\n", match ? "match" : "refusal", o->seeds);
}

static void Sc_SessionMatch(const Options* o) {
    Sc_Session(o, true);
}

static void Sc_SessionRefused(const Options* o) {
    Sc_Session(o, false);
}

static const struct {
    const char* name;
    void (*run)(const Options* o);
} kScenarios[] = {
    { "profile", Sc_Profile },
    { "sim", Sc_Sim },
    { "wrap", Sc_Wrap },
    { "outage", Sc_Outage },
    { "two_clients", Sc_TwoClients },
    { "three_clients", Sc_ThreeClients },
    { "send_buffer", Sc_SendBuffer },
    { "blackout_timeout", Sc_BlackoutTimeout },
    { "session_failed", Sc_SessionFailed },
    { "peer_closed", Sc_PeerClosed },
    { "kill_client", Sc_KillClient },
    { "clean_close", Sc_CleanClose },
    { "lost_goodbye", Sc_LostGoodbye },
    { "host_full", Sc_HostFull },
    { "version", Sc_Version },
    { "gap", Sc_Gap },
    { "duplicates", Sc_Duplicates },
    { "junk", Sc_Junk },
    { "bad_peer", Sc_BadPeer },
    { "strangers", Sc_Strangers },
    { "restart", Sc_Restart },
    { "early_traffic", Sc_EarlyTraffic },
    { "window", Sc_Window },
    { "pending", Sc_Pending },
    { "limits", Sc_Limits },
    { "disconnect", Sc_Disconnect },
    { "lifetime", Sc_Lifetime },
    { "connect_timeout", Sc_ConnectTimeout },
    { "slow_setup", Sc_SlowSetup },
    { "session_match", Sc_SessionMatch },
    { "session_refused", Sc_SessionRefused },
    { "broken_after_data", Sc_BrokenAfterData },
    { "failed_after_data", Sc_FailedAfterData },
    { "stall", Sc_Stall },
    { "bad_frames", Sc_BadFrames },
    { "acks", Sc_Acks },
    { "accept_busy", Sc_AcceptBusy },
    { "quiet", Sc_Quiet },
};

static int Sim_Main(const char* name, const Options* o) {
    size_t i;

    for (i = 0; i < sizeof(kScenarios) / sizeof(kScenarios[0]); i++) {
        if (strcmp(kScenarios[i].name, name) == 0) {
            kScenarios[i].run(o);
            FakeNet_Free();
            return 0;
        }
    }
    printf("sim: FAIL no scenario %s\n", name);
    return 2;
}

// ---- Real processes on a Steam library ----

typedef struct {
    const char* connect;
    const char* library;
    const char* idFile;
    const char* out;
    uint32_t peers;
    uint32_t reliable;
    uint32_t unreliable;
    uint32_t rate;
    uint32_t timeoutMs;
    uint32_t maxSeconds;
    uint32_t closeAfterMs;
    bool hold;
} LiveOptions;

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

static void Live_WriteResult(const LiveOptions* o, const char* role, CoopSteamId self, const Flow* flows,
                             const CoopNetStats* stats, uint32_t count, uint32_t elapsedMs, bool finished) {
    FILE* f = o->out ? fopen(o->out, "w") : stdout;
    uint32_t i;

    if (f == NULL) {
        return;
    }
    fprintf(f, "{\"role\":\"%s\",\"self\":\"%" PRIu64 "\",\"elapsed_ms\":%u,\"finished\":%s,\"peers\":[", role, self,
            elapsedMs, finished ? "true" : "false");
    for (i = 0; i < count; i++) {
        const Flow* fl = &flows[i];
        const CoopNetStats* s = &stats[i];

        fprintf(f,
                "%s{\"connected\":%s,\"end\":\"%s\",\"reliable_sent\":%u,\"reliable_received\":%u,"
                "\"unreliable_sent\":%u,\"unreliable_received\":%u,\"out_of_order\":%u,\"corrupt\":%u,"
                "\"unreliable_duplicates\":%u,\"done_received\":%s,\"sent\":%u,\"received\":%u,\"pending\":%u,"
                "\"duplicates\":%u,\"rtt_ms\":%u,\"rtt_min_ms\":%u,\"rtt_max_ms\":%u,\"bad_datagrams\":%u,"
                "\"stray_datagrams\":%u}",
                i ? "," : "", fl->connected ? "true" : "false", End_Name(fl->end), fl->reliableSent,
                fl->reliableReceived, fl->unreliableSent, fl->unreliableReceived, fl->outOfOrder, fl->corrupt,
                fl->unreliableDuplicates, fl->doneReceived ? "true" : "false", s->sent, s->received, s->pending,
                s->duplicates, s->rttMs, s->rttMinMs, s->rttMaxMs, s->badDatagrams, s->strayDatagrams);
    }
    fprintf(f, "]}\n");
    if (o->out) {
        fclose(f);
    }
}

static int Live_Main(const LiveOptions* o) {
    char error[1024] = { 0 };
    CoopNetConfig config = { 0 };
    CoopSteamBackend* backend = CoopSteamFlat_Open(o->library, error, sizeof(error));
    CoopTransport* t;
    Flow flows[COOP_NET_MAX_PEERS];
    CoopNetStats stats[COOP_NET_MAX_PEERS];
    CoopSteamId self;
    bool isHost = o->connect == NULL;
    uint32_t count = isHost ? o->peers : 1;
    uint32_t start = SDL_GetTicks();
    uint32_t connectedAt = 0;
    uint32_t finishedAt = 0;
    uint32_t nextSend = start;
    uint32_t interval = 1000 / (o->rate ? o->rate : 60);
    bool finished = false;
    uint32_t i;

    if (backend == NULL) {
        printf("live: ERROR %s\n", error);
        return 2;
    }
    self = backend->ops->self(backend);
    printf("live: my SteamID is %" PRIu64 "\n", self);
    fflush(stdout);
    config.timeoutMs = o->timeoutMs;
    config.maxPeers = (uint8_t) o->peers;
    memset(stats, 0, sizeof(stats));
    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        Flow_Init(&flows[i], o->unreliable);
    }
    if (isHost) {
        t = CoopNetSteam_Host(backend, &config);
        if (o->idFile != NULL) {
            FILE* f = fopen(o->idFile, "w");

            if (f != NULL) {
                fprintf(f, "%" PRIu64 "\n", self);
                fclose(f);
            }
        }
    } else {
        t = CoopNetSteam_Connect(backend, strtoull(o->connect, NULL, 10), &config);
    }
    if (t == NULL) {
        printf("live: ERROR no transport: the SteamID to connect to is empty, or your own\n");
        return 2;
    }

    for (;;) {
        CoopNetEvent ev;
        uint32_t now = SDL_GetTicks();
        uint32_t ended = 0;
        uint32_t done = 0;

        while (t->ops->poll(t, &ev)) {
            Flow* f = &flows[ev.peer];

            Flow_OnEvent(f, &ev);
            if (ev.type == COOP_NET_EVENT_CONNECTED) {
                connectedAt = now;
            } else if (ev.type == COOP_NET_EVENT_DISCONNECTED) {
                t->ops->stats(t, ev.peer, &stats[ev.peer]);
            }
        }
        if ((int32_t) (now - nextSend) >= 0) {
            nextSend = now + interval;
            for (i = 0; i < count; i++) {
                if (flows[i].connected && !flows[i].ended && !o->hold) {
                    Flow_SendNext(t, (int) i, &flows[i], o->reliable, o->unreliable);
                }
            }
        }
        for (i = 0; i < count; i++) {
            if (flows[i].ended) {
                ended++;
            } else if (flows[i].connected) {
                t->ops->stats(t, (int) i, &stats[i]);
                done += flows[i].doneSent && flows[i].doneReceived && stats[i].pending == 0 &&
                        flows[i].unreliableSent == o->unreliable;
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
        if ((finishedAt != 0 && now - finishedAt > 1000) ||
            (o->closeAfterMs != 0 && connectedAt != 0 && now - connectedAt > o->closeAfterMs)) {
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
    Live_WriteResult(o, isHost ? "host" : "client", self, flows, stats, count, SDL_GetTicks() - start, finished);
    t->ops->close(t);
    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        free(flows[i].seen);
    }
    return 0;
}

static int Open_Main(const char* library) {
    char error[1024] = { 0 };
    CoopSteamBackend* backend = CoopSteamFlat_Open(library, error, sizeof(error));

    if (backend == NULL) {
        printf("open: ERROR %s\n", error);
        return 1;
    }
    printf("open: OK %" PRIu64 "\n", backend->ops->self(backend));
    backend->ops->destroy(backend);
    return 0;
}

int main(int argc, char** argv) {
    Options o;
    LiveOptions live;
    const char* scenario = NULL;
    int i;

    memset(&o, 0, sizeof(o));
    memset(&live, 0, sizeof(live));
    o.seeds = 20;
    o.direction = "both";
    live.peers = 1;
    live.rate = 60;
    live.reliable = 600;
    live.unreliable = 600;
    live.maxSeconds = 120;
    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc < 2) {
        printf("usage: coop_steam_test sim|open|live ..., see the top of coop_steam_test.c\n");
        return 2;
    }
    for (i = 2; i < argc; i++) {
        const char* a = argv[i];
        const char* v = i + 1 < argc ? argv[i + 1] : "";

        if (strcmp(a, "host") == 0 || strcmp(a, "client") == 0) {
            continue;
        } else if (strcmp(a, "--hold") == 0) {
            live.hold = true;
            continue;
        }
        i++;
        if (strcmp(a, "--scenario") == 0) {
            scenario = v;
        } else if (strcmp(a, "--seeds") == 0) {
            o.seeds = (uint32_t) atoi(v);
        } else if (strcmp(a, "--direction") == 0) {
            o.direction = v;
        } else if (strcmp(a, "--latency-ms") == 0) {
            o.link.latencyMs = (uint32_t) atof(v);
        } else if (strcmp(a, "--jitter-ms") == 0) {
            o.link.jitterMs = (uint32_t) atof(v);
        } else if (strcmp(a, "--loss") == 0) {
            o.link.loss = atof(v);
        } else if (strcmp(a, "--burst-enter") == 0) {
            o.link.burstEnter = atof(v);
        } else if (strcmp(a, "--burst-exit") == 0) {
            o.link.burstExit = atof(v);
        } else if (strcmp(a, "--burst-loss") == 0) {
            o.link.burstLoss = atof(v);
        } else if (strcmp(a, "--reorder") == 0) {
            o.link.reorder = atof(v);
        } else if (strcmp(a, "--duplicate") == 0) {
            o.link.duplicate = atof(v);
        } else if (strcmp(a, "--library") == 0) {
            live.library = v;
        } else if (strcmp(a, "--connect") == 0) {
            live.connect = v;
        } else if (strcmp(a, "--id-file") == 0) {
            live.idFile = v;
        } else if (strcmp(a, "--out") == 0) {
            live.out = v;
        } else if (strcmp(a, "--peers") == 0) {
            live.peers = (uint32_t) atoi(v);
        } else if (strcmp(a, "--reliable") == 0) {
            live.reliable = (uint32_t) atoi(v);
        } else if (strcmp(a, "--unreliable") == 0) {
            live.unreliable = (uint32_t) atoi(v);
        } else if (strcmp(a, "--rate") == 0) {
            live.rate = (uint32_t) atoi(v);
        } else if (strcmp(a, "--timeout-ms") == 0) {
            live.timeoutMs = (uint32_t) atoi(v);
        } else if (strcmp(a, "--max-seconds") == 0) {
            live.maxSeconds = (uint32_t) atoi(v);
        } else if (strcmp(a, "--close-after-ms") == 0) {
            live.closeAfterMs = (uint32_t) atoi(v);
        } else {
            printf("unknown option %s\n", a);
            return 2;
        }
    }

    if (strcmp(argv[1], "sim") == 0) {
        return scenario != NULL ? Sim_Main(scenario, &o) : 2;
    }
    if (strcmp(argv[1], "open") == 0) {
        return Open_Main(live.library);
    }
    if (strcmp(argv[1], "live") == 0) {
        return Live_Main(&live);
    }
    printf("unknown mode %s\n", argv[1]);
    return 2;
}
