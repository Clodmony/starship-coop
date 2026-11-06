#!/usr/bin/env python3
"""Run a replay with several co-op players and check what each player did, from the per-frame player trace (WP-11).

    python tools/coop/player_trace.py run   local/replays/corneria.sfcr --players 2   # play it, then check
    python tools/coop/player_trace.py check local/test-trace-<time>/trace.txt          # check an existing trace

`run` makes a fresh folder under local/ (data files copied in, a config with only gCoopDebugPlayerCount set), then
starts the game with SF64_COOP_REPLAY_PLAY (input replay, WP-03) and SF64_COOP_TRACE
(src/port/coop/test/player_trace.h). The owner's own config, saves and mods are never used. The end of a replay does
not close the game, so `run` stops it once the replay report is written. The run fails when the game exits first or
no report comes before --timeout. Two or more players leave the recording on purpose, so FAIL_MISMATCH is fine there;
one player must PASS.

`check` reads the trace and prints, per player, its state changes and how long it was active. It then checks the WP-11
rules and exits 0 when all hold:

  - the trace holds --players players (nothing more is checked with one),
  - players 1 and up stay parked (PLAYERSTATE_INIT) while the level is on rails,
  - each of them becomes active in all-range mode, on the frame player 0 does,
  - and then flies: at least --min-active frames, on average more than --min-move per frame, and never still for more
    than --max-still frames in a row while player 0 moves,
  - a parked player is hidden. One frame is allowed: Player_Update draws a player again on the frame it goes down.
"""
import argparse
import json
import math
import os
import shutil
import subprocess
import sys
import time
from collections import namedtuple
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "coop"))
from replay import read_report  # noqa: E402
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

Slot = namedtuple("Slot", "state cs x y z parked draw")  # one player on one frame, as player_trace.c writes it


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

    report_path = run_dir / "replay-report.txt"
    env = dict(os.environ)
    for key in ("SF64_COOP_REPLAY_RECORD", "SF64_COOP_HASH_LOG", "SF64_COOP_HASH_POKE"):
        env.pop(key, None)
    env["SHIP_HOME"] = str(run_dir)
    env["SF64_COOP_REPLAY_PLAY"] = str(replay)
    env["SF64_COOP_REPLAY_REPORT"] = str(report_path)
    env["SF64_COOP_TRACE"] = str(run_dir / "trace.txt")
    print(f"player_trace: run folder {run_dir}")
    start = time.monotonic()
    proc = subprocess.Popen([str(exe)], cwd=run_dir, env=env)
    report = None
    try:
        while report is None and proc.poll() is None and time.monotonic() - start < args.timeout:
            time.sleep(1)
            report = read_report(report_path)
    finally:
        if proc.poll() is None:
            # Only the process this script started.
            proc.terminate()
            try:
                proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
    elapsed = time.monotonic() - start
    report = report or read_report(report_path)
    if report is None:
        print(f"player_trace: FAIL, no replay report after {elapsed:.0f}s (exit code {proc.returncode}); "
              f"the game crashed, hung or does not have the replay code")
        return 1
    result = report["result"]
    print(f"player_trace: replay finished after {elapsed:.0f}s: {result}")
    if result != "PASS" and not (result == "FAIL_MISMATCH" and args.players > 1):
        print("player_trace: FAIL, the replay did not play through")
        return 1
    trace = run_dir / "trace.txt"
    if not trace.exists():
        print("player_trace: FAIL, no trace written (is the trace code in this build?)")
        return 1
    return check_trace(trace, args)


def parse(path):
    rows = []
    for line in path.read_text().splitlines():
        f = dict(kv.split("=", 1) for kv in line.split())
        players = []
        for i in range(int(f["players"])):
            s = f[f"p{i}"].split(",")
            players.append(Slot(int(s[0]), int(s[1]), float(s[2]), float(s[3]), float(s[4]), int(s[5]), int(s[6])))
        rows.append((int(f["frame"]), int(f["level"]), int(f["mode"]), players))
    return rows


def position(slot):
    return slot.x, slot.y, slot.z


