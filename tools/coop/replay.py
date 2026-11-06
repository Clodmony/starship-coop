#!/usr/bin/env python3
"""Record or play back controller input for Starship Co-op tests (WP-03).

    python tools/coop/replay.py record local/replays/corneria.sfcr   # you play; close the window to stop
    python tools/coop/replay.py play   local/replays/corneria.sfcr   # unattended; prints the result

Every run gets a fresh folder under local/ (config, save and logs start empty), so a recording and its playback start
from the same state and the owner's own setup is never touched. The game is told what to do through the
SF64_COOP_REPLAY_* environment variables (src/port/coop/test/replay.h). Playback exits 0 on PASS, 1 otherwise.

A replay file holds raw structs: it only plays back on the same build and platform it was recorded on.
"""
import argparse
import os
import shutil
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
    run = ROOT / "local" / f"test-replay-{mode}-{time.strftime('%Y%m%d-%H%M%S')}"
    run.mkdir(parents=True)
    for name in DATA_FILES:
        src = find_data(exe, name)
        if src is None:
            if name.endswith(".o2r"):
                sys.exit(f"replay: {name} not found; run python tools/coop/build.py --assets first")
            continue
        shutil.copy2(src, run / name)
    return run


def read_report(path):
    if not path.exists():
        return None
    values = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        key, sep, value = line.partition("=")
        if sep:
            values[key] = value
    return values if "result" in values else None


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("mode", choices=["record", "play"])
    p.add_argument("file", help="replay file (keep it under local/, it is made from your game)")
    p.add_argument("--exe", help="Starship executable (default: newest build found)")
    p.add_argument("--timeout", type=float, default=1800, help="playback: give up after this many seconds")
    args = p.parse_args()

    exe = find_exe(args.exe)
    replay = Path(args.file).resolve()
    if args.mode == "play" and not replay.exists():
        sys.exit(f"replay: {replay} does not exist")
    replay.parent.mkdir(parents=True, exist_ok=True)

    run = make_run_dir(exe, args.mode)
    report = run / "replay-report.txt"
    env = dict(os.environ)
    for key in ("SF64_COOP_REPLAY_RECORD", "SF64_COOP_REPLAY_PLAY"):
        env.pop(key, None)
    env["SF64_COOP_REPLAY_RECORD" if args.mode == "record" else "SF64_COOP_REPLAY_PLAY"] = str(replay)
    env["SF64_COOP_REPLAY_REPORT"] = str(report)

    print(f"replay: {args.mode} {replay}")
    print(f"replay: exe {exe}")
    print(f"replay: run folder {run}")
    if args.mode == "record":
        print("replay: play now; close the game window to stop recording")
    start = time.monotonic()
    proc = subprocess.Popen([str(exe)], cwd=run, env=env)

    values = None
    try:
        while True:
            if args.mode == "play":
                values = read_report(report)
                if values is not None:
                    break
                if time.monotonic() - start > args.timeout:
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
    elapsed = time.monotonic() - start
    if values is None:
        print(f"replay: FAILED, no report written (exit code {proc.returncode}, {elapsed:.0f}s); see {run / 'logs'}")
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
