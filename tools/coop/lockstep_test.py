#!/usr/bin/env python3
"""Real game clients in lockstep (WP-22, #20): two games over loopback, each with its own folder, playing one game.

    python tools/coop/lockstep_test.py                     # sim, then every game scenario but the long ones
    python tools/coop/lockstep_test.py --only sim          # no game, no ROM
    python tools/coop/lockstep_test.py --only steer,pause
    python tools/coop/lockstep_test.py --only full         # all of Corneria, about 10 minutes
    python tools/coop/lockstep_test.py --list
    python tools/coop/lockstep_test.py --only release --feed <lockstep log> --release-exe <Starship>

Every game gets a fresh folder under local/ with its own SHIP_HOME, plays a replay with no input so no controller reaches
it, and takes its input from src/port/coop/test/lockstep_driver.h, which writes port 0 before the input layer reads the
local controller. From there on the input goes the road a pad's does: stamped with a frame, delayed, sent, read back.

What counts is the state hash log from the first frame after Play_Init on, every frame and every region, as in
join_test.py. A lockstep log of inputs and game state per frame has to be equal as well. A game that plays alone cannot
pass: a client's input reaches the host only through lockstep, and `control` shows the same run differs without it.

  sim       src/port/coop/sync/lockstep.c alone, in a virtual network with loss, lag, reordering and duplicates, for two to
            four players (tools/coop/locksteptest).
  steer     each client flies its own player. Hashes equal, each game looks through its own player.
  control   the same run with SF64_COOP_TEST_NO_LOCKSTEP: it must differ, or this test says nothing.
  pause     Start on either client pauses both on the same frame, Start on either resumes.
  save      a host whose save has Corneria cleared presses Start in the level intro: no client skips it, as the skip reads
            a save the clients do not share.
  zero_seed a seed zeroed on both clients: no clock read, so they stay equal.
  slow      one game takes much longer on some frames: the other one waits, and nothing differs.
  poke      a bit flipped on one client shows at exactly that frame, and nowhere before.
  lag       the clients talk through the chaos relay, with lag, jitter and a little loss.
  delay     another input delay: the same flight, the same hashes.
  left      a client is killed: the host says so, ends the session and plays on.
  host_left the same with the host.
  leave     a player leaves through the real menu: the other reads why, in a window it never opened.
  stall     a client stops sending input with its connection up: the session ends after COOP_LOCKSTEP_STALL_MS.
  frozen    a client stops dead (SIGSTOP): the session ends on the transport's timeout, and it can be thawed.
  menu      the whole way through the real menu, with real widgets.
  full      all of Corneria: pilots fly it live, one on each client, then plain runs repeat what they flew. Takes a while.
  release   the plain run on a Release build (--release-exe), which has to hash as the Debug run did.
  rates     the plain run at real time, with the two clients at different gInterpolationFPS.

Prints one line per scenario and a RESULT line, exits 0 only when everything passed. Needs a build with assets.
"""
import argparse
import json
import math
import os
import platform
import re
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(ROOT / "tools" / "coop" / "chaos"))
import full_level  # noqa: E402
import hash_check  # noqa: E402
import join_test  # noqa: E402
import menu_test  # noqa: E402
import player_trace  # noqa: E402
import replay as replay_tool  # noqa: E402

IS_LINUX = platform.system() == "Linux"
BUTTON_A, BUTTON_START = 0x8000, 0x1000
START_FRAME = 600
REPLAY_FRAMES = 30000  # longer than any run: the games stop on `done`, not at the end of this
INPUT_DELAY = 3  # COOP_LOCKSTEP_INPUT_DELAY
TIMEOUT_MS = 30000  # the join driver's transport timeout
STALL_SECONDS = 10  # COOP_LOCKSTEP_STALL_MS
SEEDS = "1111,2222,3333,4444,5555,6666"  # every level start of a run: the flight then depends on nothing but the input
JOIN_FRAME = 1700  # a little after both players fly: level frames, 1674 in NOTES.md, #142
SHORT_FRAMES = 2700
LEVEL_COMPLETE, MAP, PLAY_PAUSE = 7, 4, 100  # PLAYERSTATE_LEVEL_COMPLETE, GSTATE_MAP, PLAY_PAUSE
MAP_TAIL = 60
FLY_FRAMES = 14000  # Corneria took 11200 frames from level start to the map in #142, the rest is room


class Fail(Exception):
    pass


def expect(condition, text):
    if not condition:
        raise Fail(text)


