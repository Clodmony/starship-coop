#!/usr/bin/env python3
"""Tests for the replay tools themselves (#54): a run stays in its own folder, and a damaged replay never passes.

    python tools/coop/replay_test.py                                # unattended; prints PASS or FAIL
    python tools/coop/replay_test.py --only header-only,cut-frame
    python tools/coop/replay_test.py --list

Records a short replay nobody plays, then plays it whole and damaged through the command lines of replay.py,
hash_check.py and determinism.py, and checks each exit code and result line. Needs a build with assets
(python tools/coop/build.py --assets). Exit code 0 when every scenario holds, 1 otherwise.

Every tool starts with SHIP_HOME set to a decoy folder, never the value from your shell, so a tool that lets it through
can only write into the decoy. That fails the scenario, and so does a run folder the game did not write its config
into. The decoy holds sf64.o2r, as a real install does: without it the game exits at once and writes nothing
(src/port/Engine.cpp:78), which would hide the bug. On Linux the game runs without a window and off the GPU. Everything
lands under local/test-*: the folders are deleted when all scenarios hold and kept for reading when one fails.
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import time
from collections import namedtuple
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import replay as replay_tool  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
ARCHIVE = "sf64.o2r"  # the one file a decoy starts with
HEADER_SIZE = 40  # ReplayHeader in src/port/coop/test/replay.c
FRAMES = 120  # whole frames in the test replay: 4 seconds of game
CUT_FRAMES = 30  # whole frames left before the cut in cut-frame
CHANGED_FRAME = 20  # frame whose checkpoint is changed in mismatch and cut-mismatch
TIMEOUT = "120"  # per game run; a run takes about 20 s

COMMANDS = {
    "replay": lambda file: ["replay.py", "play", file, "--timeout", TIMEOUT],
    "hash": lambda file: ["hash_check.py", "run", file, "--timeout", TIMEOUT],
    "det": lambda file: ["determinism.py", file, "--only", "base-2", "--timeout", TIMEOUT],
}

# name: replay file, tool, wanted exit code, wanted result line and frames played (replay.py prints those two)
Scenario = namedtuple("Scenario", "file tool code result frames")
SCENARIOS = {
    "whole": Scenario("whole", "replay", 0, "PASS", FRAMES),
    "header-only": Scenario("header-only", "replay", 1, "FAIL_NO_FRAMES", 0),
    "cut-frame": Scenario("cut-frame", "replay", 1, "FAIL_TRUNCATED", CUT_FRAMES),
    "cut-first-frame": Scenario("cut-first-frame", "replay", 1, "FAIL_TRUNCATED", 0),
    "mismatch": Scenario("mismatch", "replay", 1, "FAIL_MISMATCH", FRAMES),
    "cut-mismatch": Scenario("cut-mismatch", "replay", 1, "FAIL_TRUNCATED", CUT_FRAMES),
    "short-header": Scenario("short-header", "replay", 1, "ERROR_BAD_FILE", 0),
    "empty-file": Scenario("empty-file", "replay", 1, "ERROR_BAD_FILE", 0),
    "hash-whole": Scenario("whole", "hash", 0, None, None),
    "hash-header-only": Scenario("header-only", "hash", 1, None, None),
    "hash-short-header": Scenario("short-header", "hash", 1, None, None),
    "det-whole": Scenario("whole", "det", 0, None, None),
    "det-header-only": Scenario("header-only", "det", 1, None, None),
}


class Bench:
    """The test's own folder under local/, and every run folder the tools made."""

    def __init__(self):
        self.folder = ROOT / "local" / f"test-replaytools-{time.strftime('%Y%m%d-%H%M%S')}"
        self.folder.mkdir(parents=True)
        self.archive = replay_tool.find_data(replay_tool.find_exe(None), ARCHIVE)
        if self.archive is None:
            sys.exit(f"replay_test: {ARCHIVE} not found; run python tools/coop/build.py --assets first")
        self.runs = []

    def tool(self, tag, command):
        """Run one tool with its own decoy. Returns (exit code, output, failures seen in any run)."""
        decoy = self.folder / f"decoy-{tag}"
        decoy.mkdir()
        shutil.copy2(self.archive, decoy / ARCHIVE)
        env = dict(os.environ)
        env["SHIP_HOME"] = str(decoy)
        if sys.platform.startswith("linux"):
            env.update(SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy", LIBGL_ALWAYS_SOFTWARE="1",
                       GALLIUM_DRIVER="llvmpipe")
        log = self.folder / f"{tag}.log"
        with log.open("w", encoding="utf-8") as out:
            code = subprocess.run([sys.executable, "-u", str(ROOT / "tools" / "coop" / command[0]), *command[1:]],
                                  cwd=ROOT, env=env, stdout=out, stderr=subprocess.STDOUT).returncode
        output = log.read_text(encoding="utf-8", errors="replace")

        failures = []
        wrote = sorted(p.name for p in decoy.iterdir() if p.name != ARCHIVE)
        if wrote:
            failures.append(f"game wrote into SHIP_HOME: {', '.join(wrote)}")
        # By folder name, not by line: the threads of hash_check.py can print onto each other's lines.
        runs = [ROOT / "local" / r for r in sorted(set(re.findall(r"test-replay-[a-z0-9-]*?\d{8}-\d{6}", output)))]
        self.runs += runs
        if not wrote and any(not (r / "starship.cfg.json").exists() for r in runs):
            failures.append("game did not write its config into the run folder")
        if "Traceback" in output:
            failures.append("tool crashed: " + output.strip().splitlines()[-1])
        return code, output, failures

    def clean(self):
        for folder in [self.folder, *self.runs]:
            # Only folders this test made: they sit directly in local/ and are named test-*.
            if folder.parent == ROOT / "local" and folder.name.startswith("test-"):
                shutil.rmtree(folder, ignore_errors=True)


