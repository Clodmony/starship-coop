// The co-op transport over Steam (decision 0004). The wire form is in docs/coop/PROTOCOL.md.
//
// Steam does the reliability: no resends here, no windows of held messages. What is left is what Steam does not give:
//
// - connections: its sessions have no goodbye and no event when a peer closes, so connecting, refusing, keep-alive and
//   goodbye are ours, with the same messages as on UDP
// - "never delivered twice" on the unreliable lane, because Steam may deliver the same unreliable message twice
// - stats.pending: the peer counts the reliable messages it got, and that count rides on every frame
//
// Control messages that must keep their place go on the reliable lane. An unreliable accept could be lost or overtaken
// by the host's first message, and unlike on UDP nothing would ever send that message again.
//
// A message goes straight from Steam to the caller of poll and is released on the next poll, so there are no queues
// here and nothing can overflow. Every received byte is untrusted: Frame_Parse checks sizes, the rest works on checked
// values.

#ifdef USE_STEAM_RELAY

#include "transport_steam.h"

#include <stdlib.h>
#include <string.h>

#include "protocol.h"

#define MAGIC 0x4353 // "SC" on the wire
#define HEADER_SIZE 16
#define WINDOW 128 // reliable messages in flight per direction, as on UDP
#define DEFAULT_TIMEOUT_MS 5000
#define DEFAULT_CONNECT_MS 20000 // Steam has to find a route first
#define DEFAULT_PING_MS 250
#define CONNECT_RETRY_MS 1000
// Per poll, so that a flood cannot hold up a game frame.
#define MAX_RECEIVES_PER_POLL 256

#define FLAG_RELIABLE 1

#define REFUSE_FULL 1
#define REFUSE_VERSION 2

typedef enum {
    LINK_FREE,
    LINK_CONNECTING,
    LINK_CONNECTED,
    LINK_ENDED, // waiting for its event to be polled
} LinkState;

typedef struct {
    LinkState state;
    CoopSteamId id;
    uint32_t connId;
    CoopNetEnd end;
    bool acceptDue; // host: the accept did not go out yet, and nothing may go before it
    bool ackDue;
    bool broken; // the session is gone, and the link ends once everything that arrived has been read
    uint32_t startedAt;
    uint32_t lastHeard;
    uint32_t nextPing;
    uint32_t nextConnect;

    uint16_t txNext;  // sequence number of the next reliable message, so also how many were sent
    uint16_t txAcked; // how many the peer says it got
    uint16_t rxNext;  // how many we got

    uint16_t unreliableNext;
    bool unreliableSeenAny;
    uint16_t unreliableTop;  // highest unreliable sequence number seen
    uint64_t unreliableSeen; // bit n: unreliableTop - n has arrived

    bool hasRtt;
    uint32_t srtt;
    CoopNetStats stats;
} Link;

typedef struct {
    CoopTransport base;
    CoopSteamBackend* backend;
    bool isHost;
    uint32_t timeoutMs;
    uint32_t connectMs;
    uint32_t pingMs;
    uint8_t maxLinks;
    uint32_t badDatagrams;
    uint32_t strayDatagrams;
    bool holding; // message was handed to the caller and goes back on the next poll
    CoopSteamMessage message;
    Link links[COOP_NET_MAX_PEERS];
} SteamTransport;

typedef struct {
    uint8_t version;
    uint8_t flags;
    uint16_t msgId;
    uint16_t size;
    uint32_t connId;
    uint16_t seq;
    uint16_t ack;
    const uint8_t* payload;
} Frame;

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

static bool Frame_Parse(const uint8_t* data, uint32_t size, Frame* f) {
    if (size < HEADER_SIZE || size > HEADER_SIZE + COOP_NET_MAX_MESSAGE || Get16(data) != MAGIC) {
        return false;
    }
    f->version = data[2];
    f->flags = data[3];
    f->msgId = Get16(data + 4);
    f->size = Get16(data + 6);
    f->connId = Get32(data + 8);
    f->seq = Get16(data + 12);
    f->ack = Get16(data + 14);
    f->payload = data + HEADER_SIZE;
    return f->size == size - HEADER_SIZE;
}

