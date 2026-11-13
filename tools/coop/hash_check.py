#!/usr/bin/env python3
"""State hash checks for Starship Co-op (WP-04).

    python tools/coop/hash_check.py run  local/replays/corneria.sfcr   # unattended; prints PASS or FAIL
    python tools/coop/hash_check.py run  --players 2 --poke-player 1   # same, co-op, input by rule, poke in player 2
    python tools/coop/hash_check.py diff a/state-hash.txt b/state-hash.txt
    python tools/coop/hash_check.py probe --players 2                  # unattended; prints PASS or FAIL
    python tools/coop/hash_check.py views                              # unattended; prints PASS or FAIL
    python tools/coop/hash_check.py views --pilot --level meteo        # a level to its end; prints PASS or FAIL
    python tools/coop/hash_check.py slots --level katina               # unattended; prints PASS or FAIL
    python tools/coop/hash_check.py builds --debug a/Starship --release b/Starship   # unattended; prints PASS or FAIL
    python tools/coop/hash_check.py rates --exe b/Starship             # unattended; prints PASS or FAIL
    python tools/coop/hash_check.py draw                               # unattended; prints PASS or FAIL
    python tools/coop/hash_check.py base --base a/Starship             # unattended; prints PASS or FAIL
    python tools/coop/hash_check.py base --base a/Starship --level macbeth --frames 3000   # co-op off in Macbeth
    python tools/coop/hash_check.py start --players 2                  # unattended; prints PASS or FAIL

Input by rule starts where replay.py's Start says. run and views play it past Corneria's intro, probe through it, as a
parked player needs one, and builds and rates through the menus: frame interpolation first went wrong in the option menu
(#141), and these two are the only tools to run it in Release or at many rates. run --start and probe --start pick
another. A replay file plays from where it was made.

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
  --section picks Corneria's rails, its all-range section or both. Prints the first differing frame and regions. The
  client with index 1 has also ticked VIEW_SYS_FRAME_SKEW more frames since boot (--skew), as gSysFrameCount is not
  simulation state. --level flies other Arwing levels instead (VIEW_LEVELS, or all) and --cvar NAME=VALUE sets a CVar (#129).
  --pilot flies each level to its end first, boss included, with a pilot that reads game state, and the two clients play
  what it flew (PILOT_LEVELS, #181); venom-2-andross flies on through the game's jump into Andross' brain and escape
  (PILOT_THROUGH).
slots plays a level with 1 player and with --players and compares the effect slots in use: co-op frees nothing for
  leaving the view, so a kind of effect that only that frees would pile up (#129). Fails when co-op holds more effects
  that have lived 900 frames or more (the hash log's "# ages" lines) than single player, at its most, by more than
  SLOTS_LEAK (#284): an effect nothing ends grows old, a burst or a set that is renewed does not. --rule-test checks
  that rule on made-up samples, with no game.
builds plays one replay on two runs of a Release build and one of a Debug build of the same commit (point 3 of decision
  0002): the two Release runs must be equal on every frame, and so must Debug against Release. Input by rule, as for
  views. Give each build as its own --debug and --release: a build folder holds one build type.
rates plays one replay at several values of gInterpolationFPS, which no handshake compares, so two clients can differ in
  it. Every run must hash like the first. A rate is the number of draws a game frame gets, and vsync can clamp it, so
  vsync is off and each run must log the draws its rate asks for (state_hash.c, "# draws", counted at the draw loop); a
  run that did not is a failure, not a pass at a lower rate. Input by rule, as for views.
draw plays one replay with and without SF64_COOP_TEST_NO_DRAW, per section and start (#147): the draw code runs in both
  and the renderer only gets its work in one, so the hash must not change. The run with the switch must log 0 draws and
  the other run more, or the two would be the same run. Input by rule, as for views, from all three starts unless
  --start names one: the switch sits under every tool, on whichever start that tool plays. Run by hand, it deletes its
  folders under local/ when it passes and keeps them when it fails (#308); started by a run that notes its folders
  (test_all.py, replay_test.py, OWNED_ENV in run_folder.py), it leaves them to that run, which reads or deletes them.
base plays input by rule through the menus, with co-op off, on this build and on --base, the build before a change: the
  two must be equal on every frame, which is what co-op off means. --base can be a build without any of the switches
  here, so it plays nothing that needs one. --level NAME (#283) starts both games in that level instead, through its
  intro (SF64_COOP_TEST_START_LEVEL, src/port/coop/test/start_level.h, which --base must know), so co-op off can be
  checked outside Corneria, the vehicle levels too; --frames sets how long, counted from when player 0 flies.

builds and rates run in real time: at test speed a game frame is drawn once, so no game would interpolate, and
interpolation is what they are there to see (#141, #137). So they ask for drawing too, and each game must log what it
drew. --speed on rates is for replay_test.py, which checks that a game at speed fails it.
start is what a start in the level leaves different from coming through the menus (#148). It plays Corneria through the
menus, takes the seeds that game entered the level with, and plays the same input twice from those seeds, starting in
the level. The two starts in the level must be equal from their first frame to their last, and each must equal the game
from the menus in every region from level frame 1 on. With --players 1 region scenery360 is left out, and the tool says
how often it differed: see cmd_start. It also says if START_SEEDS in replay.py no longer are those seeds; that is not a
failure, as then only the tools' flights change. What the hash cannot see is in docs/coop/NOTES.md.

Exit code 0 on PASS (or no difference for diff), 1 otherwise.
"""
import argparse
import copy
import math
import os
import re
import struct
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import replay as replay_tool  # noqa: E402
from run_folder import OwnedFolders, make_run_folder  # noqa: E402


# Buttons as OSContPad.button holds them.
BUTTON_A = 0x8000
BUTTON_L = 0x0020

# Input by rule: replay.py's Start.fires on pad 0, L jumps to all-range with CVar gDebugJumpToAllRange, and co-op
# players join a little later. Frames here count from when player 0 flies, so they fit every Start.
GENERATED_L = 65
GENERATED_POKE = 625  # both players flying in all-range
GENERATED_END = 1025

WHOLE_PATH_FRAMES = 20  # a game started in a level shows a title screen for one frame
INTERPOLATED = 2  # draws of a game frame at real pacing, with 60 for gInterpolationFPS and a game of 30 frames a second

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
    "gShieldTimer", "gShieldAlpha", "gCoopTankState", "gCoopMarineState",  # the last two: #16, one vehicle set per player
)
PLAYER_GLOBALS = ("gLifeCount", "gBombCount", "gPlayerScores")  # HashGlobals: region globals, with co-op off too
# HashGlobals: one value, region globals, in co-op only (#129). The low-health radio timer: a drifted one would show only
# at the next limit that draws a number (#87).
COOP_GLOBALS = ("gTeamLowHealthMsgTimer", "voiceClock")
# voiceClock (#230, src/port/coop/sync/voice_clock.h): the voice sequence's tempo in the sound thread, which says on which
# frame the radio leaves a line. Probed in a level only, while the voice player runs.
# HashKeptTargets: whom each boss and all-range enemy follows and whom a boss holds (#213, aim.h), region globals, in
# co-op only. Probed as keptTarget[0] (the first boss's target) and keptTarget[last] (the last boss's held player).
KEPT_TARGETS = "keptTarget"
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


def write_generated_replay(path, frames=None, l_at=GENERATED_L, start=replay_tool.MENUS, seeds=None):
    """Write a replay from the rule above, for a game that starts at start: frames long, by default to GENERATED_END,
    with L at l_at, counted from when player 0 flies, or none. It holds no checkpoints, so playback reports
    FAIL_MISMATCH at frame 0 and plays on; the layout is ReplayHeader and ReplayFrame in src/port/coop/test/replay.c."""
    data = bytearray(b"SFCR" + struct.pack("<III6i", 2, 20, 204, *(seeds or start.seeds)))
    idle = bytes(20)
    for frame in range(start.frame(GENERATED_END) if frames is None else frames):
        flight = frame - start.active
        button = BUTTON_A if start.fires(flight) else 0
        if flight == l_at:
            button |= BUTTON_L
        pads = struct.pack("<H18x", button) + idle * 3
        data += struct.pack("<I", frame) + pads + pads + bytes(40)
    path.parent.mkdir(parents=True, exist_ok=True)
    # Other test runs may be reading the file, so it appears complete or not at all.
    temp = path.with_name(f"{path.name}.{os.getpid()}")
    temp.write_bytes(data)
    temp.replace(path)


