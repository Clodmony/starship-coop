#pragma once

// The sound thread's voice line, as the radio reads it (#230). See docs/coop/NOTES.md, 2026-11-10, #230.
//
// The radio steps when the voice sequence says a line is over (Audio_GetCurrentVoice, src/engine/fox_radio.c and
// src/overlays/ovl_menu/fox_map.c). The sound thread runs that sequence on its own tempo: per audio update its player
// adds the tempo to an accumulator and takes a sequence tick each time that passes a limit, so where a line ends falls
// on one frame or the next by where the accumulator stood. The player starts over only at an audio heap reset, and
// what it did before that is the game's own history. Two games whose voice players started on different frames end
// the same line on different frames, and their radio steps apart.
//
// Read on the game thread between two audio frames (push_frame in src/port/Game.cpp), when the sound thread waits.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t tempo;        // sequence ticks per minute, as the player has it
    uint16_t tempoAcc;     // where between two sequence ticks the player stands
    uint16_t delay;        // sequence ticks before the player's own script goes on
    uint16_t channelDelay; // the same for channel 15, the one that plays a line; 0 without that channel
    int8_t lineIo;         // channel 15's IO port 1, what Audio_GetCurrentVoice reads; -2 without that channel
    uint8_t pad[3];        // 0, so the struct hashes the same on every build
} CoopVoiceClock;

// False while the voice sequence player is stopped (an audio heap reset is under way, or none has run since boot): its
// fields then hold whatever it last had, and the next start sets them. out is cleared either way.
bool CoopVoiceClock_Read(CoopVoiceClock* out);

// Test only (hash_check.py probe): the first byte of the player's tempo accumulator, or NULL while it is stopped.
uint8_t* CoopVoiceClock_ProbeByte(void);

#ifdef __cplusplus
}
#endif
