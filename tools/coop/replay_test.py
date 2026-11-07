#!/usr/bin/env python3
"""Tests for the replay tools themselves (#54): a run stays in its own folder, and a damaged replay never passes.

    python tools/coop/replay_test.py                                # unattended; prints PASS or FAIL
    python tools/coop/replay_test.py --jobs 2                       # two scenarios at a time
    python tools/coop/replay_test.py --only header-only,cut-frame
    python tools/coop/replay_test.py --list
    python tools/coop/replay_test.py --exe <build>/Starship         # default: the newest build found

Records a short replay nobody plays, then plays it whole and damaged through the command lines of replay.py,
hash_check.py and determinism.py, and checks each exit code and result line. Needs a build with assets
(python tools/coop/build.py --assets). Exit code 0 when every scenario holds, 1 otherwise. same-second starts three
tools at once: each must play in a run folder of its own (#69).

Every tool starts with SHIP_HOME set to a decoy folder, never the value from your shell, so a tool that lets it through
can only write into the decoy. That fails the scenario, and so does a run folder the game did not write its config
into. The decoy holds sf64.o2r, as a real install does: without it the game exits at once and writes nothing
(src/port/Engine.cpp:78), which would hide the bug. On Linux the game runs without a window and off the GPU. Everything
lands under local/test-*: the folders are deleted when all scenarios hold and kept for reading when one fails.

The leak-* scenarios start a tool with the variables an earlier test can leave in a shell (#70). Each one shows when it
reaches the game: a file in the decoy, a test switch line in the game's log, or a hash_check.py run that fails because
its poke went to a player the run does not have. pass-audio is the other side: a variable a tool sets on purpose still
gets through, and that line does show up in the log. SF64_COOP_HASH_PROBE is not among them, as it acts only in a level
and this replay never reaches one.

Every game logs the speed it runs at, so every scenario checks that its tool used the right one (#137): TEST_SPEED, or
real time for determinism.py without its speed variant and for replay.py --speed 1 (real-time). The bare-* scenarios
(Linux) start a tool from a shell without any SDL driver, as one typed by hand, and read the driver from each running
game's environment: a test game must be silent and have no window. A game that is not gets SIGKILL on sight, before it
opens a sound device. person starts replay.py record without --frames, the one run a person plays: that game must be
left in real time, and this test then stops it. rates-real and rates-at-speed run hash_check.py rates, whose guard
counts the draws a game really made: it must hold at real time and fail at speed, where no game interpolates.

The level-* scenarios are about a game that starts straight in a level (#148). A second recording is made that way, and
level-whole and level-hash play it with no word about where it starts: the file says. level-wrong-start plays the
recording from the menus in a level, which must not pass. level-det and level-full give the file made in a level to the
tools that keep the whole path from boot, which must refuse it.
"""
import argparse
import os
import re
import shutil
import signal
import struct
import subprocess
import sys
import time
from collections import namedtuple
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import replay as replay_tool  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
ARCHIVE = "sf64.o2r"  # the one file a decoy starts with
HEADER_SIZE = 40  # ReplayHeader in src/port/coop/test/replay.c
FRAMES = 120  # whole frames in the test replay: 4 seconds of game
CUT_FRAMES = 30  # whole frames left before the cut in cut-frame
TOGETHER = 3  # tools in same-second: three starts within a second cover at most two clock seconds, so two share one
CHANGED_FRAME = 20  # frame whose checkpoint is changed in mismatch and cut-mismatch
TIMEOUT = "120"  # per game run; a run takes about 20 s
RUN_FOLDER = re.compile(r"test-(?:replay|trace)-[a-z0-9-]*?\d{8}-\d{6}(?:-\d+)?")  # as the tools name them
QUIET = {"SDL_AUDIODRIVER": "dummy", "SDL_VIDEODRIVER": "offscreen", "LIBGL_ALWAYS_SOFTWARE": "1",
         "GALLIUM_DRIVER": "llvmpipe"}  # what game_env gives a test game on Linux
TEST_SPEED = replay_tool.TEST_SPEED

RATES = ["hash_check.py", "rates", "--rates", "30,60", "--section", "rails", "--players", "1", "--frames", str(FRAMES),
         "--timeout", TIMEOUT]  # a replay of its own, by rule; the file does not come into it