def make_replays(bench):
    """Record a replay nobody plays and write it whole and damaged. Returns the files by name, or None."""
    recording = bench.folder / "recording.sfcr"
    code, _, failures = bench.tool("record", ["replay.py", "record", str(recording), "--frames", str(FRAMES),
                                              "--timeout", TIMEOUT])
    data = recording.read_bytes() if recording.exists() else b""
    if len(data) < HEADER_SIZE:
        print(f"replay_test: recording failed (exit code {code}); see {bench.folder / 'record.log'}")
        return None
    frame_size = struct.unpack_from("<I", data, 12)[0]
    if code != 0 or len(data) < HEADER_SIZE + FRAMES * frame_size:
        print(f"replay_test: recording too short ({len(data)} bytes, exit code {code})")
        return None
    for failure in failures:
        print(f"replay_test: while recording: {failure}")

    # The game stops a little after FRAMES, so cut to exactly that many. A frame ends with its checkpoint.
    whole = data[:HEADER_SIZE + FRAMES * frame_size]
    cut_at = HEADER_SIZE + CUT_FRAMES * frame_size + frame_size // 2
    changed = bytearray(whole)
    changed[HEADER_SIZE + (CHANGED_FRAME + 1) * frame_size - 1] ^= 1
    contents = {
        "whole": whole,
        "header-only": data[:HEADER_SIZE],
        "cut-frame": whole[:cut_at],
        "cut-first-frame": data[:HEADER_SIZE + frame_size // 2],
        "mismatch": bytes(changed),
        "cut-mismatch": bytes(changed[:cut_at]),
        "short-header": data[:8],
        "empty-file": b"",
    }
    files = {}
    for name, content in contents.items():
        files[name] = bench.folder / f"{name}.sfcr"
        files[name].write_bytes(content)
    return files


def check(bench, name, scenario, files):
    """Run one scenario. Returns its failures."""
    code, output, failures = bench.tool(name, COMMANDS[scenario.tool](str(files[scenario.file])))
    if code != scenario.code:
        failures.append(f"exit code {code}, want {scenario.code}")
    if scenario.result is not None:
        results = re.findall(r"^replay: (\S+) \(play, ", output, re.MULTILINE)
        result = results[-1] if results else "no result line"
        if result != scenario.result:
            failures.append(f"result {result}, want {scenario.result}")
        frames = re.findall(r"^  frames: (\d+)$", output, re.MULTILINE)
        played = int(frames[-1]) if frames else None
        if played != scenario.frames:
            failures.append(f"{played} frames played, want {scenario.frames}")
    return failures


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--only", help="comma-separated scenarios (default: all)")
    p.add_argument("--list", action="store_true", help="print the scenarios and stop")
    args = p.parse_args()

    if args.list:
        for name, s in SCENARIOS.items():
            print(f"{name}: {s.file} replay through {s.tool}, wants exit code {s.code}"
                  + (f" and {s.result}" if s.result else ""))
        return 0
    chosen = list(SCENARIOS) if not args.only else [s.strip() for s in args.only.split(",") if s.strip()]
    unknown = [s for s in chosen if s not in SCENARIOS]
    if unknown:
        sys.exit(f"replay_test: unknown scenario(s) {', '.join(unknown)}; known: {', '.join(SCENARIOS)}")

    start = time.monotonic()
    bench = Bench()
    print(f"replay_test: recording {FRAMES} frames with nobody playing, in {bench.folder}", flush=True)
    files = make_replays(bench)
    if files is None:
        print("replay_test: FAIL")
        return 1

    failed = 0
    for name in chosen:
        began = time.monotonic()
        failures = check(bench, name, SCENARIOS[name], files)
        failed += bool(failures)
        print(f"replay_test: {name}: " + (f"FAIL: {'; '.join(failures)}" if failures else "ok")
              + f" ({time.monotonic() - began:.0f}s)", flush=True)

    print(f"replay_test: {len(chosen) - failed}/{len(chosen)} scenarios OK, {time.monotonic() - start:.0f}s")
    if failed:
        print(f"replay_test: logs and run folders kept: {bench.folder}")
    else:
        bench.clean()
    print("replay_test: FAIL" if failed else "replay_test: PASS")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
