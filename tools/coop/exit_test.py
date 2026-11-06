#!/usr/bin/env python3
"""Tests that a test run stops soon after a stop signal and leaves its files whole (#71). Linux only.

    python tools/coop/exit_test.py                       # unattended; prints PASS or FAIL
    python tools/coop/exit_test.py --only record,play --repeat 10
    python tools/coop/exit_test.py --list

Each run starts one game in its own folder, waits until it draws frames, sends the signal at a random moment and gives
the game EXIT_LIMIT seconds to end with exit code 0. A game still running then gets SIGKILL and fails the run. Each
scenario also checks what the game wrote on the way out: the closing line of the hash log and, with a replay, the
report and a replay file of whole frames with nothing missing at the end.

The game only takes this path in a test run, which it knows by the variables in src/port/coop/test/stop_signal.h. A run
without a replay asks for it with SF64_COOP_HASH_LOG. The scenario plain sets none of them and shows the other side: the
game keeps upstream's handler, which never ends it with exit code 0. It waits PLAIN_WAIT seconds for that, then SIGKILL.

Needs a build with assets (python tools/coop/build.py --assets) or --exe. Exit code 0 when every run holds, 1 otherwise.

On Linux the game runs without a window and off the GPU. Everything lands under local/test-*: the folders are deleted
when all runs hold and kept for reading when one fails.
"""
import argparse
import os
import random
import re
import shutil
import signal
import struct
import subprocess
import sys
import time
from collections import namedtuple
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hash_check  # noqa: E402
import replay as replay_tool  # noqa: E402
from run_folder import make_run_folder  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
EXIT_LIMIT = 10  # seconds; run_game in replay.py allows 15 before it uses SIGKILL
PLAIN_WAIT = 3  # seconds; ten times what the slowest test run took to end
READY_LIMIT = 120  # seconds until the game must have drawn frames
READY_FILE = "imgui.ini"  # ImGui writes it a few seconds after it starts to draw
MAX_DELAY = 1.5  # seconds after READY_FILE, so the signal does not land on the same point of a frame each time

# name: signal to send, mode ("plain", "hash", "record" or "play"), result line wanted in the report
Scenario = namedtuple("Scenario", "signal mode result")
SCENARIOS = {
    "plain": Scenario(signal.SIGTERM, "plain", None),
    "idle": Scenario(signal.SIGTERM, "hash", None),
    "sigint": Scenario(signal.SIGINT, "hash", None),
    "sigquit": Scenario(signal.SIGQUIT, "hash", None),
    "record": Scenario(signal.SIGTERM, "record", "RECORDED"),
    "play": Scenario(signal.SIGTERM, "play", "FAIL_INCOMPLETE"),
}


