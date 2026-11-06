// Connection and reliability logic of the UDP transport. The wire layout is in docs/coop/PROTOCOL.md.
//
// Reliable lane: each message gets a sequence number and stays in tx[] until acknowledged. Every datagram carries the
// next sequence number expected plus a bitmask of the 32 after it, so one lost ack costs nothing. The receiver holds
// early messages in rx[] and hands them out in order. Unreliable lane: its own sequence number, used only to drop
// duplicates. Every received byte is untrusted: Packet_Parse checks sizes, the rest works on checked values.

#ifdef USE_NETWORKING

#include "net_core.h"

#include <stdlib.h>
#include <string.h>

#include "protocol.h"

#define MAGIC 0x4353 // "SC" on the wire
#define WINDOW 128   // reliable messages in flight per direction; must divide 65536
#define INBOX 32     // unreliable messages waiting for a poll
#define DEFAULT_TIMEOUT_MS 5000
#define DEFAULT_PING_MS 250
#define CONNECT_RETRY_MS 200
#define INITIAL_RTO_MS 200 // resend delay until the first round trip is measured
#define MIN_RTO_MS 50
#define MAX_RTO_MS 1000
#define GOODBYE_COPIES 3 // a goodbye is not acknowledged, so send it more than once

#define FLAG_RELIABLE 1

#define REFUSE_FULL 1
#define REFUSE_VERSION 2

typedef enum {
    LINK_FREE,
    LINK_CONNECTING,
    LINK_CONNECTED,
    LINK_ENDED, // waiting for its last messages and its event to be polled
} LinkState;

typedef struct {
    bool used;
    uint16_t msgId;
    uint16_t size;
    uint32_t sentAt;
    uint32_t rto;
    uint8_t data[COOP_NET_MAX_MESSAGE];
} TxSlot;

typedef struct {
    bool filled;
    uint16_t msgId;
    uint16_t size;
    uint8_t data[COOP_NET_MAX_MESSAGE];
} RxSlot;

typedef struct {
    LinkState state;
    CoopNetAddr addr;
    uint32_t connId;
    CoopNetEnd end;
    bool connectedEvent; // COOP_NET_EVENT_CONNECTED not polled yet
    uint32_t startedAt;
    uint32_t lastHeard;
    uint32_t nextPing;
    uint32_t nextConnect;

    uint16_t txBase; // oldest unacknowledged reliable message
    uint16_t txNext;
    TxSlot tx[WINDOW];

    uint16_t rxNext; // next reliable message to hand out
    bool ackDue;
    RxSlot rx[WINDOW];

    uint16_t unreliableNext;
    bool unreliableSeenAny;
    uint16_t unreliableTop;  // highest unreliable sequence number seen
    uint64_t unreliableSeen; // bit n: unreliableTop - n has arrived
    RxSlot inbox[INBOX];
    uint16_t inboxHead;
    uint16_t inboxCount;

    bool hasRtt;
    uint32_t srtt;
    uint32_t rttVar;
    CoopNetStats stats;
} Link;

struct CoopNetCore {
    bool isHost;
    uint32_t timeoutMs;
    uint32_t pingMs;
    uint8_t maxLinks;
    CoopNetTxFn tx;
    void* user;
    uint32_t badDatagrams;
    uint32_t strayDatagrams;
    uint8_t nextPoll;
    Link links[COOP_NET_MAX_PEERS];
};

typedef struct {
    uint8_t version;
    uint8_t flags;
    uint16_t msgId;
    uint16_t size;
    uint32_t connId;
    uint16_t seq;
    uint16_t ack;
    uint32_t ackBits;
    const uint8_t* payload;
} Packet;

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

uint32_t CoopNetCore_Crc32(const uint8_t* data, uint32_t size) {
    static const uint32_t sNibbles[16] = {
        0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
        0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
    };
    uint32_t crc = 0xFFFFFFFF;
    uint32_t i;

    for (i = 0; i < size; i++) {
        crc = sNibbles[(crc ^ data[i]) & 0xF] ^ (crc >> 4);
        crc = sNibbles[(crc ^ (data[i] >> 4)) & 0xF] ^ (crc >> 4);
    }
    return ~crc;
}

