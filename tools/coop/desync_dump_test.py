#!/usr/bin/env python3
"""Tests the dump of a desync frame between two games (#189): src/port/coop/sync/desync_dump.c alone, no game, no
socket, and its line against the Python tool's.

    python tools/coop/desync_dump_test.py     # unattended; prints PASS or FAIL. No build, no ROM, a few seconds.

After a desync each game of the pair sends the other the bytes its state hash ran over (SYNC_DUMP), and the game that
holds both writes `first difference ...` into its co-op log. desync_dump.c holds the stream, the parts and the
comparison, with nothing of the game in it, so this test compiles it with
tools/coop/desyncdumptest/coop_desync_dump_test.c and the platform's own compiler, with AddressSanitizer and UBSan
where the compiler has them, and runs it:

  stream   the stream of a state dump is what PROTOCOL.md says, chunk by chunk
  parts    every size around the part boundaries goes out and comes in whole; a game without a dump says so
  refused  a part of another run, frame or player, a size above the cap, a part out of order or of the wrong size:
           none is taken, and nothing is read or written past its end
  line     equal dumps, one differing byte, a single value, a chunk that ends inside the word, a region with
           different chunks, and streams cut at every byte
  golden   the same two dumps as two report files and as two streams: `desync_report.py diff` and the C code name
           the same region, field, count, offset, bytes and words, for a set of random pairs

The exchange between two real games is `lockstep_test.py --only survive_desync`. Needs a C compiler (CC, cc, gcc,
clang, or cl on Windows). Exit code 0 when everything holds, 1 otherwise. The folder local/test-desync-dump/ is deleted
when all hold and kept for reading when one fails.
"""
import os
import random
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "src" / "port" / "coop" / "sync"
DRIVER = ROOT / "tools" / "coop" / "desyncdumptest" / "coop_desync_dump_test.c"
WORK = ROOT / "local" / "test-desync-dump"
TESTS = ["stream", "parts", "refused", "line"]
MIN_HOSTILE = 15
GOLDEN_PAIRS = 40
# src/port/coop/sync/state_hash.c, sRegionNames: the order of CoopHashRegion.
REGIONS = ["players", "actors", "bosses", "effects", "items", "scenery", "sprites", "scenery360", "shots", "misc",
           "rng", "globals"]
FNV_OFFSET, FNV_PRIME, MASK = 0xCBF29CE484222325, 0x100000001B3, (1 << 64) - 1

sys.path.insert(0, str(Path(__file__).resolve().parent))


def find_compiler():
    for name in (os.environ.get("CC"), "cc", "gcc", "clang", "cl"):
        if name and shutil.which(name):
            return name
    sys.exit("desync_dump_test: no C compiler found (set CC)")


