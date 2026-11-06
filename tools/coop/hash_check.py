#!/usr/bin/env python3
"""State hash checks for Starship Co-op (WP-04).

    python tools/coop/hash_check.py run  local/replays/corneria.sfcr   # unattended; prints PASS or FAIL
    python tools/coop/hash_check.py run  --players 2 --poke-player 1   # same, co-op, input by rule, poke in player 2
    python tools/coop/hash_check.py diff a/state-hash.txt b/state-hash.txt
    python tools/coop/hash_check.py probe --players 2                  # unattended; prints PASS or FAIL
    python tools/coop/hash_check.py views                              # unattended; prints PASS or FAIL

run plays the replay three times at once, each in a fresh folder under local/ (see tools/coop/replay.py):
  A and B  plain playback; their per-frame state hashes must be equal on every recorded frame.
  C        playback with SF64_COOP_HASH_POKE set: one bit of game state is flipped at one frame; the first frame
           whose hash differs from A must be exactly that frame. --poke-player flips a bit of that player, not of an
           actor. Without a replay file, input by rule is played (write_generated_replay), so no recording is needed.
diff compares every column of two hash logs and prints the first frame where they differ and which regions differ
  there. It compares the common frames, so read the two lengths it prints first.
probe plays one game with SF64_COOP_HASH_PROBE: every --period frames the game changes single values, hashes, changes
  them back and logs what the hash did (state_hash.c, Probe). See PROBE_RULES for what must and must not show. Both
  hashes of a probe come from the same frame of the same process, so this needs no second run to agree with: it says
  whether the hash can see a value, not whether two runs match. Without a replay file, as for run.
views plays one replay twice per section, with SF64_COOP_TEST_LOCAL_INDEX 0 and 1: two clients of one session, who look
  through different players and must still hash alike. Input by rule flies every player (player_trace.py, rail_pad), and
  --section picks Corneria's rails, its all-range section or both. Prints the first differing frame and regions.

Exit code 0 on PASS (or no difference for diff), 1 otherwise.
"""
import argparse
import os
import struct
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import player_trace  # noqa: E402
import replay as replay_tool  # noqa: E402


# Buttons as OSContPad.button holds them.
BUTTON_A = 0x8000
BUTTON_L = 0x0020

# Input by rule: A every 30 frames gets through the menus into Corneria (the level starts about frame 3100) and keeps
# firing there, L jumps to all-range with CVar gDebugJumpToAllRange, and co-op players join a little later.
GENERATED_L_FRAME = 4840
GENERATED_POKE_FRAME = 5400  # both players flying in all-range
GENERATED_FRAMES = 5800
GENERATED_SEEDS = (1234, 5678, 9012, 111, 222, 333)

# What a probe must show: target -> (hash changes?, the only region that may change).
PROBE_RULES = {
    "player": (True, "players"),
    "iwork-negative": (True, "actors"),
    "iwork-foreign": (False, None),
    "scenery360-active": (True, "scenery360"),
    "scenery360-free": (False, None),
}
# With co-op off the hash stays as it was, so a single-player log lines up with one from an older build.
PROBE_RULES_COOP_OFF = {
    "iwork-negative": (False, None),
    "scenery360-free": (True, "scenery360"),
}
# Values with one slot per player, probed as name[i] for all four slots. In co-op a slot counts only while its player is
# in the level: a probe says parked=1 for the others, and those must leave the hash alone.
PLAYER_ARRAYS = (  # state_hash.c, sPlayerArrays: region players, in co-op only
    "gLeftWingHealth", "gRightWingHealth", "gLaserStrength", "gChargeTimers", "gUturnDownTimers", "gUturnBrakeTimers",
    "gLoopDownTimers", "gLoopBoostTimers", "gLeftWingDebrisTimer", "gRightWingDebrisTimer", "gHasShield",
    "gShieldTimer", "gShieldAlpha",
)
PLAYER_GLOBALS = ("gLifeCount", "gBombCount", "gPlayerScores")  # HashGlobals: region globals, with co-op off too
PLAYER_SLOTS = 4
# state_hash.c, sTeamArrays: one value per team member, region globals, in co-op only. name -> (slots, first slot read,
# slot after the last one read); the slots nobody reads must leave the hash alone.
TEAM_ARRAYS = {
    "gTeamDamage": (6, 1, 4),
    "gSavedTeamShields": (6, 1, 6),
    "gPrevPlanetTeamShields": (6, 1, 6),
    "D_ctx_80177C58": (6, 1, 6),
    "gTeamEventActorIndex": (4, 0, 4),
    "D_enmy_Timer_80161670": (4, 0, 4),
}


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
    """Return (index, row a, row b) of the first row that differs in any column, or None. Compares up to the shorter
    log (or limit)."""
    n = min(len(a_rows), len(b_rows))
    if limit is not None:
        n = min(n, limit)
    for i in range(n):
        a, b = a_rows[i], b_rows[i]
        if a != b:
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
        print(f"hash: no difference on the {min(len(a_rows), len(b_rows))} common frames: state, level, total and "
              f"{len(names)} regions")
        return 0
    print(f"hash: first difference at {describe(names, diff)}")
    return 1


