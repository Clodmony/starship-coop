#pragma once

// Arctangent and arccosine for a co-op session (#186, docs/coop/decisions/0011-deterministic-sine-in-sessions.md).
//
// The C library's atanf and acosf are not the same function on Windows and Linux, and the game turns things towards the
// camera with them (Math_Atan2F, 268 uses). CoopMath_FAtanF is the game's own original Math_FAtanF, the continued
// fraction in the comment in src/libc_math64.c, in float arithmetic; CoopMath_FAcosF is built on it. The same bits on
// every compiler that does not fuse multiply-adds (atan.c is built with -ffp-contract=off where the compiler has it).
//
// src/libc_math64.c calls these while CoopMath_InSession() (sincos.h) is on, and the C library otherwise.
// tools/coop/math_test.py compiles atan.c alone.

#ifdef __cplusplus
extern "C" {
#endif

float CoopMath_FAtanF(float x);
// 0 for 1, pi for -1, a NaN outside [-1, 1] and for a NaN, as the C library does.
float CoopMath_FAcosF(float x);

#ifdef __cplusplus
}
#endif