def pad_columns(button, x, y, last_button):
    return f"{button} {x} {y} {button & ~last_button} {x} {y}"


def write_feed(path, frames, script):
    """A feed for lockstep_driver.h: script(frame, player) gives (button, stick x, stick y) for each frame."""
    last = [0, 0, 0, 0]
    lines = []
    for frame in range(frames):
        columns = []
        for player in range(4):
            button, x, y = script(frame, player)
            columns.append(pad_columns(button, x, y, last[player]))
            last[player] = button
        lines.append(f"{frame} 0 0 " + " ".join(columns))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def sway(frame, player):
    """Both players steer, against each other, and fire. Only what flies can show in the hash, so from the join on."""
    if frame < JOIN_FRAME:
        return 0, 0, 0
    side = 1 if player == 0 else -1
    x = int(60 * side * math.sin(frame * 0.05))
    y = int(30 * math.sin(frame * 0.03 + player))
    button = BUTTON_A if frame % (7 + 2 * player) == 0 else 0
    return button, x, y


class Client(join_test.LevelLog):
    """One game. It joins through the join driver unless menu is set: then menu_test.Game clicks the real widgets."""

    def __init__(self, args, folder, name, env):
        self.name = name
        self.run = replay_tool.make_run_dir(args.exe, f"lockstep-{name}")
        self.report_path = self.run / "join-report.txt"
        self.sync_report = self.run / "lockstep-report.txt"
        self.sync_log = self.run / "lockstep-log.txt"
        replay = self.run / "input.sfcr"
        join_test.write_replay(replay, REPLAY_FRAMES, join_test.SEEDS.get(name, join_test.SEEDS["client0"]))
        self.env = replay_tool.game_env(self.run)
        self.env.update({
            "SF64_COOP_REPLAY_PLAY": str(replay),
            "SF64_COOP_REPLAY_REPORT": str(self.run / "replay-report.txt"),
            "SF64_COOP_HASH_LOG": str(self.run / "state-hash.txt"),
            "SF64_COOP_JOIN_REPORT": str(self.report_path),
            "SF64_COOP_LOCKSTEP_REPORT": str(self.sync_report),
            "SF64_COOP_LOCKSTEP_LOG": str(self.sync_log),
            "SF64_COOP_TRACE": str(self.run / "trace.txt"),
        })
        self.env.update(env)
        self.log = open(folder / f"{name}.log", "wb")
        self.proc = None
        self.exe = args.exe

    def start(self, config=None):
        if config is not None:
            (self.run / "starship.cfg.json").write_text(json.dumps(config, indent=4), encoding="utf-8")
        self.proc = subprocess.Popen([str(self.exe)], cwd=self.run, env=self.env, stdout=self.log,
                                     stderr=subprocess.STDOUT)

    def sync(self):
        return join_test.read_report(self.sync_report)

    def stop(self, kill=False):
        if self.proc is not None and self.proc.poll() is None:
            # Only the process this script started.
            if kill:
                self.proc.kill()
            else:
                self.proc.terminate()
            try:
                self.proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()

    def close(self):
        self.stop()
        self.log.close()


def session(args, folder, host_env=None, client_env=None, both=None, config=None, client_config=None, client_port=None):
    """A host and a client through the join driver, on their way to a level. Returns them started."""
    folder.mkdir(parents=True, exist_ok=True)
    port_file = folder / "port"
    env = dict(both or {})
    env["SF64_COOP_TEST_START_SEEDS"] = SEEDS
    host = Client(args, folder, "host", {**env, "SF64_COOP_JOIN_PORT_FILE": str(port_file), "SF64_COOP_JOIN_CLIENTS": "1",
                                         "SF64_COOP_JOIN_LEVEL": "0", "SF64_COOP_JOIN_START_FRAME": str(START_FRAME),
                                         **(host_env or {})})
    host.start(config)
    deadline = time.monotonic() + 180
    while not (port_file.exists() and port_file.read_text(encoding="utf-8").strip()):
        expect(host.proc.poll() is None and time.monotonic() < deadline, "the host did not report its port")
        time.sleep(0.2)
    port = port_file.read_text(encoding="utf-8").strip()
    if client_port is not None:
        port = client_port(port)
    client = Client(args, folder, "client0", {**env, "SF64_COOP_JOIN_CONNECT": f"127.0.0.1:{port}",
                                              "SF64_COOP_JOIN_PICKS": "falco", **(client_env or {})})
    client.start(client_config if client_config is not None else config)
    return host, client


