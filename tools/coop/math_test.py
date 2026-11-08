#!/usr/bin/env python3
"""Tests the sine and cosine that a co-op session uses instead of the C library (#186, decision 0011).

    python tools/coop/math_test.py     # unattended; prints PASS or FAIL. No game, no ROM, a few seconds.

Windows and Linux part in a session because their C libraries round some sinf inputs differently. The session's sine
(src/port/coop/math/sincos.c) is the N64 libultra algorithm in plain double arithmetic, so it must give the same bits
everywhere. This test compiles that one file with the platform's own compiler and compares it, bit for bit, with a
Python port of the same algorithm. Python's floats are IEEE doubles and use no C library math here, so the expected
values are the same on every machine and need no stored table. The inputs are the angles the game feeds (SIN_DEG and
COS_DEG: every half degree from -720 to 720, as the game computes them), random floats, and the edges of each branch.

It also prints, for information, how many inputs the platform's own sinf and cosf round differently from the N64
algorithm: on those a Windows and a Linux game would part without the session's sine.

Needs a C compiler (CC, cc, gcc, clang, or cl on Windows). Exit code 0 when every input holds, 1 otherwise. The folder
local/test-math/ is deleted when all hold and kept for reading when one fails.
"""
import math
import os
import random
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "src" / "port" / "coop" / "math"
WORK = ROOT / "local" / "test-math"

# The constants of libultra's sinf.c / cosf.c (src/libultra/gu), as the bits of the doubles they spell.
P1 = struct.unpack("<d", struct.pack("<Q", 0xBFC55554BC83656D))[0]
P2 = struct.unpack("<d", struct.pack("<Q", 0x3F8110ED3804C2A0))[0]
P3 = struct.unpack("<d", struct.pack("<Q", 0xBF29F6FFEEA56814))[0]
P4 = struct.unpack("<d", struct.pack("<Q", 0x3EC5DBDF0E314BFE))[0]
RPI = struct.unpack("<d", struct.pack("<Q", 0x3FD45F306DC9C883))[0]
PIHI = struct.unpack("<d", struct.pack("<Q", 0x400921FB50000000))[0]
PILO = struct.unpack("<d", struct.pack("<Q", 0x3E6110B4611A6263))[0]
QNAN = 0x7FC00000

