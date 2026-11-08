#!/usr/bin/env python3
"""Real game clients in lockstep (WP-22, #20): two games over loopback, each with its own folder, playing one game.

    python tools/coop/lockstep_test.py                     # sim, then every game scenario but the long ones
    python tools/coop/lockstep_test.py --only sim          # no game, no ROM
    python tools/coop/lockstep_test.py --only steer,pause
    python tools/coop/lockstep_test.py --only full         # all of Corneria, about 10 minutes
    python tools/coop/lockstep_test.py --list
    python tools/coop/lockstep_test.py --only release,rates,audio,menu --feed <lockstep log of full> --release-exe <Starship>

Every game gets a fresh folder under local/ with its own SHIP_HOME, plays a replay with no input so no controller reaches
it, and takes its input from src/port/coop/test/lockstep_driver.h, which writes port 0 before the input layer reads the
local controller. From there on the input goes the road a pad's does: stamped with a frame, delayed, sent, read back.

What counts is the state hash log from the first frame after Play_Init on, every frame and every region, as in
join_test.py. A lockstep log of inputs and game state per frame has to be equal as well. A game that plays alone cannot
pass: a client's input reaches the host only through lockstep, and `control` shows the same run differs without it.

  sim       src/port/coop/sync/lockstep.c alone, in a virtual network with loss, lag, reordering and duplicates, for two to
            four players (tools/coop/locksteptest).
  steer     each client flies its own player. Hashes equal, each game looks through its own player.
  three     the same with three games, so that input goes through the host to the other client.
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
  audio     the plain run with the two clients at different audio sample counts, as a radio line follows the sound thread.
  release   the plain run on a Release build (--release-exe) and on Debug: all four clients have to hash alike.
  rates     the plain run at real time, with the two clients at different gInterpolationFPS.
  desync_sim src/port/coop/sync/desync.c alone, in a virtual network (tools/coop/desynctest), WP-23.
  desync    a bit flipped on one client, on rails and in all-range: both games name that frame, the players and the
            region, write a report, and the session ends; desync_report.py names the flipped value.
  desync_control the same with SF64_COOP_TEST_NO_DESYNC_CHECK: the hashes differ and nobody says so.
  desync_three a third game that stays in step hears which two differed, and writes its report too.
  handover  the handover (#189): after a desync both folders hold both reports, byte for byte, desync_report.py diff
            reads the two of one folder as it reads the originals, and each folder's copy of the other player's log
            runs to its `session closed`.
  log       the co-op log (#172): each game's folder holds its own log and the other player's, streamed during the
            session; the hash stays equal with streaming on; nothing personal is in the files; coop_log.py merges them.

Every scenario that compares two games also checks that neither wrote a desync report (WP-23).

Prints one line per scenario and a RESULT line, exits 0 only when everything passed. Needs a build with assets.
"""
import argparse
import getpass
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
import coop_log  # noqa: E402
import desync_report  # noqa: E402
import full_level  # noqa: E402
import hash_check  # noqa: E402
import join_test  # noqa: E402
import menu_test  # noqa: E402
import player_trace  # noqa: E402
import replay as replay_tool  # noqa: E402

IS_LINUX = platform.system() == "Linux"
BUTTON_A, BUTTON_START, BUTTON_L = 0x8000, 0x1000, 0x0020
BUTTON_C_LEFT = 0x0002  # gBoostButton, src/engine/fox_game.c
BOOST_AT, BOOST_FRAMES = 2000, 40  # feed frames of sc_boost
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
DESYNC_PERIOD = 10  # COOP_DESYNC_PERIOD: a desync shows within this many frames plus the input delay
DESYNC_SEEN_WITHIN = 2 * DESYNC_PERIOD + 2 * INPUT_DELAY + 10  # frames from the flipped bit to the session's end


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
        self.env["SF64_COOP_TEST_SPEED"] = replay_tool.MEETING_SPEED  # games that meet over sockets, as in join_test.py
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


