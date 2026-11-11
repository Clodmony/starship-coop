#!/usr/bin/env python3
"""Tests the handover at the end of a session (#189): src/port/coop/sync/handover.c alone, no game, no socket.

    python tools/coop/handover_test.py     # unattended; prints PASS or FAIL. No build, no ROM, a few seconds.

Before a game closes a session it hands its desync report and the rest of its co-op log to every player still
connected, and takes theirs. handover.c holds the rules and the state of that, with nothing of the game in it, so this
test compiles it with tools/coop/handovertest/coop_handover_test.c and the platform's own compiler, with AddressSanitizer
and UBSan where the compiler has them, and runs it:

  names   report names of the sender pass; `..`, a path separator, an absolute path, the receiver's own player number
          and another prefix fail, so no received file can be written outside the game's folder or over its own
  caps    size 0, above 2 MiB and a fifth file are refused, sent or received, and all peers together hand in 4 MiB

The transfer itself is tested with two real games: `lockstep_test.py --only handover`. Needs a C compiler (CC, cc, gcc,
clang, or cl on Windows). Exit code 0 when everything holds, 1 otherwise. The folder local/test-handover/ is deleted
when all hold and kept for reading when one fails.
"""
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "src" / "port" / "coop" / "sync"
DRIVER = ROOT / "tools" / "coop" / "handovertest" / "coop_handover_test.c"
WORK = ROOT / "local" / "test-handover"
TESTS = ["names", "caps"]
MIN_HOSTILE = 5


def find_compiler():
    for name in (os.environ.get("CC"), "cc", "gcc", "clang", "cl"):
        if name and shutil.which(name):
            return name
    sys.exit("handover_test: no C compiler found (set CC)")


def build(cc):
    """The driver with handover.c, sanitized if the compiler can. Returns (exe, how) or (None, why)."""
    exe = WORK / ("handover_test.exe" if sys.platform == "win32" else "handover_test")
    sources = [str(DRIVER), str(SRC / "handover.c")]
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


def main():
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    for path in (SRC / "handover.c", SRC / "handover.h", DRIVER):
        if not path.exists():
            print(f"handover_test: FAIL, {path.relative_to(ROOT)} does not exist")
            return 1
    exe, how = build(find_compiler())
    if exe is None:
        print("handover_test: FAIL, the test does not build:\n" + how)
        return 1
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:abort_on_error=0", UBSAN_OPTIONS="print_stacktrace=1")
    done = subprocess.run([str(exe)], capture_output=True, text=True, cwd=WORK, env=env, timeout=600)
    (WORK / "output.txt").write_text(done.stdout + done.stderr, encoding="utf-8")
    lines = done.stdout.splitlines()
    failures = [line for line in lines if line.startswith("FAIL")]
    passed = {m.group(1) for m in (re.match(r"ok (\w+):", line) for line in lines) if m}
    hostile = next((int(m.group(1)) for m in (re.match(r"hostile names: (\d+)", line) for line in lines) if m), 0)
    if done.returncode != 0 and not failures:
        failures.append(f"the test exited with {done.returncode}: {(done.stdout + done.stderr)[-1200:]}")
    failures += [f"no result for {name}" for name in TESTS if name not in passed]
    if hostile < MIN_HOSTILE:
        failures.append(f"only {hostile} hostile names tested, want {MIN_HOSTILE}")
    for line in lines:
        if line.startswith("ok "):
            print(f"handover_test: {line}")
    print(f"handover_test: built {how}; {hostile} hostile names")
    if failures:
        print(f"handover_test: FAIL, {len(failures)} problems; first {min(10, len(failures))}:")
        for failure in failures[:10]:
            print("  " + failure)
        print(f"handover_test: kept {WORK}")
        return 1
    shutil.rmtree(WORK, ignore_errors=True)
    print("handover_test: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
