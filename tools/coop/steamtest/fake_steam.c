// The fake Steam of the transport tests, see fake_steam.h.

#include "fake_steam.h"

#include <stdlib.h>
#include <string.h>

#define DEFAULT_SESSION_TIMEOUT_MS 10000
#define DEFAULT_SEND_BUFFER 524288
#define MAX_RESENDS 12
#define MAX_EVENTS 64

typedef struct FakePacket {
    struct FakePacket* next;
    struct FakePacket* prev;
    uint32_t due;
    int from;
    int to;
    uint32_t generation; // which run of the sending program
    bool reliable;
    bool closes; // no data: the sender closed the session
    bool waited; // sat out an outage
    uint32_t size;
    uint8_t data[1];
} FakePacket;

typedef struct {
    bool open;     // we sent, or accepted
    bool accepted; // what the peer sends reaches us
    bool requested;
    bool broken;
    uint32_t peerGeneration;
    uint32_t establishedAt;
    uint32_t lastReliableDue;
    uint32_t queuedBytes;
    uint32_t goodAt; // last time the link to the peer worked
} FakeSession;

typedef struct {
    CoopSteamBackend base;
    bool used;
    bool alive;
    bool crashed;
    int index;
    uint32_t generation;
    CoopSteamId id;
    FakeSession sessions[FAKE_MAX_ENDPOINTS];
    uint32_t outageUntil[FAKE_MAX_ENDPOINTS]; // towards that endpoint
    bool outage[FAKE_MAX_ENDPOINTS];
    FakePacket* inboxHead;
    FakePacket* inboxTail;
    FakePacket* heldHead;
    FakePacket* heldTail;
    CoopSteamEvent events[MAX_EVENTS];
    uint32_t eventHead;
    uint32_t eventCount;
    uint32_t busyNext; // reliable sends still to refuse
} FakeEndpoint;

static struct {
    FakeLink link;
    FakeCounters counters;
    uint32_t now;
    uint64_t rng;
    bool burst;
    FakePacket* head;
    FakePacket* tail;
    FakeEndpoint endpoints[FAKE_MAX_ENDPOINTS];
} sNet;

uint32_t FakeNet_Random(void) {
    // SplitMix64
    uint64_t z = (sNet.rng += 0x9E3779B97F4A7C15ull);

    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return (uint32_t) ((z ^ (z >> 31)) >> 32);
}

// memset through a pointer the compiler cannot see through: a plain memset just before free is a dead store, which
// an optimised build leaves out (GCC -O2 did), and then a message read after its release still holds its bytes (#158).
static void* (*volatile sPoison)(void*, int, size_t) = memset;

static bool Fake_Chance(double chance) {
    return (FakeNet_Random() >> 8) / (double) (1u << 24) < chance;
}

static bool Fake_Before(uint32_t a, uint32_t b) {
    return (int32_t) (a - b) < 0;
}

static bool Fake_Lost(void) {
    if (Fake_Chance(sNet.burst ? sNet.link.burstExit : sNet.link.burstEnter)) {
        sNet.burst = !sNet.burst;
    }
    return Fake_Chance(sNet.burst ? sNet.link.burstLoss : sNet.link.loss);
}

static uint32_t Fake_Delay(void) {
    return sNet.link.latencyMs + (sNet.link.jitterMs ? FakeNet_Random() % (sNet.link.jitterMs + 1) : 0);
}

static void Fake_Unlink(FakePacket* p) {
    if (p->prev != NULL) {
        p->prev->next = p->next;
    } else {
        sNet.head = p->next;
    }
    if (p->next != NULL) {
        p->next->prev = p->prev;
    } else {
        sNet.tail = p->prev;
    }
    p->next = NULL;
    p->prev = NULL;
}

// In order of due time; equal times stay in the order they came, which keeps a reliable stream in order.
static void Fake_Queue(FakePacket* p) {
    FakePacket* after = sNet.tail;

    while (after != NULL && Fake_Before(p->due, after->due)) {
        after = after->prev;
    }
    p->prev = after;
    p->next = after != NULL ? after->next : sNet.head;
    if (p->next != NULL) {
        p->next->prev = p;
    } else {
        sNet.tail = p;
    }
    if (after != NULL) {
        after->next = p;
    } else {
        sNet.head = p;
    }
}