def play(replay, exe, timeout, tag, poke=None, env_extra=None, config=None, start=None):
    values, run, elapsed = replay_tool.run_game("play", replay, exe, timeout, hash_log=True, poke=poke, tag=tag,
                                                env_extra=env_extra, config=config, start=start)
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
    """Return (replay, config, start) for a run: the given file, or input by rule with what that needs. start is
    args.start for input by rule; a file plays from where it was made."""
    cvars = {}
    start = None
    if args.players != 1:
        cvars["gCoopDebugPlayerCount"] = args.players
    if args.file:
        replay = Path(args.file).resolve()
        if not replay.exists():
            sys.exit(f"hash: {replay} does not exist")
    else:
        start = replay_tool.STARTS[args.start]
        # A folder of its own: the tools of two scenarios can run at once, on different starts.
        replay = make_run_folder(replay_tool.ROOT / "local", f"test-hash-{start.name}-") / "input.sfcr"
        write_generated_replay(replay, start=start)
        cvars["gDebugJumpToAllRange"] = 1
    return replay, {"CVars": cvars} if cvars else None, start


def whole_path_problem(states, through=(replay_tool.GS_TITLE,)):
    """What is wrong with a game that has to come the whole way from boot, or None. states are (frame, game state), or
    longer rows that start with those, of a hash log or a replay. Every state in through must come before the first
    level frame, for long enough to be that screen and not the frames a start in the level goes through."""
    level = next((row[0] for row in states if row[1] == replay_tool.GS_PLAY), None)
    if level is None:
        return None
    for screen in through:
        frames = sum(1 for row in states if row[0] < level and row[1] == screen)
        if frames < WHOLE_PATH_FRAMES:
            return (f"started in the level: {frames} frames of game state {screen} before the level at frame {level}, "
                    f"this test keeps the whole path from boot (#148)")
    return None


def played_through(values):
    """Input by rule has no checkpoints and extra players leave a recording on purpose, so a mismatch says nothing in
    those runs; they only have to reach the end of the replay."""
    return bool(values) and values.get("result") in ("PASS", "FAIL_MISMATCH")


def cmd_run(args):
    exe = replay_tool.find_exe(args.exe)
    replay, config, start = test_setup(args)
    lenient = args.file is None or args.players != 1
    failures = []

    poke = args.poke_frame
    if poke is None and args.file is None:
        poke = start.frame(GENERATED_POKE)
    if poke is None:
        # Default: the middle of the replay. Any frame works, because the poked bytes (gActors) are hashed in every
        # game state.
        poke = replay_tool.replay_frame_count(replay) // 2
    poke_env = None if args.poke_player is None else {"SF64_COOP_HASH_POKE_PLAYER": str(args.poke_player)}
    print(f"hash: runs A, B and C (poke at frame {poke}) in parallel")

    # The three runs are independent (own folders, same replay), so they run at the same time: running them in a row
    # would take three times as long.
    with ThreadPoolExecutor(max_workers=3) as pool:
        a_job = pool.submit(play, replay, exe, args.timeout, "hash-a", config=config, start=start)
        b_job = pool.submit(play, replay, exe, args.timeout, "hash-b", config=config, start=start)
        c_job = pool.submit(play, replay, exe, args.timeout, "hash-c", poke, env_extra=poke_env, config=config,
                            start=start)
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
    if name in COOP_GLOBALS or name == KEPT_TARGETS:
        return (True, "globals") if coop else (False, None)
    if name in TEAM_ARRAYS:
        _, first, end = TEAM_ARRAYS[name]
        return (True, "globals") if coop and first <= int(index) < end else (False, None)
    if name == "slot":
        # A player who is in the level or not: the values above count or stop counting with it.
        return (True, "players,globals") if coop and int(index) < players else (False, None)
    if name == "returnTimer":
        # How long a downed player still waits (#14): hashed beside its slot, for every player of the level.
        return (True, "players") if coop and int(index) < players else (False, None)
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
        wanted += [(f"{name}[{i}]", state) for name in PLAYER_ARRAYS + PLAYER_GLOBALS + ("slot", "returnTimer")
                   for state in states]
    wanted += [(f"{name}[{i}]", "0") for name, (slots, _, _) in TEAM_ARRAYS.items() for i in range(slots)]
    wanted += [(name, "0") for name in COOP_GLOBALS]
    wanted += [(f"{KEPT_TARGETS}[{i}]", "0") for i in ("0", "last")]
    for target, parked in wanted:
        if (target, parked) not in groups:
            failures.append(f"no probe of {target}{' parked' if parked == '1' else ''} ran")
    extra = [t for t, _ in groups if t.startswith("player") and int(t[6:]) >= players]
    if extra:
        failures.append(f"probes of {', '.join(sorted(set(extra)))}, but the run has {players} player(s)")
    return failures


def cmd_probe(args):
    exe = replay_tool.find_exe(args.exe)
    replay, config, start = test_setup(args)
    print(f"hash: probe run, {args.players} player(s), every {args.period} frames")
    values, run, elapsed = replay_tool.run_game("play", replay, exe, args.timeout, hash_log=True, tag="hash-probe",
                                                env_extra={"SF64_COOP_HASH_PROBE": str(args.period)}, config=config,
                                                start=start)
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


VIEW_SECTIONS = {  # name -> (frames from when player 0 flies, when L goes into all-range)
    "rails": (None, None),  # None: player_trace.RAIL_FLIGHT
    "all-range": (3225, GENERATED_L),
}


# Levels `views --level` flies, from include/sf64level.h: the Arwing levels, on rails or in all-range, where extra players
# join (players.c, CoopPlayers_SectionSupported). The Landmaster and Blue Marine levels are WP-18 (#16).
VIEW_LEVELS = {
    "corneria": 0, "meteo": 1, "sector-x": 2, "area-6": 3, "sector-y": 5, "venom-1": 6, "solar": 7, "zoness": 8, "venom-andross": 9, "training": 10,
    "fortuna": 14, "katina": 16, "bolse": 17, "sector-z": 18, "venom-2": 19,
}
VIEW_LEVEL_FRAMES = 2400  # frames from when player 0 flies: 80 seconds of play
# The second client of `views` has ticked this many more frames since boot (SF64_COOP_TEST_SYS_FRAME_OFFSET): two clients
# of one session booted at different times, and gSysFrameCount is not simulation state. Not a multiple of 8, which is
# what Katina's low-poly choice takes it by, and even (switches.c). Two games started together can also differ in it by
# chance, as the boot logo runs on wall-clock time, so without this a run can pass or fail by luck.
VIEW_SYS_FRAME_SKEW = 6


def parse_cvars(items):
    """{name: int} of repeated --cvar NAME[=VALUE] options (1 when there is no value)."""
    cvars = {}
    for item in items or []:
        name, _, value = item.partition("=")
        cvars[name] = int(value) if value else 1
    return cvars


def level_start(name):
    """Where `views --level` starts a game: straight in the level, past its intro, as FLYING does in Corneria. The
    frame numbers are Corneria's; a level that takes player 0 later only shifts the input by rule."""
    return replay_tool.Start(f"flying-{name}", VIEW_LEVELS[name], True, replay_tool.FLYING.level,
                             replay_tool.FLYING.active, replay_tool.START_SEEDS)


def read_views(path):
    """The camera a co-op run drew, from the "# view" lines of its log: frame -> (player looked through, eye)."""
    views = {}
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("# view "):
            parts = line.split()
            views[int(parts[2].split("=")[1])] = (int(parts[3].split("=")[1]), parts[4:])
    return views


def read_slots(path):
    """The "# slots" lines of a log: [(frame, {array: (used, capacity)})]. A build of #284 on also writes "# ages"
    lines: their count of old effects is "old", as (old, the effect slots)."""
    rows, ages = [], {}
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("# slots "):
            parts = line.split()
            counts = {kv.split("=")[0]: tuple(int(n) for n in kv.split("=")[1].split("/")) for kv in parts[3:]}  # radio=n
            rows.append((int(parts[2].split("=")[1]), counts))
        elif line.startswith("# ages "):
            fields = dict(kv.split("=", 1) for kv in line.split()[2:])
            ages[int(fields["frame"])] = int(fields["old"])
    for frame, counts in rows:
        if frame in ages and "effects" in counts:
            counts["old"] = (ages[frame], counts["effects"][1])
    return rows


