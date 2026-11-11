// The N64's sine and cosine for a co-op session, see sincos.h.
//
// This is the algorithm of src/libultra/gu/sinf.c and cosf.c, line for line, with three changes that do not touch the
// arithmetic: the constants are given as the bits of the doubles they spell (no union with a byte order), the float's
// bits are read with memcpy (no pointer cast), and the quiet NaN is spelled out. The expected values in
// tools/coop/math_test.py come from a Python port of the same lines.

#include "sincos.h"

#include <stdint.h>
#include <string.h>

static int sInSession = 0;

void CoopMath_SetSession(int on) {
    sInSession = (on != 0);
}

int CoopMath_InSession(void) {
    return sInSession;
}

// A compiler folds these into constants; written as calls so that no table has to be set up at start, which an audio
// thread could race with (the audio code calls __sinf and __cosf too).
static double Bits(uint64_t bits) {
    double d;

    memcpy(&d, &bits, sizeof(d));
    return d;
}

#define P1 Bits(0xBFC55554BC83656DULL)
#define P2 Bits(0x3F8110ED3804C2A0ULL)
#define P3 Bits(0xBF29F6FFEEA56814ULL)
#define P4 Bits(0x3EC5DBDF0E314BFEULL)
#define RPI Bits(0x3FD45F306DC9C883ULL)  // 1 / pi
#define PIHI Bits(0x400921FB50000000ULL) // pi, high part
#define PILO Bits(0x3E6110B4611A6263ULL) // pi, low part

static float QuietNaN(void) {
    uint32_t bits = 0x7FC00000u;
    float f;

    memcpy(&f, &bits, sizeof(f));
    return f;
}

// libultra's ROUND: half away from zero, then C's cast to int.
static int Round(double d) {
    return (int) ((d >= 0.0) ? (d + 0.5) : (d - 0.5));
}

// sin(dx) for dx within +/- pi/2: dx + dx^3 * (P1 + P2 x^2 + P3 x^4 + P4 x^6)
static double Poly(double dx) {
    double xsq = dx * dx;
    double poly = ((P4 * xsq + P3) * xsq + P2) * xsq + P1;

    return dx + (dx * xsq) * poly;
}

float CoopMath_Sinf(float x) {
    double dx, dn, result;
    int32_t ix;
    int n, xpt;

    memcpy(&ix, &x, sizeof(ix));
    xpt = (int) (((uint32_t) ix >> 22) & 0x1FF); // exponent and one bit of the mantissa

    if (xpt < 0xFF) {
        // |x| < 1.5
        if (xpt >= 0xE6) {
            // |x| >= 2^-12
            return (float) Poly((double) x);
        }
        return x;
    }

    if (xpt < 0x136) {
        // |x| < 2^28: reduce to +/- pi/2, and negate the result when n is odd
        dx = (double) x;
        dn = dx * RPI;
        n = Round(dn);
        dn = (double) n;
        dx = dx - dn * PIHI;
        dx = dx - dn * PILO;
        result = Poly(dx);
        if ((n & 1) == 0) {
            return (float) result;
        }
        return -(float) result;
    }

    if (x != x) {
        return QuietNaN();
    }
    return 0.0f;
}

float CoopMath_Cosf(float x) {
    double dx, dn, result;
    float absx;
    int32_t ix;
    int n, xpt;

    memcpy(&ix, &x, sizeof(ix));
    xpt = (int) (((uint32_t) ix >> 22) & 0x1FF);

    if (xpt < 0x136) {
        // |x| < 2^28: Cody and Waite, in double precision
        absx = (x < 0) ? -x : x;
        dx = (double) absx;
        dn = dx * RPI + 0.5;
        n = Round(dn);
        dn = (double) n;
        dn -= 0.5;
        dx = dx - dn * PIHI;
        dx = dx - dn * PILO; // dx = x - (n - 0.5) * pi
        result = Poly(dx);
        if ((n & 1) == 0) {
            return (float) result;
        }
        return -(float) result;
    }

    if (x != x) {
        return QuietNaN();
    }
    return 0.0f;
}
