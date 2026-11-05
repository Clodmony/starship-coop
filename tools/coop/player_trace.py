#!/usr/bin/env python3
"""Run a replay with several co-op players and check what each player did, from the per-frame player trace (WP-11).

    python tools/coop/player_trace.py run   local/replays/corneria.sfcr --players 2   # play it, then check
    python tools/coop/player_trace.py check local/test-trace-<time>/trace.txt          # check an existing trace

`run` makes a fresh folder under local/ (data files copied in, a config with only gCoopDebugPlayerCount set), then
starts the game with SF64_COOP_REPLAY_PLAY (input replay, WP-03; needs that code in the build) and SF64_COOP_TRACE
(src/port/coop/test/player_trace.h). The owner's own config, saves and mods are never used.

`check` reads the trace and prints, per player, its state changes and how long it was active. With more than one
player it then checks the WP-11 rules and exits 0 when all hold:

  - players 1 and up stay parked (PLAYERSTATE_INIT) while the level is on rails,
  - each of them becomes active in all-range mode, on the frame player 0 does,
  - and then moves (average distance per active frame above --min-move).
"""
import argparse
import json
import math
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

# include/sf64player.h
STATES = {0: "STANDBY", 1: "INIT", 2: "LEVEL_INTRO", 3: "ACTIVE", 4: "DOWN", 5: "U_TURN", 6: "NEXT",
          7: "LEVEL_COMPLETE", 8: "ENTER_WARP_ZONE", 9: "START_360", 10: "GFOX_REPAIR", 11: "ANDROSS_MOUTH",
          12: "UNK_12", 13: "VS_STANDBY"}
INIT, ACTIVE = 1, 3
ON_RAILS, ALL_RANGE = 0, 1


def find_exe(given):
    if given:
        return Path(given).resolve()
    found = [ROOT / c for c in EXE_CANDIDATES if (ROOT / c).exists()]
    if not found:
        sys.exit("player_trace: no Starship build found; run python tools/coop/build.py --assets first, or pass --exe")
    return max(found, key=lambda p: p.stat().st_mtime)


def run(args):
    exe = find_exe(args.exe)
    replay = Path(args.replay).resolve()
    run_dir = ROOT / "local" / f"test-trace-{args.players}p-{time.strftime('%Y%m%d-%H%M%S')}"
    run_dir.mkdir(parents=True)
    for name in DATA_FILES:
        for d in (exe.parent, exe.parent.parent, ROOT / "build/x64", ROOT / "build-cmake", ROOT):
            if (d / name).exists():
                shutil.copy2(d / name, run_dir / name)
                break
        else:
            if name.endswith(".o2r"):
                sys.exit(f"player_trace: {name} not found; run python tools/coop/build.py --assets first")
    (run_dir / "starship.cfg.json").write_text(json.dumps({"CVars": {"gCoopDebugPlayerCount": args.players}}))

    env = dict(os.environ)
    env["SF64_COOP_REPLAY_PLAY"] = str(replay)
    env["SF64_COOP_REPLAY_REPORT"] = str(run_dir / "replay-report.txt")
    env["SF64_COOP_TRACE"] = str(run_dir / "trace.txt")
    print(f"player_trace: run folder {run_dir}")
    start = time.time()
    try:
        rc = subprocess.run([str(exe)], cwd=run_dir, env=env, timeout=args.timeout).returncode
        print(f"player_trace: game exited with {rc} after {time.time() - start:.0f}s")
    except subprocess.TimeoutExpired:
        print(f"player_trace: stopped the game after {args.timeout}s")
    trace = run_dir / "trace.txt"
    if not trace.exists():
        sys.exit("player_trace: no trace written (is the trace code in this build?)")
    return check_trace(trace, args.min_move)


def parse(path):
    rows = []
    for line in path.read_text().splitlines():
        f = dict(kv.split("=", 1) for kv in line.split())
        players = []
        for i in range(int(f["players"])):
            s = f[f"p{i}"].split(",")
            players.append((int(s[0]), int(s[1]), float(s[2]), float(s[3]), float(s[4])))
        rows.append((int(f["frame"]), int(f["level"]), int(f["mode"]), players))
    return rows


def check_trace(path, min_move):
    rows = parse(path)
    if not rows:
        print("player_trace: empty trace")
        return 1
    count = len(rows[0][3])
    print(f"player_trace: {len(rows)} frames, {count} player(s)")
    for i in range(count):
        prev = None
        print(f"  player {i}:")
        for frame, level, mode, players in rows:
            key = (mode, players[i][0])
            if key != prev:
                st, _, x, y, z = players[i]
                print(f"    frame {frame:6d} level {level} mode {mode} {STATES.get(st, st):<14} pos ({x:.0f}, {y:.0f}, {z:.0f})")
                prev = key
    if count < 2:
        return 0

    ok = True
    for i in range(1, count):
        # Only the first attempt at the level counts: after a restart the parking starts over.
        first = []
        for r in rows:
            if first and r[0] < first[-1][0]:
                break
            first.append(r)
        rails = [r for r in first if r[2] == ON_RAILS]
        parked_on_rails = all(r[3][i][0] == INIT for r in rails)
        p0_active = next((r[0] for r in first if r[2] == ALL_RANGE and r[3][0][0] == ACTIVE), None)
        pi_active = next((r[0] for r in first if r[2] == ALL_RANGE and r[3][i][0] == ACTIVE), None)
        active = [r[3][i] for r in first if r[3][i][0] == ACTIVE]
        moves = [math.dist(a[2:], b[2:]) for a, b in zip(active, active[1:])]
        avg = sum(moves) / len(moves) if moves else 0.0
        results = [
            (f"player {i} parked on rails ({len(rails)} frames)", parked_on_rails),
            (f"player {i} active in all-range on frame {pi_active}, player 0 on {p0_active}",
             pi_active is not None and pi_active == p0_active),
            (f"player {i} moves {avg:.1f} per active frame over {len(active)} frames (min {min_move})", avg > min_move),
        ]
        for text, good in results:
            print(f"  {'PASS' if good else 'FAIL'}  {text}")
            ok = ok and good
    print(f"player_trace: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run", help="play a replay with N players and check the trace")
    r.add_argument("replay", help="replay file (keep it under local/)")
    r.add_argument("--players", type=int, default=2)
    r.add_argument("--exe")
    r.add_argument("--timeout", type=int, default=600, help="seconds before the game is stopped (default 600)")
    r.add_argument("--min-move", type=float, default=5.0)
    c = sub.add_parser("check", help="check an existing trace file")
    c.add_argument("trace")
    c.add_argument("--min-move", type=float, default=5.0)
    args = ap.parse_args()
    if args.cmd == "run":
        sys.exit(run(args))
    sys.exit(check_trace(Path(args.trace), args.min_move))


if __name__ == "__main__":
    main()
