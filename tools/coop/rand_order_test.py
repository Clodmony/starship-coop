#!/usr/bin/env python3
"""Tests the order of two random draws in one expression (#199, #209).

    python tools/coop/rand_order_test.py                 # unattended; prints PASS or FAIL. No game, no ROM, seconds.
    python tools/coop/rand_order_test.py --no-compiler   # only the scanner part (check.py runs this)

Rand_ZeroOne is one sequence, and C leaves the order of two calls in one expression to the compiler. In Macbeth, a
Windows game took the right draw of `RAND_FLOAT(1.0f) - RAND_FLOAT(2.0f)` (src/engine/fox_tank.c) first and a Linux
game the left one, and the session parted. Titania has 18 statements of the form
`((Rand_ZeroOne() < 0.5f) ? -1 : 1) * (a + RAND_FLOAT(b))` (src/overlays/ovl_i5/fox_ti.c, #209). Two parts:

  scanner  tools/coop/rand_order.py on made-up statements: each class is found where C leaves the order open, nothing
           where the language orders the draws, and a statement with a CoopRand_ call counts as handled. Then its list
           check: a new site and a lost one both fail.
  helper   src/port/coop/math/rand_order.c compiled alone with the platform's own compiler (CC, cc, gcc, clang, or cl
           on Windows), its Rand_ZeroOne fed from a script, compared bit for bit with a Python reference that takes the
           draws left to right. Python's floats are doubles, and a double operation rounded to float is the float
           operation for + - and *, so the expected values are the same everywhere and need no table. Run it on Windows
           too: the same run on both platforms is the proof. CoopRand_Difference and CoopRand_SignedFloat both; for
           the second also the game's six spellings of the statement, each compiled alone, which must give the
           helper's bits in one order or the other (`-1` or `-1.0f`, `a + RAND_FLOAT(b)` or `RAND_FLOAT(b) + a`, no
           offset).

For information, the helper part also says in which order this compiler takes the game's own expressions, compiled
alone in a file of their own. That is a hint, not a rule: the compiler may pick otherwise inside the game's function.

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
    ("x = CoopPlayers_IsCoop() ? CoopRand_SignedFloat(4.0f, 3.0f)\n"
     "    : ((Rand_ZeroOne() < 0.5f) ? -1.0f : 1.0f) * (4.0f + RAND_FLOAT(3.0f));", None),
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
float Upstream_Signed(int spelling);

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

// Each line: D (CoopRand_Difference) or S (CoopRand_SignedFloat), the helper's two arguments, the two draws, and the
// spelling of the game's statement to compare with (-1: none).
int main(void) {
    char kind;
    unsigned a, b, r1, r2;
    int spelling;

    while (scanf(" %c %x %x %x %x %d", &kind, &a, &b, &r1, &r2, &spelling) == 6) {
        float coop, upstream = 0.0f;

        sDraws[0] = Float(r1);
        sDraws[1] = Float(r2);
        sNext = 0;
        coop = kind == 'D' ? CoopRand_Difference(Float(a), Float(b)) : CoopRand_SignedFloat(Float(a), Float(b));
        sNext = 0;
        if (kind == 'D') {
            upstream = Upstream_Difference();
        } else if (spelling >= 0) {
            upstream = Upstream_Signed(spelling);
        }
        printf("%08x %08x\n", Bits(coop), Bits(upstream));
    }
    return 0;
}
"""

# The game's own expressions (src/engine/fox_tank.c, src/overlays/ovl_i5/fox_ti.c, include/macros.h), alone in a file
# of their own. Upstream_Signed's cases are the six spellings of Titania's statement, in the order of SPELLINGS.
UPSTREAM = r"""
float Rand_ZeroOne(void);

float Upstream_Difference(void) {
    return (Rand_ZeroOne() * (1.0f)) - (Rand_ZeroOne() * (2.0f));
}

float Upstream_Signed(int spelling) {
    switch (spelling) {
        case 0:
            return ((Rand_ZeroOne() < 0.5f) ? -1.0f : 1.0f) * (4.0f + (Rand_ZeroOne() * (3.0f)));
        case 1:
            return ((Rand_ZeroOne() < 0.5f) ? -1.0f : 1.0f) * ((Rand_ZeroOne() * (1.0f)) + 0.5f);
        case 2:
            return ((Rand_ZeroOne() < 0.5f) ? -1.0f : 1.0f) * (Rand_ZeroOne() * (0.7f));
        case 3:
            return ((Rand_ZeroOne() < 0.5f) ? -1 : 1) * (15.0f + (Rand_ZeroOne() * (2.0f)));
        case 4:
            return ((Rand_ZeroOne() < 0.5f) ? -1 : 1) * ((Rand_ZeroOne() * (5.0f)) + 5.0f);
        default:
            return ((Rand_ZeroOne() < 0.5f) ? -1 : 1) * ((Rand_ZeroOne() * (1.0f)) + 1.0f);
    }
}
"""

