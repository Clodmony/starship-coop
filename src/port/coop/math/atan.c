// The game's own arctangent for a co-op session, see atan.h.
//
// CoopMath_FAtanF is the commented-out original of Math_FAtanF in src/libc_math64.c, line for line, in float
// arithmetic; every float operation rounds to float, and nothing is fused. M_PI is spelled as the bits of the double it
// is, as strict C does not define it. The expected values in tools/coop/math_test.py come from a Python port of the
// same lines.

#include "atan.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static double Bits(uint64_t bits) {
    double d;

    memcpy(&d, &bits, sizeof(d));
    return d;
}

#define PI Bits(0x400921FB54442D18ULL) // M_PI

float CoopMath_FAtanF(float x) {
    int sector;
    int i;
    float sq;
    float conv = 0.0f;
    float z;

    if (x > 1.0f) {
        sector = 1;
        x = 1.0f / x;
    } else if (x < -1.0f) {
        sector = -1;
        x = 1.0f / x;
    } else {
        sector = 0;
    }

    sq = x * x;

    z = 24.0f;
    for (i = 24; i != 0; i--) {
        conv = z * z * sq / (2.0f * z + 1.0f + conv);
        z -= 1.0f;
    }

    // M_PI / 2.0f is a double, so the difference is one rounding, at the return, as in the original.
    if (sector > 0) {
        return (float) (PI / 2.0 - (x / (1.0f + conv)));
    } else if (sector < 0) {
        return (float) (-PI / 2.0 - (x / (1.0f + conv)));
    } else {
        return x / (1.0f + conv);
    }
}

float CoopMath_FAcosF(float x) {
    float pi = (float) PI;
    float t;

    if (x != x) {
        return x;
    }
    if ((x > 1.0f) || (x < -1.0f)) {
        // outside the domain: a NaN, as the C library gives, so a comparison with it stays false as before
        uint32_t nan = 0x7FC00000u;
        float f;

        memcpy(&f, &nan, sizeof(f));
        return f;
    }
    if (x == 1.0f) {
        return 0.0f;
    }
    if (x == -1.0f) {
        return pi;
    }
    if (x == 0.0f) {
        return (float) (PI / 2.0);
    }
    // sqrtf is exactly rounded on every IEEE platform
    t = sqrtf(1.0f - x * x);
    if (x > 0.0f) {
        return CoopMath_FAtanF(t / x);
    }
    return pi - CoopMath_FAtanF(t / -x);
}
