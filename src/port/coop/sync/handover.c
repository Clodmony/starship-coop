// The handover at the end of a session, see handover.h.

#include "handover.h"

#include <stdlib.h>
#include <string.h>

// What goes to a peer next, in this order.
enum {
    PHASE_BEGIN, // BEGIN is due
    PHASE_FILES, // file `file`: FILE_START unless fileStarted, then FILE_DATA from `offset`
    PHASE_DONE,  // DONE is due
    PHASE_WAIT,  // DONE is out; BYE goes after CoopHandover_Close
    PHASE_OVER,  // BYE is out
};

static void Put32(uint8_t* out, uint32_t value) {
    out[0] = (uint8_t) value;
    out[1] = (uint8_t) (value >> 8);
    out[2] = (uint8_t) (value >> 16);
    out[3] = (uint8_t) (value >> 24);
}

static uint32_t Get32(const uint8_t* in) {
    return (uint32_t) in[0] | ((uint32_t) in[1] << 8) | ((uint32_t) in[2] << 16) | ((uint32_t) in[3] << 24);
}

// ---- Names ----

static bool Digit(char c) {
    return (c >= '0') && (c <= '9');
}

// The same text at name + *at, moving past it.
static bool Take(const char* name, size_t len, size_t* at, const char* text) {
    size_t n = strlen(text);

    if ((len - *at < n) || (memcmp(name + *at, text, n) != 0)) {
        return false;
    }
    *at += n;
    return true;
}

// A frame as %u writes it: 0, or a digit 1 to 9 and up to nine more, below 2^32.
static bool Take_Frame(const char* name, size_t len, size_t* at) {
    uint64_t value = 0;
    size_t digits = 0;

    while ((*at < len) && Digit(name[*at])) {
        if ((digits == 1) && (value == 0)) {
            return false; // a leading zero
        }
        value = value * 10 + (uint64_t) (name[*at] - '0');
        if ((++digits > 10) || (value > 0xFFFFFFFFull)) {
            return false;
        }
        (*at)++;
    }
    return digits != 0;
}

bool CoopHandover_NameValid(const char* name, size_t len, int32_t sender, int32_t receiver) {
    size_t at = 0;

    if ((name == NULL) || (len == 0) || (len > COOP_HANDOVER_NAME_MAX) || (sender < 0) || (sender > 3) ||
        (sender == receiver)) {
        return false;
    }
    if (!Take(name, len, &at, "coop-desync-") || !Take_Frame(name, len, &at) || !Take(name, len, &at, "-player") ||
        (at >= len) || (name[at] != (char) ('1' + sender))) {
        return false;
    }
    at++;
    // desync_report.c numbers a second report of the frame -2 to -9.
    if ((len - at >= 2) && (name[at] == '-')) {
        if ((name[at + 1] < '2') || (name[at + 1] > '9')) {
            return false;
        }
        at += 2;
    }
    return Take(name, len, &at, ".txt") && (at == len);
}

// ---- Setting up ----

void CoopHandover_Init(CoopHandover* h, const CoopHandoverConfig* config) {
    memset(h, 0, sizeof(*h));
    h->config = *config;
}

void CoopHandover_AddPeer(CoopHandover* h, int peer, int32_t player) {
    CoopHandoverPeer* p;

    if ((peer < 0) || (peer >= COOP_HANDOVER_PEERS) || (player < 0) || (player > 3) || (player == h->config.self)) {
        return;
    }
    p = &h->peers[peer];
    memset(p, 0, sizeof(*p));
    p->open = true;
    p->player = player;
    p->phase = PHASE_BEGIN;
}

bool CoopHandover_AddFile(CoopHandover* h, const char* name, const uint8_t* data, uint32_t size) {
    CoopHandoverOut* out;
    size_t len = (name != NULL) ? strlen(name) : 0;

    if (h->begun || (h->outCount >= COOP_HANDOVER_FILES) || (data == NULL) || (size == 0) ||
        (size > COOP_HANDOVER_FILE_MAX) || !CoopHandover_NameValid(name, len, h->config.self, -1)) {
        return false;
    }
    out = &h->out[h->outCount++];
    memcpy(out->name, name, len);
    out->name[len] = '\0';
    out->data = data;
    out->size = size;
    return true;
}

void CoopHandover_Begin(CoopHandover* h, uint8_t reason, uint32_t nowMs, uint32_t capMs) {
    if (h->begun) {
        return;
    }
    h->begun = true;
    h->reason = reason;
    h->startMs = nowMs;
    h->capMs = capMs;
}

// ---- Receiving ----

static void In_Refuse(CoopHandover* h, CoopHandoverIn* in) {
    free(in->data);
    in->data = NULL;
    in->state = COOP_HANDOVER_IN_REFUSED;
    h->stats.refused++;
}

// A file still coming when its sender said DONE, BYE or went: it will not be whole.
static void Peer_Unfinished(CoopHandover* h, CoopHandoverPeer* p) {
    int i;

    for (i = 0; i < COOP_HANDOVER_FILES; i++) {
        if (p->in[i].state == COOP_HANDOVER_IN_RECEIVING) {
            In_Refuse(h, &p->in[i]);
        }
    }
}