static CoopSteamSendResult Steam_Write(SteamTransport* s, CoopSteamId to, uint32_t connId, uint16_t msgId,
                                       CoopNetLane lane, uint16_t seq, uint16_t ack, const void* payload,
                                       uint16_t size) {
    uint8_t buffer[HEADER_SIZE + COOP_NET_MAX_MESSAGE];

    Put16(buffer, MAGIC);
    buffer[2] = COOP_PROTOCOL_VERSION;
    buffer[3] = lane == COOP_NET_RELIABLE ? FLAG_RELIABLE : 0;
    Put16(buffer + 4, msgId);
    Put16(buffer + 6, size);
    Put32(buffer + 8, connId);
    Put16(buffer + 12, seq);
    Put16(buffer + 14, ack);
    if (size != 0) {
        memcpy(buffer + HEADER_SIZE, payload, size);
    }
    return s->backend->ops->send(s->backend, to, buffer, (uint32_t) (HEADER_SIZE + size), lane);
}

static void Link_Reset(Link* l, CoopSteamId id, uint32_t connId, LinkState state, uint32_t now) {
    memset(l, 0, sizeof(*l));
    l->state = state;
    l->id = id;
    l->connId = connId;
    l->startedAt = now;
    l->lastHeard = now;
    l->nextPing = now;
    l->nextConnect = now;
}

static void Link_End(Link* l, CoopNetEnd end) {
    l->state = LINK_ENDED;
    l->end = end;
}

static CoopSteamSendResult Link_Write(SteamTransport* s, Link* l, uint16_t msgId, CoopNetLane lane, uint16_t seq,
                                      const void* payload, uint16_t size) {
    CoopSteamSendResult result;

    if (l->broken) {
        return COOP_STEAM_BROKEN;
    }
    result = Steam_Write(s, l->id, l->connId, msgId, lane, seq, l->rxNext, payload, size);
    if (result == COOP_STEAM_SENT) {
        l->stats.sent++;
        l->ackDue = false;
    } else if (result == COOP_STEAM_BROKEN) {
        // The peer may have closed right after its last messages, so they can still be waiting here.
        l->broken = true;
    }
    return result;
}

static void Link_OnAck(Link* l, uint16_t ack) {
    // An old ack that was overtaken, or nonsense from the wire. Newer acks say the same and more.
    if ((uint16_t) (ack - l->txAcked) <= (uint16_t) (l->txNext - l->txAcked)) {
        l->txAcked = ack;
    }
}

// Remembers the sequence number. True if it has been seen, or is too old to tell.
static bool Link_SeenUnreliable(Link* l, uint16_t seq) {
    uint16_t ahead = (uint16_t) (seq - l->unreliableTop);
    uint16_t behind = (uint16_t) (l->unreliableTop - seq);

    if (!l->unreliableSeenAny) {
        l->unreliableSeenAny = true;
        l->unreliableTop = seq;
        l->unreliableSeen = 1;
        return false;
    }
    if (ahead != 0 && ahead < 0x8000) {
        l->unreliableSeen = ahead >= 64 ? 0 : l->unreliableSeen << ahead;
        l->unreliableSeen |= 1;
        l->unreliableTop = seq;
        return false;
    }
    if (behind >= 64 || (l->unreliableSeen & (1ull << behind))) {
        return true;
    }
    l->unreliableSeen |= 1ull << behind;
    return false;
}

static void Link_OnRtt(Link* l, uint32_t sample) {
    if (!l->hasRtt) {
        l->hasRtt = true;
        l->srtt = sample;
        l->stats.rttMinMs = sample;
        l->stats.rttMaxMs = sample;
    } else {
        l->srtt = (7 * l->srtt + sample) / 8;
        if (sample < l->stats.rttMinMs) {
            l->stats.rttMinMs = sample;
        }
        if (sample > l->stats.rttMaxMs) {
            l->stats.rttMaxMs = sample;
        }
    }
    l->stats.rttMs = l->srtt;
}