def wait_for(games, word, seconds, what=None):
    """Until every game's lockstep report says word."""
    deadline = time.monotonic() + seconds
    while True:
        pending = [game for game in games if not join_test.said(game.sync(), word)]
        if not pending:
            return
        for game in pending:
            expect(game.proc.poll() is None, f"{game.name} ended with code {game.proc.returncode} before `{word}`")
        expect(time.monotonic() < deadline,
               f"no `{word}` from {', '.join(g.name for g in pending)} after {seconds} s {what or ''}")
        time.sleep(0.5)


def compare(host, client, frames=None):
    """Hash logs and lockstep logs equal, frame by frame. Returns the number of frames compared."""
    names, _, ours = host.rows()
    _, _, theirs = client.rows()
    n = min(len(ours), len(theirs))
    if frames is not None:
        expect(n >= frames, f"only {n} frames in the level, want {frames}")
        n = frames
    difference = join_test.first_difference(names, ours[:n], theirs[:n])
    expect(difference is None, f"the hash logs differ at {difference}")
    a = host.sync_log.read_text(encoding="utf-8").splitlines()
    b = client.sync_log.read_text(encoding="utf-8").splitlines()
    m = min(len(a), len(b))
    for i in range(m):
        expect(a[i] == b[i], f"the lockstep logs differ at frame {i}: host {a[i]}, client {b[i]}")
    return n


def view_of(game):
    """(frame, player looked through) of every `# view` line of the hash log, in the level."""
    found = []
    for line in (game.run / "state-hash.txt").read_text(encoding="utf-8", errors="replace").splitlines():
        match = re.match(r"# view frame=(\d+) player=(\d+)", line)
        if match:
            found.append((int(match.group(1)), int(match.group(2))))
    return found


def flown(game):
    """Per player, how far it moved across the path in all, from the trace of this game."""
    rows = player_trace.parse(game.run / "trace.txt")
    moved = [0.0, 0.0]
    for a, b in zip(rows, rows[1:]):
        for i in range(min(2, len(a.players), len(b.players))):
            if not a.players[i].parked and not b.players[i].parked:
                moved[i] += abs(b.players[i].x - a.players[i].x)
    return moved


def played(game):
    held = join_test.said(game.sync(), "done")
    return f"{game.name} held {held[0]['held']} frames, {held[0]['held_ms']} ms" if held else ""


def run_short(args, folder, script=sway, frames=SHORT_FRAMES, host_env=None, client_env=None, both=None, seconds=900):
    """Both games fed by script, to frame `frames`. Returns the two, stopped."""
    folder.mkdir(parents=True, exist_ok=True)
    feed = folder / "feed.txt"
    write_feed(feed, frames + 16, script)
    env = {"SF64_COOP_LOCKSTEP_FEED": str(feed), "SF64_COOP_LOCKSTEP_END": str(frames), **(both or {})}
    host, client = session(args, folder, host_env, client_env, env)
    try:
        wait_for([host, client], "done", seconds)
    finally:
        host.close()
        client.close()
    return host, client


# ---- Scenarios. Each returns the text for its summary line. ----

def sc_sim(args, folder):
    folder.mkdir(parents=True, exist_ok=True)
    build = replay_tool.ROOT / "build-cmake"
    if not args.no_build:
        done = subprocess.run(["cmake", "--build", str(build), "--target", "coop_lockstep_test"], capture_output=True,
                              text=True)
        expect(done.returncode == 0, f"could not build coop_lockstep_test: {done.stdout[-400:]}")
    program = build / "coop-lockstep-test" / "coop_lockstep_test"
    done = subprocess.run([str(program)], capture_output=True, text=True, timeout=300)
    (folder / "sim.log").write_text(done.stdout + done.stderr, encoding="utf-8")
    expect(done.returncode == 0, f"coop_lockstep_test failed: {done.stdout[-600:]}")
    return done.stdout.strip().splitlines()[-1]


def sc_steer(args, folder):
    host, client = run_short(args, folder)
    frames = compare(host, client, SHORT_FRAMES)
    views = {name: {player for frame, player in view_of(game)} for name, game in (("host", host), ("client", client))}
    expect(1 in views["client"], f"the client never looked through player 1: {views['client']}")
    expect(views["host"] == {0}, f"the host looked through {views['host']}")
    moved = flown(client)
    expect(moved[0] > 500 and moved[1] > 500, f"a player did not fly: moved {moved}")
    return f"same hash on {frames} frames, moved {moved[0]:.0f} and {moved[1]:.0f}; {played(host)}, {played(client)}"