def write_generated_replay(path, frames=GENERATED_FRAMES, l_frame=GENERATED_L_FRAME):
    """Write a replay from the rule above. It holds no checkpoints, so playback reports FAIL_MISMATCH at frame 0 and
    plays on; the layout is ReplayHeader and ReplayFrame in src/port/coop/test/replay.c."""
    data = bytearray(b"SFCR" + struct.pack("<III6i", 2, 20, 204, *GENERATED_SEEDS))
    idle = bytes(20)
    for frame in range(frames):
        button = BUTTON_A if frame >= 60 and frame % 30 == 0 else 0
        if frame == l_frame:
            button |= BUTTON_L
        pads = struct.pack("<H18x", button) + idle * 3
        data += struct.pack("<I", frame) + pads + pads + bytes(40)
    path.parent.mkdir(parents=True, exist_ok=True)
    # Other test runs may be reading the file, so it appears complete or not at all.
    temp = path.with_name(f"{path.name}.{os.getpid()}")
    temp.write_bytes(data)
    temp.replace(path)


def play(replay, exe, timeout, tag, poke=None, env_extra=None, config=None):
    values, run, elapsed = replay_tool.run_game("play", replay, exe, timeout, hash_log=True, poke=poke, tag=tag,
                                                env_extra=env_extra, config=config)
    log = run / "state-hash.txt"
    result = values.get("result") if values else "NO_REPORT"
    print(f"hash: run {tag}: replay {result}, {elapsed:.0f}s, log {log}")
    if values:
        values["log"] = log
    if not log.exists():
        return values, None, None
    names, rows = read_log(log)
    return values, names, rows


def test_setup(args):
    """Return (replay, config) for a run: the given file, or input by rule with what that needs."""
    cvars = {}
    if args.players != 1:
        cvars["gCoopDebugPlayerCount"] = args.players
    if args.file:
        replay = Path(args.file).resolve()
        if not replay.exists():
            sys.exit(f"hash: {replay} does not exist")
    else:
        replay = replay_tool.ROOT / "local" / "test-hash-generated.sfcr"
        write_generated_replay(replay)
        cvars["gDebugJumpToAllRange"] = 1
    return replay, {"CVars": cvars} if cvars else None


def played_through(values):
    """Input by rule has no checkpoints and extra players leave a recording on purpose, so a mismatch says nothing in
    those runs; they only have to reach the end of the replay."""
    return bool(values) and values.get("result") in ("PASS", "FAIL_MISMATCH")


def cmd_run(args):
    exe = replay_tool.find_exe(args.exe)
    replay, config = test_setup(args)
    lenient = args.file is None or args.players != 1
    failures = []

    poke = args.poke_frame
    if poke is None and args.file is None:
        poke = GENERATED_POKE_FRAME
    if poke is None:
        # Default: the middle of the replay. Any frame works, because the poked bytes (gActors) are hashed in every
        # game state.
        poke = replay_tool.replay_frame_count(replay) // 2
    poke_env = None if args.poke_player is None else {"SF64_COOP_HASH_POKE_PLAYER": str(args.poke_player)}
    print(f"hash: runs A, B and C (poke at frame {poke}) in parallel")

    # The three runs are independent (own folders, same replay), so they run at the same time: the game runs at a
    # fixed 30 logic frames per second, and running them in a row would take three times as long.
    with ThreadPoolExecutor(max_workers=3) as pool:
        a_job = pool.submit(play, replay, exe, args.timeout, "hash-a", config=config)
        b_job = pool.submit(play, replay, exe, args.timeout, "hash-b", config=config)
        c_job = pool.submit(play, replay, exe, args.timeout, "hash-c", poke, env_extra=poke_env, config=config)
        a_values, names, a_rows = a_job.result()
        b_values, _, b_rows = b_job.result()
        c_values, _, c_rows = c_job.result()

    if a_rows is None or b_rows is None:
        print("hash: FAIL (no hash log written)")
        return 1
    for tag, values in (("A", a_values), ("B", b_values)):
        if not (played_through(values) if lenient else values and values.get("result") == "PASS"):
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


def read_probes(path):
    """Return the "# probe" lines of a hash log as dicts."""
    probes = []
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("# probe "):
            probes.append(dict(kv.split("=", 1) for kv in line.split()[2:]))
    return probes


