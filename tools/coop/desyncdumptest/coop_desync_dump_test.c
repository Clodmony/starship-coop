// Test program for the dump of a desync frame between two games (#189): src/port/coop/sync/desync_dump.c alone, no
// game, no socket. tools/coop/desync_dump_test.py compiles and runs it.
//
//   coop_desync_dump_test selftest        the stream of a state dump, the parts out and in, what is refused, the line
//   coop_desync_dump_test diff A B        the first difference of two stream files, as the game would log it; the
//                                         Python test compares it with desync_report.py diff on the same two dumps
//
// Prints `ok <name>: ...` per test and `FAIL ...` for a problem; exits 0 only when nothing failed.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "desync_dump.h"

static int sFailures = 0;

static void Fail(const char* test, const char* what) {
    printf("FAIL %s: %s\n", test, what);
    sFailures++;
}

#define EXPECT(cond, test, what) \
    do {                         \
        if (!(cond)) {           \
            Fail(test, what);    \
        }                        \
    } while (0)

// The regions of the game's hash, in order (src/port/coop/sync/state_hash.h), as the report's `regions:` line has them.
static const char* sRegions[] = { "players", "actors",     "bosses", "effects", "items", "scenery",
                                  "sprites", "scenery360", "shots",  "misc",    "rng",   "globals" };

static const char* Region_Name(uint8_t region) {
    return (region < sizeof(sRegions) / sizeof(sRegions[0])) ? sRegions[region] : NULL;
}

// ---- A small state dump: three regions, five chunks ----

#define DUMP_BYTES 64

static uint8_t sBytes[DUMP_BYTES];
static CoopStateChunk sChunks[5];

static void Dump_Make(CoopStateDump* dump) {
    static const struct {
        const char* name;
        int32_t index;
        uint8_t region;
        uint32_t size;
    } chunks[5] = {
        { "gPlayer", 0, 0, 16 },     { "gPlayer", 1, 0, 16 },    { "gActors", 59, 1, 9 },
        { "sRandSeed1", -1, 10, 4 }, { "gLifeCount", 0, 11, 2 },
    };
    uint32_t offset = 0;
    int i;

    for (i = 0; i < DUMP_BYTES; i++) {
        sBytes[i] = (uint8_t) (i * 7 + 3);
    }
    for (i = 0; i < 5; i++) {
        sChunks[i].name = chunks[i].name;
        sChunks[i].index = chunks[i].index;
        sChunks[i].region = chunks[i].region;
        sChunks[i].size = chunks[i].size;
        sChunks[i].offset = offset;
        offset += chunks[i].size;
    }
    memset(dump, 0, sizeof(*dump));
    dump->bytes = sBytes;
    dump->cap = sizeof(sBytes);
    dump->used = offset;
    dump->chunks = sChunks;
    dump->chunkCap = 5;
    dump->count = 5;
}

// The stream of the dump above, with the byte at `offset` of chunk `chunk` changed by `xor` (0 for none).
static uint8_t* Stream_Make(uint32_t* size, int chunk, uint32_t offset, uint8_t xor) {
    CoopStateDump dump;
    uint8_t* stream;
    uint8_t old;

    Dump_Make(&dump);
    old = sBytes[sChunks[chunk].offset + offset];
    sBytes[sChunks[chunk].offset + offset] = (uint8_t) (old ^ xor);
    *size = (uint32_t) CoopDesyncDump_StreamSize(&dump);
    stream = malloc(*size);
    if (CoopDesyncDump_Stream(&dump, stream, *size) != *size) {
        Fail("stream", "the stream is not as long as its size");
    }
    sBytes[sChunks[chunk].offset + offset] = old;
    return stream;
}

static void Test_Stream(void) {
    const char* test = "stream";
    // region, name length, name, index, size, bytes: the first chunk by hand, as PROTOCOL.md gives it.
    static const uint8_t head[] = { 0, 7, 'g', 'P', 'l', 'a', 'y', 'e', 'r', 0, 0, 0, 0, 16, 0, 0, 0, 3, 10, 17 };
    static const uint8_t single[] = { 10,  10,  's',  'R',  'a',  'n',  'd', 'S', 'e', 'e',
                                      'd', '1', 0xFF, 0xFF, 0xFF, 0xFF, 4,   0,   0,   0 };
    CoopStateDump dump;
    uint8_t small[8];
    uint8_t* stream;
    uint32_t size;
    uint32_t at;

    Dump_Make(&dump);
    stream = Stream_Make(&size, 0, 0, 0);
    EXPECT(size == 5 * 10 + 7 + 7 + 7 + 10 + 10 + 47, test, "the stream's size is not head, name and bytes per chunk");
    EXPECT(memcmp(stream, head, sizeof(head)) == 0, test, "the first chunk is not what PROTOCOL.md says");
    at = (10 + 7 + 16) * 2 + (10 + 7 + 9);
    EXPECT(memcmp(stream + at, single, sizeof(single)) == 0, test, "a single value does not carry index -1");
    EXPECT(CoopDesyncDump_Stream(&dump, small, sizeof(small)) == 0, test, "a stream was written into too little room");
    free(stream);
    printf("ok stream: %u bytes for 5 chunks, first chunk and a single value as PROTOCOL.md gives them\n",
           (unsigned) size);
}

