#!/usr/bin/env python3
"""Tests for the input source of each player (WP-14): who steers whom, in a game with no one at the controls.

    python tools/coop/input_test.py                  # every scenario
    python tools/coop/input_test.py --only unit,live
    python tools/coop/input_test.py --list
    python tools/coop/input_test.py --no-build --exe <file>

Each game scenario starts one game in a fresh folder under local/ with its own SHIP_HOME, past Corneria's intro
(replay.py, Start), and flies its rail section. The live local controller is a scripted gamepad (src/port/coop/test/test_pad.h). It goes through SDL and
libultraship to port 0, as a real pad does. At the same time pad 0 of the replay and port 1 of the machine say something
else on purpose: those are the decoys, and nobody may follow them. The checks read the pads and the flight of every
player from the per-frame trace (src/port/coop/test/player_trace.h).

  unit   no game, no ROM: src/port/coop/game/input.c alone, with every local index from 0 to 3, which the game cannot
         reach yet because CoopPlayers_LocalIndex() is 0 (tools/coop/inputtest/coop_input_test.c).
  live   a live local player beside a replayed one: SF64_COOP_REPLAY_LIVE_LOCAL. Player 0 follows the gamepad and not
         pad 0 of the replay, player 1 follows pad 1 of the replay exactly and not port 1.
  alone  co-op with no replay at all: player 0 follows the gamepad, player 1 has no stream and stays idle whatever
         port 1 does. Enemies aim at the nearest human (#193), so the idle player gets shot and knocked about: it may
         move only by the knock-back of a hit, and only as far as one can push it (#243).
  whole  a replay without SF64_COOP_REPLAY_LIVE_LOCAL still flies every player, gamepad or not.
  shots  what a player's shot does after it left the ship answers to that player: a lock-search shot lives on its owner's
         A, a bomb goes off on its owner's B, a lock-on runs down when its owner lets go of A. Pad 0 says the opposite.
  solo   co-op off: SF64_COOP_REPLAY_LIVE_LOCAL changes nothing, the replay keeps flying player 0.

Prints one line per check and a RESULT line, exits 0 only when everything passed. Run it with no window and off the
shared GPU; see docs/coop/QA.md.
"""
import argparse
import json
import platform
import shutil
import subprocess
import sys
import time
from collections import namedtuple
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
IS_WINDOWS = platform.system() == "Windows"
sys.path.insert(0, str(ROOT / "tools" / "coop"))
import replay as replay_tool  # noqa: E402
from player_trace import BUTTON_A, ACTIVE, parse_row, rail_pad, write_rail_replay  # noqa: E402

BUTTON_B = 0x4000
BUTTON_R = 0x0010  # R_TRIG
BUTTON_Z = 0x2000  # Z_TRIG
SHOT_BOMB, SHOT_LOCK_SEARCH = 3, 4  # include/sf64player.h

# The game starts past Corneria's intro: who steers whom is the same with or without one (replay.py, Start). Frames of a
# rule below count from when player 0 flies. From INPUT_START the gamepad and pad 0 of the replay each repeat CYCLE
# frames, always steering the other way.
START = replay_tool.FLYING
INPUT_START = 25
CYCLE = 240
FRAMES = START.frame(1225)
MIN_FLOWN = 1100
STICK = 80
PRESS_FRAMES = 5  # a press has to outlast the two frames a gamepad takes to reach the game
MAX_LAG = 6  # frames from a gamepad step to gControllerHold[0]; two on the runs in NOTES.md (#68)
STICK_MIN = 40  # a gamepad at STICK must show at least this much after libultraship's dead zone
MIN_SWEEP = 600.0  # as `rails` in player_trace.py: a player who flies by a stick covers this much of x
MAX_DRIFT = 100.0  # and one with no input moves at most this far in the flight box, besides what hits pushed it
# x that one hit may push it. An enemy shot sets a knock-back of 20 (src/port/coop/game/effects.c, CoopEffects_ShotHit),
# which Player_Update adds to pos.x and runs down to 0 (src/engine/fox_play.c, Math_SmoothStepToF in fox_std_lib.c): 212
# the one way and 233 the other, as it runs down by 0.5 a frame at the end of a negative one.
MAX_PUSH = 250.0
SETTLE = 10  # frames after a change of direction left out of the flight check

