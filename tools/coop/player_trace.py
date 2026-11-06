#!/usr/bin/env python3
"""Run a replay with several co-op players and check what each player did, from the per-frame player trace.

    python tools/coop/player_trace.py rails --players 2                                # input by rule, 1 and N players
    python tools/coop/player_trace.py run   local/replays/corneria.sfcr --players 2   # play it, then check
    python tools/coop/player_trace.py check local/test-trace-<time>/trace.txt          # check an existing trace

`run` makes a fresh folder under local/ (data files copied in, a config with only gCoopDebugPlayerCount set), then
starts the game with SF64_COOP_REPLAY_PLAY (input replay, WP-03) and SF64_COOP_TRACE
(src/port/coop/test/player_trace.h). The owner's own config, saves and mods are never used. The end of a replay does
not close the game, so `run` stops it once the replay report is written. The run fails when the game exits first or
no report comes before --timeout. Two or more players leave the recording on purpose, so FAIL_MISMATCH is fine there;
one player must PASS (a replay by rule holds no checkpoints, so `rails` takes FAIL_MISMATCH from one player too).

`check` reads the trace and prints, per player, its state changes and how long it was active. It then checks these
rules and exits 0 when all hold:

  - the trace holds --players players (nothing more is checked with one),
  - players 1 and up are parked (PLAYERSTATE_INIT) until player 0 first flies,
  - on rails in an Arwing, each of them becomes active on the frame player 0 does (WP-12), and has player 0's path
    (zPath, xPath, yPath) on every frame it flies there, while gPathProgress stays player 0's. Other forms stay parked
    on rails (WP-18),
  - in all-range mode, each of them becomes active on the frame player 0 does (WP-11),
  - and then flies: at least --min-active frames, always drawn, on average more than --min-move per frame, and never
    still for more than --max-still frames in a row while player 0 moves,
  - it is parked within a frame of player 0 no longer flying (level complete, down, the cut from rails to all-range). A
    frame, because player 0's state can change after the player loop (objects, hooks), and this player only sees that
    on the next loop,
  - a parked player is hidden. One frame is allowed: Player_Update draws a player again on the frame it goes down,
  - after every restart it is parked, or flying on the frame player 0 is.

`rails` is the test for WP-12. It plays one replay by rule (see write_rail_replay: pad 0 gets through the menus, fires,
boosts and brakes, the other pads fly stick sweeps, boost, brake and a loop) with one player and with --players at the
same time, checks the second trace as above, and compares the two on every frame player 0 flies Corneria's rail section
with both: gPathProgress, gPathVelZ and gPathTexScroll must match single player, the section must end on the same frame,
and each extra player must have covered --min-sweep units of x, stayed in the flight box, boosted and braked (its
camDist only) and looped. Both runs set gInvincible: flying by rule, player 0 is shot down half way through the section.

Stopping the game can cut the last line of the trace; only that line may be cut.
"""
import argparse
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import time
from collections import namedtuple
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "coop"))
from replay import game_env, read_report  # noqa: E402
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
INIT, ACTIVE, U_TURN = 1, 3, 5
FLYING = (ACTIVE, U_TURN)
ON_RAILS, ALL_RANGE = 0, 1
ARWING = 0

# One player on one frame and one whole frame, as player_trace.c writes them.
Slot = namedtuple("Slot", "state cs x y z parked draw zpath camdist xpath ypath width shields somersault form")
Row = namedtuple("Row", "frame level mode players path pathvel scroll")

# OSContPad.button
BUTTON_A = 0x8000
BUTTON_C_LEFT = 0x0002  # gBoostButton, src/engine/fox_game.c
BUTTON_C_DOWN = 0x0004  # gBrakeButton

# Input by rule for `rails`. Pad 0 presses A every 30 frames: that gets through the menus into Corneria, where player 0
# is active at about frame 4775, and keeps firing there. From RAIL_INPUT_START all pads repeat RAIL_CYCLE frames:
# rail_lead on pad 0, rail_pad on the others.
RAIL_INPUT_START = 4800
RAIL_CYCLE = 600
RAIL_FRAMES = 10500
RAIL_SEEDS = (1234, 5678, 9012, 111, 222, 333)
RAIL_REPLAY_HEADER = 40  # ReplayHeader in src/port/coop/test/replay.c
RAIL_SETTLE = 100  # frames of the section left out of the speed check: the level starts with its own boost