def start_game(exe, run, scenario, replay):
    env = {k: v for k, v in os.environ.items() if not k.startswith("SF64_COOP_")}
    env.update(SHIP_HOME=str(run), SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy", LIBGL_ALWAYS_SOFTWARE="1",
               GALLIUM_DRIVER="llvmpipe")
    if scenario.mode in ("record", "play"):
        env["SF64_COOP_REPLAY_RECORD" if scenario.mode == "record" else "SF64_COOP_REPLAY_PLAY"] = str(replay)
        env["SF64_COOP_REPLAY_REPORT"] = str(run / "replay-report.txt")
    if scenario.mode != "plain":
        env["SF64_COOP_HASH_LOG"] = str(run / "state-hash.txt")
    return subprocess.Popen([str(exe)], cwd=run, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def wait_ready(proc, run):
    start = time.monotonic()
    while not (run / READY_FILE).exists():
        if proc.poll() is not None or time.monotonic() - start > READY_LIMIT:
            return False
        time.sleep(0.2)
    return True


def stop(proc, sig):
    """Send sig to the game this script started. Returns (seconds until it ended or None, failures)."""
    start = time.monotonic()
    proc.send_signal(sig)
    try:
        proc.wait(timeout=EXIT_LIMIT)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        return None, [f"still running {EXIT_LIMIT}s after {sig.name}"]
    elapsed = time.monotonic() - start
    return elapsed, [] if proc.returncode == 0 else [f"exit code {proc.returncode}, want 0"]


def stop_plain(proc, sig):
    """Send sig to a game with no test variable. Only the handler of a test run ends it with exit code 0, so that is the
    failure; a game still hanging after PLAIN_WAIT seconds is what upstream's handler does. Same return as stop()."""
    start = time.monotonic()
    proc.send_signal(sig)
    try:
        proc.wait(timeout=PLAIN_WAIT)
    except subprocess.TimeoutExpired:
        return None, []
    elapsed = time.monotonic() - start
    return elapsed, ["exit code 0: a plain game lost upstream's handler"] if proc.returncode == 0 else []


def check_replay_file(replay, report_frames):
    data = replay.read_bytes() if replay.exists() else b""
    if len(data) < replay_tool.HEADER_SIZE:
        return ["replay file has no header"]
    frame_size = struct.unpack_from("<I", data, 12)[0]
    body = len(data) - replay_tool.HEADER_SIZE
    if body % frame_size:
        return [f"replay file ends inside a frame ({body % frame_size} bytes left over)"]
    if body // frame_size != report_frames:
        return [f"replay file holds {body // frame_size} frames, the report says {report_frames}"]
    if report_frames:
        last = struct.unpack_from("<I", data, len(data) - frame_size)[0]
        if last != report_frames - 1:
            return [f"last frame in the replay file is {last}, want {report_frames - 1}"]
    return []


def check_files(run, scenario, replay):
    """What the game must have written on its way out. Returns (failures, frames in the report or None)."""
    if scenario.mode == "plain":
        return [], None
    failures = []
    frames = None
    if scenario.mode != "hash":
        report = replay_tool.read_report(run / "replay-report.txt")
        if report is None:
            return ["no report, or a report without its result line"], None
        if report["result"] != scenario.result:
            failures.append(f"result {report['result']}, want {scenario.result}")
        frames = int(report.get("frames", -1))
        if frames <= 0:
            failures.append("report says no frames ran")
        if scenario.mode == "record":
            failures += check_replay_file(replay, frames)
    log = run / "state-hash.txt"
    lines = log.read_text(encoding="utf-8", errors="replace").splitlines() if log.exists() else []
    end = re.fullmatch(r"# end frames=(\d+)", lines[-1]) if lines else None
    if end is None:
        failures.append("hash log has lost its closing line")
    elif frames is None:
        frames = int(end.group(1))
    elif int(end.group(1)) != frames:
        failures.append(f"hash log says {end.group(1)} frames, the report {frames}")
    return failures, frames


def run_once(exe, folder, name, scenario, number):
    """One game. Returns (seconds until it ended or None, one result line, failures, run folder)."""
    run = replay_tool.make_run_dir(exe, f"exit-{name}-{number}")
    replay = run / "exit.sfcr"
    if scenario.mode == "play":
        shutil.copy2(folder / "play.sfcr", replay)
    proc = start_game(exe, run, scenario, replay)
    try:
        if not wait_ready(proc, run):
            return None, f"game did not get to draw (exit code {proc.poll()})", ["game did not start"], run
        time.sleep(random.uniform(0, MAX_DELAY))
        elapsed, failures = (stop_plain if scenario.mode == "plain" else stop)(proc, scenario.signal)
    finally:
        if proc.poll() is None:
            # Only the process this script started.
            proc.kill()
            proc.wait()
    more, frames = check_files(run, scenario, replay)
    failures += more
    took = "no exit" if elapsed is None else f"{elapsed:.2f}s"
    line = f"{took}, exit code {proc.returncode}" + (f", {frames} frames" if frames is not None else "")
    return elapsed, line, failures, run


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--only", help="comma-separated scenarios (default: all)")
    p.add_argument("--repeat", type=int, default=3, help="games per scenario (default: 3)")
    p.add_argument("--exe", help="Starship executable (default: newest build found)")
    p.add_argument("--list", action="store_true", help="print the scenarios and stop")
    args = p.parse_args()

    if args.list:
        for name, s in SCENARIOS.items():
            print(f"{name}: {s.signal.name}, {s.mode}")
        return 0
    if not sys.platform.startswith("linux"):
        sys.exit("exit_test: Linux only; only there does libultraship catch these signals")
    chosen = list(SCENARIOS) if not args.only else [s.strip() for s in args.only.split(",") if s.strip()]
    unknown = [s for s in chosen if s not in SCENARIOS]
    if unknown:
        sys.exit(f"exit_test: unknown scenario(s) {', '.join(unknown)}; known: {', '.join(SCENARIOS)}")

    exe = replay_tool.find_exe(args.exe)
    folder = make_run_folder(ROOT / "local", "test-exit-")
    hash_check.write_generated_replay(folder / "play.sfcr")
    print(f"exit_test: exe {exe}")

    start = time.monotonic()
    runs = []
    failed = 0
    slowest = 0.0
    for name in chosen:
        for number in range(args.repeat):
            elapsed, line, failures, run = run_once(exe, folder, name, SCENARIOS[name], number)
            runs.append(run)
            failed += bool(failures)
            slowest = max(slowest, elapsed or 0.0)
            print(f"exit_test: {name} {number + 1}: {'FAIL' if failures else 'OK'}, {line}"
                  + "".join(f"\n  {f}" for f in failures), flush=True)

    total = len(chosen) * args.repeat
    print(f"exit_test: {total - failed}/{total} runs OK, slowest exit {slowest:.2f}s, {time.monotonic() - start:.0f}s")
    if failed:
        print(f"exit_test: FAIL, folders kept under {ROOT / 'local'}")
        return 1
    for path in [folder, *runs]:
        # Only folders this test made: they sit directly in local/ and are named test-*.
        if path.parent == ROOT / "local" and path.name.startswith("test-"):
            shutil.rmtree(path, ignore_errors=True)
    print("exit_test: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