DRIVER = r"""
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sincos.h"

int main(void) {
    unsigned in;
    int before, during, after;

    before = CoopMath_InSession();
    CoopMath_SetSession(1);
    during = CoopMath_InSession();
    CoopMath_SetSession(0);
    after = CoopMath_InSession();
    printf("S %d %d %d\n", before, during, after);
    while (scanf("%x", &in) == 1) {
        uint32_t a = in, s, c, ls, lc;
        float x, r;

        memcpy(&x, &a, 4);
        r = CoopMath_Sinf(x);
        memcpy(&s, &r, 4);
        r = CoopMath_Cosf(x);
        memcpy(&c, &r, 4);
        r = sinf(x);
        memcpy(&ls, &r, 4);
        r = cosf(x);
        memcpy(&lc, &r, 4);
        printf("%08x %08x %08x %08x %08x\n", a, s, c, ls, lc);
    }
    return 0;
}
"""


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def bits32(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def from_bits(b):
    return struct.unpack("<f", struct.pack("<I", b))[0]


def rounded(d):
    """libultra's ROUND(d): half away from zero, then C's cast to int (toward zero)."""
    return int(d + 0.5) if d >= 0.0 else int(d - 0.5)


def poly_of(dx):
    xsq = dx * dx
    poly = ((P4 * xsq + P3) * xsq + P2) * xsq + P1
    return dx + (dx * xsq) * poly


def xpt_of(xb):
    ix = xb - (1 << 32) if xb >= (1 << 31) else xb
    return (ix >> 22) & 0x1FF


def ref_sin(xb):
    """src/libultra/gu/sinf.c, line for line. Returns the bits of the float result."""
    x = from_bits(xb)
    xpt = xpt_of(xb)
    if xpt < 0xFF:
        if xpt >= 0xE6:
            return bits32(f32(poly_of(x)))
        return xb
    if xpt < 0x136:
        dx = x
        dn = dx * RPI
        n = rounded(dn)
        dn = float(n)
        dx = dx - dn * PIHI
        dx = dx - dn * PILO
        r = f32(poly_of(dx))
        return bits32(r) if (n & 1) == 0 else bits32(-r)
    if x != x:
        return QNAN
    return 0


def ref_cos(xb):
    """src/libultra/gu/cosf.c, line for line."""
    x = from_bits(xb)
    xpt = xpt_of(xb)
    if xpt < 0x136:
        absx = -x if x < 0 else x
        dx = absx
        dn = dx * RPI + 0.5
        n = rounded(dn)
        dn = float(n)
        dn -= 0.5
        dx = dx - dn * PIHI
        dx = dx - dn * PILO
        r = f32(poly_of(dx))
        return bits32(r) if (n & 1) == 0 else bits32(-r)
    if x != x:
        return QNAN
    return 0


def inputs():
    """The bits of every float the test feeds. 0x406a927f (210 degrees) is the input of the first desync, frame 22."""
    rng = random.Random(186)
    seen = {}

    def add(b):
        seen[b & 0xFFFFFFFF] = True

    for i in range(-1440, 1441):  # SIN_DEG / COS_DEG: __sinf(M_DTOR * angle), M_DTOR = (M_PI / 180.0f) as a double
        add(bits32(f32((math.pi / 180.0) * f32(i / 2.0))))
    for _ in range(6000):
        add(bits32(f32(rng.uniform(-1000.0, 1000.0))))
    for _ in range(1500):
        add(bits32(f32(rng.uniform(-8.0, 8.0))))
    for _ in range(500):
        add(bits32(f32(rng.uniform(-1e8, 1e8))))
    edges = [0.0, 1.0, 1.5, 2.0 ** -12, 2.0 ** -13, 2.0 ** 28, 2.0 ** 27, math.pi, math.pi / 2, 7 * math.pi / 6]
    for e in edges:
        for sign in (1.0, -1.0):
            b = bits32(f32(sign * e))
            for step in range(-3, 4):
                add(b + step)
    for b in (0x80000000, 0x00000001, 0x7F800000, 0xFF800000, 0x7FC00000, 0xFFC00000, 0x7F7FFFFF, 0x406A927F):
        add(b)
    return list(seen)


def find_compiler():
    for name in (os.environ.get("CC"), "cc", "gcc", "clang", "cl"):
        if name and shutil.which(name):
            return name
    sys.exit("math_test: no C compiler found (set CC)")


def build(cc):
    (WORK / "driver.c").write_text(DRIVER, encoding="utf-8")
    exe = WORK / ("driver.exe" if sys.platform == "win32" else "driver")
    sincos = SRC / "sincos.c"
    if Path(cc).stem.lower() == "cl":
        cmd = [cc, "/nologo", "/O2", "/D_CRT_SECURE_NO_WARNINGS", f"/I{SRC}", "driver.c", str(sincos), f"/Fe:{exe}"]
    else:
        cmd = [cc, "-O2", "-std=c11", "-ffp-contract=off", "-I", str(SRC), "driver.c", str(sincos), "-o", str(exe),
               "-lm"]
    done = subprocess.run(cmd, cwd=WORK, capture_output=True, text=True)
    if done.returncode != 0:
        print("math_test: FAIL, the driver does not build:\n" + (done.stdout + done.stderr)[-1500:])
        return None
    return exe


def main():
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    if not (SRC / "sincos.c").exists():
        print(f"math_test: FAIL, {SRC / 'sincos.c'} does not exist")
        return 1
    cc = find_compiler()
    exe = build(cc)
    if exe is None:
        return 1
    xs = inputs()
    out = subprocess.run([str(exe)], input="\n".join(f"{b:08x}" for b in xs) + "\n", capture_output=True, text=True,
                         cwd=WORK)
    lines = out.stdout.splitlines()
    failures = []
    if out.returncode != 0 or not lines:
        failures.append(f"the driver exited with {out.returncode}")
    else:
        if lines[0] != "S 0 1 0":
            failures.append(f"the session switch reads '{lines[0]}', expected 'S 0 1 0' (off, on, off)")
        rows = [line.split() for line in lines[1:]]
        if len(rows) != len(xs):
            failures.append(f"the driver answered {len(rows)} of {len(xs)} inputs")
        differ_sin = differ_cos = 0
        for row in rows:
            a, s, c, ls, lc = (int(v, 16) for v in row)
            if s != ref_sin(a):
                failures.append(f"sin {a:08x}: compiled {s:08x}, reference {ref_sin(a):08x}")
            if c != ref_cos(a):
                failures.append(f"cos {a:08x}: compiled {c:08x}, reference {ref_cos(a):08x}")
            differ_sin += (s != ls) and not (math.isnan(from_bits(s)) and math.isnan(from_bits(ls)))
            differ_cos += (c != lc) and not (math.isnan(from_bits(c)) and math.isnan(from_bits(lc)))
            if a == 0x406A927F:
                print(f"math_test: the frame-22 input 0x406a927f: N64 sine {s:08x}, this platform's sinf {ls:08x} "
                      f"(a Windows host needed bf000000, a glibc client got beffffff)")
        print(f"math_test: {len(rows)} inputs; this platform's C library differs from the N64 algorithm on "
              f"{differ_sin} sines and {differ_cos} cosines (information, not a failure)")
    if failures:
        print(f"math_test: FAIL, {len(failures)} problems; first {min(10, len(failures))}:")
        for f in failures[:10]:
            print("  " + f)
        print(f"math_test: kept {WORK}")
        return 1
    shutil.rmtree(WORK, ignore_errors=True)
    print("math_test: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
