// The dump of a desync frame between the two games of the pair (#189). See desync_dump.h.

#include "desync_dump.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHUNK_HEAD 10 // region, name length, index, size; the name comes between
#define NAME_MAX 96

static void Put32(uint8_t* out, uint32_t value) {
    out[0] = (uint8_t) value;
    out[1] = (uint8_t) (value >> 8);
    out[2] = (uint8_t) (value >> 16);
    out[3] = (uint8_t) (value >> 24);
}

static uint32_t Get32(const uint8_t* data) {
    return (uint32_t) data[0] | ((uint32_t) data[1] << 8) | ((uint32_t) data[2] << 16) | ((uint32_t) data[3] << 24);
}

static size_t Name_Length(const char* name) {
    size_t n = (name != NULL) ? strlen(name) : 0;

    return (n > NAME_MAX) ? NAME_MAX : n;
}

size_t CoopDesyncDump_StreamSize(const CoopStateDump* dump) {
    size_t size = 0;
    uint32_t c;

    for (c = 0; c < dump->count; c++) {
        size += CHUNK_HEAD + Name_Length(dump->chunks[c].name) + dump->chunks[c].size;
    }
    return size;
}

size_t CoopDesyncDump_Stream(const CoopStateDump* dump, uint8_t* out, size_t cap) {
    size_t used = 0;
    uint32_t c;

    if (CoopDesyncDump_StreamSize(dump) > cap) {
        return 0;
    }
    for (c = 0; c < dump->count; c++) {
        const CoopStateChunk* chunk = &dump->chunks[c];
        size_t n = Name_Length(chunk->name);

        out[used++] = chunk->region;
        out[used++] = (uint8_t) n;
        memcpy(out + used, chunk->name, n);
        used += n;
        Put32(out + used, (uint32_t) chunk->index);
        Put32(out + used + 4, chunk->size);
        used += 8;
        memcpy(out + used, dump->bytes + chunk->offset, chunk->size);
        used += chunk->size;
    }
    return used;
}

// ---- Sending ----

void CoopDesyncDump_OutBegin(CoopDesyncDumpOut* o, uint8_t run, uint32_t frame, uint8_t sender, const uint8_t* stream,
                             uint32_t size) {
    memset(o, 0, sizeof(*o));
    o->run = run;
    o->frame = frame;
    o->sender = sender;
    if ((stream != NULL) && (size != 0) && (size <= COOP_DESYNC_DUMP_MAX)) {
        o->stream = stream;
        o->size = size;
        o->parts = (uint16_t) ((size + COOP_DESYNC_DUMP_CHUNK - 1) / COOP_DESYNC_DUMP_CHUNK);
    }
}

bool CoopDesyncDump_OutDone(const CoopDesyncDumpOut* o) {
    return (o->parts == 0) ? o->announced : (o->next >= o->parts);
}

uint16_t CoopDesyncDump_OutNext(const CoopDesyncDumpOut* o, uint8_t* message) {
    uint32_t offset = (uint32_t) o->next * COOP_DESYNC_DUMP_CHUNK;
    uint32_t bytes = 0;

    if (CoopDesyncDump_OutDone(o)) {
        return 0;
    }
    if (o->parts != 0) {
        bytes = o->size - offset;
        if (bytes > COOP_DESYNC_DUMP_CHUNK) {
            bytes = COOP_DESYNC_DUMP_CHUNK;
        }
    }
    message[0] = o->run;
    Put32(message + 1, o->frame);
    message[5] = o->sender;
    message[6] = (uint8_t) o->next;
    message[7] = (uint8_t) (o->next >> 8);
    message[8] = (uint8_t) o->parts;
    message[9] = (uint8_t) (o->parts >> 8);
    Put32(message + 10, o->size);
    if (bytes != 0) {
        memcpy(message + COOP_DESYNC_DUMP_HEAD, o->stream + offset, bytes);
    }
    return (uint16_t) (COOP_DESYNC_DUMP_HEAD + bytes);
}

void CoopDesyncDump_OutSent(CoopDesyncDumpOut* o) {
    if (o->parts == 0) {
        o->announced = true;
    } else if (o->next < o->parts) {
        o->next++;
    }
}

// ---- Receiving ----

void CoopDesyncDump_InFree(CoopDesyncDumpIn* in) {
    free(in->stream);
    memset(in, 0, sizeof(*in));
}