static FakePacket* Fake_NewPacket(int from, int to, const void* data, uint32_t size, bool reliable, uint32_t due) {
    FakePacket* p = calloc(1, sizeof(FakePacket) + size);

    p->from = from;
    p->to = to;
    p->generation = sNet.endpoints[from].generation;
    p->reliable = reliable;
    p->size = size;
    p->due = due;
    if (size != 0) {
        memcpy(p->data, data, size);
    }
    return p;
}

static void Fake_Append(FakePacket** head, FakePacket** tail, FakePacket* p) {
    p->next = NULL;
    if (*tail != NULL) {
        (*tail)->next = p;
    } else {
        *head = p;
    }
    *tail = p;
}

static void Fake_FreeList(FakePacket** head, FakePacket** tail) {
    while (*head != NULL) {
        FakePacket* next = (*head)->next;

        free(*head);
        *head = next;
    }
    *tail = NULL;
}

static void Fake_Post(FakeEndpoint* e, CoopSteamEventType type, CoopSteamId peer) {
    if (e->eventCount < MAX_EVENTS) {
        CoopSteamEvent* ev = &e->events[(e->eventHead + e->eventCount) % MAX_EVENTS];

        ev->type = type;
        ev->peer = peer;
        e->eventCount++;
    }
}

// A restart clears the sender's sessions while its old messages are still on their way.
static void Fake_Unqueue(FakeSession* s, const FakePacket* p) {
    if (p->reliable) {
        s->queuedBytes -= p->size < s->queuedBytes ? p->size : s->queuedBytes;
    }
}

// Drops what is on its way from one to the other. A session's end takes its queue with it.
static void Fake_DropInFlight(int from, int to) {
    FakePacket* p = sNet.head;

    while (p != NULL) {
        FakePacket* next = p->next;

        if (p->from == from && p->to == to && !p->closes) {
            Fake_Unqueue(&sNet.endpoints[from].sessions[to], p);
            Fake_Unlink(p);
            free(p);
        }
        p = next;
    }
}

static int Fake_Slot(CoopSteamId id) {
    int i;

    for (i = 0; i < FAKE_MAX_ENDPOINTS; i++) {
        if (sNet.endpoints[i].used && sNet.endpoints[i].id == id) {
            return i;
        }
    }
    for (i = 0; i < FAKE_MAX_ENDPOINTS; i++) {
        if (!sNet.endpoints[i].used) {
            sNet.endpoints[i].used = true;
            sNet.endpoints[i].id = id;
            sNet.endpoints[i].index = i;
            return i;
        }
    }
    abort();
}

static void Fake_ReleaseHeld(FakeEndpoint* e, int from) {
    FakePacket* p = e->heldHead;
    FakePacket* keepHead = NULL;
    FakePacket* keepTail = NULL;

    while (p != NULL) {
        FakePacket* next = p->next;

        if (p->from == from) {
            Fake_Append(&e->inboxHead, &e->inboxTail, p);
        } else {
            Fake_Append(&keepHead, &keepTail, p);
        }
        p = next;
    }
    e->heldHead = keepHead;
    e->heldTail = keepTail;
}

static void Fake_DropHeld(FakeEndpoint* e, int from) {
    FakePacket* p = e->heldHead;
    FakePacket* keepHead = NULL;
    FakePacket* keepTail = NULL;

    while (p != NULL) {
        FakePacket* next = p->next;

        if (p->from == from) {
            free(p);
        } else {
            Fake_Append(&keepHead, &keepTail, p);
        }
        p = next;
    }
    e->heldHead = keepHead;
    e->heldTail = keepTail;
}

static FakeSession* Fake_Open(FakeEndpoint* e, int peer) {
    FakeSession* s = &e->sessions[peer];

    if (!s->open) {
        s->open = true;
        s->establishedAt = sNet.now + (s->requested ? 0 : sNet.link.setupMs); // the one who starts pays for the route
        s->lastReliableDue = sNet.now;
        s->goodAt = sNet.now;
    }
    if (!s->accepted) {
        s->accepted = true;
        s->requested = false;
        Fake_ReleaseHeld(e, peer);
    }
    return s;
}

// Whether the link is down at that moment, the last moment of an outage included. A step can run past the end of an
// outage, so messages go by their own time.
static bool Fake_BlockedAt(const FakeEndpoint* from, int to, uint32_t when) {
    return from->outage[to] && !Fake_Before(from->outageUntil[to], when);
}