def sc_control(args, folder):
    """Can this test fail? No lockstep: each client's input stays in its own game."""
    env = {"SF64_COOP_TEST_NO_LOCKSTEP": "1", "SF64_COOP_REPLAY_LIVE_LOCAL": "1"}
    host, client = run_short(args, folder, frames=SHORT_FRAMES, both=env)
    names, _, ours = host.rows()
    _, _, theirs = client.rows()
    difference = join_test.first_difference(names, ours, theirs)
    expect(difference is not None, "the hashes agree without lockstep, so this test proves nothing")
    return f"without lockstep they differ at {difference}"


def log_rows(game):
    """The lockstep log as lists of numbers."""
    return [line.split() for line in game.sync_log.read_text(encoding="utf-8").splitlines() if line.count(" ") >= 26]


def fly(args, folder):
    """Pilots fly Corneria, one on each client. Returns (host, client, frames to the map and its tail)."""
    env = {"SF64_COOP_LOCKSTEP_PILOT": "1", "SF64_COOP_PILOT_LOG": "pilot-log.txt"}
    folder.mkdir(parents=True, exist_ok=True)
    host, client = session(args, folder, both=env)
    deadline = time.monotonic() + args.level_seconds
    at_map = None
    try:
        while True:
            rows = log_rows(host) if host.sync_log.exists() else []
            if at_map is None:
                at_map = next((i for i, row in enumerate(rows) if int(row[1]) == MAP), None)
            if at_map is not None and len(rows) >= at_map + MAP_TAIL:
                break
            expect(len(rows) < FLY_FRAMES, f"no map in {FLY_FRAMES} frames")
            for game in (host, client):
                expect(game.proc.poll() is None, f"{game.name} ended with code {game.proc.returncode} at {len(rows)}")
            expect(time.monotonic() < deadline, f"no map after {args.level_seconds} s, at frame {len(rows)}")
            time.sleep(2)
    finally:
        client.close()
        host.close()
    return host, client, at_map + MAP_TAIL


def level_story(game, frames):
    """The frames of the states a whole level has to pass, from a game's trace and lockstep log."""
    rows = log_rows(game)
    trace = [r for r in player_trace.parse(game.run / "trace.txt") if r.level == 0]
    states = []
    for r in trace:
        if r.players and (not states or states[-1] != r.players[0].state):
            states.append(r.players[0].state)
    expect(LEVEL_COMPLETE in states, f"no level complete: player 0 went through {states}")
    expect(any(int(row[1]) == MAP for row in rows[:frames]), "no map")
    full_level.print_players(game.run / "trace.txt", 2)
    return [player_trace.STATES.get(s, s) for s in states]


def plain(args, folder, feed, frames, exe=None, env=None, host_env=None, client_env=None, config=None,
          client_config=None, seconds=None, client_port=None):
    """Two games repeat a flight from its log, with no pilot in them. Returns the two, stopped."""
    both = {"SF64_COOP_LOCKSTEP_FEED": str(feed), "SF64_COOP_LOCKSTEP_END": str(frames - 1), **(env or {})}
    run_args = argparse.Namespace(**{**vars(args), "exe": exe or args.exe})
    host, client = session(run_args, folder, host_env, client_env, both, config, client_config, client_port)
    try:
        wait_for([host, client], "done", seconds or args.level_seconds, "in the plain run")
    finally:
        client.close()
        host.close()
    return host, client


def same_as_flight(names, rows, game, frames):
    _, _, theirs = game.rows()
    difference = join_test.first_difference(names, rows[:frames], theirs[:frames])
    expect(difference is None, f"{game.name} differs from the pilots' flight at {difference}")