def probe_rule(target, parked, players):
    name, _, index = target.rstrip("]").partition("[")
    coop = players > 1
    if name in PLAYER_ARRAYS:
        return (True, "players") if coop and parked == "0" else (False, None)
    if name in PLAYER_GLOBALS:
        return (True, "globals") if parked == "0" else (False, None)
    if name in TEAM_ARRAYS:
        _, first, end = TEAM_ARRAYS[name]
        return (True, "globals") if coop and first <= int(index) < end else (False, None)
    if name == "slot":
        # A player who is in the level or not: the values above count or stop counting with it.
        return (True, "players,globals") if coop and int(index) < players else (False, None)
    if not coop and target in PROBE_RULES_COOP_OFF:
        return PROBE_RULES_COOP_OFF[target]
    return PROBE_RULES["player" if target.startswith("player") else target]


def region_allowed(seen, allowed):
    """The first allowed region must be there and nothing but the allowed ones."""
    allowed = allowed.split(",")
    seen = seen.split(",")
    return allowed[0] in seen and set(seen) <= set(allowed)


def check_probes(probes, players):
    """Return a list of failures. Prints one line per target and state."""
    failures = []
    groups = {}
    for probe in probes:
        groups.setdefault((probe["target"], probe["parked"]), []).append(probe)

    for (target, parked), group in sorted(groups.items()):
        must_change, region = probe_rule(target, parked, players)
        bad = []
        for probe in group:
            changed = probe["changed"] == "1"
            wrong_region = changed and must_change and not region_allowed(probe["regions"], region)
            if changed != must_change or wrong_region:
                bad.append(probe)
        state = " parked" if parked == "1" else ""
        print(f"hash: probe {target}{state}: {len(group) - len(bad)}/{len(group)} as expected")
        if bad:
            first = bad[0]
            want = f"changes only {region}" if must_change else "leaves the hash alone"
            failures.append(f"{target}{state} at frame {first['frame']}: changed={first['changed']} "
                            f"regions={first['regions']}, but it {want} ({len(bad)} of {len(group)} probes)")

    # A check that never ran proves nothing.
    wanted = [("iwork-negative", "0"), ("iwork-foreign", "0"), ("scenery360-active", "0"), ("scenery360-free", "0"),
              ("player0", "0")]
    for i in range(1, players):
        wanted += [(f"player{i}", "1"), (f"player{i}", "0")]
    for i in range(PLAYER_SLOTS):
        states = ["0"] if players == 1 or i == 0 else ["0", "1"] if i < players else ["1"]
        wanted += [(f"{name}[{i}]", state) for name in PLAYER_ARRAYS + PLAYER_GLOBALS + ("slot",) for state in states]
    wanted += [(f"{name}[{i}]", "0") for name, (slots, _, _) in TEAM_ARRAYS.items() for i in range(slots)]
    for target, parked in wanted:
        if (target, parked) not in groups:
            failures.append(f"no probe of {target}{' parked' if parked == '1' else ''} ran")
    extra = [t for t, _ in groups if t.startswith("player") and int(t[6:]) >= players]
    if extra:
        failures.append(f"probes of {', '.join(sorted(set(extra)))}, but the run has {players} player(s)")
    return failures


def cmd_probe(args):
    exe = replay_tool.find_exe(args.exe)
    replay, config = test_setup(args)
    print(f"hash: probe run, {args.players} player(s), every {args.period} frames")
    values, run, elapsed = replay_tool.run_game("play", replay, exe, args.timeout, hash_log=True, tag="hash-probe",
                                                env_extra={"SF64_COOP_HASH_PROBE": str(args.period)}, config=config)
    log = run / "state-hash.txt"
    print(f"hash: replay {values.get('result') if values else 'NO_REPORT'}, {elapsed:.0f}s, log {log}")
    if not played_through(values) or not log.exists():
        print("hash: FAIL (the replay did not play through)")
        return 1
    failures = check_probes(read_probes(log), args.players)
    for f in failures:
        print(f"FAIL: {f}")
    print("hash: FAIL" if failures else "hash: PASS")
    return 1 if failures else 0


VIEW_SECTIONS = {  # name -> (frames, frame of the L press into all-range)
    "rails": (player_trace.RAIL_FRAMES, None),
    "all-range": (8000, GENERATED_L_FRAME),
}


def read_views(path):
    """The camera a co-op run drew, from the "# view" lines of its log: frame -> (player looked through, eye)."""
    views = {}
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("# view "):
            parts = line.split()
            views[int(parts[2].split("=")[1])] = (int(parts[3].split("=")[1]), parts[4:])
    return views