COMMANDS = {
    "replay": lambda file: ["replay.py", "play", file, "--timeout", TIMEOUT],
    "hash": lambda file: ["hash_check.py", "run", file, "--timeout", TIMEOUT],
    "det": lambda file: ["determinism.py", file, "--only", "base-2", "--timeout", TIMEOUT],
    "det-audio": lambda file: ["determinism.py", file, "--only", "audio-low", "--timeout", TIMEOUT],
    "det-speed": lambda file: ["determinism.py", file, "--only", "speed", "--timeout", TIMEOUT],
    "trace": lambda file: ["player_trace.py", "run", file, "--players", "1", "--timeout", TIMEOUT],
    "real": lambda file: ["replay.py", "play", file, "--speed", "1", "--timeout", TIMEOUT],
    "wrong-start": lambda file: ["replay.py", "play", file, "--start", "level", "--timeout", TIMEOUT],
    "full": lambda file: ["full_level.py", "--replay", file, "--timeout", TIMEOUT],
    "rates": lambda file: RATES,
    "rates-speed": lambda file: [*RATES, "--speed", TEST_SPEED],
}
# Speeds the games of a tool must log. No speed is real time.
SPEEDS = {"replay": {TEST_SPEED}, "hash": {TEST_SPEED}, "det": set(), "det-audio": set(), "det-speed": {TEST_SPEED},
          "trace": {TEST_SPEED}, "real": set(), "wrong-start": {TEST_SPEED}, "full": set(), "rates": set(),
          "rates-speed": {TEST_SPEED}}

# name: replay file, tool, wanted exit code, wanted result line and frames played (replay.py prints those two), whether
# the tool starts with variables left in the shell, whether the game must log a test switch, whether the tool starts
# from a shell without SDL drivers, and what the tool must print
Scenario = namedtuple("Scenario", "file tool code result frames leak switch bare says",
                      defaults=(False, False, False, None))
SCENARIOS = {
    "whole": Scenario("whole", "replay", 0, "PASS", FRAMES),
    "header-only": Scenario("header-only", "replay", 1, "FAIL_NO_FRAMES", 0),
    "cut-frame": Scenario("cut-frame", "replay", 1, "FAIL_TRUNCATED", CUT_FRAMES),
    "cut-first-frame": Scenario("cut-first-frame", "replay", 1, "FAIL_TRUNCATED", 0),
    "mismatch": Scenario("mismatch", "replay", 1, "FAIL_MISMATCH", FRAMES),
    "cut-mismatch": Scenario("cut-mismatch", "replay", 1, "FAIL_TRUNCATED", CUT_FRAMES),
    "short-header": Scenario("short-header", "replay", 1, "ERROR_BAD_FILE", 0),
    "empty-file": Scenario("empty-file", "replay", 1, "ERROR_BAD_FILE", 0),
    "same-second": Scenario("whole", "replay", 0, None, None),
    "hash-whole": Scenario("whole", "hash", 0, None, None),
    "hash-header-only": Scenario("header-only", "hash", 1, None, None),
    "hash-short-header": Scenario("short-header", "hash", 1, None, None),
    "det-whole": Scenario("whole", "det", 0, None, None),
    "det-header-only": Scenario("header-only", "det", 1, None, None),
    "leak-replay": Scenario("whole", "replay", 0, "PASS", FRAMES, leak=True),
    "leak-hash": Scenario("whole", "hash", 0, None, None, leak=True),
    "leak-trace": Scenario("whole", "trace", 1, None, None, leak=True),  # no level in this replay: an empty trace
    "pass-audio": Scenario("whole", "det-audio", 0, None, None, switch=True),
    "det-speed": Scenario("whole", "det-speed", 0, None, None),
    "real-time": Scenario("whole", "real", 0, "PASS", FRAMES),
    "rates-real": Scenario("whole", "rates", 0, None, None),
    "rates-at-speed": Scenario("whole", "rates-speed", 1, None, None,
                               says="60 fps was not used: 1 draws a frame, not 2"),
    "bare-replay": Scenario("whole", "replay", 0, "PASS", FRAMES, bare=True),
    "bare-hash": Scenario("whole", "hash", 0, None, None, bare=True),
    "bare-trace": Scenario("whole", "trace", 1, None, None, bare=True),  # no level in this replay: an empty trace
    "level-whole": Scenario("in-level", "replay", 0, "PASS", FRAMES, switch=True),
    "level-hash": Scenario("in-level", "hash", 0, None, None, switch=True),
    "level-wrong-start": Scenario("whole", "wrong-start", 1, "FAIL_MISMATCH", FRAMES, switch=True),
    "level-det": Scenario("in-level", "det", 1, None, None, switch=True, says="started in the level"),
    "level-full": Scenario("in-level", "full", 1, None, None, says="started in the level"),
    "person": None,
}
if not sys.platform.startswith("linux"):  # the check reads /proc, and only Linux gets the offscreen driver
    for name in ("bare-replay", "bare-hash", "bare-trace", "person"):
        del SCENARIOS[name]


