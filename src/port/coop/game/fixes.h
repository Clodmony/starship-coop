#pragma once

// Faults of the game code that a session does not carry on (#262). Each function here stands where the game reads
// something it should not trust: an `iwork` word tested for a sign it cannot have in the port, an effect slot a boss
// remembers and no longer owns, a textured line that still holds its last user's values.
//
// None of them can part two games by itself: every 64-bit build computes the same. They are here because they make a
// session depend on what an earlier object left behind, or play differently from the N64. Outside co-op every function
// gives what upstream's code gives, so a game without a session stays upstream's, frame for frame (docs/coop/DESIGN.md,
// the gate).

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Effect;

// `word < 0` for a word of Actor.iwork. iwork is s32 on the N64 and uintptr_t in the port (include/sf64object.h), where
// that test is never true. In a session: the N64's answer, the word read as s32. Outside co-op: 0, as upstream.
int32_t CoopFixes_IworkNegative(uintptr_t word);

// A word of Actor.iwork as a factor. In a session: its s32 value, so -1 is -1. Outside co-op: the unsigned word as a
// float, as upstream computes it (-1 is about 1.8e19).
float CoopFixes_IworkFactor(uintptr_t word);

// Whether a remembered effect slot still holds the beam it was remembered for: an active OBJ_EFFECT_395 in `state`
// (2: a beam in the hand of Aquas' Garoa; 10: the beam of Area 6's Gorgon). Outside co-op: always 1, upstream does not
// look. What it cannot tell: another beam of the same kind that took the slot after the first one ended.
int32_t CoopFixes_HoldsBeam(const struct Effect* effect, int32_t state);

// The Gorgon's beam, for Area6_A6Gorgon_Update's state 7: gEffects[slot] while that still is the beam (or outside
// co-op, as upstream), else a stand-in that is zeroed on each call. With the stand-in the Gorgon's code writes nowhere
// in the game's state and ends its beam phase as soon as its cover is shut (the beam's width reads 0).
struct Effect* CoopFixes_GorgonBeam(int32_t slot);

// For a user of gTexturedLines[index], before it writes the line: in a session a line that is free (mode 0) is cleared
// first, as every other object array clears a slot that is taken (Effect_Initialize and its like), so the fields this
// user does not set are 0 and not the last user's. A line in use is left alone: most users write their line again on
// every frame. Outside co-op, and for an index outside the array, nothing.
void CoopFixes_TakeLine(int32_t index);

#ifdef __cplusplus
}
#endif