// Steam promises the reliable lane, but a session that restarted under us would leave a hole. True to hand it out.
static bool Link_OnReliable(Link* l, const Frame* f) {
    uint16_t ahead = (uint16_t) (f->seq - l->rxNext);

    if (ahead == 0) {
        l->rxNext++;
        l->ackDue = true;
        return true;
    }
    if (ahead < 0x8000) {
        Link_End(l, COOP_NET_END_TIMEOUT); // what followed is lost, and handing out the rest would break the order
    } else {
        l->stats.duplicates++;
    }
    return false;
}

static bool Link_OnUnreliable(Link* l, const Frame* f) {
    if (Link_SeenUnreliable(l, f->seq)) {
        l->stats.duplicates++;
        return false;
    }
    return true;
}

// A frame for an established connection. True if it is a message for the caller.
static bool Link_OnFrame(SteamTransport* s, Link* l, const Frame* f, uint32_t now) {
    l->lastHeard = now;
    l->stats.received++;
    Link_OnAck(l, f->ack);

    switch (f->msgId) {
        case COOP_MSG_ACK:
        case COOP_MSG_CONNECT_ACCEPT:
            return false;
        case COOP_MSG_DISCONNECT:
            Link_End(l, COOP_NET_END_CLOSED);
            return false;
        case COOP_MSG_PING:
            if (f->size == 4) {
                Link_Write(s, l, COOP_MSG_PONG, COOP_NET_UNRELIABLE, 0, f->payload, 4);
            }
            return false;
        case COOP_MSG_PONG:
            if (f->size == 4) {
                uint32_t sample = now - Get32(f->payload);

                // Our own clock came back, so only a forgery can make this absurd.
                if (sample < 60000) {
                    Link_OnRtt(l, sample);
                }
            }
            return false;
        default:
            if (f->msgId < COOP_MSG_TRANSPORT_END) {
                return false; // a transport message of a later version
            }
            return (f->flags & FLAG_RELIABLE) ? Link_OnReliable(l, f) : Link_OnUnreliable(l, f);
    }
}

static Link* Steam_FindById(SteamTransport* s, CoopSteamId id) {
    uint8_t i;

    for (i = 0; i < s->maxLinks; i++) {
        if (s->links[i].state != LINK_FREE && s->links[i].id == id) {
            return &s->links[i];
        }
    }
    return NULL;
}

static void Steam_Refuse(SteamTransport* s, CoopSteamId to, uint32_t connId, uint8_t reason) {
    Steam_Write(s, to, connId, COOP_MSG_CONNECT_REFUSE, COOP_NET_RELIABLE, 0, 0, &reason, 1);
}

static void Link_SendAccept(SteamTransport* s, Link* l) {
    l->acceptDue = Link_Write(s, l, COOP_MSG_CONNECT_ACCEPT, COOP_NET_RELIABLE, 0, NULL, 0) == COOP_STEAM_BUSY;
}

// A connect request from someone we have no link with. True if it made one.
static bool Host_NewLink(SteamTransport* s, CoopSteamId from, const Frame* f, uint32_t now, CoopNetEvent* ev) {
    uint8_t i;

    for (i = 0; i < s->maxLinks; i++) {
        Link* l = &s->links[i];

        if (l->state == LINK_FREE) {
            Link_Reset(l, from, f->connId, LINK_CONNECTED, now);
            l->stats.received++;
            Link_SendAccept(s, l);
            ev->type = COOP_NET_EVENT_CONNECTED;
            ev->peer = i;
            return true;
        }
    }
    Steam_Refuse(s, from, f->connId, REFUSE_FULL);
    return false;
}