def session(args, folder, host_env=None, client_env=None, both=None, config=None, client_config=None, client_port=None,
            clients=1, client_envs=None):
    """A host and its clients through the join driver, on their way to a level. Returns them started: with one client as
    (host, client), with more as (host, client, client, ...). client_envs holds one more dict per client."""
    folder.mkdir(parents=True, exist_ok=True)
    port_file = folder / "port"
    env = dict(both or {})
    env["SF64_COOP_TEST_START_SEEDS"] = SEEDS
    host = Client(args, folder, "host", {**env, "SF64_COOP_JOIN_PORT_FILE": str(port_file),
                                         "SF64_COOP_JOIN_CLIENTS": str(clients), "SF64_COOP_JOIN_LEVEL": "0",
                                         "SF64_COOP_JOIN_START_FRAME": str(START_FRAME), **(host_env or {})})
    host.start(config)
    deadline = time.monotonic() + 180
    while not (port_file.exists() and port_file.read_text(encoding="utf-8").strip()):
        expect(host.proc.poll() is None and time.monotonic() < deadline, "the host did not report its port")
        time.sleep(0.2)
    port = port_file.read_text(encoding="utf-8").strip()
    if client_port is not None:
        port = client_port(port)
    games = [host]
    for n, pick in zip(range(clients), ("falco", "slippy", "peppy")):
        games.append(Client(args, folder, f"client{n}", {**env, "SF64_COOP_JOIN_CONNECT": f"127.0.0.1:{port}",
                                                         "SF64_COOP_JOIN_PICKS": pick, **(client_env or {}),
                                                         **((client_envs or [{}] * clients)[n])}))
        games[-1].start(client_config if client_config is not None else config)
    return tuple(games)


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
    no_desync([host, client])
    return n


def desync_reports(game):
    """The desync reports in a game's own folder (WP-23), its own and the ones handed over to it (#189), by name."""
    return sorted(game.run.glob("coop-desync-*.txt"))


REPORT_NAME = re.compile(r"coop-desync-(\d+)-player(\d)(?:-\d)?\.txt$")


def player_of(game):
    """The player number a game flies as: 1 for the host, a client's from the join driver's `joined` line."""
    if game.name == "host":
        return 1
    return int(join_test.said(game.report(), "joined")[0]["player"]) + 1


def reports_by_player(game):
    """{player: [paths]} of the desync reports in a game's folder."""
    found = {}
    for path in desync_reports(game):
        match = REPORT_NAME.match(path.name)
        if match:
            found.setdefault(int(match.group(2)), []).append(path)
    return found


def own_reports(game):
    """The reports a game wrote itself, not the ones the others handed over."""
    return reports_by_player(game).get(player_of(game), [])


def no_desync(games):
    for game in games:
        reports = desync_reports(game)
        expect(not reports, f"{game.name} wrote a desync report in a run that stayed in step: "
                            f"{reports[0].name if reports else ''}")
        said = [line for _, _, line in game.report() if "out of step" in line]
        expect(not said, f"{game.name} said {said[0] if said else ''}")


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

def find_sim(build_dir, config):
    """The test program of `sim`, in a build folder: a Visual Studio build puts it under the configuration."""
    for path in (build_dir / "coop-lockstep-test" / "coop_lockstep_test",
                 build_dir / "coop-lockstep-test" / config / "coop_lockstep_test.exe"):
        if path.exists():
            return path
    return None


def find_desync_sim(build_dir, config):
    """The test program of `desync_sim`, as find_sim."""
    for path in (build_dir / "coop-desync-test" / "coop_desync_test",
                 build_dir / "coop-desync-test" / config / "coop_desync_test.exe"):
        if path.exists():
            return path
    return None


def run_sim(args, folder, target, find):
    folder.mkdir(parents=True, exist_ok=True)
    build = Path(args.build_dir).resolve() if args.build_dir else ROOT / "build-cmake"
    if not args.no_build:
        done = subprocess.run(["cmake", "--build", str(build), "--config", args.config, "--target", target],
                              capture_output=True, text=True)
        expect(done.returncode == 0, f"could not build {target}: {done.stdout[-400:]}")
    program = find(build, args.config)
    expect(program is not None, f"no {target} in {build}")
    done = subprocess.run([str(program)], capture_output=True, text=True, timeout=300)
    (folder / "sim.log").write_text(done.stdout + done.stderr, encoding="utf-8")
    expect(done.returncode == 0, f"{target} failed: {done.stdout[-600:]}")
    return done.stdout.strip().splitlines()[-1]


