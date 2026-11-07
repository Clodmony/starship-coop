#!/usr/bin/env python3
"""Record or play back controller input for Starship Co-op tests (WP-03).

    python tools/coop/replay.py record local/replays/corneria.sfcr   # you play; close the window to stop
    python tools/coop/replay.py play   local/replays/corneria.sfcr   # unattended; prints the result
    python tools/coop/replay.py record local/replays/idle.sfcr --frames 300   # unattended; nobody plays, stops itself

Every run gets a fresh folder under local/ (config, save and logs start empty) and SHIP_HOME set to that folder, so a
recording and its playback start from the same state and the owner's own setup is never touched, even when SHIP_HOME
is set in the shell. The game is told what to do through the SF64_COOP_REPLAY_* environment variables
(src/port/coop/test/replay.h). Playback exits 0 on PASS, 1 otherwise: only a whole file can pass, so a file with no
frames or a cut last frame fails. A game that ends on its own with a code other than 0 crashed, whatever its report
says: the result is then CRASH (#116). python tools/coop/replay_test.py and crash_test.py test all this.

A game started from here makes no sound, opens no window on Linux and runs at TEST_SPEED: see game_env. Only record
without --frames runs as a person plays it. To watch a playback, set SDL_VIDEODRIVER and SDL_AUDIODRIVER in the shell
and pass --speed 1.

A replay file holds raw structs: it only plays back on the same build and platform it was recorded on.

--start level or flying records a game that starts straight in Corneria, through SF64_COOP_TEST_START_LEVEL
(src/port/coop/test/start_level.h): flying is past the intro too. See Start below. A replay with checkpoints holds the
frames it was made on, so play takes where it starts from the file; only a replay by rule needs to be told.
"""
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import time
from collections import namedtuple
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_folder import make_run_folder  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
EXE_CANDIDATES = [
    "build/x64/Release/Starship.exe",
    "build/x64/RelWithDebInfo/Starship.exe",
    "build/x64/Debug/Starship.exe",
    "build-cmake/Starship",
    "build-cmake/Starship.app/Contents/MacOS/Starship",
]
DATA_FILES = ["sf64.o2r", "starship.o2r", "gamecontrollerdb.txt"]
HEADER_SIZE = 40  # ReplayHeader in src/port/coop/test/replay.c
TEST_SPEED = "max"  # no frame limit for a test game: every test here counts frames, not seconds (#137)
REAL_TIME = {"SF64_COOP_TEST_SPEED": "1"}  # as env_extra, for a run that needs wall-clock time; say why next to it
RENDER_THREADS = "3"  # for a software renderer: with its default, games side by side slow each other down (#137)

# include/sf64thread.h, include/sf64player.h
GS_TITLE, GS_MENU, GS_MAP, GS_PLAY = 2, 3, 4, 7
PLAYER_ACTIVE = 3
LEVEL_CORNERIA = 0  # include/sf64level.h

# What a replay starts from. A game that comes through the menus with GENERATED_SEEDS and input by rule enters Corneria
# with START_SEEDS, so a start in the level from those seeds plays that game, frame for frame (hash_check.py start).
GENERATED_SEEDS = (1234, 5678, 9012, 111, 222, 333)
START_SEEDS = (741, 16142, 16930, 6521, 10732, 6721)
FIRST_PRESS = 60  # pad 0's first A: this far into the title screen
FIRE_PERIOD = 30
FIRE_PHASE = 25  # counted from when player 0 flies: on MENUS, this makes it frames 60, 90, 120 and so on


class Start(namedtuple("Start", "name level_id skip_intro level active seeds")):
    """Where a test game starts, and what that does to the frames of a replay, which count from GSTATE_START wherever
    the game goes from there. level is the frame of level frame 0, active the frame player 0 flies. Input by rule counts
    from active, so one rule fits every start, and the same rule on MENUS is the input it always was.

    Each tool says which one it plays: the table in docs/coop/QA.md. LEVEL goes through the intro and FLYING skips it:
    a level's intro is real simulation, and only LEVEL and MENUS run it. These numbers are Corneria's."""

    @property
    def env(self):
        """What the game has to be told."""
        if self.level_id is None:
            return {}
        env = {"SF64_COOP_TEST_START_LEVEL": str(self.level_id)}
        return {**env, "SF64_COOP_TEST_START_SKIP_INTRO": "1"} if self.skip_intro else env

    @property
    def report(self):
        """What the replay report says this start is (CoopTestStart_Name)."""
        if self.level_id is None:
            return "menus"
        return f"level:{self.level_id}{':skip-intro' if self.skip_intro else ''}"

    def frame(self, flight):
        """The replay frame of a frame counted from active."""
        return self.active + flight

    def fires(self, flight):
        """Pad 0 presses A every 30 frames: that gets through the menus into the level, and there it keeps firing."""
        return self.frame(flight) >= FIRST_PRESS and flight % FIRE_PERIOD == FIRE_PHASE