static bool Fake_Blocked(const FakeEndpoint* from, int to) {
    return Fake_BlockedAt(from, to, sNet.now);
}

static void Fake_Arrive(FakePacket* p) {
    FakeEndpoint* to = &sNet.endpoints[p->to];
    FakeEndpoint* from = &sNet.endpoints[p->from];
    FakeSession* s = &to->sessions[p->from];

    Fake_Unqueue(&from->sessions[p->to], p);
    if (!to->alive) {
        free(p);
        return;
    }
    if (p->closes) {
        // Nothing tells the program: "There is no notification when a peer proactively closes the session."
        if (s->open && (s->peerGeneration == 0 || s->peerGeneration == p->generation)) {
            s->broken = true;
        }
        free(p);
        return;
    }
    // A new connection from a program that restarted, or from one we were broken with, is a new session.
    if (s->broken || (s->peerGeneration != 0 && s->peerGeneration != p->generation)) {
        memset(s, 0, sizeof(*s));
    }
    s->peerGeneration = p->generation;
    if (s->accepted) {
        Fake_Append(&to->inboxHead, &to->inboxTail, p);
        return;
    }
    Fake_Append(&to->heldHead, &to->heldTail, p);
    if (!s->requested) {
        s->requested = true;
        sNet.counters.sessionRequests++;
        Fake_Post(to, COOP_STEAM_SESSION_REQUEST, from->id);
    }
}

static void Fake_Deliver(void) {
    while (sNet.head != NULL && !Fake_Before(sNet.now, sNet.head->due)) {
        FakePacket* p = sNet.head;
        FakeEndpoint* from = &sNet.endpoints[p->from];

        Fake_Unlink(p);
        if (!p->waited && Fake_BlockedAt(from, p->to, p->due)) {
            if (p->reliable || p->closes) {
                // Steam keeps sending, and the first copy after the outage arrives. Everything due up to the end comes
                // through here in queue order, so a reliable stream stays in order.
                p->due = from->outageUntil[p->to];
                p->waited = true;
                Fake_Queue(p);
            } else {
                sNet.counters.unreliableDropped++;
                free(p);
            }
            continue;
        }
        Fake_Arrive(p);
    }
}

static void Fake_FailSession(FakeEndpoint* e, int peer) {
    FakeSession* s = &e->sessions[peer];

    if (s->open && !s->broken) {
        s->broken = true;
        sNet.counters.sessionFailures++;
        Fake_DropInFlight(e->index, peer);
        Fake_Post(e, COOP_STEAM_SESSION_FAILED, sNet.endpoints[peer].id);
    }
}

// Steam's own keep-alive: a link that does not work ends the session, whatever the program above it does.
static void Fake_CheckSessions(void) {
    uint32_t limit = sNet.link.sessionTimeoutMs ? sNet.link.sessionTimeoutMs : DEFAULT_SESSION_TIMEOUT_MS;
    int i;
    int j;

    for (i = 0; i < FAKE_MAX_ENDPOINTS; i++) {
        FakeEndpoint* e = &sNet.endpoints[i];

        for (j = 0; e->alive && j < FAKE_MAX_ENDPOINTS; j++) {
            FakeEndpoint* peer = &sNet.endpoints[j];
            FakeSession* s = &e->sessions[j];

            if (!s->open || s->broken) {
                continue;
            }
            if (peer->alive && !Fake_Blocked(e, j) && !Fake_Blocked(peer, i)) {
                s->goodAt = sNet.now;
            } else if (sNet.now - s->goodAt > limit) {
                Fake_FailSession(e, j);
            }
        }
    }
}

void FakeNet_Step(uint32_t ms) {
    sNet.now += ms;
    Fake_Deliver();
    Fake_CheckSessions();
}

static CoopSteamId Fake_Self(CoopSteamBackend* b) {
    return ((FakeEndpoint*) b)->id;
}

static uint32_t Fake_NowMs(CoopSteamBackend* b) {
    (void) b;
    return sNet.now;
}

static uint32_t Fake_Nonce(CoopSteamBackend* b) {
    (void) b;
    return FakeNet_Random();
}