def sc_sim(args, folder):
    return run_sim(args, folder, "coop_lockstep_test", find_sim)


def sc_desync_sim(args, folder):
    return run_sim(args, folder, "coop_desync_test", find_desync_sim)


def sc_steer(args, folder):
    # A PC whose first controller sits on port 1 reads team-wide input from there unless the level start says otherwise.
    host, client = run_short(args, folder, client_env={"SF64_COOP_JOIN_MAIN_PORT": "1"})
    frames = compare(host, client, SHORT_FRAMES)
    main = join_test.said(client.report(), "level_running")[0]["main"]
    expect(main == "0", f"the client reads team-wide input from slot {main}")
    views = {name: {player for frame, player in view_of(game)} for name, game in (("host", host), ("client", client))}
    expect(1 in views["client"], f"the client never looked through player 1: {views['client']}")
    expect(views["host"] == {0}, f"the host looked through {views['host']}")
    moved = flown(client)
    expect(moved[0] > 500 and moved[1] > 500, f"a player did not fly: moved {moved}")
    return f"same hash on {frames} frames, moved {moved[0]:.0f} and {moved[1]:.0f}; {played(host)}, {played(client)}"


def boost_of_player_1(frame, player):
    """sway, and player 1 alone holds boost (C-left) for BOOST_FRAMES frames from BOOST_AT. Player 0 never does."""
    button, x, y = sway(frame, player)
    if player == 1 and BOOST_AT <= frame < BOOST_AT + BOOST_FRAMES:
        button |= BUTTON_C_LEFT
    return button, x, y