// True if ev was filled in.
static bool Host_OnFrame(SteamTransport* s, CoopSteamId from, const Frame* f, uint32_t now, CoopNetEvent* ev) {
    Link* l;

    if (f->version != COOP_PROTOCOL_VERSION) {
        if (f->msgId == COOP_MSG_CONNECT_REQUEST) {
            Steam_Refuse(s, from, f->connId, REFUSE_VERSION);
        }
        return false;
    }

    l = Steam_FindById(s, from);
    if (l == NULL) {
        if (f->msgId == COOP_MSG_CONNECT_REQUEST) {
            return Host_NewLink(s, from, f, now, ev);
        }
        s->strayDatagrams++;
        return false;
    }
    if (l->connId != f->connId) {
        // The same user with a new connection: the client restarted. Its next request gets in once this link is gone.
        // Its session is the live one now, so it stays open.
        if (f->msgId == COOP_MSG_CONNECT_REQUEST && l->state == LINK_CONNECTED) {
            Link_End(l, COOP_NET_END_TIMEOUT);
        } else {
            s->strayDatagrams++;
        }
        return false;
    }
    if (l->state != LINK_CONNECTED) {
        return false;
    }
    if (f->msgId == COOP_MSG_CONNECT_REQUEST) {
        l->lastHeard = now; // sent again while the first one was on its way
        return false;
    }
    if (Link_OnFrame(s, l, f, now)) {
        ev->type = COOP_NET_EVENT_MESSAGE;
        ev->peer = (int) (l - s->links);
        ev->msgId = f->msgId;
        ev->size = f->size;
        ev->data = f->payload;
        return true;
    }
    return false;
}

// True if ev was filled in.
static bool Client_OnFrame(SteamTransport* s, CoopSteamId from, const Frame* f, uint32_t now, CoopNetEvent* ev) {
    Link* l = &s->links[0];

    // Steam names the sender, so unlike on UDP it counts here as well as the connection id.
    if (l->state == LINK_FREE || l->state == LINK_ENDED || from != l->id || f->connId != l->connId) {
        s->strayDatagrams++;
        return false;
    }
    if (l->state == LINK_CONNECTING) {
        if (f->msgId == COOP_MSG_CONNECT_REFUSE && f->size == 1) {
            // Readable whatever the version, see PROTOCOL.md.
            Link_End(l, f->payload[0] == REFUSE_FULL ? COOP_NET_END_REFUSED_FULL : COOP_NET_END_REFUSED_VERSION);
        } else if (f->version == COOP_PROTOCOL_VERSION && f->msgId == COOP_MSG_CONNECT_ACCEPT) {
            l->state = LINK_CONNECTED;
            l->lastHeard = now;
            l->nextPing = now;
            l->stats.received++;
            ev->type = COOP_NET_EVENT_CONNECTED;
            return true;
        }
        // Anything else overtook the accept, so it is unreliable and may go. A reliable message cannot get here first.
        return false;
    }
    if (f->version == COOP_PROTOCOL_VERSION && Link_OnFrame(s, l, f, now)) {
        ev->type = COOP_NET_EVENT_MESSAGE;
        ev->msgId = f->msgId;
        ev->size = f->size;
        ev->data = f->payload;
        return true;
    }
    return false;
}

// True if ev was filled in. Then the message stays with the caller until the next poll, if ev points into it.
static bool Steam_OnMessage(SteamTransport* s, uint32_t now, CoopNetEvent* ev) {
    CoopSteamMessage* m = &s->message;
    Frame f;
    bool filled = false;

    if (!Frame_Parse(m->data, m->size, &f)) {
        s->badDatagrams++;
    } else if (s->isHost) {
        filled = Host_OnFrame(s, m->from, &f, now, ev);
    } else {
        filled = Client_OnFrame(s, m->from, &f, now, ev);
    }
    if (filled && ev->type == COOP_NET_EVENT_MESSAGE) {
        s->holding = true;
    } else {
        s->backend->ops->release(s->backend, m);
    }
    return filled;
}

static void Steam_Events(SteamTransport* s) {
    CoopSteamBackend* b = s->backend;
    CoopSteamEvent e;

    while (b->ops->event(b, &e)) {
        Link* l = Steam_FindById(s, e.peer);

        if (e.type == COOP_STEAM_SESSION_REQUEST) {
            // The host takes anyone, or nobody new could reach it; only a connect request gets further. A client only
            // ever talks to its host.
            if (s->isHost || l != NULL) {
                b->ops->accept(b, e.peer);
            } else {
                s->strayDatagrams++;
            }
        } else if (l != NULL && (l->state == LINK_CONNECTING || l->state == LINK_CONNECTED)) {
            l->broken = true;
        } else {
            b->ops->closeSession(b, e.peer);
        }
    }
}