static CoopSteamSendResult Fake_Send(CoopSteamBackend* b, CoopSteamId to, const void* data, uint32_t size,
                                     CoopNetLane lane) {
    FakeEndpoint* e = (FakeEndpoint*) b;
    uint32_t bufferLimit = sNet.link.sendBufferBytes ? sNet.link.sendBufferBytes : DEFAULT_SEND_BUFFER;
    int peer;
    FakeSession* s;
    uint32_t delay;

    if (!e->alive) {
        return COOP_STEAM_SENT; // a killed program whose transport is being closed
    }
    if (size > COOP_NET_MAX_DATAGRAM) {
        sNet.counters.oversize++;
    }
    peer = Fake_Slot(to);
    s = &e->sessions[peer];
    if (s->broken) {
        sNet.counters.broken++;
        return COOP_STEAM_BROKEN;
    }
    if (lane == COOP_NET_RELIABLE && e->busyNext != 0) {
        e->busyNext--;
        sNet.counters.busy++;
        return COOP_STEAM_BUSY;
    }
    if (lane == COOP_NET_RELIABLE && s->queuedBytes + size > bufferLimit) {
        sNet.counters.busy++;
        return COOP_STEAM_BUSY;
    }
    s = Fake_Open(e, peer);

    if (lane == COOP_NET_RELIABLE) {
        uint32_t resends = 0;
        uint32_t due;

        sNet.counters.reliableSent++;
        delay = Fake_Delay();
        while (resends < MAX_RESENDS && Fake_Lost()) {
            delay += 2 * sNet.link.latencyMs + 30;
            resends++;
        }
        sNet.counters.reliableResent += resends;
        due = sNet.now + delay;
        if (Fake_Before(due, s->establishedAt)) {
            due = s->establishedAt;
        }
        if (Fake_Before(due, s->lastReliableDue)) {
            due = s->lastReliableDue;
        }
        s->lastReliableDue = due;
        s->queuedBytes += size;
        Fake_Queue(Fake_NewPacket(e->index, peer, data, size, true, due));
        return COOP_STEAM_SENT;
    }

    sNet.counters.unreliableSent++;
    if (Fake_Lost()) {
        sNet.counters.unreliableDropped++;
        return COOP_STEAM_SENT;
    }
    {
        int copies = Fake_Chance(sNet.link.duplicate) ? 2 : 1;
        int i;

        sNet.counters.unreliableDuplicated += copies - 1;
        for (i = 0; i < copies; i++) {
            uint32_t due = sNet.now + (Fake_Chance(sNet.link.reorder) ? 0 : Fake_Delay());

            if (Fake_Before(due, s->establishedAt)) {
                due = s->establishedAt;
            }
            Fake_Queue(Fake_NewPacket(e->index, peer, data, size, false, due));
        }
    }
    return COOP_STEAM_SENT;
}

static bool Fake_Receive(CoopSteamBackend* b, CoopSteamMessage* out) {
    FakeEndpoint* e = (FakeEndpoint*) b;
    FakePacket* p = e->inboxHead;

    if (p == NULL) {
        return false;
    }
    e->inboxHead = p->next;
    if (e->inboxHead == NULL) {
        e->inboxTail = NULL;
    }
    p->next = NULL;
    out->from = sNet.endpoints[p->from].id;
    out->data = p->data;
    out->size = p->size;
    out->handle = p;
    sNet.counters.received++;
    return true;
}

static void Fake_Release(CoopSteamBackend* b, CoopSteamMessage* message) {
    FakePacket* p = message->handle;

    (void) b;
    sPoison(p->data, 0xDD, p->size); // whoever keeps reading this after release gets garbage, in any build
    free(p);
    message->handle = NULL;
    message->data = NULL;
    sNet.counters.released++;
}

static bool Fake_Event(CoopSteamBackend* b, CoopSteamEvent* out) {
    FakeEndpoint* e = (FakeEndpoint*) b;

    if (e->eventCount == 0) {
        return false;
    }
    *out = e->events[e->eventHead];
    e->eventHead = (e->eventHead + 1) % MAX_EVENTS;
    e->eventCount--;
    return true;
}

static void Fake_Accept(CoopSteamBackend* b, CoopSteamId peer) {
    FakeEndpoint* e = (FakeEndpoint*) b;

    sNet.counters.accepts++;
    Fake_Open(e, Fake_Slot(peer));
}