def describe_slots(rows):
    """One line: for each object array the most slots in use, of how many, and how many at the last sample."""
    if not rows:
        return "no samples"
    parts = []
    for name in rows[0][1]:
        if name == "radio":
            parts.append(f"radio state changed {rows[-1][1][name][0]} times")
            continue
        peak = max(counts[name][0] for _, counts in rows)
        parts.append(f"{name} peak {peak}/{rows[0][1][name][1]} last {rows[-1][1][name][0]}")
    return ", ".join(parts)


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


def section_setup(section, name, args, start):
    """Return (replay, frames, config) for one section of VIEW_SECTIONS, played by rule from start."""
    import player_trace  # here, as player_trace imports this file

    flight, l_at = VIEW_SECTIONS[section]
    frames = args.frames or start.frame(flight or player_trace.RAIL_FLIGHT)
    # A folder of its own: a game reads the replay when its turn comes, which can be minutes after this, and another run
    # of this tool can start in between (#104).
    replay = make_run_folder(replay_tool.ROOT / "local", f"test-hash-{name}-{section}-") / "input.sfcr"
    player_trace.write_rail_replay(replay, frames, l_at, start=start)
    # Flying by rule, player 0 would not live through the section, as in player_trace.py rails.
    cvars = {"gCoopDebugPlayerCount": args.players, "gInvincible": 1}
    if l_at is not None:
        cvars["gDebugJumpToAllRange"] = 1
    return replay, frames, {"CVars": cvars}


def view_section(section, exe, args):
    """Play one section with each local index. Returns a list of failures."""
    start = replay_tool.FLYING
    replay, frames, config = section_setup(section, "views", args, start)
    config["CVars"].update(parse_cvars(args.cvar))
    return view_compare(section, replay, frames, config, start, exe, args)


def view_level(name, exe, args):
    """Play one level with each local index (#129). Returns a list of failures."""
    import player_trace  # here, as player_trace imports this file

    start = level_start(name)
    frames = args.frames or start.frame(VIEW_LEVEL_FRAMES)
    replay = make_run_folder(replay_tool.ROOT / "local", f"test-hash-views-{name}-") / "input.sfcr"
    player_trace.write_rail_replay(replay, frames, None, start=start)
    # Flying by rule, player 0 would not live through the level, as in player_trace.py rails.
    config = {"CVars": {"gCoopDebugPlayerCount": args.players, "gInvincible": 1, **parse_cvars(args.cvar)}}
    return view_compare(name, replay, frames, config, start, exe, args, level=VIEW_LEVELS[name])


def client_env(index, args):
    """What differs between the two clients of `views` besides the camera: the second one booted VIEW_SYS_FRAME_SKEW
    frames earlier (--skew), and each renders another sample count per audio update (--audio, #129), as a radio line
    follows the voice sequence in the sound thread."""
    env = {"SF64_COOP_TEST_LOCAL_INDEX": str(index)}
    if index == 1 and args.skew:
        env["SF64_COOP_TEST_SYS_FRAME_OFFSET"] = str(args.skew)
    if args.audio != "same":
        env["SF64_COOP_TEST_AUDIO_SAMPLES"] = ("high", "low")[index]
    return env


def view_compare(label, replay, frames, config, start, exe, args, level=None):
    """Play replay with local index 0 and 1 and compare. level, if given, is the LevelId both runs must fly in."""
    with ThreadPoolExecutor(max_workers=2) as pool:
        jobs = [pool.submit(play, replay, exe, args.timeout, f"view-{label}-{index}", config=config, start=start,
                            env_extra=client_env(index, args)) for index in (0, 1)]
        (a_values, names, a_rows), (b_values, _, b_rows) = [job.result() for job in jobs]

    if a_rows is None or b_rows is None:
        return [f"{label}: no hash log written"]
    failures = [f"{label}: index {index} did not play through: {values.get('result') if values else 'no report'}"
                for index, values in ((0, a_values), (1, b_values)) if not played_through(values)]
    if min(len(a_rows), len(b_rows)) < frames:
        failures.append(f"{label}: hash logs shorter than the replay ({len(a_rows)}, {len(b_rows)} < {frames} frames)")
    if level is not None:
        levels = level if isinstance(level, tuple) else (level,)
        where = " and ".join(str(n) for n in levels)
        for index, rows in ((0, a_rows), (1, b_rows)):
            flown = sum(1 for row in rows if row[1] == replay_tool.GS_PLAY and row[2] in levels)
            print(f"hash: {label}: index {index} flew {flown} frames in level {where}")
            if flown < frames // 2:
                failures.append(f"{label}: index {index} flew only {flown} of {frames} frames in level {where}")
    for index, values in ((0, a_values), (1, b_values)):
        print(f"hash: {label}: index {index} slots in use: {describe_slots(read_slots(values['log']))}")
    failures += check_views_differ(label, a_values, b_values)
    diff = first_difference(a_rows, b_rows, frames)
    if diff is not None:
        failures.append(f"{label}: index 0 and 1 differ: {describe(names, diff)}")
    else:
        print(f"hash: {label}: index 0 and 1 equal on all {min(frames, len(a_rows), len(b_rows))} frames")
    return failures


# `slots`: whether a level piles up effect slots in co-op that single player does not (#129, #284). What piles up is an
# effect that nothing ends, so the rule counts old effects: the hash log's "# ages" line (src/port/coop/sync/
# state_hash.c) gives how many effects have lived SLOTS_OLD frames or more. Bursts, Fortuna's burning base
# (FoBase_BurnEffects, 47 to 68 slots from frame 6900, gone before 10000) and the smoke a damaged Arwing trails
# (Effect389, Player_DamageEffects, src/engine/fox_play.c:237-289: each puff ends within about 14 frames) hold many
# slots and none of them grows old. It fails when co-op holds more old effects than single player at its most, by more
# than SLOTS_LEAK, on the samples from SLOTS_FROM on. Three rules failed before: a median of the slots in use over the
# limit failed Fortuna at --frames 9000 (16 against 15); the growth of the fewest slots in use from the first third to
# the last failed Corneria's 2400 frames on the second player's smoke (0 to 18); and the growth of the fewest old
# effects missed a mutant whose smoke never ends, as Corneria's cut to all-range clears the effects and the last third
# began without old ones (0, 51, 0).
SLOTS_FROM = 300
SLOTS_LEAK = 15  # the most old effects co-op may hold more than single player
SLOTS_OLD = 900  # EFFECT_OLD_FRAMES in state_hash.c


def slots_most(rows, name):
    """The most of counts[name] on the samples from SLOTS_FROM on, or None when there are none."""
    return max((counts[name][0] for frame, counts in rows if frame >= SLOTS_FROM and name in counts), default=None)


def slots_problem(name, players, single, coop):
    """The failure of `slots` for one level, or None; prints what it compared."""
    if not all("old" in counts for _, counts in single + coop):
        return f"{name}: no \"# ages\" lines in the hash logs: a build from before #284"
    one, more = slots_most(single, "old"), slots_most(coop, "old")
    if one is None or more is None:
        return f"{name}: too few samples to compare"
    print(f"hash: {name}: at most {more} old effects ({SLOTS_OLD} frames or more) with {players} players, {one} with 1 "
          f"(at most {SLOTS_LEAK} more); at most {slots_most(coop, 'effects')} and {slots_most(single, 'effects')} "
          f"effects in use")
    if more - one > SLOTS_LEAK:
        return (f"{name}: {players} players held {more} effects that had lived {SLOTS_OLD} frames or more, 1 player "
                f"{one} (at most {SLOTS_LEAK} more): something is not freed")
    return None


