#pragma once

// Sine and cosine for a co-op session (#186, docs/coop/decisions/0011-deterministic-sine-in-sessions.md).
//
// The C library rounds some sinf and cosf inputs differently on Windows and on Linux, so two games of one session part.
// These two are the N64's own libultra algorithm (src/libultra/gu/sinf.c and cosf.c): argument reduction and a
// polynomial, all in double arithmetic, with the result rounded to float once. The same bits on every compiler that
// does not fuse multiply-adds (this file is built with -ffp-contract=off where the compiler has it).
//
// Nothing here knows the game: tools/coop/math_test.py compiles sincos.c alone.

#ifdef __cplusplus
extern "C" {
#endif

float CoopMath_Sinf(float x);
float CoopMath_Cosf(float x);

// On while a session is running: the game's __sinf and __cosf (src/port/Engine.cpp) then call the two above, and the C
// library otherwise, as upstream. Set by the co-op menu glue when a level starts and when the session ends.
void CoopMath_SetSession(int on);
int CoopMath_InSession(void);

#ifdef __cplusplus
}
#endif
