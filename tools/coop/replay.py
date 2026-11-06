#!/usr/bin/env python3
"""Record or play back controller input for Starship Co-op tests (WP-03).

    python tools/coop/replay.py record local/replays/corneria.sfcr   # you play; close the window to stop
    python tools/coop/replay.py play   local/replays/corneria.sfcr   # unattended; prints the result
    python tools/coop/replay.py record local/replays/idle.sfcr --frames 300   # unattended; nobody plays, stops itself

Every run gets a fresh folder under local/ (config, save and logs start empty) and SHIP_HOME set to that folder, so a
recording and its playback start from the same state and the owner's own setup is never touched, even when SHIP_HOME
is set in the shell. The game is told what to do through the SF64_COOP_REPLAY_* environment variables
(src/port/coop/test/replay.h). Playback exits 0 on PASS, 1 otherwise: only a whole file can pass, so a file with no
frames or a cut last frame fails. python tools/coop/replay_test.py tests all this.

A game started from here makes no sound, opens no window on Linux and runs at TEST_SPEED: see game_env. Only record
without --frames runs as a person plays it. To watch a playback, set SDL_VIDEODRIVER and SDL_AUDIODRIVER in the shell and
pass --speed 1.

A replay file holds raw structs: it only plays back on the same build and platform it was recorded on.
"""
import argparse
import itertools
import json
import os
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXE_CANDIDATES = [
    "build/x64/Release/Starship.exe",
    "build/x64/RelWithDebInfo/Starship.exe",
    "build/x64/Debug/Starship.exe",
    "build-cmake/Starship",
    "build-cmake/Starship.app/Contents/MacOS/Starship",
]
DATA_FILES = ["sf64.o2r", "starship.o2r", "gamecontrollerdb.txt"]
HEADER_SIZE = 40  # ReplayHeader in src/port/coop/test/replay.c
TEST_SPEED = "max"  # no frame limit for a test game: every test here counts frames, not seconds (#137)
REAL_TIME = {"SF64_COOP_TEST_SPEED": "1"}  # as env_extra, for a run that needs wall-clock time; say why next to it


def find_exe(given):
    if given:
        return Path(given).resolve()
    found = [ROOT / c for c in EXE_CANDIDATES if (ROOT / c).exists()]
    if not found:
        sys.exit("replay: no Starship build found; run python tools/coop/build.py --assets first, or pass --exe")
    return max(found, key=lambda p: p.stat().st_mtime)


def find_data(exe, name):
    for d in (exe.parent, exe.parent.parent, ROOT / "build/x64", ROOT / "build-cmake", ROOT):
        if (d / name).exists():
            return d / name
    return None


def make_run_dir(exe, mode):
    # Runs can start in the same second, in two processes or in two threads of one, and with the same tag. mkdir either
    # makes the folder or says it is taken, so no two runs get one folder and the later one adds a number.
    stamped = ROOT / "local" / f"test-replay-{mode}-{time.strftime('%Y%m%d-%H%M%S')}"
    for number in itertools.count(1):
        run = stamped if number == 1 else stamped.with_name(f"{stamped.name}-{number}")
        try:
            run.mkdir(parents=True)
            break
        except FileExistsError:
            pass
    for name in DATA_FILES:
        src = find_data(exe, name)
        if src is None:
            if name.endswith(".o2r"):
                sys.exit(f"replay: {name} not found; run python tools/coop/build.py --assets first")
            continue
        shutil.copy2(src, run / name)
    return run


def replay_frame_count(replay):
    """Whole frames in a replay file: a header holding the frame size at byte 12, then one record per frame. A file
    too short to hold that header has none."""
    path = Path(replay)
    data = path.read_bytes() if path.exists() else b""
    if len(data) < HEADER_SIZE:
        return 0
    frame_size = struct.unpack_from("<I", data, 12)[0]
    return (len(data) - HEADER_SIZE) // frame_size if frame_size else 0


def read_report(path):
    if not path.exists():
        return None
    values = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        key, sep, value = line.partition("=")
        if sep:
            values[key] = value
    return values if "result" in values else None


def game_env(run, person_plays=False):
    """The environment for a game in folder run: this one with SHIP_HOME at that folder and without any SF64_COOP_*
    variable, as one left in the shell would change a test with no sign in its result.

    A test game is silent and has no window, also from a bare shell: a driver the shell sets still wins. The offscreen
    driver is for Linux only, as the default backend on Windows draws into a real window. It runs at TEST_SPEED, which
    env_extra can replace (REAL_TIME). With person_plays someone needs to see, hear and fly the game, so it gets none of
    that. The caller adds what the run needs."""
    env = {key: value for key, value in os.environ.items() if not key.startswith("SF64_COOP_")}
    env["SHIP_HOME"] = str(run)
    if not person_plays:
        env.setdefault("SDL_AUDIODRIVER", "dummy")
        if sys.platform.startswith("linux"):
            env.setdefault("SDL_VIDEODRIVER", "offscreen")
        env["SF64_COOP_TEST_SPEED"] = TEST_SPEED
    return env