def sc_full(args, folder):
    pilot_host, pilot_client, frames = fly(args, folder / "fly")
    equal = compare(pilot_host, pilot_client, frames)
    story = level_story(pilot_host, frames)
    print(f"lockstep: flight: {frames} frames, {' '.join(str(s) for s in story)}; {equal} frames equal", flush=True)
    feed = folder / "corneria-lockstep.txt"
    shutil.copy(pilot_host.sync_log, feed)
    print(f"lockstep: feed {feed}", flush=True)
    names, _, flight = pilot_host.rows()

    host, client = plain(args, folder / "plain", feed, frames)
    equal = compare(host, client, frames)
    same_as_flight(names, flight, host, frames)
    print(f"lockstep: plain run: {equal} frames equal, and equal to the flight; {played(host)}, {played(client)}",
          flush=True)

    # Both fly on, so that what the poke changes can only show in the hash of the frame it lands on.
    poke = frames - 2000
    host, client = plain(args, folder / "poke", feed, poke + 200, client_env={"SF64_COOP_LOCKSTEP_POKE": str(poke),
                                                                              "SF64_COOP_HASH_POKE_PLAYER": "1"})
    _, _, ours = host.rows()
    _, _, theirs = client.rows()
    difference = next((i for i in range(min(len(ours), len(theirs))) if ours[i] != theirs[i]), None)
    expect(difference == poke, f"poke on the client at frame {poke}, first difference at {difference}")
    regions = [names[k] for k in range(len(ours[poke][3])) if ours[poke][3][k] != theirs[poke][3][k]]
    return f"{frames} frames of Corneria to the map, equal; poke at {poke} shows there, in {','.join(regions)}"


def play_states(game):
    return [int(row[2]) for row in log_rows(game)]


def press(script, frames, button=BUTTON_START):
    """script, with `button` down for player p on each (frame, p)."""
    def pad(frame, player):
        if (frame, player) in frames:
            return button, 0, 0
        return script(frame, player)
    return pad


def idle(frame, player):
    return 0, 0, 0


def steer_only(frame, player):
    """sway without its buttons: A in the pause menu is Continue."""
    return (0, *sway(frame, player)[1:])


def sc_pause(args, folder):
    """Start from either client, in either direction. A line of the log shows the frame before it runs."""
    first = JOIN_FRAME + 200
    presses = {(first, 1): "client pauses", (first + 40, 0): "host resumes", (first + 80, 0): "host pauses",
               (first + 120, 1): "client resumes"}
    host, client = run_short(args, folder, press(steer_only, set(presses)), frames=first + 200)
    frames = compare(host, client, first + 200)
    paused = [i for i, state in enumerate(play_states(host)) if state == PLAY_PAUSE]
    want = list(range(first + 1, first + 41)) + list(range(first + 81, first + 121))
    expect(paused == want, f"paused on frames {paused[:3]}..{paused[-3:]} ({len(paused)}), want {want[0]}.. ({len(want)})")
    return f"both clients paused and resumed on the same frames, {len(paused)} of them; same hash on {frames} frames"


def sc_save(args, folder):
    """Saves are each client's own. A host with Corneria cleared presses Start in the intro, and the skip reads the save.
    It pauses instead, and the second press resumes."""
    frames = 900
    host, client = run_short(args, folder, press(idle, {(300, 0), (360, 0)}), frames=frames,
                             host_env={"SF64_COOP_LOCKSTEP_CLEARED": "1"})
    equal = compare(host, client, frames)
    return f"Start in the intro on a host whose save differs: same hash on {equal} frames"


def sc_zero_seed(args, folder):
    """A zero seed on both clients on one frame: upstream's re-seed reads the clock and parts them."""
    env = {"SF64_COOP_LOCKSTEP_ZERO_SEED": "1000:1"}
    host, client = run_short(args, folder / "seed1", frames=1500, both=env)
    equal = compare(host, client, 1500)
    env = {"SF64_COOP_LOCKSTEP_ZERO_SEED": "1000:3"}
    host, client = run_short(args, folder / "seed3", frames=1500, both=env)
    return f"seeds 1 and 3 zeroed on both clients: same hash on {equal} and {compare(host, client, 1500)} frames"


def sc_slow(args, folder):
    host, client = run_short(args, folder, client_env={"SF64_COOP_LOCKSTEP_SLOW": "40:150"})
    frames = compare(host, client, SHORT_FRAMES)
    waited = int(join_test.said(host.sync(), "done")[0]["held_ms"])
    expect(waited > 2000, f"the host waited {waited} ms in all, so nothing was tested")
    return f"a client 150 ms late every 40 frames: same hash on {frames} frames; {played(host)}"


def relay(folder, target, profile):
    info = folder / "relay.json"
    proc = subprocess.Popen([sys.executable, "-I", str(ROOT / "tools" / "coop" / "chaos" / "chaos.py"), "relay",
                             "--target", f"127.0.0.1:{target}", "--profile", profile, "--seed", "20",
                             "--info-file", str(info), "--log", str(folder / "packets.jsonl")])
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        if info.exists() and info.read_text(encoding="utf-8"):
            return proc, json.loads(info.read_text(encoding="utf-8"))["listen"], info
        time.sleep(0.05)
    proc.kill()
    raise Fail("the relay did not start")