static void Link_Update(SteamTransport* s, Link* l, uint32_t now, bool drained) {
    if (l->broken && drained && (l->state == LINK_CONNECTING || l->state == LINK_CONNECTED)) {
        // A broken session stays broken until it is closed, and then the peer can start a new one.
        s->backend->ops->closeSession(s->backend, l->id);
        Link_End(l, COOP_NET_END_TIMEOUT);
        return;
    }
    if (l->state == LINK_CONNECTING) {
        if (now - l->startedAt > s->connectMs) {
            Link_End(l, COOP_NET_END_TIMEOUT);
        } else if ((int32_t) (now - l->nextConnect) >= 0) {
            l->nextConnect = now + CONNECT_RETRY_MS;
            Link_Write(s, l, COOP_MSG_CONNECT_REQUEST, COOP_NET_RELIABLE, 0, NULL, 0);
        }
        return;
    }
    if (l->state != LINK_CONNECTED) {
        return;
    }
    // Only with nothing left to read: a long stall leaves the peer's frames waiting, and they count as heard.
    if (drained && now - l->lastHeard > s->timeoutMs) {
        Link_End(l, COOP_NET_END_TIMEOUT);
        return;
    }
    if (l->acceptDue) {
        Link_SendAccept(s, l);
    }
    if ((int32_t) (now - l->nextPing) >= 0) {
        uint8_t stamp[4];

        l->nextPing = now + s->pingMs;
        Put32(stamp, now);
        Link_Write(s, l, COOP_MSG_PING, COOP_NET_UNRELIABLE, 0, stamp, sizeof(stamp));
    }
    if (l->ackDue) {
        Link_Write(s, l, COOP_MSG_ACK, COOP_NET_UNRELIABLE, 0, NULL, 0);
    }
}

// The next link that ended, in ev. True if there was one.
static bool Steam_TakeEnded(SteamTransport* s, CoopNetEvent* ev) {
    uint8_t i;

    for (i = 0; i < s->maxLinks; i++) {
        Link* l = &s->links[i];

        if (l->state == LINK_ENDED) {
            ev->type = COOP_NET_EVENT_DISCONNECTED;
            ev->peer = i;
            ev->end = l->end;
            l->state = LINK_FREE;
            return true;
        }
    }
    return false;
}

static void Steam_ReleaseHeld(SteamTransport* s) {
    if (s->holding) {
        s->holding = false;
        s->backend->ops->release(s->backend, &s->message);
    }
}

static bool Steam_Poll(CoopTransport* t, CoopNetEvent* ev) {
    SteamTransport* s = (SteamTransport*) t;
    CoopSteamBackend* b = s->backend;
    uint32_t now = b->ops->nowMs(b);
    bool drained = false;
    uint8_t i;
    int count;

    Steam_ReleaseHeld(s);
    memset(ev, 0, sizeof(*ev));
    if (Steam_TakeEnded(s, ev)) {
        return true;
    }
    // Messages before everything else: a session that failed may have left some, and they go before its end.
    for (count = 0; count < MAX_RECEIVES_PER_POLL; count++) {
        if (!b->ops->receive(b, &s->message)) {
            drained = true;
            break;
        }
        if (Steam_OnMessage(s, now, ev) || Steam_TakeEnded(s, ev)) {
            return true;
        }
    }
    Steam_Events(s);
    for (i = 0; i < s->maxLinks; i++) {
        Link_Update(s, &s->links[i], now, drained);
    }
    return Steam_TakeEnded(s, ev);
}

static bool Steam_Send(CoopTransport* t, int peer, uint16_t msgId, const void* data, uint16_t size, CoopNetLane lane) {
    SteamTransport* s = (SteamTransport*) t;
    Link* l;

    if (peer < 0 || peer >= s->maxLinks || size > COOP_NET_MAX_MESSAGE || msgId < COOP_MSG_TRANSPORT_END) {
        return false;
    }
    l = &s->links[peer];
    if (l->state != LINK_CONNECTED || l->acceptDue) {
        return false;
    }
    if (lane == COOP_NET_RELIABLE) {
        if ((uint16_t) (l->txNext - l->txAcked) >= WINDOW ||
            Link_Write(s, l, msgId, COOP_NET_RELIABLE, l->txNext, data, size) != COOP_STEAM_SENT) {
            return false;
        }
        l->txNext++;
    } else {
        if (Link_Write(s, l, msgId, COOP_NET_UNRELIABLE, l->unreliableNext, data, size) != COOP_STEAM_SENT) {
            return false;
        }
        l->unreliableNext++;
    }
    return true;
}

