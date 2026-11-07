// Deterministic lockstep, see lockstep.h and SYNC_INPUT in docs/coop/PROTOCOL.md.

#include "lockstep.h"

#include <string.h>

// The first frame each player still lacks at the sender, and how many blocks follow.
#define HEADER_SIZE (4 * COOP_PLAYERS_MAX + 1)
#define BLOCK_HEADER_SIZE 6
#define MESSAGE_SIZE (HEADER_SIZE + COOP_PLAYERS_MAX * (BLOCK_HEADER_SIZE + COOP_LOCKSTEP_BLOCK_FRAMES * COOP_LOCKSTEP_FRAME_SIZE))

_Static_assert(MESSAGE_SIZE <= COOP_NET_MAX_MESSAGE, "a SYNC_INPUT has to fit one datagram");
_Static_assert(COOP_LOCKSTEP_RING > 2 * COOP_LOCKSTEP_MAX_DELAY + COOP_LOCKSTEP_BLOCK_FRAMES / 2,
               "the ring has to hold what the slowest client can still ask for");

static void Put32(uint8_t* out, uint32_t value) {
    out[0] = (uint8_t) value;
    out[1] = (uint8_t) (value >> 8);
    out[2] = (uint8_t) (value >> 16);
    out[3] = (uint8_t) (value >> 24);
}

static uint32_t Get32(const uint8_t* in) {
    return (uint32_t) in[0] | ((uint32_t) in[1] << 8) | ((uint32_t) in[2] << 16) | ((uint32_t) in[3] << 24);
}

static void Pad_Put(uint8_t* out, const OSContPad* pad) {
    out[0] = (uint8_t) pad->button;
    out[1] = (uint8_t) (pad->button >> 8);
    out[2] = (uint8_t) pad->stick_x;
    out[3] = (uint8_t) pad->stick_y;
}

static void Pad_Get(const uint8_t* in, OSContPad* pad) {
    memset(pad, 0, sizeof(*pad));
    pad->button = (uint16_t) (in[0] | (in[1] << 8));
    pad->stick_x = (int8_t) in[2];
    pad->stick_y = (int8_t) in[3];
}

static bool Is_Present(const CoopLockstep* l, int32_t player) {
    return (player >= 0) && (player < COOP_PLAYERS_MAX) && ((l->config.present & (1u << player)) != 0);
}

// Who sits at the other end of a link.
static int32_t Link_Player(const CoopLockstep* l, int link) {
    return l->config.host ? link + 1 : 0;
}

void CoopLockstep_Start(CoopLockstep* l, const CoopLockstepConfig* config, uint32_t nowMs) {
    int i;

    memset(l, 0, sizeof(*l));
    l->config = *config;
    l->progressMs = nowMs;
    // Empty frames, so nobody needs a message from before the level. Only this client's own stream is known yet.
    l->next[config->self] = config->delay;
    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        l->links[i].open = config->host ? Is_Present(l, Link_Player(l, i)) : (i == 0);
        l->links[i].dirty = l->links[i].open;
    }
}

int32_t CoopLockstep_Waiting(const CoopLockstep* l) {
    int32_t i;

    for (i = 0; i < COOP_PLAYERS_MAX; i++) {
        if (Is_Present(l, i) && (l->next[i] <= l->frame)) {
            return i;
        }
    }
    return -1;
}

void CoopLockstep_Read(const CoopLockstep* l, uint32_t frame, int32_t player, CoopInputFrame* out) {
    const uint8_t* in;

    memset(out, 0, sizeof(*out));
    if (!Is_Present(l, player)) {
        return;
    }
    in = l->ring[player][frame % COOP_LOCKSTEP_RING];
    Pad_Get(&in[0], &out->hold);
    Pad_Get(&in[4], &out->press);
}

// Who gets what. A host passes on everyone's input, a client only has its own to give. Nothing goes back to its owner
// even so: an owner holds every frame of its own, and its messages say that.
static bool Link_Carries(const CoopLockstep* l, int32_t player) {
    return l->config.host ? Is_Present(l, player) : (player == l->config.self);
}

static void Link_Send(CoopLockstep* l, int link, uint32_t nowMs) {
    CoopLockstepLink* to = &l->links[link];
    uint8_t message[MESSAGE_SIZE];
    uint8_t* out = message + HEADER_SIZE;
    uint8_t blocks = 0;
    int32_t player;

    for (player = 0; player < COOP_PLAYERS_MAX; player++) {
        Put32(&message[4 * player], l->next[player]);
    }
    for (player = 0; player < COOP_PLAYERS_MAX; player++) {
        uint32_t newest = l->next[player];
        uint32_t first = to->next[player];
        uint32_t count;
        uint32_t i;

        if (!Link_Carries(l, player) || (first >= newest)) {
            continue;
        }
        // Older frames are gone from the ring. Nothing should still ask for them; the other end then waits for a stall.
        if ((newest - first) > COOP_LOCKSTEP_RING) {
            first = newest - COOP_LOCKSTEP_RING;
        }
        count = newest - first;
        if (count > COOP_LOCKSTEP_BLOCK_FRAMES) {
            count = COOP_LOCKSTEP_BLOCK_FRAMES;
        }
        out[0] = (uint8_t) player;
        Put32(&out[1], first);
        out[5] = (uint8_t) count;
        out += BLOCK_HEADER_SIZE;
        for (i = 0; i < count; i++) {
            memcpy(out, l->ring[player][(first + i) % COOP_LOCKSTEP_RING], COOP_LOCKSTEP_FRAME_SIZE);
            out += COOP_LOCKSTEP_FRAME_SIZE;
        }
        blocks++;
    }
    message[HEADER_SIZE - 1] = blocks;
    l->config.send(l->config.user, link, message, (uint16_t) (out - message));
    to->sentMs = nowMs;
    to->dirty = false;
}