def sc_boost(args, folder):
    """#194: on the rails any player's boost speeds the level for everyone. Player 1 boosts, player 0 does not."""
    host, client = run_short(args, folder, script=boost_of_player_1)
    frames = compare(host, client, SHORT_FRAMES)
    seen = []
    for game in (host, client):
        rows = [r for r in player_trace.parse(game.run / "trace.txt") if r.level == 0 and r.mode == 0
                and len(r.players) > 1 and r.players[1].state == 3 and not r.players[1].parked]
        before = sorted(r.pathvel for r in rows if r.frame < BOOST_AT and r.pathvel > 0)
        expect(len(before) > 50 and any(r.frame >= BOOST_AT for r in rows),
               f"{game.name}: only {len(before)} frames on the rails with both flying before the boost")
        base = before[len(before) // 2]
        window = [r for r in rows if BOOST_AT <= r.frame < BOOST_AT + BOOST_FRAMES + 40]
        top = max(r.pathvel for r in window)
        lead = min(r.players[1].camdist for r in window)
        # Player 0 does not press boost: inside the window his camDist stays where it was just before it (the level
        # intro pulls it forward earlier on, so the minimum over the whole run says nothing).
        own_before = sorted(r.players[0].camdist for r in rows if BOOST_AT - 60 <= r.frame < BOOST_AT)
        own_base = own_before[len(own_before) // 2] if own_before else 0.0
        own = min(r.players[0].camdist for r in window)
        expect(lead < -100, f"{game.name}: player 1 did not boost, camDist only {lead}")
        expect(top > base + 15, f"{game.name}: level speed {base} before, {top} at most while player 1 boosts")
        expect(own > own_base - 10, f"{game.name}: player 0 boosted too, camDist {own} against {own_base} before")
        seen.append((base, top, [(r.frame, r.pathvel) for r in rows]))
    # A game can run a few frames past `done` before it is stopped, so compare the frames both traces hold.
    ours, theirs = dict(seen[0][2]), dict(seen[1][2])
    common = sorted(set(ours) & set(theirs))
    expect(len(common) > 50 and all(ours[f] == theirs[f] for f in common),
           f"the two games do not have one level speed ({len(common)} common frames)")
    return (f"same hash on {frames} frames; level speed {seen[0][0]:.1f} -> {seen[0][1]:.1f} in both games while only "
            f"player 1 boosts")


def sc_three(args, folder):
    """Client input that has to go through the host to reach the other client."""
    folder.mkdir(parents=True, exist_ok=True)
    feed = folder / "feed.txt"
    write_feed(feed, SHORT_FRAMES + 16, sway)
    games = session(args, folder, both={"SF64_COOP_LOCKSTEP_FEED": str(feed), "SF64_COOP_LOCKSTEP_END": str(SHORT_FRAMES)},
                    clients=2)
    try:
        wait_for(list(games), "done", 900)
    finally:
        for game in games:
            game.close()
    host, a, b = games
    equal = (compare(host, a, SHORT_FRAMES), compare(host, b, SHORT_FRAMES))
    expect({player for _, player in view_of(host)} == {0}, "the host looked through someone else")
    for game in (a, b):
        own = int(join_test.said(game.report(), "joined")[0]["player"])  # whoever joined first is player 1
        expect(own in {player for _, player in view_of(game)}, f"{game.name} never looked through player {own}")
    return f"three games: {equal[0]} and {equal[1]} frames equal, each looking through its own player"


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
          client_config=None, seconds=None, client_port=None, until="done"):
    """Two games repeat a flight from its log, with no pilot in them. Returns the two, stopped once both said `until`."""
    both = {"SF64_COOP_LOCKSTEP_FEED": str(feed), "SF64_COOP_LOCKSTEP_END": str(frames - 1), **(env or {})}
    run_args = argparse.Namespace(**{**vars(args), "exe": exe or args.exe})
    host, client = session(run_args, folder, host_env, client_env, both, config, client_config, client_port)
    try:
        wait_for([host, client], until, seconds or args.level_seconds, "in the plain run")
        if until == "over":
            wait_for([host, client], "alive", 120, "after the session ended: a game hangs")
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

    # Both fly on, so that what the poke changes can only show in the hash of the frame it lands on. The desync check
    # then ends the session (WP-23), and each game has to say where.
    poke = frames - 2000
    host, client = plain(args, folder / "poke", feed, poke + 400, client_env={"SF64_COOP_LOCKSTEP_POKE": str(poke),
                                                                              "SF64_COOP_HASH_POKE_PLAYER": "1"},
                         until="over")
    _, _, ours = host.rows()
    _, _, theirs = client.rows()
    difference = next((i for i in range(min(len(ours), len(theirs))) if ours[i] != theirs[i]), None)
    expect(difference == poke, f"poke on the client at frame {poke}, first difference at {difference}")
    regions = [names[k] for k in range(len(ours[poke][3])) if ours[poke][3][k] != theirs[poke][3][k]]
    found = check_desync(host, client, poke, "all-range")
    return (f"{frames} frames of Corneria to the map, equal; poke at {poke} shows there, in {','.join(regions)}; "
            f"{found}")


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
                             "--info-file", str(info), "--log", str(folder / "packets.jsonl")],
                            stdout=subprocess.DEVNULL)
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
    # The real widgets reach the lockstep start through the menu glue, the other scenarios through the join driver: both
    # have to switch on the session's sine (decision 0011).
    expect_math_line(host, 1)
    expect_math_line(client, 2)
    return (f"through the real menu: same hash on {equal} frames, the client looked through player 1, both logs say "
            f"the session's sine is on")


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


def sc_audio(args, folder):
    """Radio lines follow the sound thread (#129), and how much a client renders per audio update is its own business."""
    expect(args.feed, "needs --feed")
    host, client = plain(args, folder, args.feed, args.feed_frames, host_env={"SF64_COOP_TEST_AUDIO_SAMPLES": "high"},
                         client_env={"SF64_COOP_TEST_AUDIO_SAMPLES": "low"})
    return f"high and low audio samples: {compare(host, client, args.feed_frames)} frames equal"


def sc_release(args, folder):
    """Two Release clients, and the same flight on two Debug clients: all four have to hash alike."""
    expect(args.feed and args.release_exe, "needs --feed and --release-exe")
    frames = args.feed_frames
    host, client = plain(args, folder / "release", args.feed, frames, exe=Path(args.release_exe).resolve())
    equal = compare(host, client, frames)
    debug_host, debug_client = plain(args, folder / "debug", args.feed, frames)
    compare(debug_host, debug_client, frames)
    names, _, release_rows = host.rows()
    _, _, debug_rows = debug_host.rows()
    difference = join_test.first_difference(names, release_rows[:frames], debug_rows[:frames])
    expect(difference is None, f"Release and Debug differ at {difference}")
    return f"two Release clients: {equal} frames equal, and equal to two Debug clients; {played(host)}, {played(client)}"


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