// One side sends, the other takes: every size around the part boundaries.
static void Test_Parts(void) {
    const char* test = "parts";
    static const uint32_t sizes[] = { 1,
                                      2,
                                      COOP_DESYNC_DUMP_CHUNK - 1,
                                      COOP_DESYNC_DUMP_CHUNK,
                                      COOP_DESYNC_DUMP_CHUNK + 1,
                                      3 * COOP_DESYNC_DUMP_CHUNK,
                                      3 * COOP_DESYNC_DUMP_CHUNK + 7,
                                      117000 };
    uint8_t message[COOP_DESYNC_DUMP_MESSAGE_MAX];
    size_t s;
    uint32_t messages = 0;

    for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        uint32_t size = sizes[s];
        uint8_t* stream = malloc(size);
        CoopDesyncDumpOut out;
        CoopDesyncDumpIn in;
        uint32_t i;
        uint16_t n;

        for (i = 0; i < size; i++) {
            stream[i] = (uint8_t) (i * 31 + s);
        }
        memset(&in, 0, sizeof(in));
        CoopDesyncDump_OutBegin(&out, 3, 2000, 1, stream, size);
        EXPECT(out.parts == (size + COOP_DESYNC_DUMP_CHUNK - 1) / COOP_DESYNC_DUMP_CHUNK, test, "the wrong part count");
        while ((n = CoopDesyncDump_OutNext(&out, message)) != 0) {
            EXPECT(!in.done, test, "a part after the stream was whole");
            EXPECT(n <= COOP_DESYNC_DUMP_MESSAGE_MAX, test, "a message above its bound");
            EXPECT(CoopDesyncDump_InReceive(&in, 3, 2000, 1, message, n), test, "a good part was not taken");
            CoopDesyncDump_OutSent(&out);
            messages++;
        }
        EXPECT(CoopDesyncDump_OutDone(&out), test, "the sender is not done");
        EXPECT(in.done && !in.none && !in.refused && in.size == size && in.got == size, test,
               "the stream is not whole");
        EXPECT(in.stream != NULL && memcmp(in.stream, stream, size) == 0, test, "the stream did not arrive as it was");
        // Nothing more is taken once it is whole.
        CoopDesyncDump_OutBegin(&out, 3, 2000, 1, stream, size);
        n = CoopDesyncDump_OutNext(&out, message);
        EXPECT(!CoopDesyncDump_InReceive(&in, 3, 2000, 1, message, n), test, "a part was taken after the last one");
        CoopDesyncDump_InFree(&in);
        free(stream);
    }
    {
        // No dump: one message that says so.
        CoopDesyncDumpOut out;
        CoopDesyncDumpIn in;
        uint16_t n;

        memset(&in, 0, sizeof(in));
        CoopDesyncDump_OutBegin(&out, 3, 2000, 1, NULL, 0);
        EXPECT(!CoopDesyncDump_OutDone(&out), test, "nothing to say for a game without a dump");
        n = CoopDesyncDump_OutNext(&out, message);
        EXPECT(n == COOP_DESYNC_DUMP_HEAD, test, "the message for no dump is not a head alone");
        EXPECT(CoopDesyncDump_InReceive(&in, 3, 2000, 1, message, n) && in.done && in.none, test,
               "`no dump` was not taken");
        CoopDesyncDump_OutSent(&out);
        EXPECT(CoopDesyncDump_OutDone(&out) && CoopDesyncDump_OutNext(&out, message) == 0, test,
               "`no dump` goes out twice");
        // A stream above the cap is not sent: the other side hears `no dump`.
        CoopDesyncDump_OutBegin(&out, 3, 2000, 1, message, COOP_DESYNC_DUMP_MAX + 1);
        EXPECT(out.parts == 0 && out.stream == NULL, test, "a stream above the cap is sent");
    }
    printf("ok parts: %u sizes around the part boundaries, %u messages, and `no dump`\n",
           (unsigned) (sizeof(sizes) / sizeof(sizes[0])), (unsigned) messages);
}