Pad = namedtuple("Pad", "button x y press_button press_x press_y")
Shot = namedtuple("Shot", "id owner timer exploded")
Knock = namedtuple("Knock", "x y z hit_timer")
Frame = namedtuple("Frame", "tframe row pads shots locks knocks")


def pad_rule(flight):
    """(button, stick_x, stick_y) of the gamepad, `flight` frames after player 0 flies."""
    if flight < INPUT_START:
        return 0, 0, 0
    t = (flight - INPUT_START) % CYCLE
    button = (BUTTON_A if t % 40 < PRESS_FRAMES else 0) | (BUTTON_R if 60 <= t < 75 else 0)
    return button, STICK if t < CYCLE // 2 else -STICK, 0


def decoy_rule(flight):
    """(button, stick_x, stick_y) of pad 0 of the replay: the other way from the gamepad, with a button the gamepad never
    presses."""
    if flight < INPUT_START:
        return 0, 0, 0
    _, stick_x, _ = pad_rule(flight)
    return BUTTON_Z, -stick_x, 0


PORT1_DECOY = (BUTTON_A | BUTTON_R, -70, 30)  # port 1 from INPUT_START on

# `shots`: player 1 charges in long holds of A, with nobody on pad 0 holding A meanwhile. Then one bomb. A bomb can only
# go off by button below shot timer 25, 5 frames after it left (src/engine/fox_beam.c, PlayerShot_UpdateBomb). Player 0
# presses B first, while its own bomb cannot leave because the team has one bomb slot, and player 1 after that.
CHARGE_START = 75
CHARGE_HOLD = 150
CHARGE_GAP = 50
CHARGE_END = 725
BOMB_LAUNCH = 825
BOMB_DECOY = 835
BOMB_OWNER = 843
# Frames with a search shot of player 1 in flight. No shot leaves while a lock-on holds (Player_CanLockOn), so this depends
# on what there is to lock on to. Before WP-14 it was 0: the shot died on the frame it left unless player 0 held A.
MIN_SEARCH_FRAMES = 20
LOCK_RELEASE = 25  # frames a lock-on may outlast its owner's A: lockOnTimers starts at 20 and runs down once a frame


def shots_lead(flight):
    """Pad 0 of `shots`: nothing but B at BOMB_DECOY."""
    return (BUTTON_B if flight == BOMB_DECOY else 0), 0, 0


def shots_pad(flight, pad):
    """Pad 1 of `shots`, the others idle."""
    if pad != 1:
        return 0, 0, 0
    if CHARGE_START <= flight < CHARGE_END and (flight - CHARGE_START) % (CHARGE_HOLD + CHARGE_GAP) < CHARGE_HOLD:
        return BUTTON_A, 0, 0
    return (BUTTON_B if flight in (BOMB_LAUNCH, BOMB_OWNER) else 0), 0, 0


def write_pad_script(path, frames):
    """Write the file SF64_COOP_TEST_PAD reads: a step for every change of the gamepad, and one for port 1."""
    lines = []
    last = None
    for frame in range(frames):
        now = pad_rule(frame - START.active)
        if now != last:
            lines.append((frame, 0, *now))
            last = now
    lines.append((START.frame(INPUT_START), 1, *PORT1_DECOY))
    lines.sort(key=lambda step: step[:2])
    path.write_text("".join(f"{f} {port} {b:x} {x} {y}\n" for f, port, b, x, y in lines), encoding="utf-8")


def parse_trace(path):
    frames = []
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    for n, line in enumerate(lines):
        try:
            fields = dict(kv.split("=", 1) for kv in line.split())
            pads = [Pad(*(int(v) for v in fields[f"pad{i}"].split(","))) for i in range(4)]
            shots = [] if fields["shots"] == "-" else [Shot(*(int(v) for v in shot.split(":")))
                                                       for shot in fields["shots"].split(";")]
            locks = [int(v) for v in fields["locks"].split(",")]
            # A build before #243 writes no knock-back: no player has one then, and the idle check fails.
            knocks = [Knock(*(float(v) for v in k.split(","))) for k in fields["knock"].split(";")] \
                if "knock" in fields else []
            frames.append(Frame(int(fields["tframe"]), parse_row(line), pads, shots, locks, knocks))
        except (KeyError, ValueError, IndexError, TypeError):
            if n != len(lines) - 1:  # stopping the game can cut the last line
                raise
    return frames