def report_diff(a, b):
    """desync_report.py diff of two reports; it has to find a difference."""
    done = subprocess.run([sys.executable, "-I", str(ROOT / "tools" / "coop" / "desync_report.py"), "diff", str(a),
                           str(b)], capture_output=True, text=True, timeout=120)
    expect(done.returncode == 0, f"desync_report.py diff {a.name} {b.name}: exit {done.returncode}: "
                                 f"{done.stdout[-400:]}")
    return done


def check_desync(host, client, poke, mode, pair=(1, 2), clients=None):
    """Both games found the flipped bit of player 2 at exactly `poke`: a report each, with the frame, the pair, the
    region and a verified dump, the session over within a few frames and the text for the player; and the diff tool
    names gPlayer[1]. Returns a line about it."""
    games = [host, client, *(clients or [])]
    reports = {}
    for game in games:
        found = own_reports(game)
        expect(len(found) == 1, f"{game.name} wrote {len(found)} desync reports")
        reports[game.name] = desync_report.Report(found[0])
        header = reports[game.name].header
        expect(header.get("frame") == str(poke), f"{game.name}: {found[0].name} says frame {header.get('frame')}")
        expect(header.get("pair") == f"players {pair[0]} and {pair[1]}", f"{game.name}: pair {header.get('pair')}")
        expect(mode in header.get("my state", ""), f"{game.name}: {header.get('my state')} is not {mode}")
        problems = reports[game.name].verify()
        expect(not problems, f"{game.name}: {problems[0] if problems else ''}")
        for key in ("this game", "other game"):
            expect(header.get(key, "").startswith("build "), f"{game.name}: {key}: {header.get(key)}")
        over = join_test.said(game.sync(), "over")
        expect(over, f"{game.name}: the session did not end")
        frame = int(over[0]["frame"])
        expect(poke <= frame <= poke + DESYNC_SEEN_WITHIN, f"{game.name}: over at frame {frame}, poke at {poke}")
        ended = [line for _, _, line in game.report() if line.startswith("ended")]
        expect(ended, f"{game.name} says nothing about the end")
        if game in (host, client):
            expect(f"out of step at frame {poke} of Corneria ({mode})" in ended[-1], f"{game.name}: {ended[-1]}")
            expect("report for the developers is in" in ended[-1], f"{game.name}: no report named: {ended[-1]}")
        else:
            # A game outside the pair waits the grace out, and the host closes first: it reads the host's end, and its
            # report comes from the session's end (NOTES.md, 2026-11-07, #21).
            expect("out of step" in ended[-1] or "closed" in ended[-1], f"{game.name}: {ended[-1]}")
    for game in (host, client):
        expect(reports[game.name].header.get("regions differing") == "players",
               f"{game.name}: regions {reports[game.name].header.get('regions differing')}")
    for game in clients or []:
        expect(reports[game.name].header.get("regions differing") == "none",
               f"{game.name}: regions {reports[game.name].header.get('regions differing')}")
    done = report_diff(own_reports(host)[0], own_reports(client)[0])
    first = next((line.strip() for line in done.stdout.splitlines() if "gPlayer[1]" in line), None)
    expect(first is not None and first.startswith("players: gPlayer[1] differs"),
           f"desync_report.py diff did not name gPlayer[1]: {done.stdout[-400:]}")
    return f"both reports verified, `{first}`"


def poke_session(args, folder, poke, jump=None, both=None, client_envs=None, clients=1):
    """A session with sway, a bit of player 2 flipped on the first client at `poke`, into all-range first when `jump`
    says when player 1 presses L (CVar gDebugJumpToAllRange on every game). Waits for the session to end, or for
    `done` when `both` turns the check off. Returns the games, stopped."""
    folder.mkdir(parents=True, exist_ok=True)
    feed = folder / "feed.txt"
    frames = poke + 400
    write_feed(feed, frames + 16, press(sway, {(jump, 0)}, BUTTON_L) if jump is not None else sway)
    config = {"CVars": {"gDebugJumpToAllRange": 1}} if jump is not None else None
    env = {"SF64_COOP_LOCKSTEP_FEED": str(feed), "SF64_COOP_LOCKSTEP_END": str(frames), **(both or {})}
    envs = [{"SF64_COOP_LOCKSTEP_POKE": str(poke), "SF64_COOP_HASH_POKE_PLAYER": "1"}] + [{}] * (clients - 1)
    games = session(args, folder, both=env, config=config, clients=clients, client_envs=client_envs or envs)
    try:
        if both and "SF64_COOP_TEST_NO_DESYNC_CHECK" in both:
            wait_for(list(games), "done", 900, "with the check off")
        else:
            wait_for(list(games), "over", 900, "after the flipped bit")
            wait_for(list(games), "alive", 120, "after the session ended: a game hangs")
    finally:
        for game in games:
            game.close()
    return games