static bool Dropped(CoopHandover* h) {
    h->stats.dropped++;
    return false;
}

static bool Receive_Start(CoopHandover* h, int peer, CoopHandoverPeer* p, const uint8_t* data, uint16_t size) {
    CoopHandoverIn* in;
    uint32_t fileSize;
    uint8_t nameLen;
    const char* name;
    int i;

    if ((size < 7) || p->done) {
        return Dropped(h);
    }
    nameLen = data[6];
    if ((size != 7 + nameLen) || (data[1] >= COOP_HANDOVER_FILES) || (p->in[data[1]].state != COOP_HANDOVER_IN_NONE)) {
        return Dropped(h);
    }
    in = &p->in[data[1]];
    p->started++;
    fileSize = Get32(&data[2]);
    name = (const char*) &data[7];
    // From here on the file number is taken, so whatever else comes for it is dropped with it.
    in->state = COOP_HANDOVER_IN_REFUSED;
    if (!CoopHandover_NameValid(name, nameLen, p->player, h->config.self) || (fileSize == 0) ||
        (fileSize > COOP_HANDOVER_FILE_MAX) || (fileSize > COOP_HANDOVER_TOTAL_MAX - h->totalIn)) {
        h->stats.refused++;
        return false;
    }
    for (i = 0; i < COOP_HANDOVER_FILES; i++) {
        if ((&p->in[i] != in) && (p->in[i].state != COOP_HANDOVER_IN_NONE) && (strlen(p->in[i].name) == nameLen) &&
            (memcmp(p->in[i].name, name, nameLen) == 0)) {
            h->stats.refused++; // the same name twice
            return false;
        }
    }
    memcpy(in->name, name, nameLen);
    in->name[nameLen] = '\0';
    in->data = malloc(fileSize);
    if (in->data == NULL) {
        h->stats.refused++;
        return false;
    }
    in->state = COOP_HANDOVER_IN_RECEIVING;
    in->size = fileSize;
    in->got = 0;
    h->totalIn += fileSize;
    (void) peer;
    return true;
}

static bool Receive_Data(CoopHandover* h, int peer, CoopHandoverPeer* p, const uint8_t* data, uint16_t size) {
    CoopHandoverIn* in;
    uint32_t offset;
    uint32_t n;

    if ((size < 7) || (size > COOP_HANDOVER_MESSAGE_MAX) || (data[1] >= COOP_HANDOVER_FILES)) {
        return Dropped(h);
    }
    in = &p->in[data[1]];
    offset = Get32(&data[2]);
    n = (uint32_t) size - 6;
    if (in->state == COOP_HANDOVER_IN_SAVED && (offset <= in->size) && (n <= in->size - offset)) {
        h->stats.duplicates++; // a part of a file that is whole already
        return true;
    }
    if (in->state != COOP_HANDOVER_IN_RECEIVING) {
        return Dropped(h);
    }
    if ((offset > in->size) || (n > in->size - offset)) {
        In_Refuse(h, in); // beyond the size it gave
        return false;
    }
    if (offset < in->got) {
        if ((n <= in->got - offset) && (memcmp(in->data + offset, &data[6], n) == 0)) {
            h->stats.duplicates++;
            return true;
        }
        In_Refuse(h, in); // overlaps what came, with other bytes
        return false;
    }
    if (offset != in->got) {
        In_Refuse(h, in); // a gap: the lane is ordered, so this is no peer of ours
        return false;
    }
    memcpy(in->data + in->got, &data[6], n);
    in->got += n;
    if (in->got == in->size) {
        if (h->config.save != NULL) {
            h->config.save(h->config.user, peer, in->name, in->data, in->size);
        }
        free(in->data);
        in->data = NULL;
        in->state = COOP_HANDOVER_IN_SAVED;
        h->stats.saved++;
        h->stats.bytesIn += in->size;
    }
    return true;
}

bool CoopHandover_Receive(CoopHandover* h, int peer, const uint8_t* data, uint16_t size) {
    CoopHandoverPeer* p;

    if ((peer < 0) || (peer >= COOP_HANDOVER_PEERS) || !h->peers[peer].open || (data == NULL) || (size == 0)) {
        return Dropped(h);
    }
    p = &h->peers[peer];
    if (p->bye) {
        return Dropped(h); // nothing comes after BYE
    }
    switch (data[0]) {
        case COOP_HANDOVER_BEGIN:
            if (size != 2) {
                return Dropped(h);
            }
            if (!p->begun) {
                p->begun = true;
                p->reason = data[1];
            }
            return true;
        case COOP_HANDOVER_FILE_START:
            return Receive_Start(h, peer, p, data, size);
        case COOP_HANDOVER_FILE_DATA:
            return Receive_Data(h, peer, p, data, size);
        case COOP_HANDOVER_DONE:
            if ((size != 2) || p->done) {
                return Dropped(h);
            }
            p->done = true;
            p->announced = data[1];
            Peer_Unfinished(h, p);
            return true;
        case COOP_HANDOVER_BYE:
            if (size != 1) {
                return Dropped(h);
            }
            // DONE comes first on an ordered lane; a BYE without it still ends what came.
            p->done = true;
            p->bye = true;
            Peer_Unfinished(h, p);
            return true;
        default:
            return Dropped(h);
    }
}

