#!/usr/bin/env python3
"""Fly all of Corneria with co-op players, play what was flown twice and compare (#142, decision 0002, point 2).

    python tools/coop/full_level.py                  # unattended; prints PASS or FAIL
    python tools/coop/full_level.py --replay <file>  # only the plain runs, on a replay this tool wrote before
    python tools/coop/full_level.py --by-rule        # input by rule, no pilot: what happens without one

A replay cannot see, and flying by rule player 0 is shot down halfway through the rails. So a run has two parts:

  pilot    One game plays input by rule through the menus, and from the moment a player is active the pilot flies it
           (src/port/coop/test/pilot.h): it reads game state and writes pads, and it writes every frame to a replay,
           checkpoints included. This run must reach level complete and then the map. The replay is cut there.
  A, B     Two games play that replay with no pilot. Only gCoopDebugPlayerCount is set, as a session is the only other
           way to have a second player: no debug switch. Every checkpoint must match, so the replay result must be PASS,
           and the two state hash logs must be equal on every frame. They must equal the pilot's log too: that shows
           the pilot changed nothing but the pads.
  C        One more game, the same replay with one bit of player 2 flipped in all-range: the hash must first differ
           at exactly that frame, or two equal logs would say nothing.

A, B and C run at once. The frames and level states the run reached come from the checkpoints in the replay, and each
player's frames, shields and downs from the pilot's trace. A run where a player goes down still counts, and says so.
Everything stays in a fresh local/test-full-level-* folder and in local/test-replay-*. A replay holds raw structs, so it
only plays on the build that made it. About 3 minutes, each game runs for as long as the level takes.

It runs at test speed (game_env in replay.py), the pilot too: it reads game state once a frame and never the clock, so a
frame is the same frame at any speed, and its log must still equal the plain runs' on every frame (#137).

Exit code 0 on PASS, 1 otherwise. --by-rule shows why a pilot is needed, so it fails when the level is not finished.
"""
import argparse
import struct
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hash_check  # noqa: E402
import player_trace  # noqa: E402
import replay as replay_tool  # noqa: E402
from run_folder import make_run_folder  # noqa: E402

# include/sf64thread.h, include/sf64level.h, include/sf64player.h
GS_MAP, GS_PLAY = 4, 7
LEVEL_CORNERIA = 0
DOWN, LEVEL_COMPLETE, START_360 = 4, 7, 9
ACTIVE = player_trace.ACTIVE

# Corneria from level start to the map took 11200 frames, and the menus 3100. The rest is room for a longer boss fight.
RULE_FRAMES = 16000
MAP_TAIL = 60  # frames kept after the map comes up
POKE_AFTER = 300  # frames into all-range for run C: both players fly
ORDER = (("level start", None), ("player 0 active", ACTIVE), ("the cut to all-range", START_360),
         ("level complete", LEVEL_COMPLETE))


