#!/usr/bin/env python3
"""Test that a crashed game fails the run in every test tool (#116). Needs no ROM and no build.

    python tools/coop/crash_test.py
    python tools/coop/crash_test.py --only play-crash,record-crash

A crashed test game still writes its replay report, from an atexit handler (src/port/coop/test/replay.c,
CoopReplay_OnExit), and then ends with exit code 1 (src/port/coop/test/stop_signal.c, #98). The report alone can say
RECORDED, or PASS if the crash came just after the replay ended. So the tools must read how the game ended: a game
that ends on its own with a code other than 0 crashed. A game the tool stops itself is not a crash, although on
Windows the code of a stopped process is 1 too.

Each scenario runs one tool with a stand-in for the game: a small Python program behind a wrapper that the tool starts
as its --exe. It writes the report a passing game writes, and a replay file when it records, and then either
  crash   ends with exit code 1, or
  stays   waits to be stopped, as a real game does (and gives up after STAY_SECONDS).
The stand-in folder holds empty .o2r files, so the tools find their data next to the --exe.

Prints one line per scenario and "crash_test: PASS" or "crash_test: FAIL", exit code 0 or 1.
"""
import argparse
import os
import subprocess
import sys
import textwrap
from collections import namedtuple
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_folder import make_run_folder  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools" / "coop"
ENDING = "COOP_CRASH_TEST_ENDING"  # read by the stand-in; not SF64_COOP_*, which game_env removes
STAY_SECONDS = 20  # a stand-in that stays ends itself after this, in case a tool never stops it
FRAMES = 30  # in the stand-in's replay file
FRAME_SIZE = 8

STAND_IN = textwrap.dedent(f"""\
    import os, struct, sys, time
    record = os.environ.get("SF64_COOP_REPLAY_RECORD")
    if record:
        with open(record, "wb") as f:
            f.write(b"SFCR" + bytes(8) + struct.pack("<I", {FRAME_SIZE}) + bytes(24) + bytes({FRAMES * FRAME_SIZE}))
    report = os.environ.get("SF64_COOP_REPLAY_REPORT")
    if report:
        with open(report, "w") as f:
            f.write("mode=%s\\nframes={FRAMES}\\nrecorded_frames={FRAMES}\\nresult=%s\\n"
                    % (("record", "RECORDED") if record else ("play", "PASS")))
    if os.environ.get("{ENDING}") == "crash":
        sys.exit(1)
    time.sleep({STAY_SECONDS})
""")

# tool and its arguments ({replay} is the stand-in replay file); ending of the stand-in; exit code the tool must end
# with, None for any; text its output must hold; text it must not hold
Scenario = namedtuple("Scenario", "command ending code says not_says")
SCENARIOS = {
    "play-stays": Scenario(["replay.py", "play", "{replay}"], "stays", 0, "replay: PASS", "CRASH"),
    "play-crash": Scenario(["replay.py", "play", "{replay}"], "crash", 1, "replay: CRASH", None),
    "record-stays": Scenario(["replay.py", "record", "{recording}", "--frames", "10"], "stays", 0, "replay: RECORDED",
                             "CRASH"),
    "record-crash": Scenario(["replay.py", "record", "{recording}", "--frames", "10"], "crash", 1, "replay: CRASH",
                             None),
    # These fail with the stand-in either way: it writes no hash log or trace. What shows is why.
    "hash-stays": Scenario(["hash_check.py", "run", "{replay}"], "stays", None, "replay PASS", "CRASH"),
    "hash-crash": Scenario(["hash_check.py", "run", "{replay}"], "crash", 1, "replay CRASH", None),
    "det-crash": Scenario(["determinism.py", "{replay}", "--only", "speed"], "crash", 1, "base run failed: CRASH", None),
    "trace-stays": Scenario(["player_trace.py", "run", "{replay}"], "stays", 1, "no trace written", "CRASH"),
    "trace-crash": Scenario(["player_trace.py", "run", "{replay}"], "crash", 1, "CRASH", None),
}


def make_stand_in(bench):
    """Write the stand-in and the program a tool starts for it into bench. Returns that program."""
    (bench / "stand_in.py").write_text(STAND_IN, encoding="utf-8")
    for name in ("sf64.o2r", "starship.o2r"):
        (bench / name).write_bytes(b"")
    if sys.platform == "win32":
        # A tool that stops it ends cmd.exe and not the stand-in, which then ends itself after STAY_SECONDS.
        exe = bench / "starship.cmd"
        exe.write_text(f'@"{sys.executable}" -I "{bench / "stand_in.py"}"\r\n@exit /b %errorlevel%\r\n',
                       encoding="utf-8")
    else:
        exe = bench / "starship"
        exe.write_text(f'#!/bin/sh\nexec "{sys.executable}" -I "{bench / "stand_in.py"}"\n', encoding="utf-8")
        exe.chmod(0o755)
    return exe


def run(name, scenario, bench, exe):
    """Run one scenario. Returns a list of failures."""
    replay = bench / "stand-in.sfcr"
    if not replay.exists():
        replay.write_bytes(b"SFCR" + bytes(8) + FRAME_SIZE.to_bytes(4, "little") + bytes(24) + bytes(FRAMES * FRAME_SIZE))
    args = [a.format(replay=replay, recording=bench / f"{name}.sfcr") for a in scenario.command]
    env = {key: value for key, value in os.environ.items() if not key.startswith("SF64_COOP_")}
    env[ENDING] = scenario.ending
    log = bench / f"{name}.log"
    with log.open("w", encoding="utf-8") as out:
        code = subprocess.run([sys.executable, "-u", str(TOOLS / args[0]), *args[1:], "--exe", str(exe)], cwd=ROOT,
                              env=env, stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                              timeout=300).returncode
    output = log.read_text(encoding="utf-8", errors="replace")
    failures = []
    if scenario.code is not None and code != scenario.code:
        failures.append(f"exit code {code}, want {scenario.code}")
    if scenario.says not in output:
        failures.append(f"output lacks {scenario.says!r}")
    if scenario.not_says is not None and scenario.not_says in output:
        failures.append(f"output holds {scenario.not_says!r}")
    return failures


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--only", help="comma-separated scenarios (default: all: " + ", ".join(SCENARIOS) + ")")
    args = p.parse_args()
    chosen = list(SCENARIOS) if not args.only else [s.strip() for s in args.only.split(",") if s.strip()]
    unknown = [s for s in chosen if s not in SCENARIOS]
    if unknown:
        sys.exit(f"crash_test: unknown scenario(s) {', '.join(unknown)}; known: {', '.join(SCENARIOS)}")

    bench = make_run_folder(ROOT / "local", "test-crash-")
    exe = make_stand_in(bench)
    print(f"crash_test: bench {bench}")
    failed = 0
    for name in chosen:
        failures = run(name, SCENARIOS[name], bench, exe)
        print(f"crash_test: {name}: {'FAIL: ' + '; '.join(failures) if failures else 'PASS'}")
        failed += bool(failures)
    print(f"crash_test: {len(chosen) - failed}/{len(chosen)} scenarios pass")
    print("crash_test: FAIL" if failed else "crash_test: PASS")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
