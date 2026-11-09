#!/usr/bin/env python3
"""Tests the order of two random draws in one expression (#199).

    python tools/coop/rand_order_test.py                 # unattended; prints PASS or FAIL. No game, no ROM, seconds.
    python tools/coop/rand_order_test.py --no-compiler   # only the scanner part (check.py runs this)

Rand_ZeroOne is one sequence, and C leaves the order of two calls in one expression to the compiler. In Macbeth, a
Windows game took the right draw of `RAND_FLOAT(1.0f) - RAND_FLOAT(2.0f)` (src/engine/fox_tank.c) first and a Linux
game the left one, and the session parted. Two parts:

  scanner  tools/coop/rand_order.py on made-up statements: each class is found where C leaves the order open, nothing
           where the language orders the draws, and a statement with a CoopRand_ call counts as handled. Then its list
           check: a new site and a lost one both fail.
  helper   src/port/coop/math/rand_order.c compiled alone with the platform's own compiler (CC, cc, gcc, clang, or cl
           on Windows), its Rand_ZeroOne fed from a script, compared bit for bit with a Python reference that takes the
           draws left to right. Python's floats are doubles, and a double operation rounded to float is the float
           operation for + - and *, so the expected values are the same everywhere and need no table. Run it on Windows
           too: the same run on both platforms is the proof.

For information, the helper part also says in which order this compiler takes the game's own expression, compiled alone
in a file of its own. That is a hint, not a rule: the compiler may pick otherwise inside the game's function.

Exit code 0 when everything holds, 1 otherwise. The folder local/test-rand-order/ is deleted when all hold and kept for
reading when one fails.
"""
import argparse
import random
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "src" / "port" / "coop" / "math"
WORK = ROOT / "local" / "test-rand-order"
sys.path.insert(0, str(Path(__file__).resolve().parent))
import rand_order  # noqa: E402

# (statement, the classes rand_order.classify must find, or None for a statement the scan must not list)
SNIPPETS = [
    ("x = RAND_FLOAT(1.0f) - RAND_FLOAT(2.0f);", {"operator"}),
    ("x = ((Rand_ZeroOne() < 0.5f) ? -1 : 1) * (RAND_FLOAT(5.0f) + 5.0f);", {"operator"}),
    ("f(RAND_FLOAT(1.0f) + RAND_FLOAT(2.0f), 3.0f);", {"operator"}),
    ("x = RAND_FLOAT(2.0f) + RAND_FLOAT_SEEDED(3.0f) * y;", {"operator"}),
    ("f(RAND_FLOAT_CENTERED(10.0f) + a, b, RAND_FLOAT(5.0f) + c, 1.0f);", {"args"}),
    ("f(g(RAND_INT(4.0f)), RAND_FLOAT(2.0f));", {"args"}),
    ("Vec3f v = { RAND_FLOAT(1.0f), RAND_FLOAT(2.0f), 0.0f };", {"init"}),
    ("if ((Rand_ZeroOne() < 0.5f) && (Rand_ZeroOne() < 0.2f)) {", None),
    ("x = (Rand_ZeroOne() < 0.5f) ? RAND_FLOAT(1.0f) : RAND_FLOAT(2.0f);", None),
    ("x = RAND_FLOAT(RAND_FLOAT(2.0f));", None),
    ("for (i = RAND_INT(2.0f); i < RAND_INT(3.0f); i++) {", None),
    ("x = RAND_FLOAT(1.0f); y = RAND_FLOAT(2.0f);", None),
    ("x = (a = RAND_FLOAT(1.0f), a - RAND_FLOAT(2.0f));", None),
    ("x = CoopPlayers_IsCoop() ? CoopRand_Difference(1.0f, 2.0f) : RAND_FLOAT(1.0f) - RAND_FLOAT(2.0f);", None),
    ("// x = RAND_FLOAT(1.0f) - RAND_FLOAT(2.0f);\nx = 1.0f;", None),
    ('puts("RAND_FLOAT(1.0f) - RAND_FLOAT(2.0f)");', None),
    ("x = RAND_FLOAT(1.0f) - y;", None),
]

DRIVER = r"""
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rand_order.h"

float Upstream_Difference(void);

static float sDraws[2];
static int sNext;

float Rand_ZeroOne(void) {
    return sDraws[sNext++ & 1];
}

static float Float(uint32_t u) {
    float f;

    memcpy(&f, &u, sizeof(f));
    return f;
}

static uint32_t Bits(float f) {
    uint32_t u;

    memcpy(&u, &f, sizeof(u));
    return u;
}

int main(void) {
    unsigned a, b, r1, r2;

    while (scanf("%x %x %x %x", &a, &b, &r1, &r2) == 4) {
        float coop, upstream;

        sDraws[0] = Float(r1);
        sDraws[1] = Float(r2);
        sNext = 0;
        coop = CoopRand_Difference(Float(a), Float(b));
        sNext = 0;
        upstream = Upstream_Difference();
        printf("%08x %08x\n", Bits(coop), Bits(upstream));
    }
    return 0;
}
"""