static bool Packet_Parse(const uint8_t* data, uint32_t size, Packet* p) {
    if (size < COOP_NET_HEADER_SIZE || size > COOP_NET_MAX_DATAGRAM) {
        return false;
    }
    if (Get32(data) != CoopNetCore_Crc32(data + 4, size - 4) || Get16(data + 4) != MAGIC) {
        return false;
    }
    p->version = data[6];
    p->flags = data[7];
    p->msgId = Get16(data + 8);
    p->size = Get16(data + 10);
    p->connId = Get32(data + 12);
    p->seq = Get16(data + 16);
    p->ack = Get16(data + 18);
    p->ackBits = Get32(data + 20);
    p->payload = data + COOP_NET_HEADER_SIZE;
    return p->size == size - COOP_NET_HEADER_SIZE;
}

static void Core_Write(CoopNetCore* core, CoopNetAddr to, uint32_t connId, uint16_t msgId, uint8_t flags, uint16_t seq,
                       uint16_t ack, uint32_t ackBits, const void* payload, uint16_t size) {
    uint8_t buffer[COOP_NET_MAX_DATAGRAM];

    Put16(buffer + 4, MAGIC);
    buffer[6] = COOP_PROTOCOL_VERSION;
    buffer[7] = flags;
    Put16(buffer + 8, msgId);
    Put16(buffer + 10, size);
    Put32(buffer + 12, connId);
    Put16(buffer + 16, seq);
    Put16(buffer + 18, ack);
    Put32(buffer + 20, ackBits);
    if (size != 0) {
        memcpy(buffer + COOP_NET_HEADER_SIZE, payload, size);
    }
    Put32(buffer, CoopNetCore_Crc32(buffer + 4, COOP_NET_HEADER_SIZE - 4 + size));
    core->tx(core->user, to, buffer, (uint16_t) (COOP_NET_HEADER_SIZE + size));
}

static bool Rx_Holds(const Link* l, uint16_t seq) {
    return (uint16_t) (seq - l->rxNext) < WINDOW && l->rx[seq % WINDOW].filled;
}

// What the header tells the peer: the first message missing here, and which of the 32 after it did arrive.
static uint16_t Link_AckState(const Link* l, uint32_t* bits) {
    uint16_t ack = l->rxNext;
    uint32_t i;

    while (Rx_Holds(l, ack)) {
        ack++;
    }
    *bits = 0;
    for (i = 0; i < 32; i++) {
        if (Rx_Holds(l, (uint16_t) (ack + 1 + i))) {
            *bits |= 1u << i;
        }
    }
    return ack;
}

static void Link_Write(CoopNetCore* core, Link* l, uint16_t msgId, uint8_t flags, uint16_t seq, const void* payload,
                       uint16_t size) {
    uint32_t bits;
    uint16_t ack = Link_AckState(l, &bits);

    Core_Write(core, l->addr, l->connId, msgId, flags, seq, ack, bits, payload, size);
    l->stats.sent++;
    l->ackDue = false;
}

static uint32_t Link_Rto(const Link* l) {
    uint32_t rto;

    if (!l->hasRtt) {
        return INITIAL_RTO_MS;
    }
    rto = l->srtt + 4 * l->rttVar;
    return rto < MIN_RTO_MS ? MIN_RTO_MS : (rto > MAX_RTO_MS ? MAX_RTO_MS : rto);
}

static void Link_Reset(Link* l, CoopNetAddr addr, uint32_t connId, LinkState state, uint32_t now) {
    l->state = state;
    l->addr = addr;
    l->connId = connId;
    l->end = COOP_NET_END_NONE;
    l->connectedEvent = false;
    l->startedAt = now;
    l->lastHeard = now;
    l->nextPing = now;
    l->nextConnect = now;
    l->txBase = 0;
    l->txNext = 0;
    l->rxNext = 0;
    l->ackDue = false;
    l->unreliableNext = 0;
    l->unreliableSeenAny = false;
    l->unreliableTop = 0;
    l->unreliableSeen = 0;
    l->inboxHead = 0;
    l->inboxCount = 0;
    l->hasRtt = false;
    l->srtt = 0;
    l->rttVar = 0;
    memset(&l->stats, 0, sizeof(l->stats));
    memset(l->tx, 0, sizeof(l->tx));
    memset(l->rx, 0, sizeof(l->rx));
}

