// Desync detection, see desync.h and docs/coop/PROTOCOL.md, Desync check.

#include "desync.h"

#include <string.h>

#include "port/coop/net/protocol.h"

_Static_assert(COOP_DESYNC_HASH_MESSAGE_MAX <= COOP_NET_MAX_MESSAGE, "a SYNC_HASH has to fit one datagram");
_Static_assert(COOP_DESYNC_RING > COOP_DESYNC_PERIOD + 16 + 8,
               "the ring has to hold what the slowest client asks about");
_Static_assert(COOP_DESYNC_PERIOD <= COOP_DESYNC_MAX_BATCH, "a batch holds a period");

static void Put32(uint8_t* out, uint32_t value) {
    out[0] = (uint8_t) value;
    out[1] = (uint8_t) (value >> 8);
    out[2] = (uint8_t) (value >> 16);
    out[3] = (uint8_t) (value >> 24);
}

static uint32_t Get32(const uint8_t* in) {
    return (uint32_t) in[0] | ((uint32_t) in[1] << 8) | ((uint32_t) in[2] << 16) | ((uint32_t) in[3] << 24);
}

static void Put64(uint8_t* out, uint64_t value) {
    Put32(out, (uint32_t) value);
    Put32(out + 4, (uint32_t) (value >> 32));
}

static uint64_t Get64(const uint8_t* in) {
    return (uint64_t) Get32(in) | ((uint64_t) Get32(in + 4) << 32);
}

static bool Is_Present(const CoopDesync* d, int32_t player) {
    return (player >= 0) && (player < COOP_PLAYERS_MAX) && ((d->config.present & (1u << player)) != 0);
}

// Who sits at the other end of a link.
static int32_t Link_Player(const CoopDesync* d, int link) {
    return d->config.host ? link + 1 : 0;
}

static CoopDesyncEntry* Slot(CoopDesync* d, uint32_t frame) {
    return &d->ring[frame % COOP_DESYNC_RING];
}

void CoopDesync_Start(CoopDesync* d, const CoopDesyncConfig* config) {
    int i;

    memset(d, 0, sizeof(*d));
    d->config = *config;
    if ((d->config.period < 1) || (d->config.period > COOP_DESYNC_MAX_BATCH)) {
        d->config.period = COOP_DESYNC_PERIOD;
    }
    d->theirPlayer = -1;
    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        d->links[i].open = config->host ? Is_Present(d, Link_Player(d, i)) : (i == 0);
    }
}

const CoopDesyncEntry* CoopDesync_Entry(const CoopDesync* d, uint32_t frame) {
    const CoopDesyncEntry* e = &d->ring[frame % COOP_DESYNC_RING];

    return (e->held && (e->frame == frame)) ? e : NULL;
}

bool CoopDesync_IsMessage(uint16_t msgId) {
    return (msgId == COOP_MSG_SYNC_HASH) || (msgId == COOP_MSG_SYNC_DESYNC);
}

int32_t CoopDesync_NeedFrom(const CoopDesync* d) {
    if (!d->found) {
        return -1;
    }
    if (d->pair[0] == d->config.self) {
        return d->pair[1];
    }
    if (d->pair[1] == d->config.self) {
        return d->pair[0];
    }
    return d->foundFrom;
}

static void Send_All(CoopDesync* d, uint16_t msgId, const uint8_t* data, uint16_t size) {
    int i;

    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        if (d->links[i].open) {
            d->config.send(d->config.user, i, msgId, data, size);
        }
    }
}

// Our hash of the frame to everyone we talk to, held or not: the other side then knows whether to wait for more.
static void Send_Desync(CoopDesync* d) {
    uint8_t message[COOP_DESYNC_MESSAGE_SIZE];
    int i;

    Put32(&message[0], d->frame);
    message[4] = (uint8_t) d->config.self;
    message[5] = (uint8_t) d->pair[0];
    message[6] = (uint8_t) d->pair[1];
    message[7] = d->mine.held ? 1 : 0;
    memcpy(&message[8], d->mine.state, 4);
    Put64(&message[12], d->mine.held ? d->mine.hash.total : 0);
    for (i = 0; i < COOP_HASH_REGION_COUNT; i++) {
        Put64(&message[20 + 8 * i], d->mine.held ? d->mine.hash.region[i] : 0);
    }
    Send_All(d, COOP_MSG_SYNC_DESYNC, message, sizeof(message));
    d->sent = true;
}