def slots_rule_test():
    """`slots --rule-test`: the rule on made-up samples every 100 frames, as a run of `slots` takes them (#284). An
    effect counts as old SLOTS_OLD frames after its birth. Returns a list of failures."""
    def rows(used, old=lambda f: 0, end=9000):
        return [(f, {"effects": (used(f), 100), "old": (old(f), 100)}) for f in range(0, end + 1, 100)]

    def leaked(f, every, cap=100, since=0):  # leaked effects that are old at frame f, one born every `every` frames
        return min(cap, max(0, (f - since - SLOTS_OLD) // every))

    quiet = lambda f: 5 + (40 if f % 1000 == 0 else 0)  # single player, with a burst now and then
    cases = [
        ("a renewed set from frame 6900 to the end, as Fortuna's burn at --frames 9000",
         rows(lambda f: 5 + 55 * (f >= 6900)), True),
        ("a renewed set over all but 3 samples of the last third, as Fortuna's burn at --frames 10000",
         rows(lambda f: 5 + 55 * (6900 <= f < 9800) + 80 * (9800 <= f < 10000), end=10000), True),
        ("a renewed set from frame 1900 on, as the second player's smoke in Corneria's 2400 frames",
         rows(lambda f: 5 + 18 * (f >= 1900), end=2400), True),
        ("a burst to 100 slots for one sample", rows(lambda f: 100 if f == 8100 else 7), True),
        ("20 more slots than single player all along, renewed", rows(lambda f: 25 + (40 if f % 1000 == 0 else 0)), True),
        ("10 old effects in both runs from frame 1000 on", rows(quiet, lambda f: 10 * (f >= 1000)), True),
        ("a leak of an effect every 300 frames", rows(lambda f: 5 + f // 300, lambda f: leaked(f, 300)), False),
        ("a leak of an effect every 100 frames, the array full from frame 9500 on",
         rows(lambda f: min(100, 5 + f // 100), lambda f: leaked(f, 100, 95)), False),
        ("a leak that fills the array by frame 3000", rows(lambda f: min(100, 5 + f // 30), lambda f: leaked(f, 30, 95)),
         False),
        ("a leak from frame 1100 that a level cut at 3400 clears, as the mutant of Corneria's smoke",
         rows(lambda f: 5 + 60 * (1100 <= f < 3400),
              lambda f: leaked(f, 4, 95, 1100) if f < 3400 else leaked(f, 4, 95, 3400), end=6000), False),
    ]
    failures = []
    for label, coop, passes in cases:
        end = coop[-1][0]
        single = rows(quiet, lambda f: 10 * (f >= 1000) if "in both" in label else 0, end=end)
        problem = slots_problem("rule-test", 2, single, coop)
        if (problem is None) != passes:
            failures.append(f"{label}: {'failed' if problem else 'passed'}, want {'pass' if passes else 'fail'}")
    return failures


def slots_level(name, exe, args):
    """Play one level with 1 player and with args.players, the same input. Returns a list of failures."""
    import player_trace  # here, as player_trace imports this file

    start = level_start(name)
    frames = args.frames or start.frame(VIEW_LEVEL_FRAMES)
    replay = make_run_folder(replay_tool.ROOT / "local", f"test-hash-slots-{name}-") / "input.sfcr"
    player_trace.write_rail_replay(replay, frames, None, start=start)
    counts = (1, args.players)
    with ThreadPoolExecutor(max_workers=2) as pool:
        jobs = [pool.submit(play, replay, exe, args.timeout, f"slots-{name}-{n}", start=start,
                            config={"CVars": {"gCoopDebugPlayerCount": n, "gInvincible": 1}}) for n in counts]
        runs = [job.result() for job in jobs]
    failures = []
    series = []
    for n, (values, _, rows) in zip(counts, runs):
        if rows is None or not played_through(values):
            failures.append(f"{name}: {n} player(s) did not play through")
            continue
        slots = read_slots(values["log"])
        print(f"hash: {name}: {n} player(s) slots in use: {describe_slots(slots)}")
        series.append(slots)
    if len(series) == 2:
        problem = slots_problem(name, args.players, *series)
        if problem:
            failures.append(problem)
    return failures


def cmd_slots(args):
    if args.rule_test:
        failures = slots_rule_test()
        for f in failures:
            print(f"FAIL: {f}")
        print("hash: FAIL" if failures else "hash: PASS")
        return 1 if failures else 0
    exe = replay_tool.find_exe(args.exe)
    names = list(VIEW_LEVELS) if args.level == "all" else args.level.split(",")
    unknown = [name for name in names if name not in VIEW_LEVELS]
    if unknown:
        sys.exit(f"hash: no level {', '.join(unknown)}; the choices are {', '.join(VIEW_LEVELS)}")
    failures = []
    for name in names:
        failures += slots_level(name, exe, args)
    for f in failures:
        print(f"FAIL: {f}")
    print("hash: FAIL" if failures else "hash: PASS")
    return 1 if failures else 0


# `views --pilot` (#181): a pilot that reads game state (src/port/coop/test/pilot.h, pilot_aim.h) flies each level to
# its end in one game, and the two clients of `views` then play what it flew. The levels it flies: every campaign level
# that ends, the vehicle levels too, and level 4 (LEVEL_UNK_4, an unused variant of Area 6 with its own level script,
# src/engine/fox_enmy2.c), which no menu offers. Training has no level complete (ItemPathChange_Update,
# src/engine/fox_enmy.c, only starts its all-range again), and level 15 (LEVEL_UNK_15) has no environment
# (D_800D2F98, src/engine/fox_play.c): the game crashes in Play_InitEnvironment, with co-op off too (NOTES.md, #181).
PILOT_LEVELS = {**{name: level for name, level in VIEW_LEVELS.items() if name != "training"},
                "macbeth": 11, "titania": 12, "aquas": 13, "level-4": 4, "venom-2-andross": 19}
# Flights on through the game's own jump into another level (as campaign.py's W8_ANDPILOT_THROUGH, #275): name -> the
# LevelId whose end the flight waits for. Andross' second part, his face on rails, the brain in all-range and the escape
# after it, comes only after Venom 2: its clear (gLeveLClearStatus, src/overlays/ovl_i6/fox_ve2.c:123) sends the game
# into Andross with gLevelPhase 1 (fox_ve2.c:442-446). `venom-andross` starts in phase 0: the face, and after it the
# short escape of the other route; `venom-2` stops 60 frames into Andross. So no other entry has the brain (#181).
PILOT_THROUGH = {"venom-2-andross": 9}
# Levels whose level complete is not the end of a flight through a jump: Andross' escape begins as one, 240 frames with
# csState 100 (fox_andross.c:1106-1108); it ends at the trigger at the maze's exit, which sets level complete in Venom
# 2's id (fox_andross.c:683-690), and the ending follows (campaign.py's W8_ANDPILOT_NOT_AN_END).
PILOT_NOT_AN_END = {"venom-2-andross": {9}}
PILOT_NO_END = {"training": "no level complete: its path change starts the all-range again (src/engine/fox_enmy.c)",
                "level-15": "no level: its environment is NULL (src/engine/fox_play.c), and the game crashes in "
                            "Play_InitEnvironment, with co-op off too"}
PILOT_FRAMES = 24000  # most frames of a pilot's flight; Corneria took 11200 to the map, Sector X 11529 to level complete
# Goras took to 29169 in #16's run with a pilot on each of two clients, and was at half its health after 40000 frames
# of one pilot flying both; Sector Y's last Shogun had 20 health left at 24000, the Gorgon 660 of 780.
PILOT_LEVEL_FRAMES = {"titania": 70000, "sector-y": 36000, "area-6": 60000, "bolse": 40000,
                      "venom-2-andross": 60000}  # #275's campaign run: 37249 lockstep frames to the ending
# Levels the pilot flies with gInfiniteBombs, as campaign.py's BOMBS (owner's choice of 2026-11-11): their bosses need
# bombs, and the port's cheat keeps player 0 at 9 (src/port/mods/PortEnhancements.c).
PILOT_BOMBS = {"zoness", "venom-andross", "venom-2-andross"}
PILOT_TAIL = 60  # frames kept after the game leaves the level: the map, the next level or an ending
PILOT_END_TAIL = 1800  # or this many after level complete, whichever comes first: some endings run long
PILOT_CUT_MARGIN = 300  # the pilot's game runs this much past the cut, so that nothing it buffered is missing
LEVEL_COMPLETE = 7  # PLAYERSTATE_LEVEL_COMPLETE
# Level completes that end a level without its boss's death, as (LevelId, csState of player 0): Katina's base hit by
# the Saucerer's laser (src/overlays/ovl_i4/fox_ka.c:1224-1225, SAUCERER_CS_ROTATE), where the Saucerer's defeat sets
# csState 0 (fox_ka.c:1412-1414, MISSION_ACCOMPLISHED). The game goes on from both; a flight that ends at the first
# compared the level but not the boss, and counted as "to the end" until #181 found the pilot shooting at the team's
# own base. (Fortuna with Star Wolf flying, fox_fo.c:210-222, and Sector Z with the Great Fox hit, fox_sz.c:1559-1565,
# end in mission complete too; their csState does not tell them apart, so they are not here.)
FAILED_ENDS = {(16, 100): "Katina's base hit by the Saucerer's laser, so its defeat is not in the flight"}


def failed_end(trace):
    """What FAILED_ENDS says of the first level complete in a trace (src/port/coop/test/player_trace.h), or None."""
    import player_trace  # here, as player_trace imports this file

    for row in player_trace.parse(trace):
        if row.players and row.players[0].state == LEVEL_COMPLETE:
            return FAILED_ENDS.get((row.level, row.players[0].cs))
    return None


class GrowingFile:
    """New whole lines of a file another process writes, from where the last read stopped."""

    def __init__(self, path):
        self.path = Path(path)
        self.at = 0
        self.rest = b""

    def lines(self):
        if not self.path.exists():
            return []
        with open(self.path, "rb") as f:
            f.seek(self.at)
            data = f.read()
        self.at += len(data)
        data = self.rest + data
        whole, _, self.rest = data.rpartition(b"\n")
        return whole.decode("utf-8", errors="replace").splitlines() if whole else []


def pilot_flight(name, exe, args, work, config, start):
    """One game: input by rule into the level and the pilot flying every player from there, stopped once the level is
    over. Returns (pilot replay path, frames, level complete frame or None, why it stopped)."""
    import json
    import subprocess
    import time

    level = PILOT_LEVELS[name]
    target = PILOT_THROUGH.get(name, level)
    not_an_end = PILOT_NOT_AN_END.get(name, set())
    cap = args.frames or start.frame(PILOT_LEVEL_FRAMES.get(name, PILOT_FRAMES))
    complete_level = None
    reached = target == level  # a flight through a jump counts a level complete only once in the level it waits for
    rule = work / "rule.sfcr"
    write_generated_replay(rule, frames=cap, l_at=None, start=start)
    run = replay_tool.make_run_dir(exe, f"play-views-pilot-{name}")
    (run / "starship.cfg.json").write_text(json.dumps(config, indent=4), encoding="utf-8")
    flown = work / "flown.sfcr"
    env = replay_tool.game_env(run)
    env.update(start.env)
    env.update({"SF64_COOP_REPLAY_PLAY": str(rule), "SF64_COOP_REPLAY_REPORT": str(run / "replay-report.txt"),
                "SF64_COOP_HASH_LOG": str(run / "state-hash.txt"), "SF64_COOP_PILOT": str(flown),
                "SF64_COOP_PILOT_LOG": str(work / "pilot-log.txt"), "SF64_COOP_TRACE": str(work / "trace.txt")})
    print(f"hash: {name}: pilot, at most {cap} frames, folder {run}", flush=True)
    log = GrowingFile(run / "state-hash.txt")
    trace = GrowingFile(work / "trace.txt")
    began = time.monotonic()
    complete = left = None
    frames = 0
    why = None
    proc = subprocess.Popen([str(exe)], cwd=run, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    try:
        while why is None:
            time.sleep(2)
            for line in log.lines():
                if line.startswith("#") or not line.strip():
                    continue
                parts = line.split()
                frames = int(parts[0]) + 1
                in_level = parts[1] == str(replay_tool.GS_PLAY) and parts[2] == str(complete_level)
                if complete is not None and left is None and not in_level:
                    left = frames - 1
            # The trace counts level frames, so the hash log read just before says which replay frame it is now: no
            # earlier than the real one. The exact frames come from the replay's checkpoints below.
            for line in trace.lines():
                fields = dict(kv.split("=", 1) for kv in line.split() if "=" in kv)
                p0 = fields["p0"].split(",") if "p0" in fields else []
                # Once player 0 is out of the level complete that led there: the trace names the new level's id while
                # the old one ends (campaign.py).
                if not reached and fields.get("level") == str(target) and p0 and p0[0] != str(LEVEL_COMPLETE):
                    reached = True
                # Any level: Venom 1 ends in Venom 2's id, as the game sets it at the exit (fox_andross.c).
                if (complete is None and reached and p0 and p0[0] == str(LEVEL_COMPLETE) and
                        int(fields.get("level", -1)) not in not_an_end):
                    complete = frames
                    complete_level = fields.get("level")
            end = min(left + PILOT_TAIL if left is not None else cap,
                      complete + PILOT_END_TAIL if complete is not None else cap, cap)
            if frames >= min(end + PILOT_CUT_MARGIN, cap):
                why = "left the level" if left is not None else ("past level complete" if complete is not None
                                                                 else "frame cap")
            elif proc.poll() is not None:
                why = f"the game ended by itself with code {proc.returncode} at frame {frames}"
            elif time.monotonic() - began > args.timeout:
                why = f"timeout after {args.timeout:.0f} s at frame {frames}"
    finally:
        replay_tool.stop_game(proc)
    size = struct.unpack_from("<I", flown.read_bytes(), 12)[0] if flown.exists() else 0
    whole = replay_tool.replay_frame_count(flown)
    complete = left = None
    reached = target == level
    if size:
        import full_level  # here, as full_level imports this file

        for frame, game_state, at, state in full_level.read_checkpoints(flown):
            if not reached and game_state == replay_tool.GS_PLAY and at == target and state != LEVEL_COMPLETE:
                reached = True
            if (complete is None and reached and game_state == replay_tool.GS_PLAY and state == LEVEL_COMPLETE and
                    at not in not_an_end):
                complete = frame
                complete_level = at
            elif complete is not None and (game_state != replay_tool.GS_PLAY or at != complete_level):
                left = frame
                break
    frames = min(left + PILOT_TAIL if left is not None else whole,
                 complete + PILOT_END_TAIL if complete is not None else whole, whole)
    cut = work / f"{name}.sfcr"
    if size:
        cut.write_bytes(flown.read_bytes()[:replay_tool.HEADER_SIZE + frames * size])
    print(f"hash: {name}: pilot stopped ({why}) after {time.monotonic() - began:.0f} s; level complete at replay "
          f"frame {complete} (level {complete_level}), left that level at {left}; replay cut at {frames} of {whole} "
          f"frames", flush=True)
    return (cut if size else None), frames, complete, why, run / "state-hash.txt"


def pilot_players(trace, level, players):
    """Per player from a pilot's trace: (frames flown, times down, lowest shields)."""
    import player_trace  # here, as player_trace imports this file

    levels = level if isinstance(level, tuple) else (level,)
    rows = [r for r in player_trace.parse(trace) if r.level in levels]
    stories = []
    for i in range(players):
        slots = [r.players[i] for r in rows if len(r.players) > i]
        flown = sum(1 for s in slots if s.state in player_trace.FLYING and not s.parked)
        down = sum(1 for a, b in zip(slots, slots[1:]) if a.state != 4 and b.state == 4)  # PLAYERSTATE_DOWN
        lowest = min((s.shields for s in slots if s.state in player_trace.FLYING and not s.parked), default=0)
        stories.append((flown, down, lowest))
    return stories


def last_boss_line(work):
    path = work / "pilot-log.txt"
    lines = [line for line in path.read_text(errors="replace").splitlines() if " boss " in line] if path.exists() else []
    return lines[-1] if lines else "no boss seen"


def view_pilot(name, exe, args):
    """A pilot flies one level to its end, then index 0 and 1 play what it flew (#181). Returns (failures, row)."""
    if name in PILOT_NO_END:
        return [], (name, f"not covered: {PILOT_NO_END[name]}", 0, None, "-")
    level = PILOT_LEVELS[name]
    start = replay_tool.Start(f"flying-{name}", level, True, replay_tool.FLYING.level, replay_tool.FLYING.active,
                              replay_tool.START_SEEDS)
    work = make_run_folder(replay_tool.ROOT / "local", f"test-hash-views-pilot-{name}-")
    cvars = {"gCoopDebugPlayerCount": args.players, **parse_cvars(args.cvar)}
    if not args.mortal:
        cvars["gInvincible"] = 1
    if name in PILOT_BOMBS:
        cvars["gInfiniteBombs"] = 1
    config = {"CVars": cvars}
    replay, frames, complete, why, pilot_log = pilot_flight(name, exe, args, work, config, start)
    if replay is None or frames < 100:
        return [f"{name}: the pilot flew nothing ({why})"], (name, f"no flight: {why}", frames, complete, "-")
    levels = (level, PILOT_THROUGH[name]) if name in PILOT_THROUGH else level
    stories = pilot_players(work / "trace.txt", levels, args.players)
    downs = ",".join(str(down) for _, down, _ in stories)
    for i, (flown, down, lowest) in enumerate(stories):
        print(f"hash: {name}: player {i} flew {flown} level frames, went down {down} times, lowest shields {lowest}")
    failures = view_compare(name, replay, frames, config, start, exe, args, level=levels)
    for index in (0, 1):
        tag = f"view-{name}-{index}"
        report = sorted((replay_tool.ROOT / "local").glob(f"test-replay-play-{tag}-*/replay-report.txt"))
        values = replay_tool.read_report(report[-1]) if report else None
        if not values or values.get("result") != "PASS":
            failures.append(f"{name}: index {index} played the pilot's replay with "
                            f"{values.get('result') if values else 'no report'}, not PASS")
    failed = failed_end(work / "trace.txt") if complete is not None else None
    if failed:
        failures.append(f"{name}: the level ended without its boss's death: {failed}")
    names, pilot_rows = read_log(pilot_log)
    _, a_rows = read_log(sorted((replay_tool.ROOT / "local").glob(f"test-replay-play-view-{name}-0-*"))[-1] /
                         "state-hash.txt")
    diff = first_difference(pilot_rows, a_rows, frames)
    if diff is not None:
        failures.append(f"{name}: the pilot's game and index 0 differ: {describe(names, diff)}")
    else:
        print(f"hash: {name}: the pilot's game and index 0 equal on all {frames} frames")
    if failed and len(failures) == 1:
        result = f"equal on the {frames} frames flown, but the level ended without its boss's death: {failed}"
    elif failures:
        result = "hash difference: " + "; ".join(failures)
    elif complete is None:
        result = f"pilot cannot finish ({why}); equal on the {frames} frames flown; {last_boss_line(work)}"
    else:
        result = "equal on every frame to the end"
    if name in PILOT_BOMBS:
        result += " (gInfiniteBombs on)"
    if name in PILOT_THROUGH:
        result += f" (through the game's jump into level {PILOT_THROUGH[name]})"
    if complete is None:
        failures.append(f"{name}: no level complete in {frames} frames ({why})")
    return failures, (name, result, frames, complete, downs)


def cmd_views(args):
    exe = replay_tool.find_exe(args.exe)
    failures = []
    if args.pilot:
        names = [*PILOT_LEVELS, *PILOT_NO_END] if args.level in (None, "all") else args.level.split(",")
        unknown = [name for name in names if name not in PILOT_LEVELS and name not in PILOT_NO_END]
        if unknown:
            sys.exit(f"hash: no level {', '.join(unknown)}; the choices are {', '.join(PILOT_LEVELS)}")
        rows = []
        for name in names:
            found, row = view_pilot(name, exe, args)
            failures += found
            rows.append(row)
            print(f"hash: views --pilot: {name}: {row[1]}, {row[2]} frames, level complete at {row[3]}, "
                  f"downs {row[4]}", flush=True)
        print("hash: views --pilot summary (level, replay frames, level complete at replay frame, downs per player, "
              "result):")
        for name, result, frames, complete, downs in rows:
            print(f"hash:   {name:14} {frames:6} {str(complete):>6} {downs:>5}  {result}")
        for f in failures:
            print(f"FAIL: {f}")
        print("hash: FAIL" if failures else "hash: PASS")
        return 1 if failures else 0
    if args.level:
        names = list(VIEW_LEVELS) if args.level == "all" else args.level.split(",")
        unknown = [name for name in names if name not in VIEW_LEVELS]
        if unknown:
            sys.exit(f"hash: no level {', '.join(unknown)}; the choices are {', '.join(VIEW_LEVELS)}")
        for name in names:
            failures += view_level(name, exe, args)
    else:
        for section in VIEW_SECTIONS if args.section == "both" else (args.section,):
            failures += view_section(section, exe, args)
    for f in failures:
        print(f"FAIL: {f}")
    print("hash: FAIL" if failures else "hash: PASS")
    return 1 if failures else 0


def compare_runs(section, label, a, b, frames):
    """a and b are (values, names, rows) of two runs of one replay. Returns a list of failures."""
    if a[2] is None or b[2] is None:
        return [f"{section}: {label}: no hash log written"]
    failures = []
    if min(len(a[2]), len(b[2])) < frames:
        failures.append(f"{section}: {label}: hash logs shorter than the replay "
                        f"({len(a[2])}, {len(b[2])} < {frames} frames)")
    diff = first_difference(a[2], b[2], frames)
    if diff is not None:
        failures.append(f"{section}: {label} differ: {describe(a[1], diff)}")
    else:
        print(f"hash: {section}: {label} equal on all {min(frames, len(a[2]), len(b[2]))} frames")
    return failures


def read_draws(path):
    """The most draws a game frame got in a run, from the "# draws" lines of its log; 0 if it holds none."""
    counts = [int(line.split("count=")[1]) for line in
              Path(path).read_text(encoding="utf-8", errors="replace").splitlines() if line.startswith("# draws ")]
    return max(counts, default=0)


def too_few_draws(label, run, want=INTERPOLATED):
    """A run that asked for pixels or interpolation must have had them (#147): at real pacing with the frame rate of a
    default config, a game frame takes INTERPOLATED draws. run is what play returned. Returns a list of failures."""
    values = run[0]
    if not values or "log" not in values:
        return []  # no log: reported where the run is looked at
    got = read_draws(values["log"])
    return [] if got >= want else [f"{label} did not draw: {got} draws a frame, not {want}"]


def build_section(section, debug, release, args):
    """Play one section on both Release runs and the Debug run. Returns a list of failures."""
    start = replay_tool.MENUS  # see the top of this file
    replay, frames, config = section_setup(section, "builds", args, start)
    runs = (("release-a", release), ("release-b", release), ("debug", debug))

    with ThreadPoolExecutor(max_workers=len(runs)) as pool:
        jobs = [pool.submit(play, replay, exe, args.timeout, f"build-{section}-{tag}", env_extra=replay_tool.REAL_TIME,
                            config=config, start=start) for tag, exe in runs]
        a, b, d = [job.result() for job in jobs]

    failures = [f"{section}: {tag} did not play through: {run[0].get('result') if run[0] else 'no report'}"
                for (tag, _), run in zip(runs, (a, b, d)) if not played_through(run[0])]
    for (tag, _), run in zip(runs, (a, b, d)):
        failures += too_few_draws(f"{section}: {tag}", run)
    failures += compare_runs(section, "two Release runs", a, b, frames)
    failures += compare_runs(section, "Debug and Release", d, a, frames)
    return failures


def cmd_builds(args):
    debug, release = replay_tool.find_exe(args.debug or args.exe), replay_tool.find_exe(args.release or args.exe)
    failures = []
    for section in VIEW_SECTIONS if args.section == "both" else (args.section,):
        failures += build_section(section, debug, release, args)
    for f in failures:
        print(f"FAIL: {f}")
    print("hash: FAIL" if failures else "hash: PASS")
    return 1 if failures else 0


def rate_section(section, exe, rates, args):
    """Play one section at every rate. Returns a list of failures."""
    start = replay_tool.MENUS  # see the top of this file
    replay, frames, config = section_setup(section, "rates", args, start)
    speed = replay_tool.REAL_TIME if args.speed is None else {**replay_tool.DRAWN, "SF64_COOP_TEST_SPEED": args.speed}

    def run(rate):
        rate_config = copy.deepcopy(config)
        rate_config["CVars"].update({"gInterpolationFPS": rate, "gVsyncEnabled": 0})
        return play(replay, exe, args.timeout, f"rate-{section}-{rate}", env_extra=speed, config=rate_config,
                    start=start)

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        runs = list(pool.map(run, rates))

    failures = []
    for rate, run_result in zip(rates, runs):
        values = run_result[0]
        if not played_through(values):
            failures.append(f"{section}: {rate} fps did not play through: "
                            f"{values.get('result') if values else 'no report'}")
            continue
        want, got = math.ceil(rate / 30), read_draws(values["log"])
        if got != want:
            failures.append(f"{section}: {rate} fps was not used: {got} draws a frame, not {want}")
    for rate, run_result in zip(rates[1:], runs[1:]):
        failures += compare_runs(section, f"{rates[0]} and {rate} fps", runs[0], run_result, frames)
    return failures


def cmd_rates(args):
    exe = replay_tool.find_exe(args.exe)
    rates = [int(rate) for rate in args.rates.split(",")]
    failures = []
    for section in VIEW_SECTIONS if args.section == "both" else (args.section,):
        failures += rate_section(section, exe, rates, args)
    for f in failures:
        print(f"FAIL: {f}")
    print("hash: FAIL" if failures else "hash: PASS")
    return 1 if failures else 0


def draw_section(section, start, exe, args):
    """Play one section from one start with drawing and without. Returns a list of failures."""
    label = f"{section} from {start.name}"
    replay, frames, config = section_setup(section, f"draw-{start.name}", args, start)

    with ThreadPoolExecutor(max_workers=2) as pool:
        jobs = [pool.submit(play, replay, exe, args.timeout, f"draw-{start.name}-{section}-{tag}", env_extra=env,
                            config=config, start=start) for tag, env in (("off", None), ("on", replay_tool.DRAWN))]
        off, on = [job.result() for job in jobs]

    failures = [f"{label}: {tag} did not play through: {run[0].get('result') if run[0] else 'no report'}"
                for tag, run in (("the run without drawing", off), ("the drawn run", on)) if not played_through(run[0])]
    if off[0] and "log" in off[0] and read_draws(off[0]["log"]) != 0:
        failures.append(f"{label}: the switch did nothing: {read_draws(off[0]['log'])} draws a frame, not 0")
    failures += too_few_draws(f"{label}: the drawn run", on, want=1)
    return failures + compare_runs(label, "with and without drawing", off, on, frames)


def cmd_draw(args):
    exe = replay_tool.find_exe(args.exe)
    failures = []
    starts = replay_tool.STARTS.values() if args.start == "all" else (replay_tool.STARTS[args.start],)
    for start in starts:
        for section in VIEW_SECTIONS if args.section == "both" else (args.section,):
            failures += draw_section(section, start, exe, args)
    for f in failures:
        print(f"FAIL: {f}")
    print("hash: FAIL" if failures else "hash: PASS")
    return 1 if failures else 0


# Levels `base --level` starts in (#283): every LevelId with a level, the vehicle levels too.
BASE_LEVELS = {**VIEW_LEVELS, "macbeth": 11, "titania": 12, "aquas": 13, "level-4": 4}


def base_level_start(name):
    """Where `base --level` starts both games: straight in the level, through its intro (as LEVEL does in Corneria, a
    level's intro is simulation too). The frame numbers are Corneria's; a level that takes player 0 later only shifts
    the input by rule."""
    level = replay_tool.LEVEL
    return replay_tool.Start(f"level-{name}", BASE_LEVELS[name], False, level.level, level.active, level.seeds)


def base_level_problem(tag, rows, level, frames):
    """A game of `base --level` must have flown in its level, or a base build that does not know the start would only
    show the menus on both sides (or crash in another place). Returns a failure or None."""
    flown = sum(1 for row in rows if row[1] == replay_tool.GS_PLAY and row[2] == level)
    print(f"hash: base: {tag} flew {flown} frames in level {level}")
    return None if flown >= frames // 2 else f"{tag} flew only {flown} of {frames} frames in level {level}"


def cmd_base(args):
    exe, base = replay_tool.find_exe(args.exe), replay_tool.find_exe(args.base)
    flight = args.frames or GENERATED_END
    if args.level:
        if args.level not in BASE_LEVELS:
            sys.exit(f"hash: no level {args.level}; the choices are {', '.join(BASE_LEVELS)}")
        start = base_level_start(args.level)
        replay = make_run_folder(replay_tool.ROOT / "local", f"test-hash-base-{args.level}-") / "input.sfcr"
        write_generated_replay(replay, frames=start.frame(flight), start=start)
        config = {"CVars": {"gDebugJumpToAllRange": 1}}  # what test_setup gives input by rule
    else:
        # Through the menus: the base build may be older than the switch of a start in the level, and would say so.
        args.file, args.players, args.start = None, 1, replay_tool.MENUS.name
        replay, config, start = test_setup(args)
        if args.frames:
            write_generated_replay(replay, frames=start.frame(flight), start=start)

    with ThreadPoolExecutor(max_workers=2) as pool:
        jobs = [pool.submit(play, replay, build, args.timeout, f"base-{tag}", config=config, start=start)
                for tag, build in (("before", base), ("after", exe))]
        before, after = [job.result() for job in jobs]

    failures = [f"{tag} did not play through: {run[0].get('result') if run[0] else 'no report'}"
                for tag, run in (("the base build", before), ("this build", after)) if not played_through(run[0])]
    if args.level:
        failures += [problem for tag, run in (("the base build", before), ("this build", after)) if run[2] is not None
                     for problem in [base_level_problem(tag, run[2], start.level_id, flight)] if problem]
    failures += compare_runs("co-op off", "the base build and this one", before, after, start.frame(flight))
    for f in failures:
        print(f"FAIL: {f}")
    print("hash: FAIL" if failures else "hash: PASS")
    return 1 if failures else 0


def read_level_line(path):
    """(frame, seeds) of the first "# level" line of a hash log, the frame Play_Init ran on, or None."""
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        match = re.match(r"# level frame=(\d+) seeds=(\S+)", line)
        if match:
            return int(match[1]), tuple(int(seed) for seed in match[2].split(","))
    return None


def without_region(rows, at):
    """Rows with one region and the total left out of the comparison."""
    return [(frame, state, level, "", [*regions[:at], "", *regions[at + 1:]])
            for frame, state, level, _, regions in rows]


def start_played(name, values, rows, start):
    """Return (failures, frame Play_Init ran on, seeds) for a run of cmd_start."""
    level = None if values is None or "log" not in values else read_level_line(values["log"])
    if rows is None or not played_through(values) or level is None:
        return [f"{name} did not reach a level: {values.get('result') if values else 'no report'}"], None, None
    if level[0] + 1 != start.level:
        # The input would no longer land where replay.py says.
        return [f"{name}: level frame 0 at frame {level[0] + 1}, replay.py says {start.level}"], None, None
    return [], *level


def cmd_start(args):
    exe = replay_tool.find_exe(args.exe)
    menus, level = replay_tool.MENUS, replay_tool.LEVEL
    work = make_run_folder(replay_tool.ROOT / "local", "test-hash-start-")
    config = {"CVars": {"gCoopDebugPlayerCount": args.players, "gDebugJumpToAllRange": 1}}
    frames = args.frames or GENERATED_END

    write_generated_replay(work / "menus.sfcr", menus.frame(frames), start=menus)
    menu_values, names, menu_rows = play(work / "menus.sfcr", exe, args.timeout, "start-menus", config=config,
                                         start=menus)
    failures, menu_level, seeds = start_played("the game from the menus", menu_values, menu_rows, menus)
    if failures:
        print(f"FAIL: {failures[0]}\nhash: FAIL")
        return 1
    print(f"hash: through the menus Corneria starts at frame {menu_level + 1}, from seeds {seeds}")
    if seeds != replay_tool.START_SEEDS:
        print(f"hash: note: replay.py's START_SEEDS are {replay_tool.START_SEEDS}, so the tools fly another flight")

    write_generated_replay(work / "level.sfcr", level.frame(frames), start=level, seeds=seeds)
    with ThreadPoolExecutor(max_workers=2) as pool:
        jobs = [pool.submit(play, work / "level.sfcr", exe, args.timeout, tag, config=config, start=level)
                for tag in ("start-a", "start-b")]
        (a_values, _, a_rows), (b_values, _, b_rows) = [job.result() for job in jobs]

    failures, a_level, a_seeds = start_played("start A", a_values, a_rows, level)
    more, _, _ = start_played("start B", b_values, b_rows, level)
    failures += more
    if not failures:
        recorded = level.frame(frames)
        print(f"hash: start A enters level frame 0 at frame {a_level + 1}, from seeds {a_seeds}")
        failures += compare_runs("start", "two starts in the level", (a_values, names, a_rows),
                                 (b_values, names, b_rows), recorded)
        if a_seeds != seeds:
            failures.append(f"start A entered from seeds {a_seeds}, not {seeds}")
        # From level frame 1: on the frame Play_Init runs, gGameFrameCount still counts the screen a game came from.
        menu_level_rows = [row for row in menu_rows if row[0] > menu_level]
        shifted = [(row[0] + menu_level - a_level, *row[1:]) for row in a_rows if row[0] > a_level]
        limit = recorded - a_level
        if args.players == 1:
            # Only here the hash reads free gScenery360 slots (HashScenery360), and level memory is not cleared: they
            # hold what the screens before left there, and from the menus that is more than nothing.
            at = names.index("scenery360")
            apart = sum(1 for a, b in zip(menu_level_rows[:limit], shifted[:limit]) if a[4][at] != b[4][at])
            print(f"hash: with one player free gScenery360 slots count: that region differs on {apart} frames, and is "
                  f"left out")
            menu_level_rows, shifted = without_region(menu_level_rows, at), without_region(shifted, at)
        diff = first_difference(menu_level_rows, shifted, limit)
        if diff is not None:
            failures.append(f"menus and start A differ {diff[0] + 1} frames into the level: {describe(names, diff)}")
        elif min(len(menu_level_rows), len(shifted)) < limit - 1:
            failures.append(f"only {min(len(menu_level_rows), len(shifted))} level frames to compare, not {limit - 1}")
        else:
            what = f"{len(names) - 1} regions" if args.players == 1 else f"total and {len(names)} regions"
            print(f"hash: menus and start A equal on {limit - 1} level frames: state, level, {what}")
    for failure in failures:
        print(f"FAIL: {failure}")
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
    r.add_argument("--start", choices=replay_tool.STARTS, default="flying", help="input by rule: where the game starts")
    d = sub.add_parser("diff", help="compare two hash logs")
    d.add_argument("a")
    d.add_argument("b")
    b = sub.add_parser("probe", help="play once and check what the hash can and cannot see")
    b.add_argument("file", nargs="?", help="replay file (default: input by rule, into all-range)")
    b.add_argument("--players", type=int, default=2, help="players, through gCoopDebugPlayerCount (default 2)")
    b.add_argument("--period", type=int, default=200, help="frames between probes (default 200)")
    b.add_argument("--exe", help="Starship executable (default: newest build found)")
    b.add_argument("--timeout", type=float, default=900, help="give up after this many seconds")
    b.add_argument("--start", choices=replay_tool.STARTS, default="level", help="input by rule: where the game starts")
    v = sub.add_parser("views", help="play one replay with local index 0 and 1 and compare the hashes")
    v.add_argument("--section", choices=[*VIEW_SECTIONS, "both"], default="both", help="default: both")
    v.add_argument("--level", metavar="NAME[,NAME]", help=f"fly these levels instead of Corneria's sections, or all: "
                   f"{', '.join(VIEW_LEVELS)} (#129)")
    v.add_argument("--cvar", action="append", default=[], metavar="NAME[=VALUE]", help="another CVar for both runs")
    v.add_argument("--skew", type=int, default=VIEW_SYS_FRAME_SKEW, help="frames since boot the client with index 1 "
                   f"is ahead by, 0 for none (default {VIEW_SYS_FRAME_SKEW}, #129)")
    v.add_argument("--audio", choices=("split", "same"), default="split",
                   help="split: index 0 renders the high sample count per audio update, index 1 the low (default); "
                   "same: the client's own choice (#129)")
    v.add_argument("--players", type=int, default=2, help="players, through gCoopDebugPlayerCount (default 2)")
    v.add_argument("--pilot", action="store_true", help="a pilot flies each --level to its end, boss included, and "
                   f"the two clients play what it flew (default level: all of {', '.join(PILOT_LEVELS)}; #181)")
    v.add_argument("--mortal", action="store_true", help="with --pilot: without gInvincible, so a player can go down")
    v.add_argument("--frames", type=int, help="replay length (default: what the section needs, or "
                   f"{VIEW_LEVEL_FRAMES} past when player 0 flies for a level; with --pilot the most frames it flies, "
                   f"by default {PILOT_FRAMES})")
    v.add_argument("--exe", help="Starship executable (default: newest build found)")
    v.add_argument("--timeout", type=float, default=1800, help="per run: give up after this many seconds")
    c = sub.add_parser("slots", help="play a level with 1 player and with several and compare the effect slots in use")
    c.add_argument("--level", default="katina,fortuna,corneria", metavar="NAME[,NAME]",
                   help=f"levels, or all: {', '.join(VIEW_LEVELS)} (default katina,fortuna,corneria)")
    c.add_argument("--players", type=int, default=2, help="players, through gCoopDebugPlayerCount (default 2)")
    c.add_argument("--frames", type=int, help=f"replay length (default {VIEW_LEVEL_FRAMES} past when player 0 flies)")
    c.add_argument("--rule-test", action="store_true", help="check the rule on made-up samples, with no game (#284)")
    c.add_argument("--exe", help="Starship executable (default: newest build found)")
    c.add_argument("--timeout", type=float, default=1800, help="per run: give up after this many seconds")
    u = sub.add_parser("builds", help="play one replay on two Release runs and a Debug run and compare the hashes")
    u.add_argument("--debug", help="Starship executable of the Debug build")
    u.add_argument("--release", help="Starship executable of the Release build of the same commit")
    u.add_argument("--exe", help="for replay_test.py: one build as both, where the check of the draws is what it reads")
    u.add_argument("--section", choices=[*VIEW_SECTIONS, "both"], default="both", help="default: both")
    u.add_argument("--players", type=int, default=2, help="players, through gCoopDebugPlayerCount (default 2)")
    u.add_argument("--frames", type=int, help="replay length (default: what the section needs)")
    u.add_argument("--timeout", type=float, default=1800, help="per run: give up after this many seconds")
    t = sub.add_parser("rates", help="play one replay at several frame rates and compare the hashes")
    t.add_argument("--rates", default="30,60,120,144,360",
                   help="gInterpolationFPS values, the first is the one to match (default: 30,60,120,144,360)")
    t.add_argument("--section", choices=[*VIEW_SECTIONS, "both"], default="both", help="default: both")
    t.add_argument("--players", type=int, default=2, help="players, through gCoopDebugPlayerCount (default 2)")
    t.add_argument("--frames", type=int, help="replay length (default: what the section needs)")
    t.add_argument("--jobs", type=int, default=3, help="games at a time (default 3)")
    t.add_argument("--speed", help="anything but real time must fail this check (default: real time)")
    t.add_argument("--exe", help="Starship executable (default: newest build found)")
    t.add_argument("--timeout", type=float, default=3600, help="per run: give up after this many seconds")
    w = sub.add_parser("draw", help="play one replay with and without drawing and compare the hashes")
    w.add_argument("--section", choices=[*VIEW_SECTIONS, "both"], default="both", help="default: both")
    w.add_argument("--start", choices=[*replay_tool.STARTS, "all"], default="all", help="default: all")
    w.add_argument("--players", type=int, default=2, help="players, through gCoopDebugPlayerCount (default 2)")
    w.add_argument("--frames", type=int, help="replay length (default: what the section needs)")
    w.add_argument("--exe", help="Starship executable (default: newest build found)")
    w.add_argument("--timeout", type=float, default=1800, help="per run: give up after this many seconds")
    e = sub.add_parser("base", help="play input by rule on the build before a change and this one, and compare")
    e.add_argument("--base", required=True, help="Starship executable of the build before the change")
    e.add_argument("--level", metavar="NAME", help="start both games in this level instead of coming through the menus "
                   "(#283): " + ", ".join(BASE_LEVELS))
    e.add_argument("--frames", type=int, help=f"frames from when player 0 flies (default {GENERATED_END})")
    e.add_argument("--exe", help="Starship executable (default: newest build found)")
    e.add_argument("--timeout", type=float, default=1800, help="per run: give up after this many seconds")
    s = sub.add_parser("start", help="what a start in the level leaves different from coming through the menus")
    s.add_argument("--players", type=int, default=2, help="players, through gCoopDebugPlayerCount (default 2)")
    s.add_argument("--frames", type=int, help=f"frames from when player 0 flies (default {GENERATED_END})")
    s.add_argument("--exe", help="Starship executable (default: newest build found)")
    s.add_argument("--timeout", type=float, default=900, help="per run: give up after this many seconds")
    args = p.parse_args()
    if args.cmd == "builds" and not ((args.debug and args.release) or args.exe):
        p.error("builds needs --debug and --release")
    commands = {"run": cmd_run, "diff": cmd_diff, "probe": cmd_probe, "views": cmd_views, "slots": cmd_slots, "builds": cmd_builds,
                "rates": cmd_rates, "draw": cmd_draw, "base": cmd_base, "start": cmd_start}
    if args.cmd != "draw":
        return commands[args.cmd](args)
    owned = OwnedFolders(replay_tool.ROOT / "local")  # see draw at the top of this file (#308)
    code = commands[args.cmd](args)
    if code == 0 and not owned.outer:
        owned.delete()
    else:
        owned.close()
    return code


if __name__ == "__main__":
    sys.exit(main())
