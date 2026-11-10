#!/usr/bin/env python3
"""Lag and loss with real game clients (WP-27, #25): a pilot-flown level through the chaos relay, under each profile
and a few seeds, with numbers.

    python tools/coop/chaos_run.py                                  # every profile of chaos/profiles.json, 3 seeds
    python tools/coop/chaos_run.py --profiles bad_wifi,hell --seeds 7,8
    python tools/coop/chaos_run.py --profiles hell --seeds 1 --frames 12000   # all of Corneria
    python tools/coop/chaos_run.py --lock <file>                    # an flock on <file> for each run (Linux)
    python tools/coop/chaos_run.py --outages 5,9,12                 # the link down that long, one run each

Each run is one session: a host and a client through the join driver, the client's datagrams through
tools/coop/chaos/relay.py with one profile and one seed, both ways, and a pilot on each client
(SF64_COOP_LOCKSTEP_PILOT) flying Corneria from its intro for --frames lockstep frames. The games run in real time
(--speed 1): a delay is milliseconds, and a game eight times faster than real time would wait eight times as many
frames for it. A seed fixes every choice the
relay makes, so a run that broke repeats with the same profile and seed.

Lockstep stamps every input with its frame, so the flight is the same at any lag: the network can only make a game wait
(hold a frame) or end the session. A run passes when both games reach the last frame with equal hash and input logs
and no desync report, and when its hash log equals the first passing run's on every frame. Per run it reports: the
frames held and the time held in all (`done` in the driver's report), the longest single hold (`frame N held X ms` in
the co-op log, written from 200 ms on), resends and ping (the co-op log's net lines, every 5 s, src/port/coop/session/
session.c), what the relay dropped, delayed and duplicated (its own counters), and the wall time. A session the
lockstep drops (10 s with no input from a player, COOP_LOCKSTEP_STALL_MS) or the transport drops (30 s, the join
driver's timeout) is a result: the run says at which frame, and the profile and seed repeat it. --outages takes the
link down both ways for that many seconds, once, from lockstep frame OUTAGE_FRAME on (the relay's `disconnect`), to
find where a session stops waiting and ends.

What the numbers are not: one PC, loopback, both games on the same cores, the relay in Python on the same PC. No real
WAN, no second machine, no Steam relay. Exit code 0 when every run passed.
"""
import argparse
import json
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent / "chaos"))
import join_test  # noqa: E402
import lockstep_test  # noqa: E402
import replay as replay_tool  # noqa: E402
from impair import load_profiles  # noqa: E402
from relay import ChaosClient  # noqa: E402

FRAMES = 6000  # Corneria's intro, its rails and into the all-range: both players fly from about frame 1700
OUTAGE_FRAME = 3000  # --outages: the link goes down here, on Corneria's rails with both players flying
SEEDS = (1, 2, 3)
HELD = re.compile(r"frame (\d+) held (\d+) ms for player (\d)")
NET = re.compile(r"ping (\d+) ms \((\d+) to (\d+)\), in \d+s: sent (\d+), received (\d+), resent (\d+), duplicates (\d+)")


def lock(path):
    """An flock on path, held until the returned file is closed; None where there is no fcntl."""
    if path is None:
        return None
    try:
        import fcntl
    except ImportError:
        print("chaos_run: --lock needs fcntl, which this system lacks; running without it")
        return None
    handle = open(path, "a")
    fcntl.flock(handle, fcntl.LOCK_EX)
    return handle


def start_relay(folder, target, profile, seed):
    info = folder / "relay.json"
    proc = subprocess.Popen([sys.executable, "-I", str(replay_tool.ROOT / "tools" / "coop" / "chaos" / "chaos.py"),
                             "relay", "--target", f"127.0.0.1:{target}", "--profile", profile, "--seed", str(seed),
                             "--info-file", str(info)], stdout=subprocess.DEVNULL)
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        if info.exists() and info.read_text(encoding="utf-8"):
            return proc, json.loads(info.read_text(encoding="utf-8"))
        time.sleep(0.05)
    proc.kill()
    raise lockstep_test.Fail("the relay did not start")


def own_log(game):
    """The co-op log lines this game wrote itself (src/port/coop/log/coop_log.h): its own file is the one named after
    its player."""
    player = 1 if game.name == "host" else 2
    lines = []
    for path in sorted(game.run.glob(f"coop-log-*-player{player}.txt")):
        lines += path.read_text(encoding="utf-8", errors="replace").splitlines()
    return lines