bool CoopDesyncDump_InReceive(CoopDesyncDumpIn* in, uint8_t run, uint32_t frame, uint8_t sender, const uint8_t* data,
                              uint16_t size) {
    uint16_t part, parts;
    uint32_t total, bytes, want;

    if ((size < COOP_DESYNC_DUMP_HEAD) || (size > COOP_DESYNC_DUMP_MESSAGE_MAX) || in->done || in->refused) {
        return false;
    }
    if ((data[0] != run) || (Get32(data + 1) != frame) || (data[5] != sender)) {
        return false; // of another desync, or of another player: not what this game waits for
    }
    part = (uint16_t) (data[6] | (data[7] << 8));
    parts = (uint16_t) (data[8] | (data[9] << 8));
    total = Get32(data + 10);
    bytes = (uint32_t) size - COOP_DESYNC_DUMP_HEAD;
    if (!in->begun) {
        if (parts == 0) {
            if ((total != 0) || (bytes != 0) || (part != 0)) {
                in->refused = true;
                return false;
            }
            in->begun = in->done = in->none = true;
            in->run = run;
            in->frame = frame;
            in->sender = sender;
            return true;
        }
        if ((total == 0) || (total > COOP_DESYNC_DUMP_MAX) ||
            (parts != (total + COOP_DESYNC_DUMP_CHUNK - 1) / COOP_DESYNC_DUMP_CHUNK)) {
            in->refused = true;
            return false;
        }
        in->stream = malloc(total);
        if (in->stream == NULL) {
            in->refused = true;
            return false;
        }
        in->begun = true;
        in->run = run;
        in->frame = frame;
        in->sender = sender;
        in->size = total;
        in->parts = parts;
    }
    // Every part but the last is full, and the last holds the rest: so the parts add up to the size exactly.
    want = in->size - in->got;
    if (want > COOP_DESYNC_DUMP_CHUNK) {
        want = COOP_DESYNC_DUMP_CHUNK;
    }
    if ((parts != in->parts) || (total != in->size) || (part != in->next) || (bytes != want)) {
        in->refused = true;
        return false;
    }
    memcpy(in->stream + in->got, data + COOP_DESYNC_DUMP_HEAD, bytes);
    in->got += bytes;
    in->next++;
    in->done = in->next == in->parts;
    return true;
}

// ---- Comparing ----

typedef struct {
    uint8_t region;
    uint8_t nameLength;
    const uint8_t* name;
    int32_t index;
    uint32_t size;
    const uint8_t* bytes;
} Chunk;

// The chunks of a stream. Returns how many, -1 if the stream cannot be read. *out is malloc'd.
static int32_t Chunks_Read(const uint8_t* stream, size_t size, Chunk** out) {
    Chunk* chunks = NULL;
    int32_t count = 0;
    int32_t cap = 0;
    size_t at = 0;

    *out = NULL;
    while (at < size) {
        Chunk chunk;

        if (size - at < 2) {
            free(chunks);
            return -1;
        }
        chunk.region = stream[at];
        chunk.nameLength = stream[at + 1];
        at += 2;
        if ((chunk.nameLength > NAME_MAX) || (size - at < (size_t) chunk.nameLength + 8)) {
            free(chunks);
            return -1;
        }
        chunk.name = stream + at;
        at += chunk.nameLength;
        chunk.index = (int32_t) Get32(stream + at);
        chunk.size = Get32(stream + at + 4);
        at += 8;
        if (size - at < chunk.size) {
            free(chunks);
            return -1;
        }
        chunk.bytes = stream + at;
        at += chunk.size;
        if (count == cap) {
            Chunk* grown;

            cap = (cap != 0) ? cap * 2 : 512;
            grown = realloc(chunks, sizeof(Chunk) * (size_t) cap);
            if (grown == NULL) {
                free(chunks);
                return -1;
            }
            chunks = grown;
        }
        chunks[count++] = chunk;
    }
    *out = chunks;
    return count;
}

static bool Chunk_SameLabel(const Chunk* a, const Chunk* b) {
    return (a->nameLength == b->nameLength) && (a->index == b->index) &&
           (memcmp(a->name, b->name, a->nameLength) == 0);
}