class Scenario:
    def __init__(self, name, args, folder):
        self.name = name
        self.args = args
        self.folder = folder
        self.results = []

    def check(self, text, good):
        self.results.append(good)
        print(f"  {'PASS' if good else 'FAIL'}  {text}", flush=True)
        return good

    def passed(self):
        return bool(self.results) and all(self.results)


def wait_for_trace(proc, trace, frames, timeout):
    """Wait until the trace reaches test pad frame `frames`. For a run with no replay, whose end nothing reports."""
    start = time.monotonic()
    while proc.poll() is None and time.monotonic() - start < timeout:
        time.sleep(2)
        if not trace.exists():
            continue
        with open(trace, "rb") as f:
            f.seek(0, 2)
            f.seek(max(0, f.tell() - 600))
            tail = f.read().decode("utf-8", "replace")
        marks = [int(part.split("=")[1]) for part in tail.split() if part.startswith("tframe=") and
                 part.split("=")[1].lstrip("-").isdigit()]
        if marks and marks[-1] >= frames:
            return True
    return False


def run_replay_game(s, players, replay, pad_script, live_local):
    env = {"SF64_COOP_TEST_PAD": str(pad_script)}
    trace = s.folder / f"{s.name}-trace.txt"
    env["SF64_COOP_TRACE"] = str(trace)
    if live_local:
        env["SF64_COOP_REPLAY_LIVE_LOCAL"] = "1"
    config = {"CVars": {"gCoopDebugPlayerCount": players, "gInvincible": 1}}
    values, run, seconds = replay_tool.run_game("play", replay, s.args.exe, s.args.timeout, tag=f"input-{s.name}",
                                                env_extra=env, config=config, start=START)
    result = values["result"] if values else "none"
    s.check(f"replay played through in {seconds:.0f}s: {result} (no checkpoints in it, so FAIL_MISMATCH is expected)",
            result == "FAIL_MISMATCH")
    return parse_trace(trace) if trace.exists() else [], run


def run_plain_game(s, players, pad_script):
    exe = s.args.exe
    run = replay_tool.make_run_dir(exe, f"input-{s.name}")
    trace = s.folder / f"{s.name}-trace.txt"
    config = {"CVars": {"gCoopDebugPlayerCount": players, "gInvincible": 1}}
    (run / "starship.cfg.json").write_text(json.dumps(config, indent=4), encoding="utf-8")
    env = replay_tool.game_env(run)
    env.update(START.env)
    env["SF64_COOP_TEST_PAD"] = str(pad_script)
    env["SF64_COOP_TRACE"] = str(trace)
    began = time.monotonic()
    proc = subprocess.Popen([str(exe)], cwd=run, env=env)
    try:
        reached = wait_for_trace(proc, trace, FRAMES, s.args.timeout)
    finally:
        if proc.poll() is None:
            # Only the process this script started.
            proc.terminate()
            try:
                proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
    s.check(f"game reached frame {FRAMES} by itself in {time.monotonic() - began:.0f}s", reached)
    return parse_trace(trace) if trace.exists() else [], run


def flown(frames, count):
    """Frames from INPUT_START to the end of the replay with every player flying."""
    return [f for f in frames if START.frame(INPUT_START) <= f.tframe < FRAMES and
            all(p.state == ACTIVE and not p.parked for p in f.row.players[:count])]


def same_as_rule(pad, frame, rule):
    """Hold and press as write_rail_replay stores them: the same stick in both, press has the buttons that went down."""
    flight = frame - START.active
    button, x, y = rule(flight)
    return (pad.button, pad.x, pad.y) == (button, x, y) and \
        (pad.press_button, pad.press_x, pad.press_y) == (button & ~rule(flight - 1)[0], x, y)


def same_stick(seen, pushed):
    """libultraship scales a stick, so a push shows as about the same in the same direction."""
    if pushed == 0:
        return seen == 0
    return (seen > 0) == (pushed > 0) and abs(seen) >= STICK_MIN


def script_at(f, lag):
    """What the gamepad's script said `lag` frames before f."""
    return pad_rule(f.tframe - lag - START.active)


def measure_lag(rows, port=0):
    """Frames between the script and the pad of player 0: the lag at which the buttons match best."""
    def misses(lag):
        return sum(1 for f in rows if rows and f.pads[port].button != script_at(f, lag)[0])
    return min(range(MAX_LAG + 1), key=misses)


