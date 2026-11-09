#!/usr/bin/env python3
"""Solar's heat drains every player who flies, not Fox alone (#200).

    python tools/coop/solar_check.py [--exe PATH]     # unattended; prints PASS or FAIL

Plays Solar twice, with SF64_COOP_TEST_LOCAL_INDEX 0 and 1: two clients of one session (as view_check.py, whose view
trace and start it uses). gInvincible is off, which every other check turns on, so the heat acts. The window is the
first WINDOW frames of the level from when player 1 flies. Rules:

  joined   player 1 flies (not parked) in the window, on both clients
  fall-0   player 0's shields fall by at least FALL points over the window (upstream drain, unchanged)
  fall-1   player 1's shields fall by at least FALL points: this is the fix, and fails on a build without it
  hash     both clients played one game

Exit code 0 on PASS, 1 otherwise.
"""
import argparse
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hash_check  # noqa: E402
import player_trace  # noqa: E402
import replay as replay_tool  # noqa: E402
import view_check  # noqa: E402

START = hash_check.level_start("solar")
WINDOW = 400  # level frames watched, from when player 1 flies (it does by frame 100); Solar starts both at 255 shields
FALL = 60  # the heat costs 50 to 400 in 400 frames by height (player 0 at 350 loses 100); a stray hit costs 20, so a player the heat spares stays under this
TIMEOUT = 1500


def main():
    p = argparse.ArgumentParser(description="Solar heat drain for two players")
    p.add_argument("--exe", help="Starship executable (default: newest build found)")
    p.add_argument("--timeout", type=int, default=TIMEOUT, help="per run: give up after this many seconds")
    args = p.parse_args()
    exe = replay_tool.find_exe(args.exe)

    frames = START.frame(WINDOW + 150)
    replay = replay_tool.ROOT / "local" / "test-solar-heat.sfcr"
    player_trace.write_rail_replay(replay, frames, None, start=START)
    config = {"CVars": {"gCoopDebugPlayerCount": 2, "gInvincible": 0}}
    with ThreadPoolExecutor(max_workers=2) as pool:
        jobs = [pool.submit(hash_check.play, replay, exe, args.timeout, f"solar-{index}", config=config, start=START,
                            env_extra={"SF64_COOP_TEST_LOCAL_INDEX": str(index),
                                       "SF64_COOP_VIEW_TRACE": "view-trace.txt"}) for index in (0, 1)]
        results = [job.result() for job in jobs]
    if any(values is None or rows is None for values, _, rows in results):
        print("FAIL: a run wrote no report or hash log")
        return 1
    failures = [f"client {i} did not play through: {values.get('result')}"
                for i, (values, _, _) in enumerate(results) if not hash_check.played_through(values)]
    clients = [view_check.read_trace(i, Path(values["log"]).parent / "view-trace.txt")
               for i, (values, _, _) in enumerate(results)]

    for client in clients:
        # The trace counts level frames and starts over if the level restarts: keep the first run.
        first_run = []
        for r in client.rows:
            if len(first_run) > 10 and r.frame < first_run[-1].frame:
                break
            first_run.append(r)
        window = [r for r in first_run if r.frame <= WINDOW and len(r.players) > 1]
        joined = [r for r in window if not r.players[1].parked and r.players[1].state == 3]
        ok = bool(joined) and joined[-1].frame >= WINDOW - 10
        print(f"solar: client {client.index}: joined: {'ok' if ok else 'FAIL'}: player 1 flies on {len(joined)} of "
              f"{len(window)} frames of the window")
        if not ok:
            failures.append(f"client {client.index}: joined")
            continue
        first, last = joined[0], joined[-1]
        for i in (0, 1):
            lost = first.players[i].shields - last.players[i].shields
            good = lost >= FALL
            print(f"solar: client {client.index}: fall-{i}: {'ok' if good else 'FAIL'}: player {i} shields "
                  f"{first.players[i].shields} -> {last.players[i].shields} (frames {first.frame}-{last.frame})")
            if not good:
                failures.append(f"client {client.index}: fall-{i}")

    (_, names, a_rows), (_, _, b_rows) = results
    diff = hash_check.first_difference(a_rows, b_rows, frames)
    if diff is not None:
        print(f"solar: hash: FAIL: {hash_check.describe(names, diff)}")
        failures.append("hash")
    else:
        print(f"solar: hash: ok: equal on all {min(frames, len(a_rows), len(b_rows))} frames")
    for f in failures:
        print(f"FAIL: {f}")
    print("solar: FAIL" if failures else "solar: PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