static void Found(CoopDesync* d, int32_t a, int32_t b, uint32_t frame, int32_t from, uint32_t nowMs) {
    const CoopDesyncEntry* e = CoopDesync_Entry(d, frame);

    d->found = true;
    d->frame = frame;
    d->pair[0] = (a < b) ? a : b;
    d->pair[1] = (a < b) ? b : a;
    d->foundFrom = from;
    d->foundMs = nowMs;
    if (e != NULL) {
        d->mine = *e;
    } else {
        memset(&d->mine, 0, sizeof(d->mine));
        d->mine.frame = frame;
    }
    Send_Desync(d);
}

// One frame of a peer against our own.
static void Compare(CoopDesync* d, int32_t player, uint32_t frame, uint64_t total, uint32_t nowMs) {
    const CoopDesyncEntry* e = CoopDesync_Entry(d, frame);

    if (e == NULL) {
        d->unchecked++;
        return;
    }
    d->checked++;
    if ((e->hash.total != total) && !d->found) {
        d->theirTotal = total;
        d->haveTheirTotal = true;
        Found(d, d->config.self, player, frame, player, nowMs);
    }
}

// What a peer sent for frames this game has run by now.
static void Pending_Check(CoopDesync* d, int link, uint32_t nowMs) {
    CoopDesyncLink* l = &d->links[link];

    while (l->pendingCount > 0) {
        CoopDesyncBatch* b = &l->pending[0];

        while (b->next < b->count) {
            uint32_t frame = b->first + b->next;

            if (frame >= d->frames) {
                return; // not run yet: the peer is ahead, by the input delay at most
            }
            Compare(d, Link_Player(d, link), frame, b->totals[b->next], nowMs);
            b->next++;
        }
        l->pendingCount--;
        memmove(&l->pending[0], &l->pending[1], l->pendingCount * sizeof(l->pending[0]));
    }
}

static void Send_Batch(CoopDesync* d, uint32_t last) {
    uint8_t message[COOP_DESYNC_HASH_MESSAGE_MAX];
    uint32_t first = last + 1 - d->config.period;
    uint32_t i;

    Put32(&message[0], first);
    message[4] = (uint8_t) d->config.period;
    for (i = 0; i < d->config.period; i++) {
        const CoopDesyncEntry* e = CoopDesync_Entry(d, first + i);

        Put64(&message[COOP_DESYNC_HASH_HEADER + 8 * i], (e != NULL) ? e->hash.total : 0);
    }
    Send_All(d, COOP_MSG_SYNC_HASH, message, (uint16_t) (COOP_DESYNC_HASH_HEADER + 8 * d->config.period));
}

void CoopDesync_Record(CoopDesync* d, uint32_t frame, const CoopStateHash* hash, const uint8_t state[4],
                       uint32_t nowMs) {
    CoopDesyncEntry* e = Slot(d, frame);
    int i;

    e->held = true;
    e->frame = frame;
    memcpy(e->state, state, 4);
    e->hash = *hash;
    d->frames = frame + 1;
    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        if (d->links[i].open) {
            Pending_Check(d, i, nowMs);
        }
    }
    if (((frame + 1) % d->config.period) == 0) {
        Send_Batch(d, frame);
    }
}