def check_follows_gamepad(s, rows, player):
    """Player `player` has what the gamepad pressed, some frames later, and none of what the replay's pad 0 says."""
    lag = measure_lag(rows, player)
    wrong_button = [f.tframe for f in rows if f.pads[player].button != script_at(f, lag)[0]]
    wrong_stick = [f.tframe for f in rows if not same_stick(f.pads[player].x, script_at(f, lag)[1])]
    s.check(f"player {player} buttons are the gamepad's, {lag} frames later, on {len(rows) - len(wrong_button)} of "
            f"{len(rows)} frames", bool(rows) and not wrong_button)
    s.check(f"player {player} stick is the gamepad's on {len(rows) - len(wrong_stick)} of {len(rows)} frames",
            bool(rows) and not wrong_stick)
    decoy = [f.tframe for f in rows if f.pads[player].button & BUTTON_Z]
    s.check(f"player {player} never has the replay's decoy button: {len(decoy)} frames", not decoy)
    return lag


def check_press_edges(s, frames, player):
    """A press is the frame a button went down. Both ways #68 saw it go wrong: repeating, and lost."""
    pairs = [(a, b) for a, b in zip(frames, frames[1:]) if b.tframe == a.tframe + 1]
    wrong = [b.tframe for a, b in pairs if b.pads[player].press_button !=
             (b.pads[player].button & ~a.pads[player].button)]
    edges = sum(1 for a, b in pairs if b.pads[player].button & ~a.pads[player].button)
    s.check(f"player {player} press edges right on {len(pairs) - len(wrong)} of {len(pairs)} frames, "
            f"{edges} presses in them", bool(pairs) and edges > 0 and not wrong)


def check_stream(s, rows, player, rule):
    """Player `player` has exactly what its stream says on every frame, hold and press."""
    wrong = [f for f in rows if not same_as_rule(f.pads[player], f.tframe, rule)]
    s.check(f"player {player} pad is its stream's on {len(rows) - len(wrong)} of {len(rows)} frames"
            + (f", first wrong {wrong[0].tframe}: {wrong[0].pads[player]}" if wrong else ""), bool(rows) and not wrong)


def check_idle(s, rows, player):
    wrong = [f.tframe for f in rows if any(f.pads[player])]
    first = next((f.pads[player] for f in rows if any(f.pads[player])), None)
    s.check(f"player {player} pad idle on {len(rows) - len(wrong)} of {len(rows)} frames"
            + (f", first wrong {wrong[0]}: {first}" if wrong else ""), bool(rows) and not wrong)


def offset(f, player):
    """Where player `player` is in the flight box."""
    slot = f.row.players[player]
    return slot.x - slot.xpath