# The game's own expression (src/engine/fox_tank.c, include/macros.h), alone in a file of its own.
UPSTREAM = r"""
float Rand_ZeroOne(void);

float Upstream_Difference(void) {
    return (Rand_ZeroOne() * (1.0f)) - (Rand_ZeroOne() * (2.0f));
}
"""


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def bits32(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def difference(a, b, first, second):
    """CoopRand_Difference with draws `first` then `second`, in float arithmetic."""
    return bits32(f32(f32(first * a) - f32(second * b)))


def test_scanner():
    failures = []
    for text, want in SNIPPETS:
        found = {cls for _, _, cls, _ in rand_order.sites(["snippet.c"], read=lambda _p, t=text: t + "\n")}
        if found != (want or set()):
            failures.append(f"scanner: {text!r}: found {sorted(found) or 'nothing'}, want {sorted(want or [])}")
    site = ("src/x.c", 1, "operator", "x = RAND_FLOAT(1.0f) - RAND_FLOAT(2.0f);")
    saved = dict(rand_order.KNOWN)
    try:
        rand_order.KNOWN.clear()
        if not rand_order.check([site]):
            failures.append("scanner: a site the list does not have passed the check")
        rand_order.KNOWN["src/x.c"] = {"operator": 2}
        if not rand_order.check([site]):
            failures.append("scanner: a site the list has but the code lost passed the check")
        rand_order.KNOWN["src/x.c"] = {"operator": 1}
        if rand_order.check([site]):
            failures.append("scanner: the list and the code agree, and the check failed")
    finally:
        rand_order.KNOWN.clear()
        rand_order.KNOWN.update(saved)
    return failures


def find_compiler():
    for name in ("cc", "gcc", "clang", "cl"):
        if shutil.which(name):
            return name
    return None


def build(cc):
    (WORK / "driver.c").write_text(DRIVER, encoding="utf-8")
    (WORK / "upstream.c").write_text(UPSTREAM, encoding="utf-8")
    exe = WORK / ("driver.exe" if sys.platform == "win32" else "driver")
    files = ["driver.c", "upstream.c", str(SRC / "rand_order.c")]
    if Path(cc).stem.lower() == "cl":
        cmd = [cc, "/nologo", "/O2", "/D_CRT_SECURE_NO_WARNINGS", f"/I{SRC}", *files, f"/Fe:{exe}"]
    else:
        cmd = [cc, "-O2", "-std=c11", "-ffp-contract=off", "-I", str(SRC), *files, "-o", str(exe)]
    done = subprocess.run(cmd, cwd=WORK, capture_output=True, text=True)
    if done.returncode != 0:
        print("rand_order_test: the driver does not build:\n" + (done.stdout + done.stderr)[-1500:])
        return None
    return exe


def cases():
    """(a, b, first draw, second draw) as floats: the game's scales and random ones, edge and random draws."""
    rng = random.Random(199)
    draws = [0.0, 0.5, f32(0.5 - 2 ** -25), f32(1.0 - 2 ** -24), 0.770901799, 0.557124853]
    draws += [f32(rng.random()) for _ in range(300)]
    scales = [(1.0, 2.0), (2.0, 1.0), (10.0, 10.0), (0.5, 3.0)]
    scales += [(f32(rng.uniform(0, 500)), f32(rng.uniform(0, 500))) for _ in range(20)]
    out = []
    for a, b in scales:
        for _ in range(60):
            out.append((f32(a), f32(b), rng.choice(draws), rng.choice(draws)))
    out.append((1.0, 2.0, f32(0.770901799), f32(0.557124853)))  # the two draws of the Macbeth report (#199)
    return out


def test_helper():
    cc = find_compiler()
    if cc is None:
        return ["helper: no C compiler found (set PATH; cc, gcc, clang or cl)"], None
    exe = build(cc)
    if exe is None:
        return ["helper: the driver does not build"], None
    rows = cases()
    stdin = "".join(f"{bits32(a):08x} {bits32(b):08x} {bits32(r1):08x} {bits32(r2):08x}\n" for a, b, r1, r2 in rows)
    out = subprocess.run([str(exe)], input=stdin, capture_output=True, text=True, cwd=WORK).stdout.split("\n")
    failures = []
    left = right = 0
    for (a, b, r1, r2), line in zip(rows, out):
        parts = line.split()
        if len(parts) != 2:
            failures.append(f"helper: no answer for {a} {b} {r1} {r2}")
            break
        coop, upstream = int(parts[0], 16), int(parts[1], 16)
        want = difference(a, b, r1, r2)
        if coop != want:
            failures.append(f"helper: CoopRand_Difference({a}, {b}) with draws {r1}, {r2}: {coop:08x}, want {want:08x}")
        if (r1, r2) != (r1, r1) and r1 != r2:  # who goes first shows only when the draws differ
            if upstream == difference(1.0, 2.0, r1, r2) != difference(1.0, 2.0, r2, r1):
                left += 1
            elif upstream == difference(1.0, 2.0, r2, r1) != difference(1.0, 2.0, r1, r2):
                right += 1
    if len(out) - 1 < len(rows) and not failures:
        failures.append(f"helper: {len(out) - 1} answers for {len(rows)} cases")
    order = "left draw first" if left and not right else "right draw first" if right and not left else "mixed"
    info = (f"{cc} takes RAND_FLOAT(1.0f) - RAND_FLOAT(2.0f), compiled alone: {order} "
            f"({left} cases left first, {right} right first)")
    return failures, info


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--no-compiler", action="store_true", help="only the scanner part")
    args = p.parse_args()
    failures = test_scanner()
    print(f"rand_order_test: scanner: {len(SNIPPETS)} statements and the list check: "
          f"{'FAIL' if failures else 'ok'}")
    if not args.no_compiler:
        shutil.rmtree(WORK, ignore_errors=True)
        WORK.mkdir(parents=True)
        helper, info = test_helper()
        failures += helper
        if info:
            print(f"rand_order_test: info: {info}")
        print(f"rand_order_test: helper: {len(cases())} cases: {'FAIL' if helper else 'ok'}")
        if not failures:
            shutil.rmtree(WORK, ignore_errors=True)
    for f in failures:
        print(f"FAIL: {f}")
    print(f"rand_order_test: {'FAIL' if failures else 'PASS'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