# (offset, range) that src/overlays/ovl_i5/fox_ti.c passes to CoopRand_SignedFloat for each case of Upstream_Signed.
SPELLINGS = [(4.0, 3.0), (0.5, 1.0), (0.0, 0.7), (15.0, 2.0), (5.0, 5.0), (1.0, 1.0)]


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def bits32(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def difference(a, b, first, second):
    """CoopRand_Difference with draws `first` then `second`, in float arithmetic."""
    return bits32(f32(f32(first * a) - f32(second * b)))


def signed_float(offset, range_, sign_draw, value_draw):
    """CoopRand_SignedFloat with the sign from `sign_draw` and the value from `value_draw`, in float arithmetic."""
    sign = -1.0 if sign_draw < 0.5 else 1.0
    return bits32(f32(sign * f32(offset + f32(value_draw * range_))))


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
    """(kind, a, b, first draw, second draw, spelling) with floats: D for CoopRand_Difference with the game's scales and
    random ones, S for CoopRand_SignedFloat with the six spellings of Titania and random arguments (spelling -1)."""
    rng = random.Random(199)
    draws = [0.0, 0.5, f32(0.5 - 2 ** -25), f32(1.0 - 2 ** -24), f32(0.770901799), f32(0.557124853)]
    draws += [f32(rng.random()) for _ in range(300)]
    scales = [(1.0, 2.0), (2.0, 1.0), (10.0, 10.0), (0.5, 3.0)]
    scales += [(f32(rng.uniform(0, 500)), f32(rng.uniform(0, 500))) for _ in range(20)]
    out = []
    for a, b in scales:
        for _ in range(60):
            out.append(("D", f32(a), f32(b), rng.choice(draws), rng.choice(draws), -1))
    out.append(("D", 1.0, 2.0, f32(0.770901799), f32(0.557124853), -1))  # the two draws of the Macbeth report (#199)
    signed = [(f32(o), f32(r), k) for k, (o, r) in enumerate(SPELLINGS)]
    signed += [(f32(rng.uniform(0, 50)), f32(rng.uniform(0, 50)), -1) for _ in range(10)]
    for o, r, k in signed:
        for first in draws[:6]:
            for second in draws[:6]:
                out.append(("S", o, r, first, second, k))
        for _ in range(60):
            out.append(("S", o, r, rng.choice(draws), rng.choice(draws), k))
    return out


def test_helper():
    cc = find_compiler()
    if cc is None:
        return ["helper: no C compiler found (set PATH; cc, gcc, clang or cl)"], None
    exe = build(cc)
    if exe is None:
        return ["helper: the driver does not build"], None
    rows = cases()
    stdin = "".join(f"{kind} {bits32(a):08x} {bits32(b):08x} {bits32(r1):08x} {bits32(r2):08x} {k}\n"
                    for kind, a, b, r1, r2, k in rows)
    out = subprocess.run([str(exe)], input=stdin, capture_output=True, text=True, cwd=WORK).stdout.split("\n")
    failures = []
    order = {"D": [0, 0], "S": [0, 0]}  # cases the game's expression took [left first, right first]
    for (kind, a, b, r1, r2, k), line in zip(rows, out):
        parts = line.split()
        if len(parts) != 2:
            failures.append(f"helper: no answer for {kind} {a} {b} {r1} {r2}")
            break
        coop, upstream = int(parts[0], 16), int(parts[1], 16)
        name = "CoopRand_Difference" if kind == "D" else "CoopRand_SignedFloat"
        ref = difference if kind == "D" else signed_float
        want = ref(a, b, r1, r2)
        if coop != want:
            failures.append(f"helper: {name}({a}, {b}) with draws {r1}, {r2}: {coop:08x}, want {want:08x}")
        if kind == "S" and k < 0:
            continue
        if kind == "D":
            a, b = 1.0, 2.0  # the scales of the game's expression in Upstream_Difference
        written, swapped = ref(a, b, r1, r2), ref(a, b, r2, r1)
        if kind == "S" and upstream not in (written, swapped):
            failures.append(f"helper: the game's spelling {k} of the signed draw with draws {r1}, {r2}: {upstream:08x}, "
                            f"CoopRand_SignedFloat({a}, {b}) gives {written:08x} (sign first) or {swapped:08x}")
        if written != swapped:  # who goes first shows only when the two orders give different values
            order[kind][0 if upstream == written else 1] += 1
    if len(out) - 1 < len(rows) and not failures:
        failures.append(f"helper: {len(out) - 1} answers for {len(rows)} cases")

    def said(left, right, first, second):
        return f"{first if left and not right else second if right and not left else 'mixed'} ({left} cases {first}, " \
               f"{right} {second})"

    info = (f"{cc}, compiled alone: RAND_FLOAT(1.0f) - RAND_FLOAT(2.0f) takes the "
            f"{said(*order['D'], 'left draw first', 'right draw first')}; Titania's signed draw (6 spellings) takes the "
            f"{said(*order['S'], 'sign first', 'value first')}")
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