def sc_desync(args, folder):
    rails = JOIN_FRAME + 300
    host, client = poke_session(args, folder / "rails", rails)
    on_rails = check_desync(host, client, rails, "rails")
    all_range = JOIN_FRAME + 700
    host, client = poke_session(args, folder / "all-range", all_range, jump=JOIN_FRAME + 100)
    in_all_range = check_desync(host, client, all_range, "all-range")
    return f"rails, poke at {rails}: {on_rails}; all-range, poke at {all_range}: {in_all_range}"


def sc_desync_control(args, folder):
    """Can this test fail? With the check off the hashes still differ, and nobody says so."""
    poke = JOIN_FRAME + 300
    host, client = poke_session(args, folder, poke, both={"SF64_COOP_TEST_NO_DESYNC_CHECK": "1"})
    _, _, ours = host.rows()
    _, _, theirs = client.rows()
    difference = next((i for i in range(min(len(ours), len(theirs))) if ours[i] != theirs[i]), None)
    expect(difference == poke, f"poke at {poke}, first difference at {difference}")
    no_desync([host, client])
    expect(not join_test.said(host.sync(), "over") and not join_test.said(client.sync(), "over"), "the session ended")
    return f"with the check off the hashes differ from frame {poke} on, no report, the session goes on"


def sc_desync_three(args, folder):
    """The second client flips the bit: the host and it name players 1 and 3, the first client hears it from the host."""
    poke = JOIN_FRAME + 300
    envs = [{}, {"SF64_COOP_LOCKSTEP_POKE": str(poke), "SF64_COOP_HASH_POKE_PLAYER": "1"}]
    host, a, b = poke_session(args, folder, poke, client_envs=envs, clients=2)
    own = int(join_test.said(b.report(), "joined")[0]["player"])  # whoever joined first is player 1
    found = check_desync(host, b, poke, "rails", pair=(1, own + 1), clients=[a])
    return (f"three games, poke on player {own + 1}'s at {poke}: {found}; player {4 - own}'s report says no region "
            f"differs from the host's")