void CoopHandover_Gone(CoopHandover* h, int peer) {
    CoopHandoverPeer* p;

    if ((peer < 0) || (peer >= COOP_HANDOVER_PEERS) || !h->peers[peer].open) {
        return;
    }
    p = &h->peers[peer];
    Peer_Unfinished(h, p);
    p->open = false;
}

// ---- Sending ----

// The next message for p into out; 0 if nothing is due.
static uint16_t Next(const CoopHandover* h, const CoopHandoverPeer* p, uint8_t* out) {
    const CoopHandoverOut* file;
    uint32_t n;
    size_t len;

    switch (p->phase) {
        case PHASE_BEGIN:
            out[0] = COOP_HANDOVER_BEGIN;
            out[1] = h->reason;
            return 2;
        case PHASE_FILES:
            file = &h->out[p->file];
            if (!p->fileStarted) {
                len = strlen(file->name);
                out[0] = COOP_HANDOVER_FILE_START;
                out[1] = p->file;
                Put32(&out[2], file->size);
                out[6] = (uint8_t) len;
                memcpy(&out[7], file->name, len);
                return (uint16_t) (7 + len);
            }
            n = file->size - p->offset;
            if (n > COOP_HANDOVER_CHUNK) {
                n = COOP_HANDOVER_CHUNK;
            }
            out[0] = COOP_HANDOVER_FILE_DATA;
            out[1] = p->file;
            Put32(&out[2], p->offset);
            memcpy(&out[6], file->data + p->offset, n);
            return (uint16_t) (6 + n);
        case PHASE_DONE:
            out[0] = COOP_HANDOVER_DONE;
            out[1] = h->outCount;
            return 2;
        case PHASE_WAIT:
            if (!h->closing) {
                return 0;
            }
            out[0] = COOP_HANDOVER_BYE;
            return 1;
        default:
            return 0;
    }
}

// What was sent moves p on.
static void Advance(CoopHandover* h, CoopHandoverPeer* p, uint16_t size) {
    switch (p->phase) {
        case PHASE_BEGIN:
            p->phase = (h->outCount != 0) ? PHASE_FILES : PHASE_DONE;
            break;
        case PHASE_FILES:
            if (!p->fileStarted) {
                p->fileStarted = true;
                p->offset = 0;
                break;
            }
            p->offset += (uint32_t) size - 6;
            if (p->offset >= h->out[p->file].size) {
                h->stats.filesOut++;
                p->file++;
                p->fileStarted = false;
                p->offset = 0;
                if (p->file >= h->outCount) {
                    p->phase = PHASE_DONE;
                }
            }
            break;
        case PHASE_DONE:
            p->phase = PHASE_WAIT;
            break;
        case PHASE_WAIT:
            p->phase = PHASE_OVER;
            break;
        default:
            break;
    }
}

void CoopHandover_Pump(CoopHandover* h) {
    uint8_t message[COOP_HANDOVER_MESSAGE_MAX];
    int i;

    if (!h->begun || (h->config.send == NULL)) {
        return;
    }
    for (i = 0; i < COOP_HANDOVER_PEERS; i++) {
        CoopHandoverPeer* p = &h->peers[i];

        while (p->open) {
            uint16_t size = Next(h, p, message);

            if ((size == 0) || !h->config.send(h->config.user, i, message, size)) {
                break;
            }
            h->stats.messagesOut++;
            Advance(h, p, size);
        }
    }
}

// ---- Where it stands ----

bool CoopHandover_Received(const CoopHandover* h) {
    int i;

    for (i = 0; i < COOP_HANDOVER_PEERS; i++) {
        if (h->peers[i].open && !h->peers[i].done) {
            return false;
        }
    }
    return true;
}

void CoopHandover_Close(CoopHandover* h) {
    h->closing = true;
}

bool CoopHandover_Over(const CoopHandover* h) {
    int i;

    if (!h->begun) {
        return false;
    }
    for (i = 0; i < COOP_HANDOVER_PEERS; i++) {
        if (h->peers[i].open && ((h->peers[i].phase != PHASE_OVER) || !h->peers[i].bye)) {
            return false;
        }
    }
    return true;
}

bool CoopHandover_TimeUp(const CoopHandover* h, uint32_t nowMs) {
    return h->begun && ((uint32_t) (nowMs - h->startMs) >= h->capMs);
}

bool CoopHandover_Answering(const CoopHandover* h) {
    int i;

    for (i = 0; i < COOP_HANDOVER_PEERS; i++) {
        if (h->peers[i].open && h->peers[i].begun) {
            return true;
        }
    }
    return false;
}

void CoopHandover_Free(CoopHandover* h) {
    int i, j;

    for (i = 0; i < COOP_HANDOVER_PEERS; i++) {
        for (j = 0; j < COOP_HANDOVER_FILES; j++) {
            free(h->peers[i].in[j].data);
            h->peers[i].in[j].data = NULL;
        }
    }
}