// "name[index]" or "name", printable ASCII only: the name came over the wire.
static void Chunk_Label(const Chunk* chunk, char* out, size_t cap) {
    char name[NAME_MAX + 1];
    size_t i;

    for (i = 0; i < chunk->nameLength; i++) {
        uint8_t c = chunk->name[i];

        name[i] = ((c >= 0x21) && (c <= 0x7E)) ? (char) c : '?';
    }
    name[chunk->nameLength] = '\0';
    if (chunk->index >= 0) {
        snprintf(out, cap, "%s[%d]", name, (int) chunk->index);
    } else {
        snprintf(out, cap, "%s", name);
    }
}

// The aligned 32-bit word that holds offset, little-endian; false if the chunk ends inside it.
static bool Word_At(const Chunk* chunk, uint32_t offset, uint32_t* start, uint32_t* word) {
    *start = offset - (offset % 4);
    if (*start + 4 > chunk->size) {
        return false;
    }
    *word = Get32(chunk->bytes + *start);
    return true;
}

static const char* Region_Name(const char* (*regionName)(uint8_t region), uint8_t region, char* buffer, size_t cap) {
    const char* name = (regionName != NULL) ? regionName(region) : NULL;

    if ((name != NULL) && (name[0] != '\0')) {
        return name;
    }
    snprintf(buffer, cap, "region %u", (unsigned) region);
    return buffer;
}

int CoopDesyncDump_FirstDifference(const uint8_t* a, size_t aSize, const uint8_t* b, size_t bSize,
                                   const char* (*regionName)(uint8_t region), char* out, size_t cap) {
    Chunk* ca = NULL;
    Chunk* cb = NULL;
    int32_t na = Chunks_Read(a, aSize, &ca);
    int32_t nb = Chunks_Read(b, bSize, &cb);
    char label[NAME_MAX + 16];
    char buffer[24];
    int result = 0;
    int region;

    if ((na < 0) || (nb < 0)) {
        snprintf(out, cap, "a dump that cannot be read");
        free(ca);
        free(cb);
        return -1;
    }
    // Region by region, as the Python tool goes: the chunks of a region in the order they were hashed.
    for (region = 0; (region < 256) && (result == 0); region++) {
        int32_t i = 0;
        int32_t j = 0;

        while (true) {
            while ((i < na) && (ca[i].region != region)) {
                i++;
            }
            while ((j < nb) && (cb[j].region != region)) {
                j++;
            }
            if ((i >= na) && (j >= nb)) {
                break;
            }
            if ((i >= na) || (j >= nb) || !Chunk_SameLabel(&ca[i], &cb[j]) || (ca[i].size != cb[j].size)) {
                snprintf(out, cap, "region %s has different chunks in the two games",
                         Region_Name(regionName, (uint8_t) region, buffer, sizeof(buffer)));
                result = -1;
                break;
            }
            if (memcmp(ca[i].bytes, cb[j].bytes, ca[i].size) != 0) {
                const Chunk* x = &ca[i];
                const Chunk* y = &cb[j];
                uint32_t first = 0;
                uint32_t count = 0;
                uint32_t start = 0;
                uint32_t wx = 0;
                uint32_t wy = 0;
                uint32_t k;
                int used;

                for (k = 0; k < x->size; k++) {
                    if (x->bytes[k] != y->bytes[k]) {
                        if (count == 0) {
                            first = k;
                        }
                        count++;
                    }
                }
                Chunk_Label(x, label, sizeof(label));
                used = snprintf(out, cap, "%s: %s differs in %u of %u bytes, first at +%u: %02x vs %02x",
                                Region_Name(regionName, (uint8_t) region, buffer, sizeof(buffer)), label,
                                (unsigned) count, (unsigned) x->size, (unsigned) first, x->bytes[first],
                                y->bytes[first]);
                if (Word_At(x, first, &start, &wx) && Word_At(y, first, &start, &wy) && (used > 0) &&
                    ((size_t) used < cap)) {
                    snprintf(out + used, cap - (size_t) used, "; word at +%u: 0x%08x vs 0x%08x", (unsigned) start,
                             (unsigned) wx, (unsigned) wy);
                }
                result = 1;
                break;
            }
            i++;
            j++;
        }
    }
    if (result == 0) {
        snprintf(out, cap, "the dumps are equal byte for byte");
    }
    free(ca);
    free(cb);
    return result;
}