def left_in_shell(decoy):
    """Variables an earlier test can leave in a shell. All of them show when they reach the game."""
    return {
        "SF64_COOP_TRACE": str(decoy / "trace.txt"),
        "SF64_COOP_HASH_LOG": str(decoy / "state-hash.txt"),
        "SF64_COOP_REPLAY_RECORD": str(decoy / "recording.sfcr"),
        "SF64_COOP_HASH_POKE_PLAYER": "1",
        "SF64_COOP_TEST_AUDIO_SAMPLES": "low",
        "SF64_COOP_TEST_NO_FMA3": "1",
        "SF64_COOP_TEST_SPEED": "3",
        "SF64_COOP_TEST_START_LEVEL": "0",
    }


class Bench:
    """The test's own folder under local/, and every run folder the tools made."""

    def __init__(self, exe):
        self.folder = ROOT / "local" / f"test-replaytools-{time.strftime('%Y%m%d-%H%M%S')}-{os.getpid()}"
        self.folder.mkdir(parents=True)
        self.exe = replay_tool.find_exe(exe)
        self.archive = replay_tool.find_data(self.exe, ARCHIVE)
        if self.archive is None:
            sys.exit(f"replay_test: {ARCHIVE} not found; run python tools/coop/build.py --assets first")
        self.runs = []

    def tool(self, tag, command, leak=False, bare=False, watch=None):
        """Run one tool with its own decoy. Returns (exit code, output, failures seen in any run). watch is called with
        the tool's process while it runs, and returns failures."""
        decoy = self.folder / f"decoy-{tag}"
        decoy.mkdir()
        shutil.copy2(self.archive, decoy / ARCHIVE)
        env = dict(os.environ)
        env["SHIP_HOME"] = str(decoy)
        if leak:
            env.update(left_in_shell(decoy))
        if sys.platform.startswith("linux"):
            env.update(SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy", LIBGL_ALWAYS_SOFTWARE="1",
                       GALLIUM_DRIVER="llvmpipe")
        if bare:
            for key in QUIET:
                env.pop(key, None)
        log = self.folder / f"{tag}.log"
        failures = []
        with log.open("w", encoding="utf-8") as out:
            proc = subprocess.Popen([sys.executable, "-u", str(ROOT / "tools" / "coop" / command[0]), *command[1:],
                                     "--exe", str(self.exe)],
                                    cwd=ROOT, env=env, stdout=out, stderr=subprocess.STDOUT)
            if watch is not None:
                failures += watch(proc)
            code = proc.wait()
        output = log.read_text(encoding="utf-8", errors="replace")

        wrote = sorted(p.name for p in decoy.iterdir() if p.name != ARCHIVE)
        if wrote:
            failures.append(f"game wrote into SHIP_HOME: {', '.join(wrote)}")
        # By folder name, not by line: the threads of hash_check.py can print onto each other's lines.
        runs = [ROOT / "local" / r for r in sorted(set(RUN_FOLDER.findall(output)))]
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