// The goodbye goes on the reliable lane, behind everything sent so far. The session stays open: closing it would throw
// the goodbye away, and Steam ends unused sessions by itself after a few minutes.
static void Steam_Disconnect(CoopTransport* t, int peer) {
    SteamTransport* s = (SteamTransport*) t;
    Link* l;

    if (peer < 0 || peer >= s->maxLinks) {
        return;
    }
    l = &s->links[peer];
    if (l->state == LINK_CONNECTED) {
        Link_Write(s, l, COOP_MSG_DISCONNECT, COOP_NET_RELIABLE, 0, NULL, 0);
    }
    if (l->state != LINK_FREE && l->broken) {
        s->backend->ops->closeSession(s->backend, l->id);
    }
    l->state = LINK_FREE;
}

static bool Steam_Stats(CoopTransport* t, int peer, CoopNetStats* out) {
    SteamTransport* s = (SteamTransport*) t;
    const Link* l;

    if (peer < 0 || peer >= s->maxLinks) {
        return false;
    }
    l = &s->links[peer];
    *out = l->stats;
    out->pending = (uint16_t) (l->txNext - l->txAcked);
    out->badDatagrams = s->badDatagrams;
    out->strayDatagrams = s->strayDatagrams;
    return true;
}

static void Steam_Close(CoopTransport* t) {
    SteamTransport* s = (SteamTransport*) t;
    int i;

    Steam_ReleaseHeld(s);
    for (i = 0; i < s->maxLinks; i++) {
        Steam_Disconnect(t, i);
    }
    s->backend->ops->destroy(s->backend);
    free(s);
}

static const CoopTransportOps sSteamOps = {
    Steam_Send, Steam_Poll, Steam_Disconnect, Steam_Stats, Steam_Close,
};

static SteamTransport* Steam_Create(CoopSteamBackend* backend, const CoopNetConfig* config) {
    SteamTransport* s;

    if (backend == NULL) {
        return NULL;
    }
    s = calloc(1, sizeof(SteamTransport));
    if (s == NULL) {
        backend->ops->destroy(backend);
        return NULL;
    }
    s->base.ops = &sSteamOps;
    s->backend = backend;
    s->timeoutMs = (config && config->timeoutMs) ? config->timeoutMs : DEFAULT_TIMEOUT_MS;
    s->connectMs = (config && config->timeoutMs) ? config->timeoutMs : DEFAULT_CONNECT_MS;
    s->pingMs = (config && config->pingMs) ? config->pingMs : DEFAULT_PING_MS;
    return s;
}

CoopTransport* CoopNetSteam_Host(CoopSteamBackend* backend, const CoopNetConfig* config) {
    SteamTransport* s = Steam_Create(backend, config);
    uint8_t maxPeers = config ? config->maxPeers : 0;

    if (s == NULL) {
        return NULL;
    }
    s->isHost = true;
    s->maxLinks = (maxPeers == 0 || maxPeers > COOP_NET_MAX_PEERS) ? COOP_NET_MAX_PEERS : maxPeers;
    return &s->base;
}

CoopTransport* CoopNetSteam_Connect(CoopSteamBackend* backend, CoopSteamId host, const CoopNetConfig* config) {
    SteamTransport* s;

    if (backend != NULL && (host == 0 || host == backend->ops->self(backend))) {
        backend->ops->destroy(backend); // one Steam user cannot talk to itself
        return NULL;
    }
    s = Steam_Create(backend, config);
    if (s == NULL) {
        return NULL;
    }
    s->maxLinks = 1;
    Link_Reset(&s->links[0], host, backend->ops->nonce(backend), LINK_CONNECTING, backend->ops->nowMs(backend));
    return &s->base;
}

#endif