static uint16_t Head(uint8_t* m, uint8_t run, uint32_t frame, uint8_t sender, uint16_t part, uint16_t parts,
                     uint32_t size, uint16_t bytes) {
    m[0] = run;
    m[1] = (uint8_t) frame;
    m[2] = (uint8_t) (frame >> 8);
    m[3] = (uint8_t) (frame >> 16);
    m[4] = (uint8_t) (frame >> 24);
    m[5] = sender;
    m[6] = (uint8_t) part;
    m[7] = (uint8_t) (part >> 8);
    m[8] = (uint8_t) parts;
    m[9] = (uint8_t) (parts >> 8);
    m[10] = (uint8_t) size;
    m[11] = (uint8_t) (size >> 8);
    m[12] = (uint8_t) (size >> 16);
    m[13] = (uint8_t) (size >> 24);
    memset(m + COOP_DESYNC_DUMP_HEAD, 0x5A, bytes);
    return (uint16_t) (COOP_DESYNC_DUMP_HEAD + bytes);
}

// What a peer could send that does not fit. Each case starts from nothing.
static void Test_Refused(void) {
    const char* test = "refused";
    uint8_t m[COOP_DESYNC_DUMP_MESSAGE_MAX + 8];
    CoopDesyncDumpIn in;
    int hostile = 0;

#define CASE(expectRefused, what, size)                                                 \
    do {                                                                                \
        bool taken = CoopDesyncDump_InReceive(&in, 3, 2000, 1, m, (size));              \
        EXPECT(!taken, test, what);                                                     \
        EXPECT(in.refused == (expectRefused), test, what " (refused or only dropped)"); \
        EXPECT(in.stream == NULL || in.begun, test, what " (memory)");                  \
        CoopDesyncDump_InFree(&in);                                                     \
        hostile++;                                                                      \
    } while (0)

    memset(&in, 0, sizeof(in));
    // Not what this game waits for: dropped, and the real one can still come.
    CASE(false, "a part of another run was taken", Head(m, 4, 2000, 1, 0, 1, 10, 10));
    CASE(false, "a part of another frame was taken", Head(m, 3, 2001, 1, 0, 1, 10, 10));
    CASE(false, "a part of another player was taken", Head(m, 3, 2000, 2, 0, 1, 10, 10));
    CASE(false, "a message shorter than its head was taken", (uint16_t) (COOP_DESYNC_DUMP_HEAD - 1));
    CASE(false, "an empty message was taken", 0);
    CASE(false, "a message above its bound was taken",
         (uint16_t) (Head(m, 3, 2000, 1, 0, 2, 2 * COOP_DESYNC_DUMP_CHUNK, COOP_DESYNC_DUMP_CHUNK) + 1));
    // Out of bounds or out of place: refused, and nothing more is taken.
    CASE(true, "a stream above the cap was taken",
         Head(m, 3, 2000, 1, 0, 1825, COOP_DESYNC_DUMP_MAX + 1, COOP_DESYNC_DUMP_CHUNK));
    CASE(true, "a stream of size 0 with parts was taken", Head(m, 3, 2000, 1, 0, 1, 0, 0));
    CASE(true, "`no dump` with a size was taken", Head(m, 3, 2000, 1, 0, 0, 10, 0));
    CASE(true, "`no dump` with bytes was taken", Head(m, 3, 2000, 1, 0, 0, 0, 4));
    CASE(true, "a part count that does not fit the size was taken", Head(m, 3, 2000, 1, 0, 2, 10, 10));
    CASE(true, "a part count of 1 for two parts was taken",
         Head(m, 3, 2000, 1, 0, 1, COOP_DESYNC_DUMP_CHUNK + 1, COOP_DESYNC_DUMP_CHUNK));
    CASE(true, "a stream that starts at its second part was taken",
         Head(m, 3, 2000, 1, 1, 2, COOP_DESYNC_DUMP_CHUNK + 5, 5));
    CASE(true, "a first part that is short was taken",
         Head(m, 3, 2000, 1, 0, 2, COOP_DESYNC_DUMP_CHUNK + 5, COOP_DESYNC_DUMP_CHUNK - 1));
    CASE(true, "a last part that is long was taken", Head(m, 3, 2000, 1, 0, 1, 10, 11));
    CASE(true, "a last part that is short was taken", Head(m, 3, 2000, 1, 0, 1, 10, 9));
    {
        // A second part that repeats the first, one that skips, and one that changes the size.
        static const struct {
            uint16_t part;
            uint16_t parts;
            uint32_t size;
        } second[] = { { 0, 3, 2 * COOP_DESYNC_DUMP_CHUNK + 5 },
                       { 2, 3, 2 * COOP_DESYNC_DUMP_CHUNK + 5 },
                       { 1, 3, 2 * COOP_DESYNC_DUMP_CHUNK + 6 },
                       { 1, 2, 2 * COOP_DESYNC_DUMP_CHUNK + 5 } };
        size_t i;

        for (i = 0; i < sizeof(second) / sizeof(second[0]); i++) {
            uint16_t n = Head(m, 3, 2000, 1, 0, 3, 2 * COOP_DESYNC_DUMP_CHUNK + 5, COOP_DESYNC_DUMP_CHUNK);

            EXPECT(CoopDesyncDump_InReceive(&in, 3, 2000, 1, m, n), test, "a good first part was not taken");
            n = Head(m, 3, 2000, 1, second[i].part, second[i].parts, second[i].size,
                     (second[i].part == 2) ? 5 : COOP_DESYNC_DUMP_CHUNK);
            EXPECT(!CoopDesyncDump_InReceive(&in, 3, 2000, 1, m, n) && in.refused && !in.done, test,
                   "a second part out of place was taken");
            n = Head(m, 3, 2000, 1, 1, 3, 2 * COOP_DESYNC_DUMP_CHUNK + 5, COOP_DESYNC_DUMP_CHUNK);
            EXPECT(!CoopDesyncDump_InReceive(&in, 3, 2000, 1, m, n), test, "a part was taken after a refusal");
            CoopDesyncDump_InFree(&in);
            hostile++;
        }
    }
#undef CASE
    printf("ok refused: %d messages that do not fit, none taken\n", hostile);
    printf("hostile messages: %d\n", hostile);
}