def find_exe(given):
    if given:
        return Path(given).resolve()
    found = [ROOT / c for c in EXE_CANDIDATES if (ROOT / c).exists()]
    if not found:
        sys.exit("player_trace: no Starship build found; run python tools/coop/build.py --assets first, or pass --exe")
    return max(found, key=lambda p: p.stat().st_mtime)


def parse_cvar(text):
    name, _, value = text.partition("=")
    return name, int(value or 1)


def play(args):
    """Play args.replay with args.players players. Returns the trace file, or None when the run failed."""
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
    cvars = {"gCoopDebugPlayerCount": args.players, **dict(parse_cvar(c) for c in args.cvar)}
    (run_dir / "starship.cfg.json").write_text(json.dumps({"CVars": cvars}))

    report_path = run_dir / "replay-report.txt"
    env = game_env(run_dir)
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
        return None
    result = report["result"]
    print(f"player_trace: replay finished after {elapsed:.0f}s: {result}")
    if result != "PASS" and not (result == "FAIL_MISMATCH" and (args.players > 1 or args.no_checkpoints)):
        print("player_trace: FAIL, the replay did not play through")
        return None
    trace = run_dir / "trace.txt"
    if not trace.exists():
        print("player_trace: FAIL, no trace written (is the trace code in this build?)")
        return None
    return trace


def run(args):
    trace = play(args)
    return 1 if trace is None else check_trace(trace, args)


def parse_row(line):
    f = dict(kv.split("=", 1) for kv in line.split())
    players = []
    for i in range(int(f["players"])):
        s = f[f"p{i}"].split(",")
        players.append(Slot(int(s[0]), int(s[1]), float(s[2]), float(s[3]), float(s[4]), int(s[5]), int(s[6]),
                            float(s[7]), float(s[8]), float(s[9]), float(s[10]), float(s[11]), int(s[12]), int(s[13]),
                            int(s[14])))
    return Row(int(f["frame"]), int(f["level"]), int(f["mode"]), players, float(f["path"]), float(f["pathvel"]),
               float(f["scroll"]))


def parse(path):
    lines = path.read_text().splitlines()
    rows = []
    for n, line in enumerate(lines):
        try:
            rows.append(parse_row(line))
        except (KeyError, ValueError, IndexError):
            if n != len(lines) - 1:
                raise
            print(f"player_trace: dropped a cut-off last line: {line[:60]}")
    return rows


def position(slot):
    return slot.x, slot.y, slot.z


def longest_freeze(rows, i):
    """Most frames in a row that player i was flying and did not move while player 0 did (a pause stops both)."""
    longest = current = 0
    for a, b in zip(rows, rows[1:]):
        p, q = a[3][i], b[3][i]
        flying = all(s.state in FLYING and not s.parked for s in (p, q))
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


def frames_flown_on(rows, i):
    """Most frames player i stayed in after player 0 stopped flying. None when player 0 did not stop after the join."""
    joined = False
    longest = None
    current = 0
    for row in rows:
        slot = row.players[i]
        joined = joined or (slot.state == ACTIVE and not slot.parked)
        if not joined or row.players[0].state in FLYING:
            current = 0
            continue
        longest = max(longest or 0, current)
        current = 0 if slot.parked else current + 1
        longest = max(longest, current)
    return longest


def first_attempt(rows):
    """Only the first attempt at the level: the frame count starts over after a restart."""
    first = []
    for r in rows:
        if first and r.frame < first[-1].frame:
            break
        first.append(r)
    return first


def rail_rows(rows):
    """Frames of the first attempt that player 0 flies on rails in an Arwing."""
    return [r for r in first_attempt(rows) if r.mode == ON_RAILS and r.players[0].state == ACTIVE and
            r.players[0].form == ARWING]


def flying(slot):
    return slot.state == ACTIVE and not slot.parked


def first_active(rows, i, mode):
    return next((r.frame for r in rows if r.mode == mode and flying(r.players[i])), None)