def sc_handover(args, folder):
    """#189: before the connection goes, each game hands its report and the rest of its log to the other. After a desync
    both folders hold both reports, byte for byte the sender's; the diff tool reads the two of one folder as it reads
    the originals; each folder's copy of the other player's log runs on up to its `session closed`; the handover ends
    because it is done, not because its time is up; and the session still ends, with both games playing on."""
    poke = JOIN_FRAME + 300
    host, client = poke_session(args, folder, poke)
    found = check_desync(host, client, poke, "rails")
    games = (host, client)
    names = {game: own_reports(game)[0].name for game in games}
    for game in games:
        held = reports_by_player(game)
        expect(set(held) == {1, 2}, f"{game.name}'s folder holds the reports of players {sorted(held)}, want 1 and 2")
        for other in games:
            path = game.run / names[other]
            expect(path.exists(), f"{game.name}'s folder lacks {names[other]}")
            expect(path.read_bytes() == (other.run / names[other]).read_bytes(),
                   f"{game.name}'s {names[other]} is not byte for byte {other.name}'s")
        leftovers = sorted(game.run.glob("*.part"))
        expect(not leftovers, f"{game.name} left {leftovers[0].name if leftovers else ''}")
    originals = report_diff(host.run / names[host], client.run / names[client]).stdout
    for game in games:
        here = report_diff(game.run / names[host], game.run / names[client]).stdout
        expect(here == originals, f"desync_report.py diff in {game.name}'s folder says {here[-300:]}, "
                                  f"on the originals {originals[-300:]}")
    first = next((line.strip() for line in originals.splitlines() if "differs" in line), "?")
    lines = 0
    took = []
    for own_game, other_game in ((host, client), (client, host)):
        player = player_of(own_game)
        own = [line.format() for line in coop_log.read_lines(coop_logs(own_game)[player], player)]
        got = [line.format() for line in coop_log.read_lines(coop_logs(other_game)[player], player)]
        closed = next((i for i, line in enumerate(own) if line.endswith(" session session closed")), None)
        expect(closed is not None, f"player {player}'s own log never says `session closed`")
        expect(got and got[0] in own, f"{other_game.name}'s copy of player {player}'s log starts with a line player "
                                      f"{player} never wrote: {got[:1]}")
        tail = own[own.index(got[0]):closed + 1]
        missing = [line for line in tail if line not in got]
        expect(got[:len(tail)] == tail, f"{other_game.name}'s copy of player {player}'s log stops {len(missing)} lines "
                                        f"short of `session closed`, first missing: {missing[:1]}")
        lines += len(tail)
        done = [re.search(r"handover: done in (\d+) ms", line) for line in own[closed:]]
        done = [int(match.group(1)) for match in done if match]
        expect(done, f"player {player}'s log does not say the handover was done: "
                     f"{[line for line in own if 'handover' in line][-3:]}")
        took.append(done[0])
    return (f"{found}; both folders hold both reports, byte for byte, and the diff in one folder reads as on the "
            f"originals ({first}); each copy of the other's log runs to `session closed`, {lines} lines; the "
            f"handover took {took[0]} and {took[1]} ms; both games play on")


# The line a game writes when the lockstep starts (decision 0011, #186): the N64 sine of 0x406a927f is bf000000 on every
# platform, the value a Windows host needed where glibc gave beffffff, and the game's own arctangent of 0.5 is 3eed6338
# (both from the Python port in math_test.py).
MATH_LINE = ("the N64's sine, cosine and arctangent are on for this session "
             "(sin of 0x406a927f = 0xbf000000, atan of 0x3f000000 = 0x3eed6338)")


def expect_math_line(game, player):
    """The game's own co-op log says the session's sine is on."""
    text = coop_logs(game)[player].read_text(encoding="utf-8", errors="replace")
    expect(MATH_LINE in text, f"{game.name}'s log of player {player} never says `{MATH_LINE}`")


def coop_logs(game):
    """{player: path} of the one session of co-op logs in a game's folder."""
    found = coop_log.log_files(game.run)
    expect(len(found) == 1, f"{game.name} holds {len(found)} sessions of co-op logs, want 1")
    return next(iter(found.values()))


def check_private(game, files):
    """Nothing personal in any log file: no home folder, no user name, no address; the home folder shows as ~."""
    home = str(Path.home())
    user = getpass.getuser()
    for player, path in sorted(files.items()):
        text = path.read_text(encoding="utf-8", errors="replace")
        expect(home not in text, f"{game.name}'s log of player {player} holds the home folder")
        expect(re.search(rf"(?<![\w]){re.escape(user)}(?![\w])", text) is None,
               f"{game.name}'s log of player {player} holds the user name")
        expect(re.search(r"(?<![\w.])\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}(?![\w.])", text) is None,
               f"{game.name}'s log of player {player} holds an IP address")
        expect("~/" in text or "~\\" in text, f"{game.name}'s log of player {player} never shows the home folder as ~")


