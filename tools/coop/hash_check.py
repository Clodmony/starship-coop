#!/usr/bin/env python3
"""State hash checks for Starship Co-op (WP-04).

    python tools/coop/hash_check.py run  local/replays/corneria.sfcr   # unattended; prints PASS or FAIL
    python tools/coop/hash_check.py diff a/state-hash.txt b/state-hash.txt

run plays the replay three times at once, each in a fresh folder under local/ (see tools/coop/replay.py):
  A and B  plain playback; their per-frame state hashes must be equal on every recorded frame.
  C        playback with SF64_COOP_HASH_POKE set: one bit of game state is flipped at one frame; the first frame
           whose hash differs from A must be exactly that frame.
diff prints the first frame where two hash logs differ and which regions differ there.

Exit code 0 on PASS (or no difference for diff), 1 otherwise.
"""
import argparse
import struct
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import replay as replay_tool  # noqa: E402


def read_log(path):
    """Return (region names, list of (frame, gamestate, level, total, [region hashes]))."""
    names = []
    rows = []
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("# frame "):
            names = line.split()[5:]
            continue
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        rows.append((int(parts[0]), int(parts[1]), int(parts[2]), parts[3], parts[4:]))
    return names, rows


def first_difference(a_rows, b_rows, limit=None):
    """Return (index, row a, row b) of the first differing frame, or None. Compares up to the shorter log (or limit)."""
    n = min(len(a_rows), len(b_rows))
    if limit is not None:
        n = min(n, limit)
    for i in range(n):
        a, b = a_rows[i], b_rows[i]
        if a[0] != b[0] or a[3] != b[3]:
            return i, a, b
    return None


def describe(names, diff):
    _, a, b = diff
    regions = [names[k] if k < len(names) else str(k) for k in range(len(a[4])) if a[4][k] != b[4][k]]
    return (f"frame {a[0]} (gamestate {a[1]}, level {a[2]}): total {a[3]} vs {b[3]}; "
            f"regions differing: {', '.join(regions) or 'none'}")


def cmd_diff(args):
    names, a_rows = read_log(args.a)
    _, b_rows = read_log(args.b)
    diff = first_difference(a_rows, b_rows)
    print(f"hash: {len(a_rows)} vs {len(b_rows)} frames")
    if diff is None:
        print("hash: no difference on the common frames")
        return 0
    print(f"hash: first difference at {describe(names, diff)}")
    return 1


def replay_frame_count(replay):
    """Frames in a replay file: a 40-byte header (src/port/coop/test/replay.c, ReplayHeader) holding the frame size
    at byte 12, then one record per frame."""
    data = Path(replay).read_bytes()
    frame_size = struct.unpack_from("<I", data, 12)[0]
    return (len(data) - 40) // frame_size if frame_size else 0


def play(replay, exe, timeout, tag, poke=None):
    values, run, elapsed = replay_tool.run_game("play", replay, exe, timeout, hash_log=True, poke=poke, tag=tag)
    log = run / "state-hash.txt"
    result = values.get("result") if values else "NO_REPORT"
    print(f"hash: run {tag}: replay {result}, {elapsed:.0f}s, log {log}")
    if not log.exists():
        return values, None, None
    names, rows = read_log(log)
    return values, names, rows


def cmd_run(args):
    exe = replay_tool.find_exe(args.exe)
    replay = Path(args.file).resolve()
    if not replay.exists():
        sys.exit(f"hash: {replay} does not exist")
    failures = []

    poke = args.poke_frame
    if poke is None:
        # Default: the middle of the replay. Any frame works, because the poked bytes (gActors) are hashed in every
        # game state.
        poke = replay_frame_count(replay) // 2
    print(f"hash: runs A, B and C (poke at frame {poke}) in parallel")

    # The three runs are independent (own folders, same replay), so they run at the same time: the game runs at a
    # fixed 30 logic frames per second, and running them in a row would take three times as long.
    with ThreadPoolExecutor(max_workers=3) as pool:
        a_job = pool.submit(play, replay, exe, args.timeout, "hash-a")
        b_job = pool.submit(play, replay, exe, args.timeout, "hash-b")
        c_job = pool.submit(play, replay, exe, args.timeout, "hash-c", poke)
        a_values, names, a_rows = a_job.result()
        b_values, _, b_rows = b_job.result()
        c_values, _, c_rows = c_job.result()

    if a_rows is None or b_rows is None:
        print("hash: FAIL (no hash log written)")
        return 1
    for tag, values in (("A", a_values), ("B", b_values)):
        if not values or values.get("result") != "PASS":
            failures.append(f"replay {tag} did not pass: {values.get('result') if values else 'no report'}")

    recorded = int(a_values.get("recorded_frames", len(a_rows))) if a_values else len(a_rows)
    if min(len(a_rows), len(b_rows)) < recorded:
        failures.append(f"hash logs shorter than the replay ({len(a_rows)}, {len(b_rows)} < {recorded} frames)")
    diff = first_difference(a_rows, b_rows, recorded)
    if diff is not None:
        failures.append(f"A and B differ: {describe(names, diff)}")
    else:
        print(f"hash: A and B equal on all {min(recorded, len(a_rows), len(b_rows))} recorded frames")

    if c_rows is None:
        failures.append("run C wrote no hash log")
    else:
        diff = first_difference(a_rows, c_rows, recorded)
        if diff is None:
            failures.append(f"poke at frame {poke} did not change any hash")
        elif diff[1][0] != poke:
            failures.append(f"poke at frame {poke}, but first difference at {describe(names, diff)}")
        else:
            print(f"hash: poke at frame {poke} detected: first difference at {describe(names, diff)}")
            print(f"hash: run C replay result {c_values.get('result') if c_values else 'no report'} (informational)")

    for f in failures:
        print(f"FAIL: {f}")
    print("hash: FAIL" if failures else "hash: PASS")
    return 1 if failures else 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run", help="play a replay three times and check the hashes")
    r.add_argument("file", help="replay file (from tools/coop/replay.py record)")
    r.add_argument("--exe", help="Starship executable (default: newest build found)")
    r.add_argument("--timeout", type=float, default=1800, help="per run: give up after this many seconds")
    r.add_argument("--poke-frame", type=int, help="frame for run C (default: middle of the replay)")
    d = sub.add_parser("diff", help="compare two hash logs")
    d.add_argument("a")
    d.add_argument("b")
    args = p.parse_args()
    return cmd_run(args) if args.cmd == "run" else cmd_diff(args)


if __name__ == "__main__":
    sys.exit(main())