void CoopLockstep_Tick(CoopLockstep* l, uint32_t nowMs) {
    int i;

    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        CoopLockstepLink* link = &l->links[i];

        if (link->open && (link->dirty || ((nowMs - link->sentMs) >= COOP_LOCKSTEP_RESEND_MS))) {
            Link_Send(l, i, nowMs);
        }
    }
}

void CoopLockstep_Advance(CoopLockstep* l, const CoopInputFrame* local, uint32_t nowMs) {
    int32_t self = l->config.self;
    uint8_t* out = l->ring[self][l->next[self] % COOP_LOCKSTEP_RING];
    int i;

    Pad_Put(&out[0], &local->hold);
    Pad_Put(&out[4], &local->press);
    l->next[self]++;
    l->frame++;
    l->progressMs = nowMs;
    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        l->links[i].dirty = l->links[i].open;
    }
    CoopLockstep_Tick(l, nowMs);
}

// Takes the frames of one block that we lack. Frames older than that are repeats. False if they cannot be held.
static bool Block_Take(CoopLockstep* l, int32_t player, uint32_t first, uint32_t count, const uint8_t* in) {
    uint32_t i;

    if (first > l->next[player]) {
        return false; // a sender starts from what we confirmed, so there is never a gap
    }
    if ((first + count) > (l->frame + COOP_LOCKSTEP_RING)) {
        return false; // it would write over frames we still have to run
    }
    for (i = 0; i < count; i++) {
        uint32_t frame = first + i;

        if (frame == l->next[player]) {
            memcpy(l->ring[player][frame % COOP_LOCKSTEP_RING], &in[i * COOP_LOCKSTEP_FRAME_SIZE],
                   COOP_LOCKSTEP_FRAME_SIZE);
            l->next[player]++;
        }
    }
    return true;
}

bool CoopLockstep_Receive(CoopLockstep* l, int peer, const uint8_t* data, uint16_t size, uint32_t nowMs) {
    CoopLockstepLink* from;
    const uint8_t* in = data + HEADER_SIZE;
    const uint8_t* end = data + size;
    uint32_t before[COOP_PLAYERS_MAX];
    uint8_t blocks;
    uint8_t b;
    int32_t player;
    int i;

    if ((peer < 0) || (peer >= COOP_NET_MAX_PEERS) || !l->links[peer].open || (size < HEADER_SIZE)) {
        return false;
    }
    from = &l->links[peer];
    blocks = data[HEADER_SIZE - 1];
    memcpy(before, l->next, sizeof(before));
    for (b = 0; b < blocks; b++) {
        uint32_t first;
        uint32_t count;

        if ((end - in) < BLOCK_HEADER_SIZE) {
            return false;
        }
        player = in[0];
        first = Get32(&in[1]);
        count = in[5];
        in += BLOCK_HEADER_SIZE;
        if ((count == 0) || (count > COOP_LOCKSTEP_BLOCK_FRAMES) || ((uint32_t) (end - in) < count * COOP_LOCKSTEP_FRAME_SIZE)) {
            return false;
        }
        // A client speaks for itself, and only the host speaks for the others.
        if (!Is_Present(l, player) || (player == l->config.self) ||
            (l->config.host && (player != Link_Player(l, peer)))) {
            return false;
        }
        if (!Block_Take(l, player, first, count, in)) {
            return false;
        }
        in += count * COOP_LOCKSTEP_FRAME_SIZE;
    }
    if (in != end) {
        return false;
    }
    for (player = 0; player < COOP_PLAYERS_MAX; player++) {
        uint32_t confirmed = Get32(&data[4 * player]);

        if (confirmed > from->next[player]) {
            from->next[player] = confirmed;
        }
    }
    // What came in from one client is news for the others.
    if (l->config.host && (memcmp(before, l->next, sizeof(before)) != 0)) {
        for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
            l->links[i].dirty = l->links[i].dirty || (l->links[i].open && (i != peer));
        }
    }
    CoopLockstep_Tick(l, nowMs);
    return true;
}

int32_t CoopLockstep_StalledOn(const CoopLockstep* l, uint32_t nowMs) {
    uint32_t limit = (l->frame == 0) ? COOP_LOCKSTEP_START_MS : COOP_LOCKSTEP_STALL_MS;

    return ((nowMs - l->progressMs) >= limit) ? CoopLockstep_Waiting(l) : -1;
}