def check_views_differ(section, a_values, b_values):
    """Equal hashes only mean something if the two runs drew different cameras. Returns a list of failures."""
    if not a_values or not b_values:
        return []
    a_views, b_views = read_views(a_values["log"]), read_views(b_values["log"])
    common = sorted(set(a_views) & set(b_views))
    apart = [frame for frame in common if a_views[frame][1] != b_views[frame][1]]
    print(f"hash: {section}: the cameras drawn differ on {len(apart)} of {len(common)} sampled frames")
    failures = []
    if not apart:
        failures.append(f"{section}: both runs drew the same camera, so equal hashes say nothing")
    if not any(b_views[frame][0] == 1 for frame in common):
        failures.append(f"{section}: the run with index 1 never looked through player 1")
    return failures


def view_section(section, exe, args):
    """Play one section with each local index. Returns a list of failures."""
    frames, l_frame = VIEW_SECTIONS[section]
    frames = args.frames or frames
    replay = replay_tool.ROOT / "local" / f"test-hash-views-{section}.sfcr"
    player_trace.write_rail_replay(replay, frames, l_frame)
    # Flying by rule, player 0 would not live through the section, as in player_trace.py rails.
    cvars = {"gCoopDebugPlayerCount": args.players, "gInvincible": 1}
    if l_frame is not None:
        cvars["gDebugJumpToAllRange"] = 1
    config = {"CVars": cvars}

    with ThreadPoolExecutor(max_workers=2) as pool:
        jobs = [pool.submit(play, replay, exe, args.timeout, f"view-{section}-{index}", config=config,
                            env_extra={"SF64_COOP_TEST_LOCAL_INDEX": str(index)}) for index in (0, 1)]
        (a_values, names, a_rows), (b_values, _, b_rows) = [job.result() for job in jobs]

    if a_rows is None or b_rows is None:
        return [f"{section}: no hash log written"]
    failures = [f"{section}: index {index} did not play through: {values.get('result') if values else 'no report'}"
                for index, values in ((0, a_values), (1, b_values)) if not played_through(values)]
    if min(len(a_rows), len(b_rows)) < frames:
        failures.append(f"{section}: hash logs shorter than the replay ({len(a_rows)}, {len(b_rows)} < {frames} frames)")
    failures += check_views_differ(section, a_values, b_values)
    diff = first_difference(a_rows, b_rows, frames)
    if diff is not None:
        failures.append(f"{section}: index 0 and 1 differ: {describe(names, diff)}")
    else:
        print(f"hash: {section}: index 0 and 1 equal on all {min(frames, len(a_rows), len(b_rows))} frames")
    return failures


def cmd_views(args):
    exe = replay_tool.find_exe(args.exe)
    failures = []
    for section in VIEW_SECTIONS if args.section == "both" else (args.section,):
        failures += view_section(section, exe, args)
    for f in failures:
        print(f"FAIL: {f}")
    print("hash: FAIL" if failures else "hash: PASS")
    return 1 if failures else 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run", help="play a replay three times and check the hashes")
    r.add_argument("file", nargs="?", help="replay file (from tools/coop/replay.py record; default: input by rule)")
    r.add_argument("--players", type=int, default=1, help="players, through gCoopDebugPlayerCount (default 1)")
    r.add_argument("--exe", help="Starship executable (default: newest build found)")
    r.add_argument("--timeout", type=float, default=1800, help="per run: give up after this many seconds")
    r.add_argument("--poke-frame", type=int, help="frame for run C (default: middle of the replay)")
    r.add_argument("--poke-player", type=int, help="run C flips a bit of this player (0 is the first), not of an actor")
    d = sub.add_parser("diff", help="compare two hash logs")
    d.add_argument("a")
    d.add_argument("b")
    b = sub.add_parser("probe", help="play once and check what the hash can and cannot see")
    b.add_argument("file", nargs="?", help="replay file (default: input by rule, into all-range)")
    b.add_argument("--players", type=int, default=2, help="players, through gCoopDebugPlayerCount (default 2)")
    b.add_argument("--period", type=int, default=200, help="frames between probes (default 200)")
    b.add_argument("--exe", help="Starship executable (default: newest build found)")
    b.add_argument("--timeout", type=float, default=900, help="give up after this many seconds")
    v = sub.add_parser("views", help="play one replay with local index 0 and 1 and compare the hashes")
    v.add_argument("--section", choices=[*VIEW_SECTIONS, "both"], default="both", help="default: both")
    v.add_argument("--players", type=int, default=2, help="players, through gCoopDebugPlayerCount (default 2)")
    v.add_argument("--frames", type=int, help="replay length (default: what the section needs)")
    v.add_argument("--exe", help="Starship executable (default: newest build found)")
    v.add_argument("--timeout", type=float, default=1800, help="per run: give up after this many seconds")
    args = p.parse_args()
    return {"run": cmd_run, "diff": cmd_diff, "probe": cmd_probe, "views": cmd_views}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