def through_relay(args, folder, profile, frames=SHORT_FRAMES):
    folder.mkdir(parents=True, exist_ok=True)
    feed = folder / "feed.txt"
    write_feed(feed, frames + 16, sway)
    procs = []

    def to_relay(port):
        proc, listen, _ = relay(folder, port, profile)
        procs.append(proc)
        return listen

    env = {"SF64_COOP_LOCKSTEP_FEED": str(feed), "SF64_COOP_LOCKSTEP_END": str(frames)}
    host, client = session(args, folder, both=env, client_port=to_relay)
    try:
        wait_for([host, client], "done", 1200, f"through {profile}")
    finally:
        host.close()
        client.close()
        for proc in procs:
            proc.terminate()
            proc.wait()
    return f"{compare(host, client, frames)} frames equal, {played(host)}"


def sc_lag(args, folder):
    results = [f"{profile}: {through_relay(args, folder / profile, profile)}" for profile in args.profiles.split(",")]
    return "; ".join(results)


def sc_delay(args, folder):
    runs = []
    for delay in ("1", "6"):
        host, client = run_short(args, folder / delay, both={"SF64_COOP_TEST_INPUT_DELAY": delay})
        compare(host, client, SHORT_FRAMES)
        runs.append(host)
    names, _, one = runs[0].rows()
    _, _, six = runs[1].rows()
    difference = join_test.first_difference(names, one[:SHORT_FRAMES], six[:SHORT_FRAMES])
    expect(difference is None, f"delay 1 and delay 6 fly differently: {difference}")
    return f"delays 1 and 6: {SHORT_FRAMES} frames equal between clients and between the two runs"


def lost_player(args, folder, leaver, stayer_name, how):
    """Runs a session into the level, and has the player `leaver` ('host' or 'client') go away by how(game)."""
    folder.mkdir(parents=True, exist_ok=True)
    feed = folder / "feed.txt"
    write_feed(feed, 3 * SHORT_FRAMES, sway)
    env = {"SF64_COOP_LOCKSTEP_FEED": str(feed), "SF64_COOP_LOCKSTEP_END": str(JOIN_FRAME)}
    host, client = session(args, folder, both=env)
    gone, stays = (host, client) if leaver == "host" else (client, host)
    try:
        wait_for([host, client], "done", 900, "before the player left")
        started = time.monotonic()
        how(gone)
        wait_for([stays], "over", TIMEOUT_MS / 1000 + 30, "after the player left")
        ended = time.monotonic() - started
        wait_for([stays], "alive", 120, "after the session ended: the game hangs")
        said = [line for _, _, line in stays.report() if line.startswith(("left", "ended"))]
        expect(said, f"{stays.name} says nothing about the end")
        expect(stays.proc.poll() is None, f"{stays.name} is gone")
        return f"{stays.name} ended the session after {ended:.0f} s, said `{said[-1]}`, and played on"
    finally:
        host.close()
        client.close()


def sc_left(args, folder):
    return lost_player(args, folder, "client", "host", lambda game: game.stop(kill=True))


def sc_host_left(args, folder):
    return lost_player(args, folder, "host", "client", lambda game: game.stop(kill=True))


def sc_stall(args, folder):
    """A client that keeps its connection and stops sending input: nothing but lockstep can see that."""
    folder.mkdir(parents=True, exist_ok=True)
    feed = folder / "feed.txt"
    write_feed(feed, 3 * SHORT_FRAMES, sway)
    mute = JOIN_FRAME + 50
    host, client = session(args, folder, client_env={"SF64_COOP_LOCKSTEP_MUTE": str(mute)},
                           both={"SF64_COOP_LOCKSTEP_FEED": str(feed), "SF64_COOP_LOCKSTEP_END": str(JOIN_FRAME)})
    try:
        wait_for([host, client], "done", 900, "before the stall")
        started = time.monotonic()
        wait_for([host], "over", STALL_SECONDS + 40, "after the stall")
        ended = time.monotonic() - started
        wait_for([host, client], "alive", 120, "after the stall: a game hangs")
        over = int(join_test.said(host.sync(), "over")[0]["frame"])
        expect(mute <= over <= mute + 2 * INPUT_DELAY, f"the host ran to frame {over}, input stopped at {mute}")
        said = [line for game in (host, client) for _, _, line in game.report() if line.startswith("ended")]
        expect(any("stopped sending input" in line for line in said), f"no clear end: {said}")
        return f"ended after {ended:.0f} s at frame {over}: {said}"
    finally:
        host.close()
        client.close()