def check_extra_player(rows, i, args):
    """Return the rule results for extra player i: a list of (text, passed)."""
    first = first_attempt(rows)
    restarts = [b for a, b in zip(rows, rows[1:]) if b.frame < a.frame]  # the frame count starts over
    p0_flies = next((r.frame for r in first if r.players[0].state == ACTIVE), None)
    before = [r for r in first if p0_flies is None or r.frame < p0_flies]
    rails = rail_rows(rows)
    joined_rails = [r for r in rails if flying(r.players[i])]
    off_path = sum(1 for r in joined_rails if (r.players[i].zpath, r.players[i].xpath, r.players[i].ypath) !=
                   (r.players[0].zpath, r.players[0].xpath, r.players[0].ypath))
    not_p0 = sum(1 for r in rails if r.path != r.players[0].zpath)
    other_rails = [r for r in first if r.mode == ON_RAILS and r.players[0].form != ARWING]
    p0_360 = next((r.frame for r in first if r.mode == ALL_RANGE and r.players[0].state == ACTIVE), None)
    pi_360 = first_active(first, i, ALL_RANGE)
    p0_rails = rails[0].frame if rails else None
    pi_rails = first_active(rails, i, ON_RAILS)
    active = [r.players[i] for r in first if flying(r.players[i])]
    moves = [math.dist(position(a), position(b)) for a, b in zip(active, active[1:])]
    avg = sum(moves) / len(moves) if moves else 0.0
    freeze = longest_freeze(first, i)
    shown = longest_shown_parked(first, i)
    flown_on = frames_flown_on(first, i)
    left = "player 0 did not stop" if flown_on is None else f"{flown_on} frames after player 0 stopped"
    late = sum(1 for r in restarts if not r.players[i].parked and r.players[0].state != ACTIVE)
    results = [
        (f"player {i} parked until player 0 flies ({len(before)} frames)",
         all(r.players[i].parked and r.players[i].state == INIT for r in before)),
    ]
    if rails:
        results += [
            (f"player {i} active on rails on frame {pi_rails}, player 0 on {p0_rails}", pi_rails == p0_rails),
            (f"player {i} off player 0's path on {off_path} of {len(joined_rails)} rail frames", off_path == 0),
            (f"gPathProgress differs from player 0's zPath on {not_p0} of {len(rails)} rail frames", not_p0 == 0),
        ]
    if other_rails:
        results.append((f"player {i} parked on rails outside an Arwing ({len(other_rails)} frames)",
                        all(r.players[i].parked for r in other_rails)))
    if p0_360 is not None:
        results.append((f"player {i} active in all-range on frame {pi_360}, player 0 on {p0_360}", pi_360 == p0_360))
    results += [
        (f"player {i} flew {len(active)} frames (min {args.min_active})", len(active) >= args.min_active),
        (f"player {i} hidden on {sum(not s.draw for s in active)} of them", all(s.draw for s in active)),
        (f"player {i} moves {avg:.1f} per active frame (min {args.min_move})", avg > args.min_move),
        (f"player {i} still for at most {freeze} frames while player 0 moved (max {args.max_still})",
         freeze <= args.max_still),
        (f"player {i} parked: {left} (max 1)", (flown_on or 0) <= 1),
        (f"player {i} drawn while parked for at most {shown} frame(s) in a row (max 1)", shown <= 1),
        (f"player {i} parked or with player 0 after {len(restarts)} restart(s)", late == 0),
    ]
    return results


def report_results(results):
    ok = True
    for text, good in results:
        print(f"  {'PASS' if good else 'FAIL'}  {text}")
        ok = ok and good
    return ok