def sc_log(args, folder):
    """Every folder holds every player's log, the received copy is the sender's own, the hash stays equal with the
    streaming on, and the merge tool orders them."""
    host, client = run_short(args, folder)
    frames = compare(host, client, SHORT_FRAMES)
    files = {game: coop_logs(game) for game in (host, client)}
    for game in (host, client):
        expect(set(files[game]) == {1, 2},
               f"{game.name} holds logs of players {sorted(files[game], key=lambda p: p or 0)}, want 1 and 2")
        check_private(game, files[game])
    received = 0
    for own_game, other_game, player in ((host, client, 1), (client, host, 2)):
        own = [line.format() for line in coop_log.read_lines(files[own_game][player], player)]
        got = [line.format() for line in coop_log.read_lines(files[other_game][player], player)]
        expect(len(own) >= 40, f"player {player}'s own log has only {len(own)} lines")
        missing = [line for line in got if line not in own]
        expect(not missing, f"{other_game.name} holds a line of player {player} that player never wrote: "
                            f"{missing[0] if missing else ''}")
        expect(got == [line for line in own if line in set(got)], f"player {player}'s lines arrived out of order")
        expect(len(got) >= 0.8 * len(own), f"{other_game.name} got {len(got)} of player {player}'s {len(own)} lines")
        categories = {line.category for line in coop_log.read_lines(files[own_game][player], player)}
        want = {"session", "net", "lockstep", "desync", "game", "state", "log"}
        expect(want <= categories, f"player {player}'s log lacks {sorted(want - categories)}")
        texts = "\n".join(own)
        # The session's sine (decision 0011, #186) is on before the level.
        for phrase in ("lockstep started at frame 0", "level start: level 0", "desync check on", "log closed",
                       MATH_LINE):
            expect(phrase in texts, f"player {player}'s log never says `{phrase}`")
        # A test game is told to quit, so its last lines may still be queued; none may have been lost to a full queue.
        closed = [line for line in own if "log closed" in line][-1]
        expect("0 lines dropped for full queues" in closed, f"player {player}'s log: {closed}")
        received += len(got)
    session, merged = coop_log.merge([host.run, client.run])
    expect({line.player for line in merged} == {1, 2}, "the merge lacks a player")
    times = [line.time for line in merged]
    expect(times == sorted(times), "the merged timeline is out of order")
    done = subprocess.run([sys.executable, "-I", str(ROOT / "tools" / "coop" / "coop_log.py"), "merge", str(host.run),
                           str(client.run), "--category", "session,desync"], capture_output=True, text=True)
    expect(done.returncode == 0 and "level start" in done.stdout, f"coop_log.py merge failed: {done.stdout[-300:]}")
    return (f"same hash on {frames} frames with streaming on; both folders hold both logs, {received} lines received, "
            f"{len(merged)} in the merged timeline of session {session}; nothing personal in them")


SCENARIOS = {
    "sim": sc_sim,
    "desync_sim": sc_desync_sim,
    "steer": sc_steer,
    "boost": sc_boost,
    "three": sc_three,
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
    "audio": sc_audio,
    "release": sc_release,
    "rates": sc_rates,
    "desync": sc_desync,
    "desync_control": sc_desync_control,
    "desync_three": sc_desync_three,
    "handover": sc_handover,
    "log": sc_log,
}
LONG = ("full", "audio", "release", "rates")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--exe", help="Starship executable (default: newest build found)")
    ap.add_argument("--release-exe", help="for `release`: a Release build of the same commit")
    ap.add_argument("--only", help="scenarios, comma-separated")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--no-build", action="store_true", help="do not build coop_lockstep_test")
    ap.add_argument("--build-dir", help="where to find coop_lockstep_test (default: build-cmake)")
    ap.add_argument("--config", default="Debug", help="for a build with several configurations")
    ap.add_argument("--out", help="folder for the logs (default: a new one under local/)")
    ap.add_argument("--feed", help="a lockstep log of `full`, for the scenarios that repeat a flight")
    ap.add_argument("--level-seconds", type=float, default=2400, help="give up on a whole level after this long")
    ap.add_argument("--profiles", default="good_wifi", help="for `lag`: chaos relay profiles, comma-separated")
    ap.add_argument("--rates", default="60,120", help="for `rates`: the two clients' gInterpolationFPS")
    args = ap.parse_args()
    if args.list:
        print("\n".join(SCENARIOS))
        return 0
    names = args.only.split(",") if args.only else [name for name in SCENARIOS if name not in LONG]
    if any(name not in ("sim", "desync_sim") for name in names):
        args.exe = replay_tool.find_exe(args.exe)
    if args.feed:
        args.feed = Path(args.feed).resolve()
        args.feed_frames = len(args.feed.read_text(encoding="utf-8").splitlines())
    for name in names:
        if name not in SCENARIOS:
            sys.exit(f"lockstep: no scenario {name}")
    root = Path(args.out) if args.out else replay_tool.ROOT / "local" / f"test-lockstep-{time.strftime('%Y%m%d-%H%M%S')}"
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