static void Test_Line(void) {
    const char* test = "line";
    char line[COOP_DESYNC_DUMP_LINE];
    uint32_t aSize, bSize;
    uint8_t* a = Stream_Make(&aSize, 0, 0, 0);
    uint8_t* b;
    int cases = 0;

    // Equal.
    b = Stream_Make(&bSize, 0, 0, 0);
    EXPECT(CoopDesyncDump_FirstDifference(a, aSize, b, bSize, Region_Name, line, sizeof(line)) == 0 &&
               strcmp(line, "the dumps are equal byte for byte") == 0,
           test, "two equal dumps differ");
    free(b);
    cases++;

    // One bit in the second chunk: region, label, count, offset, both bytes, both words.
    b = Stream_Make(&bSize, 1, 6, 0x01);
    EXPECT(CoopDesyncDump_FirstDifference(a, aSize, b, bSize, Region_Name, line, sizeof(line)) == 1, test,
           "a flipped bit was not found");
    // sBytes[16 + 6] = 22 * 7 + 3 = 157 = 0x9d; the word at +4 holds bytes 143, 150, 157, 164.
    EXPECT(strcmp(line,
                  "players: gPlayer[1] differs in 1 of 16 bytes, first at +6: 9d vs 9c; word at +4: 0xa49d968f vs "
                  "0xa49c968f") == 0,
           test, line);
    free(b);
    cases++;

    // The other way round: a is always the lower player's.
    b = Stream_Make(&bSize, 1, 6, 0x01);
    CoopDesyncDump_FirstDifference(b, bSize, a, aSize, Region_Name, line, sizeof(line));
    EXPECT(strstr(line, ": 9c vs 9d; word at +4: 0xa49c968f vs 0xa49d968f") != NULL, test,
           "the sides are not in order");
    free(b);
    cases++;

    // A single value has no index, and a chunk that ends inside the word has no word.
    b = Stream_Make(&bSize, 3, 3, 0x80);
    CoopDesyncDump_FirstDifference(a, aSize, b, bSize, Region_Name, line, sizeof(line));
    EXPECT(strncmp(line, "rng: sRandSeed1 differs in 1 of 4 bytes, first at +3: ", 54) == 0 &&
               strstr(line, "; word at +0: 0x") != NULL,
           test, line);
    free(b);
    b = Stream_Make(&bSize, 2, 8, 0xFF);
    CoopDesyncDump_FirstDifference(a, aSize, b, bSize, Region_Name, line, sizeof(line));
    EXPECT(strncmp(line, "actors: gActors[59] differs in 1 of 9 bytes, first at +8: ", 58) == 0 &&
               strstr(line, "word") == NULL,
           test, line);
    free(b);
    b = Stream_Make(&bSize, 4, 1, 0x10);
    CoopDesyncDump_FirstDifference(a, aSize, b, bSize, Region_Name, line, sizeof(line));
    EXPECT(strncmp(line, "globals: gLifeCount[0] differs in 1 of 2 bytes, first at +1: ", 61) == 0 &&
               strstr(line, "word") == NULL,
           test, line);
    free(b);
    cases += 3;

    // A region without a name here still gets one.
    EXPECT(CoopDesyncDump_FirstDifference(a, aSize, a, aSize, NULL, line, sizeof(line)) == 0, test,
           "no names, no line");
    b = Stream_Make(&bSize, 3, 0, 0x01);
    CoopDesyncDump_FirstDifference(a, aSize, b, bSize, NULL, line, sizeof(line));
    EXPECT(strncmp(line, "region 10: sRandSeed1 differs", 29) == 0, test, line);
    free(b);
    cases++;

    // A chunk that is not the same chunk: the region is named, nothing is compared.
    b = Stream_Make(&bSize, 0, 0, 0);
    b[2] = 'h'; // gPlayer[0] becomes hPlayer[0]
    EXPECT(CoopDesyncDump_FirstDifference(a, aSize, b, bSize, Region_Name, line, sizeof(line)) == -1 &&
               strcmp(line, "region players has different chunks in the two games") == 0,
           test, line);
    free(b);
    // A chunk less on one side.
    EXPECT(CoopDesyncDump_FirstDifference(a, aSize, a, aSize - (10 + 10 + 2), Region_Name, line, sizeof(line)) == -1 &&
               strcmp(line, "region globals has different chunks in the two games") == 0,
           test, line);
    cases += 2;

    // Streams that cannot be read: cut anywhere, a name that runs past the end, a size that does.
    {
        uint32_t cut;
        uint32_t unreadable = 0;

        for (cut = 1; cut < aSize; cut++) {
            int result = CoopDesyncDump_FirstDifference(a, cut, a, aSize, Region_Name, line, sizeof(line));

            EXPECT(result == -1, test, "a cut stream compared as whole");
            unreadable += strcmp(line, "a dump that cannot be read") == 0;
        }
        EXPECT(unreadable >= aSize - 6, test, "cut streams were read");
        b = Stream_Make(&bSize, 0, 0, 0);
        b[1] = 200; // a name longer than the stream
        EXPECT(CoopDesyncDump_FirstDifference(a, aSize, b, bSize, Region_Name, line, sizeof(line)) == -1, test,
               "a name past the end was read");
        b[1] = 7;
        b[13] = 0xFF;
        b[14] = 0xFF;
        b[15] = 0xFF;
        b[16] = 0xFF; // a chunk of 4 GB
        EXPECT(CoopDesyncDump_FirstDifference(a, aSize, b, bSize, Region_Name, line, sizeof(line)) == -1, test,
               "a size past the end was read");
        free(b);
        cases += 2;
        printf("ok line: %d cases, and %u cut streams, none read past its end\n", cases, (unsigned) aSize - 1);
    }
    // A name that is not printable comes out as `?`: it came over the wire.
    b = Stream_Make(&bSize, 3, 0, 0x01);
    {
        uint32_t at = (10 + 7 + 16) * 2 + (10 + 7 + 9) + 2;
        uint8_t* a2 = Stream_Make(&aSize, 0, 0, 0);

        a2[at] = '\n';
        b[at] = '\n';
        CoopDesyncDump_FirstDifference(a2, aSize, b, bSize, Region_Name, line, sizeof(line));
        EXPECT(strncmp(line, "rng: ?RandSeed1 differs", 23) == 0 && strchr(line, '\n') == NULL, test, line);
        free(a2);
    }
    free(b);
    free(a);
}

static uint8_t* File_Read(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    uint8_t* data;
    long length;

    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    length = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc((length > 0) ? (size_t) length : 1);
    *size = (length > 0) ? fread(data, 1, (size_t) length, f) : 0;
    fclose(f);
    return data;
}

int main(int argc, char** argv) {
    if ((argc == 4) && (strcmp(argv[1], "diff") == 0)) {
        char line[COOP_DESYNC_DUMP_LINE];
        size_t aSize, bSize;
        uint8_t* a = File_Read(argv[2], &aSize);
        uint8_t* b = File_Read(argv[3], &bSize);
        int result;

        if ((a == NULL) || (b == NULL)) {
            printf("FAIL diff: cannot read the streams\n");
            return 2;
        }
        result = CoopDesyncDump_FirstDifference(a, aSize, b, bSize, Region_Name, line, sizeof(line));
        printf("%d %s\n", result, line);
        free(a);
        free(b);
        return 0;
    }
    Test_Stream();
    Test_Parts();
    Test_Refused();
    Test_Line();
    return (sFailures == 0) ? 0 : 1;
}