def sc_frozen(args, folder):
    """A client that stops dead, and comes back to a session that is over."""
    expect(IS_LINUX, "needs SIGSTOP")

    def freeze(game):
        os.kill(game.proc.pid, signal.SIGSTOP)  # the game this script started

    folder.mkdir(parents=True, exist_ok=True)
    feed = folder / "feed.txt"
    write_feed(feed, 3 * SHORT_FRAMES, sway)
    host, client = session(args, folder, both={"SF64_COOP_LOCKSTEP_FEED": str(feed),
                                               "SF64_COOP_LOCKSTEP_END": str(JOIN_FRAME)})
    try:
        wait_for([host, client], "done", 900, "before the freeze")
        freeze(client)
        started = time.monotonic()
        wait_for([host], "over", TIMEOUT_MS / 1000 + 30, "after the freeze")
        ended = time.monotonic() - started
        wait_for([host], "alive", 120, "after the freeze: the host hangs")
        os.kill(client.proc.pid, signal.SIGCONT)
        wait_for([client], "over", TIMEOUT_MS / 1000 + 30, "after the thaw")
        wait_for([client], "alive", 120, "after the thaw: the client hangs")
        said = [line for _, _, line in host.report() if line.startswith(("left", "ended"))]
        return f"the host ended the session after {ended:.0f} s ({said[-1]}), and the thawed client played on too"
    finally:
        os.kill(client.proc.pid, signal.SIGCONT)
        host.close()
        client.close()


class MenuClient(menu_test.Game):
    """A game that goes through the real menu. Its paths are relative: the folder comes from the base class."""

    def __init__(self, exe, name, out, env):
        env = {"SF64_COOP_LOCKSTEP_REPORT": "lockstep-report.txt", "SF64_COOP_LOCKSTEP_LOG": "lockstep-log.txt",
               "SF64_COOP_TEST_START_SEEDS": SEEDS, **env}
        super().__init__(exe, name, out, seeds=join_test.SEEDS["host" if name == "host" else "client0"], env_extra=env)
        self.sync_report = self.run / "lockstep-report.txt"
        self.sync_log = self.run / "lockstep-log.txt"

    def sync(self):
        return join_test.read_report(self.sync_report)

    def close(self):
        self.stop()


def menu_level(args, folder, env):
    """Host, join, pick and Start with the real widgets, with the windows closed again once the level runs."""
    folder.mkdir(parents=True, exist_ok=True)
    host, client = games = [MenuClient(args.exe, name, folder, env) for name in ("host", "client")]
    try:
        menu_test.open_windows(games)
        port = menu_test.host_session(host)
        menu_test.join(client, port, 2)
        host.wait_text("status", r", 2 of 4 players\.")
        client.click("pick-falco")
        host.wait_text("player2", r"^Player 2: Falco")
        host.wait_item("start", enabled=True)
        host.click("start")
    except Exception:
        for game in games:
            game.close()
        raise
    return host, client


def sc_menu(args, folder):
    folder.mkdir(parents=True, exist_ok=True)
    feed = args.feed if args.feed else folder / "feed.txt"
    frames = args.feed_frames if args.feed else SHORT_FRAMES
    if not args.feed:
        write_feed(feed, frames + 16, sway)
    host, client = menu_level(args, folder, {"SF64_COOP_LOCKSTEP_FEED": str(feed),
                                             "SF64_COOP_LOCKSTEP_END": str(frames - 1)})
    try:
        wait_for([host, client], "done", args.level_seconds, "through the menu")
        host.read()
        client.read()
        expect(re.search(r"as player 2", client.texts.get("status", "")), f"the client says {client.texts}")
    finally:
        host.close()
        client.close()
    equal = compare(host, client, frames)
    expect(1 in {player for _, player in view_of(client)}, "the client never looked through player 1")
    return f"through the real menu: same hash on {equal} frames, the client looked through player 1"