static void Fake_CloseSession(CoopSteamBackend* b, CoopSteamId peer) {
    FakeEndpoint* e = (FakeEndpoint*) b;
    int index = Fake_Slot(peer);
    FakeSession* s = &e->sessions[index];
    bool tellPeer = s->open && !s->broken;
    uint32_t generation = s->peerGeneration;

    sNet.counters.sessionsClosed++;
    Fake_DropInFlight(e->index, index);
    Fake_DropHeld(e, index);
    memset(s, 0, sizeof(*s));
    s->peerGeneration = generation;
    if (tellPeer) {
        FakePacket* p = Fake_NewPacket(e->index, index, NULL, 0, false, sNet.now + Fake_Delay());

        p->closes = true;
        Fake_Queue(p);
    }
}

static void Fake_Leave(FakeEndpoint* e) {
    int i;

    e->alive = false;
    e->eventCount = 0;
    Fake_FreeList(&e->inboxHead, &e->inboxTail);
    Fake_FreeList(&e->heldHead, &e->heldTail);
    for (i = 0; i < FAKE_MAX_ENDPOINTS; i++) {
        if (!e->crashed && e->sessions[i].open && !e->sessions[i].broken) {
            FakePacket* p;

            if (sNet.link.shutdownLosesData) {
                Fake_DropInFlight(e->index, i);
            }
            p = Fake_NewPacket(e->index, i, NULL, 0, false, e->sessions[i].lastReliableDue + 1);
            p->closes = true;
            if (Fake_Before(p->due, sNet.now)) {
                p->due = sNet.now;
            }
            Fake_Queue(p);
        }
    }
}

static void Fake_Destroy(CoopSteamBackend* b) {
    FakeEndpoint* e = (FakeEndpoint*) b;

    if (e->alive) {
        Fake_Leave(e);
    }
}

static const CoopSteamBackendOps sFakeOps = {
    Fake_Self,    Fake_NowMs, Fake_Nonce,  Fake_Send,         Fake_Receive,
    Fake_Release, Fake_Event, Fake_Accept, Fake_CloseSession, Fake_Destroy,
};

CoopSteamBackend* FakeNet_Join(CoopSteamId id) {
    FakeEndpoint* e = &sNet.endpoints[Fake_Slot(id)];

    memset(e->sessions, 0, sizeof(e->sessions));
    e->base.ops = &sFakeOps;
    e->alive = true;
    e->crashed = false;
    e->generation++;
    return &e->base;
}

void FakeNet_Outage(CoopSteamId from, CoopSteamId to, uint32_t ms) {
    FakeEndpoint* e = &sNet.endpoints[Fake_Slot(from)];
    int index = Fake_Slot(to);

    e->outage[index] = true;
    e->outageUntil[index] = sNet.now + ms;
}

void FakeNet_Kill(CoopSteamId id) {
    FakeEndpoint* e = &sNet.endpoints[Fake_Slot(id)];

    if (e->alive) {
        e->crashed = true;
        Fake_Leave(e);
    }
}

void FakeNet_Break(CoopSteamId a, CoopSteamId b) {
    int ia = Fake_Slot(a);
    int ib = Fake_Slot(b);

    Fake_FailSession(&sNet.endpoints[ia], ib);
    Fake_FailSession(&sNet.endpoints[ib], ia);
}

void FakeNet_BusyNext(CoopSteamId id, uint32_t sends) {
    sNet.endpoints[Fake_Slot(id)].busyNext = sends;
}

uint32_t FakeNet_Held(CoopSteamId id) {
    const FakePacket* p = sNet.endpoints[Fake_Slot(id)].heldHead;
    uint32_t count = 0;

    for (; p != NULL; p = p->next) {
        count++;
    }
    return count;
}

const FakeCounters* FakeNet_Counters(void) {
    return &sNet.counters;
}

uint32_t FakeNet_Now(void) {
    return sNet.now;
}

void FakeNet_Free(void) {
    int i;

    while (sNet.head != NULL) {
        FakePacket* p = sNet.head;

        Fake_Unlink(p);
        free(p);
    }
    for (i = 0; i < FAKE_MAX_ENDPOINTS; i++) {
        Fake_FreeList(&sNet.endpoints[i].inboxHead, &sNet.endpoints[i].inboxTail);
        Fake_FreeList(&sNet.endpoints[i].heldHead, &sNet.endpoints[i].heldTail);
    }
}

void FakeNet_Reset(uint64_t seed, const FakeLink* link, uint32_t startTime) {
    FakeNet_Free();
    memset(&sNet, 0, sizeof(sNet));
    sNet.link = *link;
    sNet.now = startTime;
    sNet.rng = seed;
}