static bool Receive_Hash(CoopDesync* d, int peer, const uint8_t* data, uint16_t size, uint32_t nowMs) {
    CoopDesyncLink* l = &d->links[peer];
    CoopDesyncBatch* b;
    uint8_t count;
    uint8_t i;

    if (size < COOP_DESYNC_HASH_HEADER) {
        return false;
    }
    count = data[4];
    if ((count == 0) || (count > COOP_DESYNC_MAX_BATCH) || (size != COOP_DESYNC_HASH_HEADER + 8 * count)) {
        return false;
    }
    if (l->pendingCount == COOP_DESYNC_PENDING) {
        // Only a peer far ahead of us fills this, and lockstep keeps it within the delay. The oldest goes unchecked.
        d->unchecked += l->pending[0].count - l->pending[0].next;
        l->pendingCount--;
        memmove(&l->pending[0], &l->pending[1], l->pendingCount * sizeof(l->pending[0]));
    }
    b = &l->pending[l->pendingCount++];
    b->first = Get32(&data[0]);
    b->count = count;
    b->next = 0;
    for (i = 0; i < count; i++) {
        b->totals[i] = Get64(&data[COOP_DESYNC_HASH_HEADER + 8 * i]);
    }
    Pending_Check(d, peer, nowMs);
    return true;
}

static bool Receive_Desync(CoopDesync* d, int peer, const uint8_t* data, uint16_t size, uint32_t nowMs) {
    CoopDesyncEntry theirs;
    uint32_t frame;
    int32_t sender;
    int32_t a;
    int32_t b;
    int i;

    if (size != COOP_DESYNC_MESSAGE_SIZE) {
        return false;
    }
    frame = Get32(&data[0]);
    sender = data[4];
    a = data[5];
    b = data[6];
    // A client speaks for itself, and the host for itself: the pair is who differed, the sender who saw it.
    if ((sender != Link_Player(d, peer)) || !Is_Present(d, a) || !Is_Present(d, b) || (a == b)) {
        return false;
    }
    memset(&theirs, 0, sizeof(theirs));
    theirs.held = (data[7] & 1) != 0;
    theirs.frame = frame;
    memcpy(theirs.state, &data[8], 4);
    theirs.hash.total = Get64(&data[12]);
    for (i = 0; i < COOP_HASH_REGION_COUNT; i++) {
        theirs.hash.region[i] = Get64(&data[20 + 8 * i]);
    }
    if (!d->found) {
        Found(d, a, b, frame, sender, nowMs);
    }
    if ((sender == CoopDesync_NeedFrom(d)) && !d->answered) {
        d->theirs = theirs;
        d->theirPlayer = sender;
        d->theirFrame = frame;
        d->answered = true;
    }
    return true;
}

bool CoopDesync_Receive(CoopDesync* d, int peer, uint16_t msgId, const uint8_t* data, uint16_t size, uint32_t nowMs) {
    if (!CoopDesync_IsMessage(msgId)) {
        return true;
    }
    if ((peer < 0) || (peer >= COOP_NET_MAX_PEERS) || !d->links[peer].open) {
        return false;
    }
    if (msgId == COOP_MSG_SYNC_HASH) {
        return Receive_Hash(d, peer, data, size, nowMs);
    }
    return Receive_Desync(d, peer, data, size, nowMs);
}

bool CoopDesync_Done(const CoopDesync* d, uint32_t nowMs) {
    if (!d->found || !d->sent) {
        return false;
    }
    if ((nowMs - d->foundMs) >= COOP_DESYNC_GRACE_MS) {
        return true;
    }
    // A game outside the pair waits the grace out: if it ended at once, the host would see it leave before the pair
    // had exchanged their hashes, and end the session for that.
    return d->answered && ((d->pair[0] == d->config.self) || (d->pair[1] == d->config.self));
}

uint32_t CoopDesync_Regions(const CoopDesync* d) {
    uint32_t bits = 0;
    int i;

    if (!d->mine.held || !d->theirs.held || (d->theirFrame != d->frame)) {
        return 0;
    }
    for (i = 0; i < COOP_HASH_REGION_COUNT; i++) {
        if (d->mine.hash.region[i] != d->theirs.hash.region[i]) {
            bits |= 1u << i;
        }
    }
    return bits;
}