def log_numbers(game):
    """(longest hold in ms and its frame, holds logged, resends, duplicates, pings) from a game's own co-op log."""
    longest = (0, None)
    holds = resent = duplicates = 0
    pings = []
    for line in own_log(game):
        m = HELD.search(line)
        if m:
            holds += 1
            if int(m.group(2)) > longest[0]:
                longest = (int(m.group(2)), int(m.group(1)))
        m = NET.search(line)
        if m:
            pings.append(int(m.group(1)))
            resent += int(m.group(6))
            duplicates += int(m.group(7))
    return longest, holds, resent, duplicates, pings


def one_run(args, folder, profile, seed, outage=None):
    """One session through the relay; with outage, the relay drops everything both ways for that many seconds from
    lockstep frame OUTAGE_FRAME on (its `disconnect`). Returns a dict for the summary."""
    folder.mkdir(parents=True, exist_ok=True)
    env = {"SF64_COOP_LOCKSTEP_PILOT": "1", "SF64_COOP_PILOT_LOG": "pilot-log.txt",
           "SF64_COOP_LOCKSTEP_END": str(args.frames), "SF64_COOP_TEST_SPEED": args.speed}
    relays = []

    def to_relay(port):
        proc, info = start_relay(folder, port, profile, seed)
        relays.append((proc, info))
        return info["listen"]

    row = {"profile": profile, "seed": seed, "folder": str(folder), "outage_s": outage}
    began = time.monotonic()
    host, client = lockstep_test.session(args, folder, both=env, client_port=to_relay,
                                         replay_frames=args.frames + 20000)
    try:
        while True:
            games = (host, client)
            if outage is not None and "outage_frame" not in row and relays and host.sync_log.exists():
                frame = len(host.sync_log.read_text(encoding="utf-8", errors="replace").splitlines())
                if frame >= OUTAGE_FRAME:
                    rpc = ChaosClient(relays[0][1]["rpc"])
                    rpc.call("disconnect", duration_ms=outage * 1000)
                    rpc.close()
                    row["outage_frame"] = frame
            if all(join_test.said(game.sync(), "done") for game in games):
                row["ended"] = "done"
                break
            over = [game.name for game in games if join_test.said(game.sync(), "over")]
            if over:
                time.sleep(5)  # the other game may still be on its way to `over`
                row["ended"] = "session ended: " + ", ".join(
                    f"{game.name} at frame {join_test.said(game.sync(), 'over')[0]['frame']}"
                    for game in games if join_test.said(game.sync(), "over"))
                break
            ended = [game for game in games if game.proc.poll() is not None]
            if ended:
                row["ended"] = ", ".join(f"{game.name} ended with code {game.proc.returncode}" for game in ended)
                break
            if time.monotonic() - began > args.run_seconds:
                row["ended"] = f"timeout after {args.run_seconds:.0f} s"
                break
            time.sleep(2)
        if relays:
            try:
                rpc = ChaosClient(relays[0][1]["rpc"])
                row["relay"] = rpc.call("stats")
                rpc.close()
            except (OSError, RuntimeError, ValueError) as error:
                row["relay"] = {"error": str(error)}
    finally:
        client.close()
        host.close()
        for proc, _ in relays:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
    row["seconds"] = round(time.monotonic() - began)
    for game in (host, client):
        done = join_test.said(game.sync(), "done")
        longest, holds, resent, duplicates, pings = log_numbers(game)
        row["host" if game.name == "host" else "client"] = {
            "held_frames": int(done[0]["held"]) if done else None,
            "held_ms": int(done[0]["held_ms"]) if done else None,
            "longest_hold_ms": longest[0], "longest_hold_frame": longest[1], "holds_logged": holds,
            "resent": resent, "duplicates": duplicates,
            "ping_median": statistics.median(pings) if pings else None, "ping_max": max(pings) if pings else None,
        }
    try:
        n = args.frames if row["ended"] == "done" else None
        row["frames"] = lockstep_test.compare(host, client, n)
        row["equal"] = True
    except (lockstep_test.Fail, join_test.Fail) as failure:
        row["equal"] = False
        row["difference"] = str(failure)
    row["host_rows"] = host
    return row