MENUS = Start("menus", None, False, 3101, 4775, GENERATED_SEEDS)
LEVEL = Start("level", LEVEL_CORNERIA, False, 10, 1684, START_SEEDS)
FLYING = Start("flying", LEVEL_CORNERIA, True, 10, 14, START_SEEDS)
STARTS = {start.name: start for start in (MENUS, LEVEL, FLYING)}


def find_exe(given):
    if given:
        return Path(given).resolve()
    found = [ROOT / c for c in EXE_CANDIDATES if (ROOT / c).exists()]
    if not found:
        sys.exit("replay: no Starship build found; run python tools/coop/build.py --assets first, or pass --exe")
    return max(found, key=lambda p: p.stat().st_mtime)


def find_data(exe, name):
    for d in (exe.parent, exe.parent.parent, ROOT / "build/x64", ROOT / "build-cmake", ROOT):
        if (d / name).exists():
            return d / name
    return None


def make_run_dir(exe, mode):
    run = make_run_folder(ROOT / "local", f"test-replay-{mode}-")
    for name in DATA_FILES:
        src = find_data(exe, name)
        if src is None:
            if name.endswith(".o2r"):
                sys.exit(f"replay: {name} not found; run python tools/coop/build.py --assets first")
            continue
        shutil.copy2(src, run / name)
    return run


def replay_frame_count(replay):
    """Whole frames in a replay file: a header holding the frame size at byte 12, then one record per frame. A file
    too short to hold that header has none."""
    path = Path(replay)
    data = path.read_bytes() if path.exists() else b""
    if len(data) < HEADER_SIZE:
        return 0
    frame_size = struct.unpack_from("<I", data, 12)[0]
    return (len(data) - HEADER_SIZE) // frame_size if frame_size else 0


def recorded_start(replay):
    """Where a replay with checkpoints was made, read from them, as it only plays from there. One by rule holds none and
    says MENUS here, so whoever writes it has to say where it starts."""
    path = Path(replay)
    data = path.read_bytes() if path.exists() else b""
    if len(data) < HEADER_SIZE:
        return MENUS
    pad_size, frame_size = struct.unpack_from("<II", data, 8)
    frames = (len(data) - HEADER_SIZE) // frame_size if frame_size else 0
    state = HEADER_SIZE + 4 + 8 * pad_size  # ReplayFrame: frame number, then 8 pads, then the checkpoint
    seen = [struct.unpack_from("<2ixxxxi", data, state + n * frame_size)
            for n in range(min(frames, FIRST_PRESS))]
    if len(seen) <= LEVEL.level or seen[LEVEL.level][0] != GS_PLAY:
        return MENUS
    level_id = seen[LEVEL.level][1]
    skip_intro = any(player == PLAYER_ACTIVE for _, _, player in seen)
    return Start(f"recorded in level {level_id}", level_id, skip_intro, LEVEL.level, None, None)


def read_report(path):
    if not path.exists():
        return None
    values = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        key, sep, value = line.partition("=")
        if sep:
            values[key] = value
    return values if "result" in values else None


def game_env(run, person_plays=False):
    """The environment for a game in folder run: this one with SHIP_HOME at that folder and without any SF64_COOP_*
    variable, as one left in the shell would change a test with no sign in its result.

    A test game is silent and has no window, also from a bare shell: a driver the shell sets still wins. The offscreen
    driver is for Linux only, as the default backend on Windows draws into a real window, and so are software rendering
    and RENDER_THREADS. It runs at TEST_SPEED, which env_extra can replace (REAL_TIME). With person_plays someone needs
    to see, hear and fly the game, so it gets none of that. The caller adds what the run needs."""
    env = {key: value for key, value in os.environ.items() if not key.startswith("SF64_COOP_")}
    env["SHIP_HOME"] = str(run)
    if not person_plays:
        env.setdefault("SDL_AUDIODRIVER", "dummy")
        if sys.platform.startswith("linux"):
            env.setdefault("SDL_VIDEODRIVER", "offscreen")
            env.setdefault("LIBGL_ALWAYS_SOFTWARE", "1")  # off the GPU other programs on this PC share
            env.setdefault("GALLIUM_DRIVER", "llvmpipe")
            env.setdefault("LP_NUM_THREADS", RENDER_THREADS)
        env["SF64_COOP_TEST_SPEED"] = TEST_SPEED
    return env