def sc_leave(args, folder):
    """Leave in the real window, with the other player's window closed: it has to come up by itself and say why."""
    folder.mkdir(parents=True, exist_ok=True)
    feed = folder / "feed.txt"
    write_feed(feed, 3 * SHORT_FRAMES, sway)
    host, client = menu_level(args, folder, {"SF64_COOP_LOCKSTEP_FEED": str(feed),
                                             "SF64_COOP_LOCKSTEP_END": str(JOIN_FRAME)})
    try:
        wait_for([host, client], "done", 900, "before leaving")
        menu_test.toggle_window(host)
        host.wait(lambda: "status" not in host.texts, "the host's window closing", 60)
        client.click("leave")
        found = host.wait_text("message", r"Player 2 left, so the session is over", 60)
        wait_for([host, client], "over", 60, "after leaving")
        wait_for([host, client], "alive", 120, "after leaving: a game hangs")
        return f"the window came up by itself: `{found.group(0)}`; both games play on"
    finally:
        host.close()
        client.close()


def sc_release(args, folder):
    expect(args.feed and args.release_exe, "needs --feed and --release-exe")
    host, client = plain(args, folder, args.feed, args.feed_frames, exe=Path(args.release_exe).resolve())
    return f"Release: {compare(host, client, args.feed_frames)} frames equal; {played(host)}, {played(client)}"


def sc_rates(args, folder):
    expect(args.feed, "needs --feed")
    a, b = args.rates.split(",")
    base = {"gVsyncEnabled": 0}
    # Real time: at test speed a game frame is drawn once, and frame interpolation is what this is about (#137).
    host, client = plain(args, folder, args.feed, args.feed_frames, env=replay_tool.REAL_TIME,
                         config={"CVars": {**base, "gInterpolationFPS": int(a)}},
                         client_config={"CVars": {**base, "gInterpolationFPS": int(b)}},
                         seconds=args.feed_frames / 10 + 600)
    draws = [hash_check.read_draws(game.run / "state-hash.txt") for game in (host, client)]
    return f"{a} and {b} fps at real time, {draws} draws a frame: {compare(host, client, args.feed_frames)} frames equal"


SCENARIOS = {
    "sim": sc_sim,
    "steer": sc_steer,
    "control": sc_control,
    "pause": sc_pause,
    "save": sc_save,
    "zero_seed": sc_zero_seed,
    "slow": sc_slow,
    "lag": sc_lag,
    "delay": sc_delay,
    "left": sc_left,
    "host_left": sc_host_left,
    "leave": sc_leave,
    "stall": sc_stall,
    "frozen": sc_frozen,
    "menu": sc_menu,
    "full": sc_full,
    "release": sc_release,
    "rates": sc_rates,
}
LONG = ("full", "release", "rates")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--exe", help="Starship executable (default: newest build found)")
    ap.add_argument("--release-exe", help="for `release`: a Release build of the same commit")
    ap.add_argument("--only", help="scenarios, comma-separated")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--no-build", action="store_true", help="do not build coop_lockstep_test")
    ap.add_argument("--feed", help="a lockstep log of `full`, for the scenarios that repeat a flight")
    ap.add_argument("--level-seconds", type=float, default=2400, help="give up on a whole level after this long")
    ap.add_argument("--profiles", default="good_wifi", help="for `lag`: chaos relay profiles, comma-separated")
    ap.add_argument("--rates", default="60,120", help="for `rates`: the two clients' gInterpolationFPS")
    args = ap.parse_args()
    if args.list:
        print("\n".join(SCENARIOS))
        return 0
    args.exe = replay_tool.find_exe(args.exe)
    names = args.only.split(",") if args.only else [name for name in SCENARIOS if name not in LONG]
    if args.feed:
        args.feed = Path(args.feed).resolve()
        args.feed_frames = len(args.feed.read_text(encoding="utf-8").splitlines())
    for name in names:
        if name not in SCENARIOS:
            sys.exit(f"lockstep: no scenario {name}")
    root = replay_tool.ROOT / "local" / f"test-lockstep-{time.strftime('%Y%m%d-%H%M%S')}"
    passed = 0
    for name in names:
        started = time.monotonic()
        try:
            detail = SCENARIOS[name](args, root / name)
            passed += 1
            print(f"ok      {name}: {detail} ({time.monotonic() - started:.0f} s)", flush=True)
        except (Fail, join_test.Fail) as failure:
            print(f"FAILED  {name}: {failure} ({time.monotonic() - started:.0f} s)", flush=True)
    print(f"RESULT: {passed}/{len(names)} scenarios passed")
    print(f"logs: {root}")
    return 0 if passed == len(names) else 1


if __name__ == "__main__":
    sys.exit(main())