static void Link_End(Link* l, CoopNetEnd end) {
    l->state = LINK_ENDED;
    l->end = end;
}

static void Link_OnAck(Link* l, uint16_t ack, uint32_t bits) {
    uint16_t inFlight = (uint16_t) (l->txNext - l->txBase);
    uint16_t ahead = (uint16_t) (ack - l->txBase);
    uint16_t seq;
    uint32_t i;

    // An old ack that arrived late, or nonsense from the wire. Newer acks say the same and more.
    if (ahead > inFlight) {
        return;
    }
    for (seq = l->txBase; seq != ack; seq++) {
        l->tx[seq % WINDOW].used = false;
    }
    for (i = 0; i < 32; i++) {
        seq = (uint16_t) (ack + 1 + i);
        if ((bits & (1u << i)) && (uint16_t) (seq - l->txBase) < inFlight) {
            l->tx[seq % WINDOW].used = false;
        }
    }
    while (l->txBase != l->txNext && !l->tx[l->txBase % WINDOW].used) {
        l->txBase++;
    }
}

static void RxSlot_Fill(RxSlot* slot, const Packet* p) {
    slot->filled = true;
    slot->msgId = p->msgId;
    slot->size = p->size;
    memcpy(slot->data, p->payload, p->size);
}

static void Link_OnReliable(Link* l, const Packet* p) {
    uint16_t ahead = (uint16_t) (p->seq - l->rxNext);

    // Answer even a duplicate: it means our ack went missing.
    l->ackDue = true;
    if (ahead >= WINDOW) {
        if (ahead < 0x8000) {
            l->stats.overflow++; // beyond the window; the sender resends it
        } else {
            l->stats.duplicates++; // already handed out
        }
    } else if (l->rx[p->seq % WINDOW].filled) {
        l->stats.duplicates++;
    } else {
        RxSlot_Fill(&l->rx[p->seq % WINDOW], p);
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

static void Link_OnUnreliable(Link* l, const Packet* p) {
    if (Link_SeenUnreliable(l, p->seq)) {
        l->stats.duplicates++;
    } else if (l->inboxCount == INBOX) {
        l->stats.overflow++;
    } else {
        RxSlot_Fill(&l->inbox[(l->inboxHead + l->inboxCount) % INBOX], p);
        l->inboxCount++;
    }
}

static void Link_OnRtt(Link* l, uint32_t sample) {
    if (!l->hasRtt) {
        l->hasRtt = true;
        l->srtt = sample;
        l->rttVar = sample / 2;
        l->stats.rttMinMs = sample;
        l->stats.rttMaxMs = sample;
    } else {
        uint32_t diff = sample > l->srtt ? sample - l->srtt : l->srtt - sample;

        l->rttVar = (3 * l->rttVar + diff) / 4;
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

// A datagram for an established connection.
static void Link_OnPacket(CoopNetCore* core, Link* l, const Packet* p, uint32_t now) {
    l->lastHeard = now;
    l->stats.received++;
    Link_OnAck(l, p->ack, p->ackBits);

    switch (p->msgId) {
        case COOP_MSG_ACK:
        case COOP_MSG_CONNECT_ACCEPT: // a copy of one we already acted on
            break;
        case COOP_MSG_DISCONNECT:
            Link_End(l, COOP_NET_END_CLOSED);
            break;
        case COOP_MSG_PING:
            if (p->size == 4) {
                Link_Write(core, l, COOP_MSG_PONG, 0, 0, p->payload, 4);
            }
            break;
        case COOP_MSG_PONG:
            if (p->size == 4) {
                uint32_t sample = now - Get32(p->payload);

                // Our own clock came back, so only damage or a forgery can make this absurd.
                if (sample < 60000) {
                    Link_OnRtt(l, sample);
                }
            }
            break;
        default:
            if (p->msgId < COOP_MSG_TRANSPORT_END) {
                break; // a transport message of a later version
            }
            if (p->flags & FLAG_RELIABLE) {
                Link_OnReliable(l, p);
            } else {
                Link_OnUnreliable(l, p);
            }
            break;
    }
}

static void Core_Refuse(CoopNetCore* core, CoopNetAddr to, uint32_t connId, uint8_t reason) {
    Core_Write(core, to, connId, COOP_MSG_CONNECT_REFUSE, 0, 0, 0, 0, &reason, 1);
}

static Link* Core_FindByAddr(CoopNetCore* core, CoopNetAddr addr) {
    uint8_t i;

    for (i = 0; i < core->maxLinks; i++) {
        if (core->links[i].state != LINK_FREE && core->links[i].addr == addr) {
            return &core->links[i];
        }
    }
    return NULL;
}

static void Core_HostReceive(CoopNetCore* core, CoopNetAddr from, const Packet* p, uint32_t now) {
    Link* l;
    uint8_t i;

    if (p->version != COOP_PROTOCOL_VERSION) {
        if (p->msgId == COOP_MSG_CONNECT_REQUEST) {
            Core_Refuse(core, from, p->connId, REFUSE_VERSION);
        }
        return;
    }

    l = Core_FindByAddr(core, from);
    if (l == NULL) {
        if (p->msgId != COOP_MSG_CONNECT_REQUEST) {
            core->strayDatagrams++;
            return;
        }
        for (i = 0; i < core->maxLinks; i++) {
            if (core->links[i].state == LINK_FREE) {
                l = &core->links[i];
                break;
            }
        }
        if (l == NULL) {
            Core_Refuse(core, from, p->connId, REFUSE_FULL);
            return;
        }
        Link_Reset(l, from, p->connId, LINK_CONNECTED, now);
        l->connectedEvent = true;
        l->stats.received++;
        Link_Write(core, l, COOP_MSG_CONNECT_ACCEPT, 0, 0, NULL, 0);
        return;
    }

    if (l->connId != p->connId) {
        // The same address with a new connection: the client restarted. Its retries get in once this link is gone.
        if (p->msgId == COOP_MSG_CONNECT_REQUEST && l->state == LINK_CONNECTED) {
            Link_End(l, COOP_NET_END_TIMEOUT);
        } else {
            core->strayDatagrams++;
        }
        return;
    }
    if (l->state != LINK_CONNECTED) {
        return;
    }
    if (p->msgId == COOP_MSG_CONNECT_REQUEST) {
        // Our accept went missing.
        l->lastHeard = now;
        Link_Write(core, l, COOP_MSG_CONNECT_ACCEPT, 0, 0, NULL, 0);
        return;
    }
    Link_OnPacket(core, l, p, now);
}

static void Core_ClientReceive(CoopNetCore* core, const Packet* p, uint32_t now) {
    Link* l = &core->links[0];

    // The connection id decides, not the sender's address: a host with several addresses may answer from another one.
    if (l->state == LINK_FREE || l->state == LINK_ENDED || l->connId != p->connId) {
        core->strayDatagrams++;
        return;
    }
    if (l->state == LINK_CONNECTING) {
        if (p->msgId == COOP_MSG_CONNECT_REFUSE && p->size == 1) {
            // Readable whatever the version, see PROTOCOL.md.
            Link_End(l, p->payload[0] == REFUSE_FULL ? COOP_NET_END_REFUSED_FULL : COOP_NET_END_REFUSED_VERSION);
        } else if (p->version == COOP_PROTOCOL_VERSION && p->msgId == COOP_MSG_CONNECT_ACCEPT) {
            l->state = LINK_CONNECTED;
            l->connectedEvent = true;
            l->lastHeard = now;
            l->nextPing = now;
            l->stats.received++;
        }
        // Anything else: the host is already sending and our accept is on its way. It will be resent.
        return;
    }
    if (p->version == COOP_PROTOCOL_VERSION) {
        Link_OnPacket(core, l, p, now);
    }
}

static CoopNetCore* Core_Create(const CoopNetConfig* config, CoopNetTxFn tx, void* user) {
    CoopNetCore* core = calloc(1, sizeof(CoopNetCore));

    if (core == NULL) {
        return NULL;
    }
    core->tx = tx;
    core->user = user;
    core->timeoutMs = (config && config->timeoutMs) ? config->timeoutMs : DEFAULT_TIMEOUT_MS;
    core->pingMs = (config && config->pingMs) ? config->pingMs : DEFAULT_PING_MS;
    return core;
}

CoopNetCore* CoopNetCore_CreateHost(const CoopNetConfig* config, CoopNetTxFn tx, void* user) {
    CoopNetCore* core = Core_Create(config, tx, user);

    if (core != NULL) {
        uint8_t maxPeers = config ? config->maxPeers : 0;

        core->isHost = true;
        core->maxLinks = (maxPeers == 0 || maxPeers > COOP_NET_MAX_PEERS) ? COOP_NET_MAX_PEERS : maxPeers;
    }
    return core;
}

CoopNetCore* CoopNetCore_CreateClient(const CoopNetConfig* config, CoopNetTxFn tx, void* user, CoopNetAddr host,
                                      uint32_t nonce, uint32_t nowMs) {
    CoopNetCore* core = Core_Create(config, tx, user);

    if (core != NULL) {
        core->maxLinks = 1;
        Link_Reset(&core->links[0], host, nonce, LINK_CONNECTING, nowMs);
    }
    return core;
}

void CoopNetCore_Destroy(CoopNetCore* core) {
    uint8_t i;

    if (core == NULL) {
        return;
    }
    for (i = 0; i < core->maxLinks; i++) {
        CoopNetCore_Disconnect(core, i);
    }
    free(core);
}

void CoopNetCore_Receive(CoopNetCore* core, CoopNetAddr from, const uint8_t* data, uint32_t size, uint32_t nowMs) {
    Packet p;

    if (!Packet_Parse(data, size, &p)) {
        core->badDatagrams++;
    } else if (core->isHost) {
        Core_HostReceive(core, from, &p, nowMs);
    } else {
        Core_ClientReceive(core, &p, nowMs);
    }
}

static void Link_Update(CoopNetCore* core, Link* l, uint32_t now) {
    uint16_t seq;

    if (l->state == LINK_CONNECTING) {
        if (now - l->startedAt > core->timeoutMs) {
            Link_End(l, COOP_NET_END_TIMEOUT);
        } else if ((int32_t) (now - l->nextConnect) >= 0) {
            l->nextConnect = now + CONNECT_RETRY_MS;
            Link_Write(core, l, COOP_MSG_CONNECT_REQUEST, 0, 0, NULL, 0);
        }
        return;
    }
    if (l->state != LINK_CONNECTED) {
        return;
    }
    if (now - l->lastHeard > core->timeoutMs) {
        Link_End(l, COOP_NET_END_TIMEOUT);
        return;
    }

    for (seq = l->txBase; seq != l->txNext; seq++) {
        TxSlot* slot = &l->tx[seq % WINDOW];

        if (slot->used && now - slot->sentAt >= slot->rto) {
            slot->sentAt = now;
            slot->rto = slot->rto * 2 > MAX_RTO_MS ? MAX_RTO_MS : slot->rto * 2;
            l->stats.resent++;
            Link_Write(core, l, slot->msgId, FLAG_RELIABLE, seq, slot->data, slot->size);
        }
    }

    if ((int32_t) (now - l->nextPing) >= 0) {
        uint8_t stamp[4];

        l->nextPing = now + core->pingMs;
        Put32(stamp, now);
        Link_Write(core, l, COOP_MSG_PING, 0, 0, stamp, sizeof(stamp));
    }
    if (l->ackDue) {
        Link_Write(core, l, COOP_MSG_ACK, 0, 0, NULL, 0);
    }
}

void CoopNetCore_Update(CoopNetCore* core, uint32_t nowMs) {
    uint8_t i;

    for (i = 0; i < core->maxLinks; i++) {
        Link_Update(core, &core->links[i], nowMs);
    }
}

bool CoopNetCore_Send(CoopNetCore* core, int peer, uint16_t msgId, const void* data, uint16_t size, CoopNetLane lane,
                      uint32_t nowMs) {
    Link* l;

    if (peer < 0 || peer >= core->maxLinks || size > COOP_NET_MAX_MESSAGE || msgId < COOP_MSG_TRANSPORT_END) {
        return false;
    }
    l = &core->links[peer];
    if (l->state != LINK_CONNECTED) {
        return false;
    }
    if (lane == COOP_NET_RELIABLE) {
        TxSlot* slot;

        if ((uint16_t) (l->txNext - l->txBase) >= WINDOW) {
            return false;
        }
        slot = &l->tx[l->txNext % WINDOW];
        slot->used = true;
        slot->msgId = msgId;
        slot->size = size;
        slot->sentAt = nowMs;
        slot->rto = Link_Rto(l);
        if (size != 0) {
            memcpy(slot->data, data, size);
        }
        Link_Write(core, l, msgId, FLAG_RELIABLE, l->txNext, slot->data, size);
        l->txNext++;
    } else {
        Link_Write(core, l, msgId, 0, l->unreliableNext, data, size);
        l->unreliableNext++;
    }
    return true;
}

static bool Link_Poll(Link* l, int peer, CoopNetEvent* ev) {
    RxSlot* slot;

    memset(ev, 0, sizeof(*ev));
    ev->peer = peer;
    if (l->connectedEvent) {
        l->connectedEvent = false;
        ev->type = COOP_NET_EVENT_CONNECTED;
        return true;
    }

    slot = &l->rx[l->rxNext % WINDOW];
    if (slot->filled) {
        slot->filled = false;
        l->rxNext++;
    } else if (l->inboxCount != 0) {
        slot = &l->inbox[l->inboxHead];
        l->inboxHead = (l->inboxHead + 1) % INBOX;
        l->inboxCount--;
    } else {
        slot = NULL;
    }
    if (slot != NULL) {
        ev->type = COOP_NET_EVENT_MESSAGE;
        ev->msgId = slot->msgId;
        ev->size = slot->size;
        ev->data = slot->data;
        return true;
    }

    if (l->state == LINK_ENDED) {
        ev->type = COOP_NET_EVENT_DISCONNECTED;
        ev->end = l->end;
        l->state = LINK_FREE;
        return true;
    }
    return false;
}

bool CoopNetCore_Poll(CoopNetCore* core, CoopNetEvent* ev) {
    uint8_t i;

    // Start after the peer served last, so a busy peer cannot starve the others.
    for (i = 0; i < core->maxLinks; i++) {
        uint8_t index = (uint8_t) ((core->nextPoll + i) % core->maxLinks);
        Link* l = &core->links[index];

        if (l->state != LINK_FREE && Link_Poll(l, index, ev)) {
            core->nextPoll = (uint8_t) ((index + 1) % core->maxLinks);
            return true;
        }
    }
    memset(ev, 0, sizeof(*ev));
    return false;
}

void CoopNetCore_Disconnect(CoopNetCore* core, int peer) {
    Link* l;
    int i;

    if (peer < 0 || peer >= core->maxLinks) {
        return;
    }
    l = &core->links[peer];
    if (l->state == LINK_CONNECTED) {
        for (i = 0; i < GOODBYE_COPIES; i++) {
            Link_Write(core, l, COOP_MSG_DISCONNECT, 0, 0, NULL, 0);
        }
    }
    l->state = LINK_FREE;
}

bool CoopNetCore_Stats(const CoopNetCore* core, int peer, CoopNetStats* out) {
    const Link* l;
    uint16_t seq;

    if (peer < 0 || peer >= core->maxLinks) {
        return false;
    }
    l = &core->links[peer];
    *out = l->stats;
    out->pending = 0;
    for (seq = l->txBase; seq != l->txNext; seq++) {
        out->pending += l->tx[seq % WINDOW].used;
    }
    out->badDatagrams = core->badDatagrams;
    out->strayDatagrams = core->strayDatagrams;
    return true;
}

#endif
