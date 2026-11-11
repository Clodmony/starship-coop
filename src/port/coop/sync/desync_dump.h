#pragma once

// The dump of a desync frame between the two games of the pair (#189): each sends the bytes its state hash ran over,
// in numbered parts, and the game that holds both names the first field that differs, with both values, as
// tools/coop/desync_report.py diff does from the two report files. So every co-op log says what differed, and nobody
// has to fetch a report from the other PC.
//
// One message, SYNC_DUMP (0x0043, docs/coop/PROTOCOL.md), on the reliable lane:
//
//   u8 run, u32 frame, u8 sender, u16 part, u16 parts, u32 size, then up to COOP_DESYNC_DUMP_CHUNK bytes of the dump
//
// `parts` 0 means the sender holds no dump of that frame. The dump as sent ("the stream") is, for each chunk of the
// state dump in order: u8 region, u8 name length n, n bytes of name, s32 index (-1 for a single value), u32 size, the
// bytes. All numbers little-endian.
//
// This file holds the stream, the parts and the comparison, with no game, no session, no clock and no file:
// desync_game.c connects it, and tools/coop/desync_dump_test.py compiles it alone and compares its line with the
// Python tool's. What a peer sends is untrusted: every length is checked before anything is read or copied, sizes are
// capped, and a part out of place is refused.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "state_hash.h"

#ifdef __cplusplus
extern "C" {
#endif

// Bytes of the stream in one message. With its head that is 1164, below COOP_NET_MAX_MESSAGE (1176).
#define COOP_DESYNC_DUMP_CHUNK 1150
#define COOP_DESYNC_DUMP_HEAD 14
#define COOP_DESYNC_DUMP_MESSAGE_MAX (COOP_DESYNC_DUMP_HEAD + COOP_DESYNC_DUMP_CHUNK)
// A stream is about 115 KB for Corneria (101590 bytes in 397 chunks, #189). Anything above this is refused.
#define COOP_DESYNC_DUMP_MAX (2u * 1024u * 1024u)
// How long a game waits for the other dump before its log says that it did not come.
#define COOP_DESYNC_DUMP_MS 8000
// The longest line CoopDesyncDump_FirstDifference writes.
#define COOP_DESYNC_DUMP_LINE 384

// The size of the stream of `dump`, and the stream itself into out (which holds at least that much). Returns the
// bytes written, 0 if cap is too small.
size_t CoopDesyncDump_StreamSize(const CoopStateDump* dump);
size_t CoopDesyncDump_Stream(const CoopStateDump* dump, uint8_t* out, size_t cap);

// What one side sends: its stream, or none.
typedef struct {
    uint8_t run;
    uint32_t frame;
    uint8_t sender;
    const uint8_t* stream; // NULL for none
    uint32_t size;
    uint16_t parts;
    uint16_t next;  // the part that goes next; done when it is `parts` and announced
    bool announced; // with no stream: the one message that says so went out
} CoopDesyncDumpOut;

void CoopDesyncDump_OutBegin(CoopDesyncDumpOut* o, uint8_t run, uint32_t frame, uint8_t sender, const uint8_t* stream,
                             uint32_t size);
// The next message into `message` (COOP_DESYNC_DUMP_MESSAGE_MAX bytes); returns its size, 0 when everything is out.
// Call CoopDesyncDump_OutSent once it went.
uint16_t CoopDesyncDump_OutNext(const CoopDesyncDumpOut* o, uint8_t* message);
void CoopDesyncDump_OutSent(CoopDesyncDumpOut* o);
bool CoopDesyncDump_OutDone(const CoopDesyncDumpOut* o);

// What comes in from the other side.
typedef struct {
    bool begun;
    bool done;    // whole, or the sender said it has none
    bool none;    // the sender has no dump
    bool refused; // a part did not fit: nothing more is taken
    uint8_t run;
    uint32_t frame;
    uint8_t sender;
    uint8_t* stream; // owned, CoopDesyncDump_InFree
    uint32_t size;
    uint32_t got;
    uint16_t parts;
    uint16_t next;
} CoopDesyncDumpIn;

// A SYNC_DUMP message. It has to be of run, frame and sender as given (what this game's own desync names), parts have
// to come in order, and the sizes have to add up. False if it was dropped or refused.
bool CoopDesyncDump_InReceive(CoopDesyncDumpIn* in, uint8_t run, uint32_t frame, uint8_t sender, const uint8_t* data,
                              uint16_t size);
void CoopDesyncDump_InFree(CoopDesyncDumpIn* in);

// The first difference between two streams, as `desync_report.py diff` finds it: regions in order, in each the chunks
// in order, the first chunk whose bytes differ, its first differing byte and how many differ, and the aligned 32-bit
// word around that byte on both sides. `a` is the stream of the player with the lower index. Writes one line into out:
//   "<region>: <name>[<index>] differs in <n> of <size> bytes, first at +<offset>: <aa> vs <bb>; word at +<w>:
//    0x<8 hex> vs 0x<8 hex>"
// which starts as the Python tool's line does. Returns 1 for a difference, 0 when the streams are equal byte for byte
// ("the dumps are equal byte for byte"), -1 when they cannot be compared (a stream that cannot be read, or a region
// with different chunks on the two sides, which the line names).
int CoopDesyncDump_FirstDifference(const uint8_t* a, size_t aSize, const uint8_t* b, size_t bSize,
                                   const char* (*regionName)(uint8_t region), char* out, size_t cap);

#ifdef __cplusplus
}
#endif