def check_trace(path, args):
    rows = parse(path)
    if not rows:
        print("player_trace: FAIL, empty trace")
        return 1
    count = len(rows[0].players)
    print(f"player_trace: {len(rows)} frames, {count} player(s)")
    for i in range(count):
        prev = None
        print(f"  player {i}:")
        for frame, level, mode, players, *_ in rows:
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
    ok = True
    for i in range(1, count):
        ok = report_results(check_extra_player(rows, i, args)) and ok
    print(f"player_trace: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


def rail_sweep(t, period, reach):
    """A triangle wave from -reach to reach and back."""
    phase = t % period
    half = period // 2
    return reach * (2 * phase - half) // half if phase < half else reach * (3 * half - 2 * phase) // half


def rail_pad(frame, index):
    """(button, stick_x, stick_y) of extra player `index` on `frame`: sweeps both ways, then boost, brake and a loop.

    Each player's sweep has its own phase, so no two fly the same line. Player_ArwingBoost starts a loop when the stick
    goes below -50 and boost is pressed within the next few frames (src/engine/fox_play.c, gLoopDownTimers and
    gLoopBoostTimers).
    """
    if frame < RAIL_INPUT_START:
        return 0, 0, 0
    t = (frame - RAIL_INPUT_START) % RAIL_CYCLE
    stick_x = rail_sweep(t + 50 * index, 200, 70)
    stick_y = rail_sweep(t + 30 * index, 300, 40)
    button = BUTTON_A if t % 20 == 0 else 0
    if 100 <= t < 125:  # short enough that the boost meter stays below its cooldown
        button |= BUTTON_C_LEFT
    if 300 <= t < 325:
        button |= BUTTON_C_DOWN
    if 480 <= t < 490:
        stick_x, stick_y = 0, -80
        if t >= 482:
            button |= BUTTON_C_LEFT
    return button, stick_x, stick_y


def rail_lead(frame):
    """Buttons of player 0 on `frame`, past the menus: a boost and a brake every cycle, so that the speed along the path
    changes and a player who does not follow it shows."""
    t = (frame - RAIL_INPUT_START) % RAIL_CYCLE
    if frame < RAIL_INPUT_START:
        return 0
    if 200 <= t < 225:
        return BUTTON_C_LEFT
    if 400 <= t < 425:
        return BUTTON_C_DOWN
    return 0


def write_rail_replay(path, frames):
    """Write the input for `rails`, in the layout of ReplayHeader and ReplayFrame in src/port/coop/test/replay.c: no
    checkpoints, so playback reports FAIL_MISMATCH at frame 0 and plays on. Hold and press pads get the same stick;
    press holds the buttons that went down on this frame."""
    data = bytearray(b"SFCR" + struct.pack("<III6i", 2, 20, 204, *RAIL_SEEDS))
    assert len(data) == RAIL_REPLAY_HEADER
    last = [0, 0, 0, 0]
    for frame in range(frames):
        hold = bytearray()
        press = bytearray()
        for pad in range(4):
            if pad == 0:
                button, stick_x, stick_y = (BUTTON_A if frame >= 60 and frame % 30 == 0 else 0) | rail_lead(frame), 0, 0
            else:
                button, stick_x, stick_y = rail_pad(frame, pad)
            hold += struct.pack("<Hbb16x", button, stick_x, stick_y)
            press += struct.pack("<Hbb16x", button & ~last[pad], stick_x, stick_y)
            last[pad] = button
        data += struct.pack("<I", frame) + hold + press + bytes(40)
    path.parent.mkdir(parents=True, exist_ok=True)
    # Two runs read the file at once, so it appears complete or not at all.
    temp = path.with_name(f"{path.name}.{os.getpid()}")
    temp.write_bytes(data)
    temp.replace(path)


def compare_path(solo, coop, args):
    """Return the rule results for path progress: `coop` (N players) against `solo` (1 player), frame by frame."""
    solo_rows = {r.frame: r for r in rail_rows(solo)}
    coop_rows = rail_rows(coop)
    both = [(solo_rows[r.frame], r) for r in coop_rows if r.frame in solo_rows]
    first, last = (both[0][1].frame, both[-1][1].frame) if both else (None, None)
    print(f"player_trace: rail section: {len(solo_rows)} frames with 1 player, {len(coop_rows)} with "
          f"{len(coop[0].players)}, {len(both)} in common (frames {first} to {last})")
    results = [(f"rail section compared on {len(both)} frames (min {args.min_rail})", len(both) >= args.min_rail)]
    if not both:
        return results
    results.append((f"rail section ends on frame {coop_rows[-1].frame}, with 1 player on {max(solo_rows)}",
                    coop_rows[-1].frame == max(solo_rows)))
    for name, get in (("gPathProgress", lambda r: r.path), ("gPathVelZ", lambda r: r.pathvel),
                      ("gPathTexScroll", lambda r: r.scroll)):
        worst = max(abs(get(a) - get(b)) for a, b in both)
        same = sum(1 for a, b in both if get(a) == get(b))
        results.append((f"{name} as in single player on {same} of {len(both)} frames, largest difference {worst:.3f} "
                        f"(max {args.max_path_diff})", worst <= args.max_path_diff))
    speeds = [r.pathvel for _, r in both[RAIL_SETTLE:]]
    results.append((f"speed along the path {min(speeds):.1f} to {max(speeds):.1f}: player 0 braked and boosted",
                    bool(speeds) and min(speeds) < 35 and max(speeds) > 45))
    a, b = both[0], both[-1]
    span = len(both) - 1
    if span:
        print(f"player_trace: path progress {a[1].path:.1f} to {b[1].path:.1f}, "
              f"{(b[1].path - a[1].path) / span:.3f} per frame; single player {(b[0].path - a[0].path) / span:.3f}")
    # Not a rule: an extra player changes the RNG and what is left of the enemies, so player 0 can be hit elsewhere.
    apart = max(math.hypot(a.players[0].x - b.players[0].x, a.players[0].y - b.players[0].y) for a, b in both)
    print(f"player_trace: player 0 at most {apart:.1f} from where it was with 1 player, in x and y")
    return results


def check_rail_flight(rows, i, args):
    """Return the rule results for what extra player i did on rails: its own flight on player 0's path."""
    flown = [r for r in rail_rows(rows) if flying(r.players[i])]
    if not flown:
        return [(f"player {i} flew on rails", False)]
    slots = [r.players[i] for r in flown]
    sweep = max(s.x for s in slots) - min(s.x for s in slots)
    rise = max(s.y for s in slots) - min(s.y for s in slots)
    apart = sum(abs(s.x - r.players[0].x) for s, r in zip(slots, flown)) / len(slots)
    # Player_PerformLoop has no box, only a floor. The frame a loop ends is still outside; the next one is clamped.
    outside = sum(1 for a, b in zip(slots, slots[1:]) if abs(b.x - b.xpath) > b.width + args.box_slack and
                  not a.somersault and not b.somersault)
    ahead = min(s.camdist for s in slots)
    behind = max(s.camdist for s in slots)
    loops = sum(1 for a, b in zip(slots, slots[1:]) if b.somersault and not a.somersault)
    return [
        (f"player {i} flew {len(flown)} rail frames, to the end of the section: shields {slots[-1].shields}",
         slots[-1].shields > 0 and len(flown) == len(rail_rows(rows))),
        (f"player {i} swept {sweep:.0f} in x (min {args.min_sweep}), {rise:.0f} in y", sweep >= args.min_sweep),
        (f"player {i} {apart:.0f} from player 0 in x on average (min 100)", apart >= 100),
        (f"player {i} outside the flight box on {outside} frames, loops left out", outside == 0),
        (f"player {i} camDist {ahead:.0f} to {behind:.0f}: boosted (below -100) and braked (above 50)",
         ahead < -100 and behind > 50),
        (f"player {i} looped {loops} time(s) (min 1)", loops >= 1),
    ]


def rails(args):
    replay = ROOT / "local" / "test-trace-rails.sfcr"
    write_rail_replay(replay, args.frames)
    args.replay = str(replay)
    args.no_checkpoints = True
    args.cvar = args.cvar + ["gInvincible=1"]  # flying by rule, player 0 would not live through the whole section
    solo_args = argparse.Namespace(**{**vars(args), "players": 1})
    with ThreadPoolExecutor(max_workers=2) as pool:
        solo_job = pool.submit(play, solo_args)
        coop_job = pool.submit(play, args)
        solo_trace, coop_trace = solo_job.result(), coop_job.result()
    if solo_trace is None or coop_trace is None:
        print("player_trace: FAIL")
        return 1
    print(f"player_trace: traces {solo_trace} and {coop_trace}")
    failed = check_trace(coop_trace, args)
    solo, coop = parse(solo_trace), parse(coop_trace)
    ok = report_results(compare_path(solo, coop, args))
    for i in range(1, args.players):
        ok = report_results(check_rail_flight(coop, i, args)) and ok
    ok = ok and not failed
    print(f"player_trace: rails {'PASS' if ok else 'FAIL'}")
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
    r.add_argument("--no-checkpoints", action="store_true",
                   help="the replay is input by rule: FAIL_MISMATCH is fine with one player too")
    r.add_argument("--cvar", action="append", default=[], metavar="NAME[=VALUE]", help="another CVar for the run")
    add_check_options(r)
    c = sub.add_parser("check", help="check an existing trace file")
    c.add_argument("trace")
    add_check_options(c)
    t = sub.add_parser("rails", help="input by rule on Corneria's rails: N players against one player")
    t.add_argument("--exe")
    t.add_argument("--timeout", type=int, default=900, help="seconds to wait for each replay report (default 900)")
    t.add_argument("--frames", type=int, default=RAIL_FRAMES, help=f"replay length (default {RAIL_FRAMES})")
    t.add_argument("--min-rail", type=int, default=4000, help="rail frames both runs must share (default 4000)")
    t.add_argument("--min-sweep", type=float, default=600.0, help="x range each extra player must cover (default 600)")
    t.add_argument("--max-path-diff", type=float, default=0.0,
                   help="largest difference from single player on the rail section (default 0)")
    t.add_argument("--box-slack", type=float, default=100.0,
                   help="how far knockback may push past the box (default 100)")
    t.add_argument("--cvar", action="append", default=[], metavar="NAME[=VALUE]", help="another CVar for both runs")
    add_check_options(t)
    args = ap.parse_args()
    if args.cmd == "run":
        sys.exit(run(args))
    if args.cmd == "rails":
        sys.exit(rails(args))
    sys.exit(check_trace(Path(args.trace), args))


if __name__ == "__main__":
    main()
