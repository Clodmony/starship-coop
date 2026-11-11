// Two random draws in their written order for a co-op session, see rand_order.h.
//
// Each draw goes into a variable of its own before the next one is taken, so no compiler can reorder them: the end of
// a full expression is a sequence point. The arithmetic is what the game's macros spell (include/macros.h), in float,
// and nothing is fused (built with -ffp-contract=off where the compiler has it).

#include "rand_order.h"

float CoopRand_Difference(float a, float b) {
    float first = Rand_ZeroOne() * a;
    float second = Rand_ZeroOne() * b;

    return first - second;
}

float CoopRand_SignedFloat(float offset, float range) {
    float sign = (Rand_ZeroOne() < 0.5f) ? -1.0f : 1.0f;
    float value = Rand_ZeroOne() * range;

    return sign * (offset + value);
}