def longest_freeze(rows, i):
    """Most frames in a row that player i was flying and did not move while player 0 did (a pause stops both)."""
    longest = current = 0
    for a, b in zip(rows, rows[1:]):
        p, q = a[3][i], b[3][i]
        flying = all(s.state == ACTIVE and not s.parked for s in (p, q))
        if flying and position(p) == position(q) and position(a[3][0]) != position(b[3][0]):
            current += 1
            longest = max(longest, current)
        else:
            current = 0
    return longest


def longest_shown_parked(rows, i):
    """Most frames in a row that player i was parked and still drawn."""
    longest = current = 0
    for row in rows:
        slot = row[3][i]
        current = current + 1 if slot.parked and slot.draw else 0
        longest = max(longest, current)
    return longest


def check_trace(path, args):
    rows = parse(path)
    if not rows:
        print("player_trace: FAIL, empty trace")
        return 1
    count = len(rows[0][3])
    print(f"player_trace: {len(rows)} frames, {count} player(s)")
    for i in range(count):
        prev = None
        print(f"  player {i}:")
        for frame, level, mode, players in rows:
            slot = players[i]
            key = (mode, slot.state, slot.parked)
            if key != prev:
                note = " parked" if slot.parked else ""
                print(f"    frame {frame:6d} level {level} mode {mode} {STATES.get(slot.state, slot.state):<14} "
                      f"pos ({slot.x:.0f}, {slot.y:.0f}, {slot.z:.0f}){note}")
                prev = key
    if count != args.players:
        print(f"player_trace: FAIL, the trace holds {count} player(s), expected {args.players}")
        return 1
    if count < 2:
        return 0

    # Only the first attempt at the level counts: after a restart the parking starts over.
    first = []
    for r in rows:
        if first and r[0] < first[-1][0]:
            break
        first.append(r)
    rails = [r for r in first if r[2] == ON_RAILS]

    ok = True
    for i in range(1, count):
        parked_on_rails = all(r[3][i].parked and r[3][i].state == INIT for r in rails)
        p0_active = next((r[0] for r in first if r[2] == ALL_RANGE and r[3][0].state == ACTIVE), None)
        pi_active = next((r[0] for r in first if r[2] == ALL_RANGE and r[3][i].state == ACTIVE and not r[3][i].parked),
                         None)
        active = [r[3][i] for r in first if r[3][i].state == ACTIVE and not r[3][i].parked]
        moves = [math.dist(position(a), position(b)) for a, b in zip(active, active[1:])]
        avg = sum(moves) / len(moves) if moves else 0.0
        freeze = longest_freeze(first, i)
        shown = longest_shown_parked(first, i)
        results = [
            (f"player {i} parked on rails ({len(rails)} frames)", parked_on_rails),
            (f"player {i} active in all-range on frame {pi_active}, player 0 on {p0_active}",
             pi_active is not None and pi_active == p0_active),
            (f"player {i} flew {len(active)} frames (min {args.min_active})", len(active) >= args.min_active),
            (f"player {i} moves {avg:.1f} per active frame (min {args.min_move})", avg > args.min_move),
            (f"player {i} still for at most {freeze} frames while player 0 moved (max {args.max_still})",
             freeze <= args.max_still),
            (f"player {i} drawn while parked for at most {shown} frame(s) in a row (max 1)", shown <= 1),
        ]
        for text, good in results:
            print(f"  {'PASS' if good else 'FAIL'}  {text}")
            ok = ok and good
    print(f"player_trace: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


def add_check_options(parser):
    parser.add_argument("--players", type=int, default=2, help="players the trace must hold (default 2)")
    parser.add_argument("--min-move", type=float, default=5.0)
    parser.add_argument("--min-active", type=int, default=300, help="frames each extra player must fly (default 300)")
    parser.add_argument("--max-still", type=int, default=30,
                        help="frames an extra player may stay still while player 0 moves (default 30)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run", help="play a replay with N players and check the trace")
    r.add_argument("replay", help="replay file (keep it under local/)")
    r.add_argument("--exe")
    r.add_argument("--timeout", type=int, default=600, help="seconds to wait for the replay report (default 600)")
    add_check_options(r)
    c = sub.add_parser("check", help="check an existing trace file")
    c.add_argument("trace")
    add_check_options(c)
    args = ap.parse_args()
    if args.cmd == "run":
        sys.exit(run(args))
    sys.exit(check_trace(Path(args.trace), args))


if __name__ == "__main__":
    main()
