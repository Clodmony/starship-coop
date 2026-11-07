#!/usr/bin/env python3
"""Determinism spike for Starship Co-op (WP-02).

    python tools/coop/determinism.py local/replays/corneria.sfcr
    python tools/coop/determinism.py local/replays/corneria.sfcr --only opengl,no-fma3
    python tools/coop/determinism.py --only speed      # input by rule, Corneria's rails into all-range

Plays one replay with the per-frame state hash (tools/coop/hash_check.py) under conditions that can differ between
two machines running the same build, and compares every run's hash log with a baseline:

  phase 1, alone      base      default settings (DirectX 11 on Windows), the only game running
  phase 2, together   base-2    default settings again, later, and with all the runs below at the same time
                      opengl    OpenGL instead of the default graphics backend
                      window    1280x960 window, vsync off, frame interpolation at 144 fps
                      audio-low, audio-high
                                audio rendered with a fixed sample count per update
                                (SF64_COOP_TEST_AUDIO_SAMPLES, src/port/coop/test/switches.h)
                      no-fma3   C runtime math without FMA3 (SF64_COOP_TEST_NO_FMA3, Windows x64 only)
                      speed     faster than real time (SF64_COOP_TEST_SPEED, #137), and drawing
                      no-draw   as every other test tool runs: faster than real time, no drawing (#147)

All but speed and no-draw run in real time, which is what this tool compares: frame pacing and the audio device's fill
level follow wall-clock time, and at test speed each game frame is drawn once and the audio buffer stays full, so those
variants would no longer vary them. Frame interpolation only runs in real time too, and a bug there can reach the game
state: a stack underflow in it corrupted the RNG seeds in Release (#141). So this tool stays the one that plays as a
person does, and speed against base is interpolation off against on. replay_test.py fails if these games log a speed.
A game that did not make the draws its variant asks for fails, whatever its hash says.

Without a replay file it plays input by rule (hash_check.py, write_generated_replay). Such a replay has no checkpoints,
so a run there only has to play through.

This tool keeps the whole path from boot, through the menus and the map, as they are simulation too and no faster start
may take them from it (#148): a game that reaches a level any other way fails it, and so does a replay made in a level.

Each run gets a fresh folder under local/ (see tools/coop/replay.py); the owner's config, saves and mods are never
used. A run passes when its replay passes and its hash equals the baseline on every recorded frame. Exit code 0 when
every run passes, 1 otherwise.
"""
import argparse
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hash_check  # noqa: E402
import replay as replay_tool  # noqa: E402

# name: (environment variables, starship.cfg.json contents or None for the defaults)
VARIANTS = {
    "base-2": ({}, None),
    "opengl": ({}, {"Window": {"Backend": {"Id": 1, "Name": "OpenGL"}}}),
    "window": ({}, {"Window": {"Width": 1280, "Height": 960},
                    "CVars": {"gVsyncEnabled": 0, "gInterpolationFPS": 144}}),
    "audio-low": ({"SF64_COOP_TEST_AUDIO_SAMPLES": "low"}, None),
    "audio-high": ({"SF64_COOP_TEST_AUDIO_SAMPLES": "high"}, None),
    "no-fma3": ({"SF64_COOP_TEST_NO_FMA3": "1"}, None),
    "speed": ({"SF64_COOP_TEST_SPEED": replay_tool.TEST_SPEED}, None),
    "no-draw": ({"SF64_COOP_TEST_SPEED": replay_tool.TEST_SPEED,
                 "SF64_COOP_TEST_NO_DRAW": replay_tool.NO_DRAW}, None),
}
if sys.platform != "win32":
    del VARIANTS["no-fma3"]
SPEED_DRAWS = 1  # draws of a game frame at test speed, which has no frame interpolation
WHOLE_PATH = (replay_tool.GS_TITLE, replay_tool.GS_MENU, replay_tool.GS_MAP)  # on the way to a level


def play(replay, exe, timeout, name, start, env_extra=None, config=None):
    env = {**replay_tool.REAL_TIME, **(env_extra or {})}  # why: see the top of this file
    values, names, rows = hash_check.play(replay, exe, timeout, f"det-{name}", env_extra=env, config=config,
                                          start=start)
    return name, values, names, rows