def check_flies_right_way(s, rows, player, lag):
    """The stick moves the ship: over each half cycle player `player` goes the way the gamepad pushes."""
    halves = {}
    for f in rows:
        t = f.tframe - lag - START.frame(INPUT_START)
        if t >= 0 and t % (CYCLE // 2) >= SETTLE:
            halves.setdefault(t // (CYCLE // 2), []).append(f)
    right = wrong = 0
    for number, part in halves.items():
        if len(part) < 2:
            continue
        moved = offset(part[-1], player) - offset(part[0], player)
        pushed = script_at(part[0], lag)[1]
        if (moved > 0) == (pushed > 0):
            right += 1
        else:
            wrong += 1
    s.check(f"player {player} flew the gamepad's way on {right} of {right + wrong} half cycles",
            right >= 4 and wrong == 0)


def sweep(rows, player):
    places = [offset(f, player) for f in rows]
    return max(places) - min(places) if places else 0.0


def idle_moves(rows, player):
    """How player `player` moved in its flight box: (x no hit explains, hits, x the hits pushed it).

    A hit sets hitTimer (Player_ApplyDamage: 20, then one less a frame) and most hits a knock-back, which moves the ship
    by itself until it has run down to 0 (src/engine/fox_play.c, Player_Update). Movement from the frame hitTimer goes up
    until the knock-back is 0 on two frames in a row is that hit's. Any other movement is not, nor is a knock-back that
    no hit started. knockback.z runs down in all-range only, so on the rails it can stay and is left out."""
    free = pushed = 0.0
    hits = 0
    pushing = False
    for a, b in zip(rows, rows[1:]):
        if b.tframe != a.tframe + 1:
            pushing = False
            continue
        ka, kb = a.knocks[player], b.knocks[player]
        if kb.hit_timer > ka.hit_timer:
            hits += 1
            pushing = True
        elif (ka.x, ka.y, kb.x, kb.y) == (0, 0, 0, 0):
            pushing = False
        moved = abs(offset(b, player) - offset(a, player))
        if pushing:
            pushed += moved
        else:
            free += moved
    return free, hits, pushed


def check_idle_place(s, rows, player):
    """An idle player stays where it is, unless a hit pushes it, and a hit pushes it at most MAX_PUSH (#243)."""
    if not (rows and all(len(f.knocks) > player for f in rows)):
        s.check(f"player {player}'s knock-back is in the trace (a build before #243 has none)", False)
        return
    free, hits, pushed = idle_moves(rows, player)
    s.check(f"player {player} with no stream moved {free:.0f} in its flight box besides hits (max {MAX_DRIFT:.0f}); "
            f"{hits} hits pushed it {pushed:.0f} (max {MAX_PUSH:.0f} a hit)",
            free <= MAX_DRIFT and pushed <= hits * MAX_PUSH)


def replay_rule(pad):
    def rule(flight):
        return decoy_rule(flight) if pad == 0 else rail_pad(flight, pad)
    return rule


def scenario_unit(s):
    args = s.args
    target = "coop_input_test"
    exe = args.build_dir / "coop-input-test" / (target + (".exe" if IS_WINDOWS else ""))
    if not args.no_build:
        code = subprocess.run(["cmake", "--build", str(args.build_dir), "--config", args.config, "--target", target],
                              cwd=ROOT, capture_output=True, text=True)
        if not s.check(f"built {target}", code.returncode == 0):
            print(code.stdout[-2000:] + code.stderr[-2000:])
            return
    if not s.check(f"found {exe.name}", exe.exists()):
        return
    run = subprocess.run([str(exe)], cwd=ROOT, capture_output=True, text=True)
    (s.folder / "unit.txt").write_text(run.stdout + run.stderr, encoding="utf-8")
    lines = run.stdout.strip().splitlines()
    for line in lines:
        if line.startswith("FAIL"):
            print(f"  {line}")
    s.check(lines[-1] if lines else "no output from the unit test", run.returncode == 0)


def scenario_live(s):
    frames, _ = run_replay_game(s, 2, s.args.replay, s.args.pad_script, live_local=True)
    rows = flown(frames, 2)
    s.check(f"{len(rows)} frames with both players flying (min {MIN_FLOWN})", len(rows) >= MIN_FLOWN)
    lag = check_follows_gamepad(s, rows, 0)
    check_press_edges(s, rows, 0)
    check_flies_right_way(s, rows, 0, lag)
    check_stream(s, rows, 1, replay_rule(1))
    check_press_edges(s, rows, 1)
    s.check(f"player 1 swept {sweep(rows, 1):.0f} of x on its stream (min {MIN_SWEEP:.0f})",
            sweep(rows, 1) >= MIN_SWEEP)


def scenario_alone(s):
    frames, _ = run_plain_game(s, 2, s.args.pad_script)
    rows = flown(frames, 2)
    s.check(f"{len(rows)} frames with both players flying (min {MIN_FLOWN})", len(rows) >= MIN_FLOWN)
    lag = check_follows_gamepad(s, rows, 0)
    check_press_edges(s, rows, 0)
    check_flies_right_way(s, rows, 0, lag)
    check_idle(s, rows, 1)
    check_idle_place(s, rows, 1)


def scenario_whole(s):
    frames, _ = run_replay_game(s, 2, s.args.replay, s.args.pad_script, live_local=False)
    rows = flown(frames, 2)
    s.check(f"{len(rows)} frames with both players flying (min {MIN_FLOWN})", len(rows) >= MIN_FLOWN)
    check_stream(s, rows, 0, replay_rule(0))
    check_stream(s, rows, 1, replay_rule(1))


def scenario_shots(s):
    frames, _ = run_replay_game(s, 2, s.args.shots_replay, s.args.pad_script_empty, live_local=False)
    rows = flown(frames, 2)
    s.check(f"{len(rows)} frames with both players flying (min {MIN_FLOWN})", len(rows) >= MIN_FLOWN)

    search = [f for f in rows if any(shot.id == SHOT_LOCK_SEARCH and shot.owner == 1 for shot in f.shots)]
    s.check(f"a lock-search shot of player 1 flew on {len(search)} frames with only its owner holding A "
            f"(min {MIN_SEARCH_FRAMES})", len(search) >= MIN_SEARCH_FRAMES)
    alive = sum(1 for f in search if not f.pads[1].button & BUTTON_A)
    s.check(f"a lock-search shot of player 1 outlived its owner's A on {alive} frames", not alive)

    bomb = [f for f in rows if any(shot.id == SHOT_BOMB and shot.owner == 1 for shot in f.shots)]
    went_off = next((f.tframe for f in bomb if any(shot.id == SHOT_BOMB and shot.exploded for shot in f.shots)), None)
    s.check(f"the bomb of player 1 left at {bomb[0].tframe if bomb else None}, went off at {went_off}: "
            f"its owner pressed B at {START.frame(BOMB_OWNER)}, player 0 at {START.frame(BOMB_DECOY)}",
            went_off == START.frame(BOMB_OWNER))

    locked = [f for f in rows if f.locks[1] > 0]
    released = [f.tframe for f in rows if f.locks[1] > 0 and not f.pads[1].button & BUTTON_A]
    longest = current = 0
    for f in rows:
        current = current + 1 if f.locks[1] > 0 and not f.pads[1].button & BUTTON_A else 0
        longest = max(longest, current)
    s.check(f"player 1 locked on to something on {len(locked)} frames", bool(locked))
    s.check(f"a lock-on of player 1 outlasted its owner's A by at most {longest} frames (max {LOCK_RELEASE}), "
            f"{len(released)} frames in all", bool(locked) and longest <= LOCK_RELEASE)


def scenario_solo(s):
    frames, _ = run_replay_game(s, 1, s.args.replay, s.args.pad_script, live_local=True)
    rows = flown(frames, 1)
    s.check(f"{len(rows)} frames with player 0 flying (min {MIN_FLOWN})", len(rows) >= MIN_FLOWN)
    check_stream(s, rows, 0, replay_rule(0))


SCENARIOS = {"unit": scenario_unit, "live": scenario_live, "alone": scenario_alone, "whole": scenario_whole,
             "shots": scenario_shots, "solo": scenario_solo}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", help="scenarios, comma separated")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--exe", help="Starship executable (default: newest build found)")
    ap.add_argument("--no-build", action="store_true", help="unit: use the test program already built")
    ap.add_argument("--build-dir", type=Path, help="default: build/x64 on Windows, build-cmake elsewhere")
    ap.add_argument("--config", default="Debug")
    ap.add_argument("--timeout", type=int, default=900, help="seconds to wait for each game (default 900)")
    args = ap.parse_args()
    if args.list:
        print("\n".join(SCENARIOS))
        return 0
    names = args.only.split(",") if args.only else list(SCENARIOS)
    unknown = [n for n in names if n not in SCENARIOS]
    if unknown:
        sys.exit(f"input_test: no scenario {', '.join(unknown)}; --list shows them")
    args.build_dir = args.build_dir or (Path("build/x64") if IS_WINDOWS else Path("build-cmake"))
    args.build_dir = args.build_dir if args.build_dir.is_absolute() else ROOT / args.build_dir

    folder = ROOT / "local" / f"test-input-{time.strftime('%Y%m%d-%H%M%S')}"
    folder.mkdir(parents=True)
    if any(n != "unit" for n in names):
        args.exe = replay_tool.find_exe(args.exe)
        args.replay = folder / "input.sfcr"
        args.pad_script = folder / "pad.txt"
        args.shots_replay = folder / "shots.sfcr"
        args.pad_script_empty = folder / "pad-empty.txt"
        write_rail_replay(args.replay, FRAMES, lead=decoy_rule, start=START)
        write_rail_replay(args.shots_replay, FRAMES, lead=shots_lead, pad_rule=shots_pad, start=START)
        write_pad_script(args.pad_script, FRAMES)
        args.pad_script_empty.write_text("", encoding="utf-8")  # only the frame count of the test pad, no steps

    failed = []
    for name in names:
        print(f"input_test: {name}", flush=True)
        s = Scenario(name, args, folder)
        SCENARIOS[name](s)
        if not s.passed():
            failed.append(name)
    print(f"input_test: logs in {folder}")
    print(f"RESULT: {'FAILED: ' + ', '.join(failed) if failed else 'PASS'} ({len(names) - len(failed)}/{len(names)})")
    if not failed:
        shutil.rmtree(folder, ignore_errors=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