def build(cc):
    """The driver with desync_dump.c, sanitized if the compiler can. Returns (exe, how) or (None, why)."""
    exe = WORK / ("desync_dump_test.exe" if sys.platform == "win32" else "desync_dump_test")
    sources = [str(DRIVER), str(SRC / "desync_dump.c")]
    if Path(cc).stem.lower() == "cl":
        tries = [([cc, "/nologo", "/W3", "/Od", "/D_CRT_SECURE_NO_WARNINGS", f"/I{SRC}", *sources, f"/Fe:{exe}"],
                  "cl")]
    else:
        base = [cc, "-std=c11", "-g", "-O1", "-Wall", "-Wextra", "-Werror", "-I", str(SRC), *sources, "-o", str(exe)]
        tries = [(base + ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"], "with ASan and UBSan"),
                 (base, "without sanitizers")]
    errors = ""
    for cmd, how in tries:
        done = subprocess.run(cmd, cwd=WORK, capture_output=True, text=True)
        if done.returncode == 0:
            return exe, how
        errors = (done.stdout + done.stderr)[-1500:]
    return None, errors


def fnv(data, h=FNV_OFFSET):
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & MASK
    return h


def make_dump(rng):
    """A dump as a list of (region index, name, index or None, bytes), regions in order as the game hashes them."""
    chunks = []
    names = ["gPlayer", "gActors", "gBosses", "gEffects", "sRandSeed1", "gLifeCount", "gSaveFile.planet", "gTeamShields"]
    for region in sorted(rng.sample(range(len(REGIONS)), rng.randrange(2, 7))):
        for n in range(rng.randrange(1, 5)):
            name = rng.choice(names)
            index = None if rng.random() < 0.3 else rng.randrange(0, 60)
            if any(c[0] == region and c[1] == name and c[2] == index for c in chunks):
                continue
            chunks.append((region, name, index, bytes(rng.randrange(256) for _ in range(rng.choice([1, 2, 3, 4, 7, 16, 63,
                                                                                                    400])))))
    return chunks


def change(rng, chunks):
    """A copy with some bytes changed in one to three chunks."""
    out = list(chunks)
    for at in rng.sample(range(len(out)), min(len(out), rng.randrange(1, 4))):
        region, name, index, data = out[at]
        data = bytearray(data)
        for _ in range(rng.randrange(1, 4)):
            i = rng.randrange(len(data))
            data[i] ^= 1 << rng.randrange(8)
        out[at] = (region, name, index, bytes(data))
    return out


def stream(chunks):
    """The stream as PROTOCOL.md gives it: u8 region, u8 name length, name, s32 index, u32 size, bytes."""
    out = bytearray()
    for region, name, index, data in chunks:
        raw = name.encode("ascii")
        out += bytes([region, len(raw)]) + raw + struct.pack("<iI", -1 if index is None else index, len(data)) + data
    return bytes(out)


def report(chunks, player, path):
    """A desync report as src/port/coop/sync/desync_report.c writes it, with region hashes that match the dump."""
    totals = [fnv(b"".join(c[3] for c in chunks if c[0] == r)) for r in range(len(REGIONS))]
    total = fnv(struct.pack("<" + "Q" * len(totals), *totals))
    lines = ["# starship-coop desync report v1", f"player: {player} of 2, {'host' if player == 1 else 'client'}",
             "frame: 2000", "pair: players 1 and 2", "regions: total " + " ".join(REGIONS),
             "mine: " + " ".join(f"{h:016x}" for h in [total, *totals]),
             "my state: gamestate 7, level 0 (Corneria), rails, playstate 2",
             f"dump: {len(chunks)} chunks, {sum(len(c[3]) for c in chunks)} bytes, as hashed"]
    last = None
    for region, name, index, data in chunks:
        if region != last:
            lines.append(f"## {REGIONS[region]}")
            last = region
        lines.append(f"{name}[{index}] {len(data)}" if index is not None else f"{name} {len(data)}")
        hexed = data.hex()
        lines += [hexed[i:i + 128] for i in range(0, len(hexed), 128)]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


WORD = re.compile(r"0x([0-9a-f]{8})")
WORD_AT = re.compile(r"word at \+(\d+)")


def golden(exe, env):
    """Random pairs of dumps, as report files for desync_report.py and as streams for the C code: one answer."""
    rng = random.Random(189)
    tool = ROOT / "tools" / "coop" / "desync_report.py"
    failures = []
    found = equal = 0
    for n in range(GOLDEN_PAIRS):
        a = make_dump(rng)
        b = change(rng, a) if n % 8 != 7 else list(a)
        paths = [WORK / f"a{n}.bin", WORK / f"b{n}.bin", WORK / f"coop-desync-2000-player1-{n}.txt",
                 WORK / f"coop-desync-2000-player2-{n}.txt"]
        paths[0].write_bytes(stream(a))
        paths[1].write_bytes(stream(b))
        report(a, 1, paths[2])
        report(b, 2, paths[3])
        c = subprocess.run([str(exe), "diff", str(paths[0]), str(paths[1])], capture_output=True, text=True, env=env,
                           timeout=60)
        py = subprocess.run([sys.executable, "-I", str(tool), "diff", str(paths[2]), str(paths[3])],
                            capture_output=True, text=True, timeout=60)
        result, _, line = c.stdout.strip().partition(" ")
        if c.returncode != 0 or result not in ("0", "1"):
            failures.append(f"pair {n}: the C code says `{c.stdout.strip()[:200]}` (exit {c.returncode}) {c.stderr[-300:]}")
            continue
        theirs = next((text.strip() for text in py.stdout.splitlines() if " differs in " in text), None)
        if result == "0":
            equal += 1
            if theirs is not None or "equal byte for byte" not in py.stdout or line != "the dumps are equal byte for byte":
                failures.append(f"pair {n}: the C code says equal, the tool says `{theirs}`")
            continue
        found += 1
        head = line.split("; word at", 1)[0]
        if theirs is None or not theirs.startswith(head + (";" if "; word at" in theirs else "")):
            failures.append(f"pair {n}: the C code says `{head}`, the tool `{theirs}`")
            continue
        if WORD.findall(line) != WORD.findall(theirs) or set(WORD_AT.findall(line)) != set(WORD_AT.findall(theirs)):
            failures.append(f"pair {n}: the words differ: `{line}` against `{theirs}`")
    return found, equal, failures


def main():
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    for path in (SRC / "desync_dump.c", SRC / "desync_dump.h", DRIVER):
        if not path.exists():
            print(f"desync_dump_test: FAIL, {path.relative_to(ROOT)} does not exist")
            return 1
    exe, how = build(find_compiler())
    if exe is None:
        print("desync_dump_test: FAIL, the test does not build:\n" + how)
        return 1
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:abort_on_error=0", UBSAN_OPTIONS="print_stacktrace=1")
    done = subprocess.run([str(exe), "selftest"], capture_output=True, text=True, cwd=WORK, env=env, timeout=600)
    (WORK / "output.txt").write_text(done.stdout + done.stderr, encoding="utf-8")
    lines = done.stdout.splitlines()
    failures = [line for line in lines if line.startswith("FAIL")]
    passed = {m.group(1) for m in (re.match(r"ok (\w+):", line) for line in lines) if m}
    hostile = next((int(m.group(1)) for m in (re.match(r"hostile messages: (\d+)", line) for line in lines) if m), 0)
    if done.returncode != 0 and not failures:
        failures.append(f"the test exited with {done.returncode}: {(done.stdout + done.stderr)[-1200:]}")
    failures += [f"no result for {name}" for name in TESTS if name not in passed]
    if hostile < MIN_HOSTILE:
        failures.append(f"only {hostile} hostile messages tested, want {MIN_HOSTILE}")
    for line in lines:
        if line.startswith("ok "):
            print(f"desync_dump_test: {line}")
    found, equal, wrong = golden(exe, env)
    failures += wrong
    if found < GOLDEN_PAIRS // 2 or equal < 2:
        failures.append(f"golden: only {found} differing and {equal} equal pairs were compared")
    print(f"desync_dump_test: ok golden: {found} differing pairs named alike by desync_report.py diff and the C code, "
          f"{equal} equal pairs")
    print(f"desync_dump_test: built {how}; {hostile} hostile messages")
    if failures:
        print(f"desync_dump_test: FAIL, {len(failures)} problems; first {min(10, len(failures))}:")
        for failure in failures[:10]:
            print("  " + failure)
        print(f"desync_dump_test: kept {WORK}")
        return 1
    shutil.rmtree(WORK, ignore_errors=True)
    print("desync_dump_test: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