def run_game(mode, replay, exe, timeout, hash_log=False, poke=None, tag=None, env_extra=None, config=None, frames=None):
    """Start one game in a fresh run folder and wait for the replay report.

    Returns (report values or None, run folder, seconds). With hash_log the run also writes state-hash.txt into the
    run folder (src/port/coop/sync/state_hash.h); poke flips one byte of game state at that frame. env_extra adds
    environment variables (for example the test switches in src/port/coop/test/switches.h); config is a dict written
    as the run folder's starship.cfg.json before the game starts (graphics backend, window size, CVars). With frames a
    recording stops once the file holds that many; it can hold more, as the game writes in blocks and this looks once a
    second.
    """
    run = make_run_dir(exe, mode if tag is None else f"{mode}-{tag}")
    if config is not None:
        (run / "starship.cfg.json").write_text(json.dumps(config, indent=4), encoding="utf-8")
    report = run / "replay-report.txt"
    env = game_env(run, person_plays=mode == "record" and frames is None)
    env["SF64_COOP_REPLAY_RECORD" if mode == "record" else "SF64_COOP_REPLAY_PLAY"] = str(replay)
    env["SF64_COOP_REPLAY_REPORT"] = str(report)
    if hash_log:
        env["SF64_COOP_HASH_LOG"] = str(run / "state-hash.txt")
        if poke is not None:
            env["SF64_COOP_HASH_POKE"] = str(poke)
    env.update(env_extra or {})

    print(f"replay: {mode} {replay}")
    print(f"replay: exe {exe}")
    print(f"replay: run folder {run}")
    if mode == "record" and frames is None:
        print("replay: play now; close the game window to stop recording")
    start = time.monotonic()
    proc = subprocess.Popen([str(exe)], cwd=run, env=env)

    values = None
    try:
        while True:
            if mode == "play":
                values = read_report(report)
                if values is not None:
                    break
            elif frames is not None and replay_frame_count(replay) >= frames:
                break
            if (mode == "play" or frames is not None) and time.monotonic() - start > timeout:
                print("replay: timeout")
                break
            if proc.poll() is not None:
                break
            time.sleep(1)
    finally:
        if proc.poll() is None:
            # Only the process this script started.
            proc.terminate()
            try:
                proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                proc.kill()

    if values is None:
        time.sleep(1)
        values = read_report(report)
    if values is None:
        print(f"replay: no report written (exit code {proc.returncode}); see {run / 'logs'}")
    return values, run, time.monotonic() - start


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("mode", choices=["record", "play"])
    p.add_argument("file", help="replay file (keep it under local/, it is made from your game)")
    p.add_argument("--exe", help="Starship executable (default: newest build found)")
    p.add_argument("--timeout", type=float, default=1800,
                   help="playback and record --frames: give up after this many seconds")
    p.add_argument("--frames", type=int, help="record: stop after this many frames, so nobody has to close the window")
    p.add_argument("--hash-log", action="store_true", help="also write state-hash.txt into the run folder (WP-04)")
    p.add_argument("--speed", help=f"times real time, 1 for real time, max for no limit (default {TEST_SPEED}; record "
                                   "without --frames always runs in real time)")
    args = p.parse_args()

    exe = find_exe(args.exe)
    replay = Path(args.file).resolve()
    if args.mode == "play" and not replay.exists():
        sys.exit(f"replay: {replay} does not exist")
    replay.parent.mkdir(parents=True, exist_ok=True)

    speed = None if args.speed is None else {"SF64_COOP_TEST_SPEED": args.speed}
    values, run, elapsed = run_game(args.mode, replay, exe, args.timeout, hash_log=args.hash_log, frames=args.frames,
                                    env_extra=speed)
    if values is None:
        print(f"replay: FAILED ({elapsed:.0f}s)")
        return 1

    for key in ("frames", "recorded_frames", "first_mismatch", "mismatch_want", "mismatch_got",
                "level_complete_frame", "level_complete_level", "final"):
        if key in values:
            print(f"  {key}: {values[key]}")
    print(f"replay: {values['result']} ({args.mode}, {elapsed:.0f}s)")
    if args.mode == "play":
        return 0 if values["result"] == "PASS" else 1
    return 0 if values["result"] == "RECORDED" else 1


if __name__ == "__main__":
    sys.exit(main())