def run_game(mode, replay, exe, timeout, hash_log=False, poke=None, tag=None, env_extra=None, config=None, frames=None,
             start=None):
    """Start one game in a fresh run folder and wait for the replay report.

    Returns (report values or None, run folder, seconds). With hash_log the run also writes state-hash.txt into the
    run folder (src/port/coop/sync/state_hash.h); poke flips one byte of game state at that frame. env_extra adds
    environment variables (for example the test switches in src/port/coop/test/switches.h); config is a dict written
    as the run folder's starship.cfg.json before the game starts (graphics backend, window size, CVars). With frames a
    recording stops once the file holds that many; it can hold more, as the game writes in blocks and this looks once a
    second. start is where the game starts: a Start, by default where the replay was made, and for a recording MENUS. A
    game that says it started elsewhere ends with result WRONG_START, as an input by rule plays on either way.
    """
    start = start or (recorded_start(replay) if mode == "play" else MENUS)
    run = make_run_dir(exe, mode if tag is None else f"{mode}-{tag}")
    if config is not None:
        (run / "starship.cfg.json").write_text(json.dumps(config, indent=4), encoding="utf-8")
    report = run / "replay-report.txt"
    env = game_env(run, person_plays=mode == "record" and frames is None)
    env["SF64_COOP_REPLAY_RECORD" if mode == "record" else "SF64_COOP_REPLAY_PLAY"] = str(replay)
    env["SF64_COOP_REPLAY_REPORT"] = str(report)
    env.update(start.env)
    if hash_log:
        env["SF64_COOP_HASH_LOG"] = str(run / "state-hash.txt")
        if poke is not None:
            env["SF64_COOP_HASH_POKE"] = str(poke)
    env.update(env_extra or {})

    print(f"replay: {mode} {replay}")
    print(f"replay: exe {exe}")
    print(f"replay: run folder {run}")
    if mode == "record" and frames is None:
        print("replay: play now; close the game window to stop recording")
    began = time.monotonic()
    proc = subprocess.Popen([str(exe)], cwd=run, env=env)

    values = None
    ended_itself = True
    try:
        while True:
            if mode == "play":
                values = read_report(report)
                if values is not None:
                    break
            elif frames is not None and replay_frame_count(replay) >= frames:
                break
            if (mode == "play" or frames is not None) and time.monotonic() - began > timeout:
                print("replay: timeout")
                break
            if proc.poll() is not None:
                break
            time.sleep(1)
    finally:
        ended_itself = stop_game(proc)

    if values is None:
        time.sleep(1)
        values = read_report(report)
    if values is None:
        print(f"replay: no report written (exit code {proc.returncode}); see {run / 'logs'}")
    elif crashed(proc, ended_itself):
        print(f"replay: the game ended on its own with exit code {proc.returncode}, after its report said "
              f"{values['result']}; see {run / 'logs'}")
        values = {**values, "result": "CRASH", "exit_code": str(proc.returncode)}
    else:
        check_start(values, start)
    return values, run, time.monotonic() - began


def check_start(values, start):
    """A build older than the switch writes no start, and starts in the menus."""
    said = values.get("start", MENUS.report)
    if said != start.report:
        print(f"replay: this game started at {said}, not at {start.report}")
        values["result"] = "WRONG_START"


def stop_game(proc):
    """Stop a game this script started, if it still runs. Returns whether it had already ended on its own."""
    if proc.poll() is not None:
        return True
    # Only the process this script started.
    proc.terminate()
    try:
        proc.wait(timeout=15)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    return False


def crashed(proc, ended_itself):
    """Whether a game that stop_game has seen to the end crashed (#116). A crashed test game still writes its report,
    which can say RECORDED or PASS, and then ends with exit code 1 (src/port/coop/test/stop_signal.c), or with what
    the system gives a crash. A game stopped by stop_game did not crash, although on Windows its code is 1 too."""
    return ended_itself and proc.returncode != 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("mode", choices=["record", "play"])
    p.add_argument("file", help="replay file (keep it under local/, it is made from your game)")
    p.add_argument("--exe", help="Starship executable (default: newest build found)")
    p.add_argument("--timeout", type=float, default=1800,
                   help="playback and record --frames: give up after this many seconds")
    p.add_argument("--frames", type=int, help="record: stop after this many frames, so nobody has to close the window")
    p.add_argument("--hash-log", action="store_true", help="also write state-hash.txt into the run folder (WP-04)")
    p.add_argument("--speed", help=f"times real time, 1 for real time, max for no limit (default {TEST_SPEED}; record "
                                   "without --frames always runs in real time)")
    p.add_argument("--start", choices=STARTS, help="where the game starts (default: record menus, play where the file "
                                                   "was made)")
    args = p.parse_args()

    exe = find_exe(args.exe)
    replay = Path(args.file).resolve()
    if args.mode == "play" and not replay.exists():
        sys.exit(f"replay: {replay} does not exist")
    replay.parent.mkdir(parents=True, exist_ok=True)

    speed = None if args.speed is None else {"SF64_COOP_TEST_SPEED": args.speed}
    values, run, elapsed = run_game(args.mode, replay, exe, args.timeout, hash_log=args.hash_log, frames=args.frames,
                                    env_extra=speed, start=STARTS.get(args.start))
    if values is None:
        print(f"replay: FAILED ({elapsed:.0f}s)")
        return 1

    for key in ("start", "frames", "recorded_frames", "first_mismatch", "mismatch_want", "mismatch_got",
                "level_complete_frame", "level_complete_level", "final"):
        if key in values:
            print(f"  {key}: {values[key]}")
    print(f"replay: {values['result']} ({args.mode}, {elapsed:.0f}s)")
    if args.mode == "play":
        return 0 if values["result"] == "PASS" else 1
    return 0 if values["result"] == "RECORDED" else 1


if __name__ == "__main__":
    sys.exit(main())