def with_variant(config, variant):
    """The run's config with a variant's on top, CVars from both."""
    if not config or not variant:
        return config or variant
    return {**config, **variant, "CVars": {**config.get("CVars", {}), **variant.get("CVars", {})}}


def check(name, values, rows, base_rows, names, recorded, played):
    """Return a list of failure strings for one run (empty when it passes)."""
    if rows is None:
        return [f"{name}: no hash log written"]
    failures = []
    if name != "no-draw":
        want = SPEED_DRAWS if name == "speed" else hash_check.INTERPOLATED
        failures += hash_check.too_few_draws(name, (values,), want)
    elif values and hash_check.read_draws(values["log"]):
        failures.append(f"{name}: drew all the same")
    if not played(values):
        failures.append(f"{name}: replay did not pass: {values.get('result') if values else 'no report'}")
    if len(rows) < recorded:
        failures.append(f"{name}: hash log shorter than the replay ({len(rows)} < {recorded} frames)")
    diff = hash_check.first_difference(base_rows, rows, recorded)
    if diff is not None:
        failures.append(f"{name}: differs from base at {hash_check.describe(names, diff)}")
    return failures


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("file", nargs="?", help="replay file (from tools/coop/replay.py record); default: input by rule")
    p.add_argument("--exe", help="Starship executable (default: newest build found)")
    p.add_argument("--timeout", type=float, default=2400, help="per run: give up after this many seconds")
    p.add_argument("--only", help="comma-separated variants for phase 2 (default: all: " + ", ".join(VARIANTS) + ")")
    args = p.parse_args()

    exe = replay_tool.find_exe(args.exe)
    args.players = 1
    args.start = replay_tool.MENUS.name
    replay, config, start = hash_check.test_setup(args)
    played = hash_check.played_through
    if args.file is not None:
        played = lambda values: bool(values) and values.get("result") == "PASS"  # noqa: E731
    chosen = list(VARIANTS) if not args.only else [v.strip() for v in args.only.split(",") if v.strip()]
    unknown = [v for v in chosen if v not in VARIANTS]
    if unknown:
        sys.exit(f"det: unknown variant(s) {', '.join(unknown)}; known: {', '.join(VARIANTS)}")

    began = time.monotonic()
    print("det: phase 1: base, alone")
    _, base_values, names, base_rows = play(replay, exe, args.timeout, "base", start, config=config)
    problem = base_rows and hash_check.whole_path_problem(base_rows, WHOLE_PATH)
    if problem:
        print(f"FAIL: base: {problem}\ndet: FAIL")
        return 1
    if base_rows is None or not played(base_values):
        print(f"det: base run failed: {base_values.get('result') if base_values else 'no report'}")
        print("det: FAIL")
        return 1
    no_draws = hash_check.too_few_draws("base", (base_values,))
    if no_draws:
        print(f"FAIL: {no_draws[0]}")
        print("det: FAIL")
        return 1
    recorded = int(base_values.get("recorded_frames", len(base_rows)))
    print(f"det: base: {len(base_rows)} frames hashed, {recorded} recorded")

    print(f"det: phase 2: {', '.join(chosen)}, together")
    with ThreadPoolExecutor(max_workers=len(chosen)) as pool:
        jobs = [pool.submit(play, replay, exe, args.timeout, name, start, VARIANTS[name][0],
                            with_variant(config, VARIANTS[name][1])) for name in chosen]
        results = [job.result() for job in jobs]

    failures = []
    for name, values, _, rows in results:
        f = check(name, values, rows, base_rows, names, recorded, played)
        print(f"det: {name}: {'FAIL' if f else 'PASS'}" + ("" if f else f" (equal to base on all {recorded} frames)"))
        failures += f

    for f in failures:
        print(f"FAIL: {f}")
    print(f"det: {len(chosen) - len({f.split(':')[0] for f in failures})}/{len(chosen)} variants equal to base, "
          f"{time.monotonic() - began:.0f}s")
    print("det: FAIL" if failures else "det: PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