def relay_text(stats):
    """Dropped, duplicated and seen per direction, from the relay's stats."""
    if not isinstance(stats, dict) or "error" in stats:
        return "relay stats: none"
    parts = []
    for direction in ("c2s", "s2c"):
        st = stats.get(direction) or stats.get("stats", {}).get(direction)
        if isinstance(st, dict):
            parts.append(f"{direction} {st.get('dropped', 0)}/{st.get('seen', 0)} dropped, "
                         f"{st.get('duplicated', 0)} duplicated")
    return "; ".join(parts) or f"relay stats: {json.dumps(stats)[:120]}"


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--profiles", help="chaos relay profiles, comma-separated (default: all of chaos/profiles.json)")
    p.add_argument("--seeds", default=",".join(str(s) for s in SEEDS), help="relay seeds per profile (default 1,2,3)")
    p.add_argument("--frames", type=int, default=FRAMES, help=f"lockstep frames per run (default {FRAMES})")
    p.add_argument("--speed", default="1", help="the games' SF64_COOP_TEST_SPEED: 1 is real time (default), as a delay "
                   "in ms means more frames at a faster game; 8 is what other session tests use")
    p.add_argument("--run-seconds", type=float, default=3600, help="give up on a run after this long (default 3600)")
    p.add_argument("--outages", help="seconds, comma-separated: in place of the profile sweep, one run per value with "
                   f"the link down both ways that long from lockstep frame {OUTAGE_FRAME} on (first profile, first seed)")
    p.add_argument("--lock", help="hold an flock on this file for each run (Linux)")
    p.add_argument("--exe", help="Starship executable (default: newest build found)")
    p.add_argument("--out", help="folder for the logs (default: a new one under local/)")
    args = p.parse_args()
    profiles = args.profiles.split(",") if args.profiles else list(load_profiles())
    unknown = [name for name in profiles if name not in load_profiles()]
    if unknown:
        sys.exit(f"chaos_run: no profile {', '.join(unknown)}; the choices are {', '.join(load_profiles())}")
    seeds = [int(s) for s in args.seeds.split(",")]
    args.exe = replay_tool.find_exe(args.exe)
    root = Path(args.out).resolve() if args.out else replay_tool.ROOT / "local" / f"test-chaos-{time.strftime('%Y%m%d-%H%M%S')}"
    print(f"chaos_run: {len(profiles)} profiles x {len(seeds)} seeds, {args.frames} frames of Corneria each, "
          f"speed {args.speed}; logs {root}", flush=True)
    rows = []
    reference = None
    runs = [(profile, seed, None) for profile in profiles for seed in seeds]
    if args.outages:
        runs = [(profiles[0], seeds[0], float(s)) for s in args.outages.split(",")]
    for profile, seed, outage in runs:
        held = lock(args.lock)
        try:
            name = f"{profile}-{seed}" if outage is None else f"{profile}-{seed}-outage-{outage:g}s"
            row = one_run(args, root / name, profile, seed, outage)
        except (lockstep_test.Fail, join_test.Fail, OSError) as failure:
            row = {"profile": profile, "seed": seed, "ended": f"no session: {failure}", "equal": False}
        finally:
            if held is not None:
                held.close()
        host = row.pop("host_rows", None)
        if host is not None and row.get("equal") and row.get("ended") == "done":
            names, _, ours = host.rows()
            if reference is None:
                reference = (f"{profile} seed {seed}", names, ours[:args.frames])
            else:
                difference = join_test.first_difference(names, ours[:args.frames], reference[2])
                row["same_as_first"] = difference is None
                if difference is not None:
                    row["equal"] = False
                    row["difference"] = f"another flight than {reference[0]}: {difference}"
        rows.append(row)
        h, c = row.get("host", {}), row.get("client", {})
        down = f", link down {outage:g} s from frame {row.get('outage_frame')}" if outage is not None else ""
        print(f"chaos_run: {profile} seed {seed}{down}: {row['ended']}, "
              f"{'equal' if row.get('equal') else 'NOT equal: ' + str(row.get('difference'))} on "
              f"{row.get('frames', 0)} frames; held {h.get('held_frames')} frames {h.get('held_ms')} ms (host), "
              f"{c.get('held_frames')} frames {c.get('held_ms')} ms (client); longest hold "
              f"{max(h.get('longest_hold_ms', 0), c.get('longest_hold_ms', 0))} ms; resent {h.get('resent')} "
              f"(host), {c.get('resent')} (client); ping median {c.get('ping_median')} max {c.get('ping_max')} ms; "
              f"{relay_text(row.get('relay'))}; {row.get('seconds')} s", flush=True)
        root.mkdir(parents=True, exist_ok=True)
        (root / "summary.json").write_text(json.dumps(rows, indent=1, default=str), encoding="utf-8")
    passed = sum(1 for row in rows if row.get("equal") and row.get("ended") == "done")
    print(f"chaos_run: {passed}/{len(rows)} runs reached frame {args.frames} with equal hashes; logs {root}")
    return 0 if passed == len(rows) else 1


if __name__ == "__main__":
    sys.exit(main())