def read_checkpoints(path):
    """Return (frame, game state, level, player 0 state) of every frame of a replay (ReplayFrame in
    src/port/coop/test/replay.c)."""
    data = Path(path).read_bytes()
    pad_size, frame_size = struct.unpack_from("<II", data, 8)
    at = replay_tool.HEADER_SIZE + 4 + 8 * pad_size
    return [(frame, *struct.unpack_from("<2ixxxxi", data, at + frame * frame_size))
            for frame in range((len(data) - replay_tool.HEADER_SIZE) // frame_size)]


def level_states(checkpoints):
    """The first frame of each state of Corneria and of the map after it: a list of (frame, game state, player
    state)."""
    states = []
    for frame, game_state, level, state in checkpoints:
        if level != LEVEL_CORNERIA or game_state not in (GS_PLAY, GS_MAP):
            continue
        if game_state == GS_MAP and not any(s == LEVEL_COMPLETE for _, _, s in states):
            continue  # the map before the level
        if game_state == GS_PLAY and state < 0:
            continue  # no player yet
        if not states or states[-1][1:] != (game_state, state):
            states.append((frame, game_state, state))
        if game_state == GS_MAP:
            break
    return states


def print_states(states):
    for frame, game_state, state in states:
        name = "map" if game_state == GS_MAP else f"Corneria, player 0 {player_trace.STATES.get(state, state)}"
        print(f"full_level: frame {frame:6}: {name}")


def milestones(states):
    """Frames of the states a whole level has to pass. Returns (name -> frame, what is missing)."""
    found = {}
    for name, wanted in ORDER:
        found[name] = next((f for f, _, s in states if wanted is None or s == wanted), None)
    found["map"] = next((f for f, g, _ in states if g == GS_MAP), None)
    all_range = found["the cut to all-range"]
    if all_range is not None:
        found["all-range active"] = next((f for f, _, s in states if f > all_range and s == ACTIVE), None)
    return found, [name for name, frame in found.items() if frame is None]


def print_players(trace, players):
    """Per player: what a trace says about Corneria."""
    rows = [r for r in player_trace.parse(trace) if r.level == LEVEL_CORNERIA]
    for i in range(players):
        slots = [r.players[i] for r in rows]
        flown = sum(1 for s in slots if s.state in player_trace.FLYING and not s.parked)
        down = sum(1 for a, b in zip(slots, slots[1:]) if a.state != DOWN and b.state == DOWN)
        lowest = min((s.shields for s in slots if s.state in player_trace.FLYING and not s.parked), default=0)
        end = next((s.shields for r, s in zip(rows, slots) if r.players[0].state == LEVEL_COMPLETE), None)
        print(f"full_level: player {i}: flew {flown} game frames, went down {down} times, lowest shields {lowest}, "
              f"shields at level complete {end if end is not None else 'not reached'}")


def config_for(players):
    return {"CVars": {"gCoopDebugPlayerCount": players}} if players != 1 else None


def rule_replay(work, frames):
    rule = work / "rule.sfcr"
    hash_check.write_generated_replay(rule, frames=frames, l_frame=-1)
    return rule


def fly(args, exe, work):
    """The pilot's run. Returns (replay cut at the map, milestones, the run's hash log rows), or None."""
    flown = work / "flown.sfcr"
    trace = work / "trace.txt"
    env = {"SF64_COOP_PILOT": str(flown), "SF64_COOP_PILOT_LOG": str(work / "pilot-log.txt"),
           "SF64_COOP_TRACE": str(trace)}
    print(f"full_level: pilot, {args.frames} frames at most, {args.players} players")
    values, _, rows = hash_check.play(rule_replay(work, args.frames), exe, args.timeout, "full-pilot", env_extra=env,
                                      config=config_for(args.players))
    if not flown.exists():
        print("full_level: FAIL, the pilot wrote no replay (does this build have src/port/coop/test/pilot.c?)")
        return None
    states = level_states(read_checkpoints(flown))
    print_states(states)
    found, missing = milestones(states)
    if trace.exists():
        print_players(trace, args.players)
    if not values or values.get("result") not in ("PASS", "FAIL_MISMATCH"):
        print(f"full_level: FAIL, the pilot's run ended with {values.get('result') if values else 'no report'}")
        return None
    if missing:
        print(f"full_level: FAIL, the pilot did not reach: {', '.join(missing)}")
        return None
    frames = found["map"] + MAP_TAIL
    size = struct.unpack_from("<I", flown.read_bytes(), 12)[0]
    cut = work / "corneria.sfcr"
    cut.write_bytes(flown.read_bytes()[:replay_tool.HEADER_SIZE + frames * size])
    print(f"full_level: replay {cut}, {frames} frames")
    return cut, found, rows


def compare(args, exe, replay, found, pilot_rows):
    """Runs A, B and C. Returns a list of failures."""
    frames = replay_tool.replay_frame_count(replay)
    poke = found["all-range active"] + POKE_AFTER
    config = config_for(args.players)
    print(f"full_level: runs A, B and C (poke in player {args.players - 1} at frame {poke}) at once")
    with ThreadPoolExecutor(max_workers=3) as pool:
        jobs = [pool.submit(hash_check.play, replay, exe, args.timeout, "full-a", config=config),
                pool.submit(hash_check.play, replay, exe, args.timeout, "full-b", config=config),
                pool.submit(hash_check.play, replay, exe, args.timeout, "full-c", poke, config=config,
                            env_extra={"SF64_COOP_HASH_POKE_PLAYER": str(args.players - 1)})]
        (a_values, names, a_rows), (b_values, _, b_rows), (_, _, c_rows) = [job.result() for job in jobs]

    failures = []
    for tag, values in (("A", a_values), ("B", b_values)):
        result = values.get("result") if values else "no report"
        if result != "PASS":
            failures.append(f"run {tag} ended with {result}: {values.get('mismatch_got') if values else ''}")
        elif int(values["recorded_frames"]) != frames:
            failures.append(f"run {tag} played {values['recorded_frames']} of {frames} frames")
        else:
            print(f"full_level: run {tag}: replay PASS, every checkpoint of {frames} frames matched")
    if a_rows is None or b_rows is None or c_rows is None:
        return failures + ["a run wrote no hash log"]
    if min(len(a_rows), len(b_rows)) < frames:
        return failures + [f"hash logs shorter than the replay ({len(a_rows)}, {len(b_rows)} < {frames} frames)"]

    diff = hash_check.first_difference(a_rows, b_rows, frames)
    if diff is not None:
        failures.append(f"A and B differ: {hash_check.describe(names, diff)}")
    else:
        print(f"full_level: A and B equal on all {frames} frames: state, level, total and {len(names)} regions")
    if pilot_rows is not None:
        diff = hash_check.first_difference(pilot_rows, a_rows, frames)
        if diff is not None:
            failures.append(f"the pilot's run and A differ: {hash_check.describe(names, diff)}")
        else:
            print(f"full_level: the pilot's run and A equal on all {frames} frames")
    diff = hash_check.first_difference(a_rows, c_rows, frames)
    if diff is None:
        failures.append(f"poke at frame {poke} did not change any hash")
    elif diff[1][0] != poke:
        failures.append(f"poke at frame {poke}, but first difference at {hash_check.describe(names, diff)}")
    else:
        print(f"full_level: poke seen at exactly its frame: {hash_check.describe(names, diff)}")
    return failures


def by_rule(args, exe, work):
    trace = work / "trace.txt"
    print(f"full_level: input by rule, no pilot, {args.frames} frames, {args.players} players")
    values, _, _ = hash_check.play(rule_replay(work, args.frames), exe, args.timeout, "full-rule",
                                   env_extra={"SF64_COOP_TRACE": str(trace)}, config=config_for(args.players))
    if not values or not trace.exists():
        print("full_level: FAIL, the run did not play through")
        return 1
    print_players(trace, args.players)
    done = int(values.get("level_complete_frame", -1))
    print(f"full_level: final: {values.get('final')}")
    if done < 0:
        print(f"full_level: FAIL, no level complete in {args.frames} frames")
        return 1
    print(f"full_level: level complete at frame {done}")
    print("full_level: PASS")
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--players", type=int, default=2, help="players, through gCoopDebugPlayerCount (default 2)")
    p.add_argument("--frames", type=int, default=RULE_FRAMES, help=f"most frames to fly (default {RULE_FRAMES})")
    p.add_argument("--replay", help="skip the pilot: a replay this tool wrote, for runs A, B and C")
    p.add_argument("--by-rule", action="store_true", help="input by rule, no pilot; fails unless the level is finished")
    p.add_argument("--exe", help="Starship executable (default: newest build found)")
    p.add_argument("--timeout", type=float, default=1800, help="per run: give up after this many seconds")
    args = p.parse_args()

    exe = replay_tool.find_exe(args.exe)
    work = make_run_folder(replay_tool.ROOT / "local", "test-full-level-")
    print(f"full_level: folder {work}")
    print(f"full_level: CVars set: {(config_for(args.players) or {'CVars': {}})['CVars']}, no debug switch beside it")
    if args.by_rule:
        return by_rule(args, exe, work)

    pilot_rows = None
    if args.replay:
        replay = Path(args.replay).resolve()
        states = level_states(read_checkpoints(replay))
        print_states(states)
        found, missing = milestones(states)
        if missing:
            print(f"full_level: FAIL, this replay does not reach: {', '.join(missing)}")
            return 1
    else:
        flown = fly(args, exe, work)
        if flown is None:
            return 1
        replay, found, pilot_rows = flown

    failures = compare(args, exe, replay, found, pilot_rows)
    for failure in failures:
        print(f"FAIL: {failure}")
    print(f"full_level: level complete at frame {found['level complete']}, map at frame {found['map']}")
    print("full_level: FAIL" if failures else "full_level: PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
