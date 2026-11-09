#pragma once

// Two random draws in one expression of the game, taken in the order the source writes them, for a co-op session
// (#199).
//
// C leaves the order of two calls in one expression to the compiler. Rand_ZeroOne is one sequence for the whole game,
// so the order decides which draw lands in which operand. In Macbeth, `RAND_FLOAT(1.0f) - RAND_FLOAT(2.0f)` for the
// Landmaster's dust (func_tank_800438E0, src/engine/fox_tank.c) took the right draw first in a Windows (MSVC) game and
// the left draw first in a Linux (GCC) game: the same two numbers, two velocities, and the session parted. This takes
// the draws left to right, as the source reads and as GCC builds do. The game file calls it while CoopPlayers_IsCoop()
// is on, the gate the generator itself uses (src/sys/sys_math.c), and keeps its own expression otherwise.
// tools/coop/rand_order.py finds the expressions of this kind; tools/coop/rand_order_test.py compiles rand_order.c
// alone against a Python reference.
//
// Rand_ZeroOne is the game's, declared here as in include/sf64math.h, so rand_order.c builds without the game's
// headers.

#ifdef __cplusplus
extern "C" {
#endif

float Rand_ZeroOne(void);

// RAND_FLOAT(a) - RAND_FLOAT(b), with the draw for a first.
float CoopRand_Difference(float a, float b);

#ifdef __cplusplus
}
#endif