def record(bench, name, extra=()):
    """Record a replay nobody plays. Returns (its bytes, the size of a frame), or None."""
    recording = bench.folder / f"{name}.sfcr"
    code, _, failures = bench.tool(name, ["replay.py", "record", str(recording), "--frames", str(FRAMES),
                                          "--timeout", TIMEOUT, *extra])
    data = recording.read_bytes() if recording.exists() else b""
    if len(data) < HEADER_SIZE:
        print(f"replay_test: recording failed (exit code {code}); see {bench.folder / f'{name}.log'}")
        return None
    frame_size = struct.unpack_from("<I", data, 12)[0]
    if code != 0 or len(data) < HEADER_SIZE + FRAMES * frame_size:
        print(f"replay_test: recording too short ({len(data)} bytes, exit code {code})")
        return None
    for failure in failures:
        print(f"replay_test: while recording: {failure}")
    return data, frame_size


def make_replays(bench):
    """Record replays nobody plays and write them whole and damaged. Returns the files by name, or None."""
    menus, in_level = record(bench, "record"), record(bench, "record-level", ["--start", "level"])
    if menus is None or in_level is None:
        return None
    data, frame_size = menus

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
        "in-level": in_level[0][:HEADER_SIZE + FRAMES * in_level[1]],
    }
    files = {}
    for name, content in contents.items():
        files[name] = bench.folder / f"{name}.sfcr"
        files[name].write_bytes(content)
    return files


def game_logs(output):
    """The text of the log of every game the tool started."""
    logs = [ROOT / "local" / run / "logs" / "Starship.log" for run in set(RUN_FOLDER.findall(output))]
    return [log.read_text(encoding="utf-8", errors="replace") for log in logs if log.exists()]


def switch_logged(output):
    """Whether a game the tool started logged a test switch (src/port/coop/test/switches.c)."""
    return any("coop: test switch" in log for log in game_logs(output))


def speeds_logged(output):
    """The speeds the games a tool started logged (src/port/coop/test/switches.c)."""
    return {speed for log in game_logs(output) for speed in re.findall(r"coop: test speed (\S+)", log)}


def games_of(tool, exe):
    """The running games a tool started: {pid: their environment}. While the tool forks, /proc shows a process that is
    not the game yet and still has the tool's environment, so the process must run exe. For a moment of the exec it runs
    exe and has no environment at all: that is not a game without drivers either, so it counts from the next look."""
    found = {}
    for pid in filter(str.isdigit, os.listdir("/proc")):
        try:
            parent = int(Path(f"/proc/{pid}/stat").read_text().rpartition(")")[2].split()[1])
            if parent == tool.pid and os.readlink(f"/proc/{pid}/exe") == str(exe):
                data = Path(f"/proc/{pid}/environ").read_bytes()
                if data:
                    pairs = data.decode("utf-8", "replace").split("\0")
                    found[int(pid)] = dict(pair.partition("=")[::2] for pair in pairs)
        except (OSError, ValueError, IndexError):
            pass  # it ended while we looked
    return found


def watch_quiet(bench):
    """Returns the check of a bare-* scenario: every game has the drivers of QUIET, looked at as it starts."""
    def watch(tool):
        failures = []
        seen = set()
        while tool.poll() is None:
            for pid, env in games_of(tool, bench.exe).items():
                if pid not in seen:
                    seen.add(pid)
                    wrong = {key: env.get(key) for key, value in QUIET.items() if env.get(key) != value}
                    if wrong:
                        os.kill(pid, signal.SIGKILL)  # Only a game this test's tool started: it would make sound.
                        failures.append(f"game started with {wrong}")
            time.sleep(0.02)
        return failures if seen else ["no game seen"]
    return watch


def watch_person(bench):
    """Returns the check of person: the game runs in real time. Nobody is there to close it, so this stops it once it
    has written its config, which the run folder check wants to see."""
    def watch(tool):
        end = time.monotonic() + int(TIMEOUT)
        while tool.poll() is None and time.monotonic() < end:
            games = games_of(tool, bench.exe)
            if games:
                pid, env = next(iter(games.items()))
                while tool.poll() is None and time.monotonic() < end:
                    if Path(f"/proc/{pid}/cwd/starship.cfg.json").exists():
                        break
                    time.sleep(0.1)
                os.kill(pid, signal.SIGTERM)  # Only the game this test's tool started.
                speed = env.get("SF64_COOP_TEST_SPEED")
                return [f"a game a person plays runs at speed {speed}"] if speed is not None else []
            time.sleep(0.02)
        return ["no game seen"]
    return watch


def check(bench, name, scenario, files):
    """Run one scenario. Returns its failures."""
    code, output, failures = bench.tool(name, COMMANDS[scenario.tool](str(files[scenario.file])), scenario.leak,
                                        scenario.bare, watch_quiet(bench) if scenario.bare else None)
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
    if scenario.says is not None and scenario.says not in output:
        failures.append(f"tool did not say: {scenario.says}")
    if switch_logged(output) != scenario.switch:
        failures.append("game log shows a test switch" if not scenario.switch else "no test switch in the game log")
    if speeds_logged(output) != SPEEDS[scenario.tool]:
        failures.append(f"games ran at speed {sorted(speeds_logged(output))}, want {sorted(SPEEDS[scenario.tool])}")
    return failures


def check_person(bench):
    """Start a recording for a person to play. Returns its failures."""
    recording = bench.folder / "person.sfcr"
    _, output, failures = bench.tool("person", ["replay.py", "record", str(recording)], watch=watch_person(bench))
    if speeds_logged(output):
        failures.append(f"game logged speed {sorted(speeds_logged(output))}")
    return failures


def check_together(bench, name, files):
    """Start TOGETHER tools at the same moment. Returns their failures."""
    command = COMMANDS["replay"](str(files["whole"]))
    with ThreadPoolExecutor(max_workers=TOGETHER) as pool:
        results = list(pool.map(lambda i: bench.tool(f"{name}-{i}", command), range(TOGETHER)))
    failures = [failure for _, _, found in results for failure in found]
    failures += [f"exit code {code}, want 0" for code, _, _ in results if code != 0]
    folders = {folder for _, output, _ in results for folder in RUN_FOLDER.findall(output)}
    if len(folders) != TOGETHER:
        failures.append(f"{TOGETHER} tools played in {len(folders)} run folders")
    return failures


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--jobs", type=int, default=1,
                   help="scenarios at a time (default 1); a hash or same-second scenario is three games by itself")
    p.add_argument("--only", help="comma-separated scenarios (default: all)")
    p.add_argument("--list", action="store_true", help="print the scenarios and stop")
    p.add_argument("--exe", help="Starship executable (default: newest build found)")
    args = p.parse_args()

    if args.list:
        for name, s in SCENARIOS.items():
            print(f"{name}: a recording for a person to play, real time" if s is None else
                  f"{name}: {s.file} replay through {s.tool}, wants exit code {s.code}"
                  + (f" and {s.result}" if s.result else ""))
        return 0
    chosen = list(SCENARIOS) if not args.only else [s.strip() for s in args.only.split(",") if s.strip()]
    unknown = [s for s in chosen if s not in SCENARIOS]
    if unknown:
        sys.exit(f"replay_test: unknown scenario(s) {', '.join(unknown)}; known: {', '.join(SCENARIOS)}")

    start = time.monotonic()
    bench = Bench(args.exe)
    print(f"replay_test: recording {FRAMES} frames with nobody playing, in {bench.folder}", flush=True)
    files = make_replays(bench)
    if files is None:
        print("replay_test: FAIL")
        return 1

    def run(name):
        began = time.monotonic()
        if name == "same-second":
            failures = check_together(bench, name, files)
        elif name == "person":
            failures = check_person(bench)
        else:
            failures = check(bench, name, SCENARIOS[name], files)
        return failures, time.monotonic() - began

    failed = 0
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for name, (failures, seconds) in zip(chosen, pool.map(run, chosen)):
            failed += bool(failures)
            print(f"replay_test: {name}: " + (f"FAIL: {'; '.join(failures)}" if failures else "ok")
                  + f" ({seconds:.0f}s)", flush=True)

    print(f"replay_test: {len(chosen) - failed}/{len(chosen)} scenarios OK, {time.monotonic() - start:.0f}s")
    if failed:
        print(f"replay_test: logs and run folders kept: {bench.folder}")
    else:
        bench.clean()
    print("replay_test: FAIL" if failed else "replay_test: PASS")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
