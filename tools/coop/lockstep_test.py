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
  aim       enemies aim at the nearest human (#193): on Corneria's rails some enemy shots or missiles fly at player 1,
            the two games log the same ones (src/port/coop/test/aim_trace.h), and the hashes stay equal.
  return    a player 2-4 who goes down comes back (decision 0007 rule 4, #14): player 1 is forced down on Corneria's
            rails, is out for exactly RETURN_FRAMES frames, comes back beside player 0 with full shields, 3 bombs and
            one life less in the pool, gLifeCount[0]; down again with the pool at 0 it stays out until a life comes in.
            The two games hash alike on every frame.
  return_all_range the same in Corneria's all-range section, where a return also waits for player 0 to be ACTIVE.
  return_fox_down player 1 goes down, and player 0 goes down before player 1 is due back: the return is dropped,
            the level restarts as for one player, player 0's death spends the life and player 1 joins again for free.
  laser     a laser item in front of player 1 only (#201): its lasers go 0 to 1 and its wings are set on both games,
            player 0's stay, the hashes are equal.
  laser_wing1 player 1's wing is broken and it collects a laser item: its wings are repaired, lasers not raised (#201).
  laser_wing0 only Fox's wing is broken and player 1 collects a laser item: player 1's lasers go up, Fox's wings stay.
  repair    a wing repair item collected by player 1 repairs player 1's wings, not Fox's.
  crate     a supply crate bursts with Fox at twin lasers and player 1 at single: it drops lasers.
  kill      the team scores (decision 0008 4b, #210): in Corneria's all-range section an enemy killed by player 1 adds
            its hit to gHitCount and, with the drop counter at 14, drops lasers; one with DMG_SRC_2 and one shot down
            by Falco add nothing, one by player 0 adds its hit (src/port/coop/test/score_probe.h).
  lives     the sixth gold ring and a 1-up, collected by player 1, add a life to the pool gLifeCount[0] and not to
            player 1's own slot (decision 0007 rule 2, #210).
  wing      EVC_WING_BROKEN fires on player 1's broken wing (#210, the owner's "any active player" of #201): the
            event actor of Corneria's script 39 turns into a wing repair item, and with every wing whole it does not.
  saucerer  Katina's Saucerer takes damage from player 1's shot (decision 0008 1a, #210): put into its fight state as
            Katina's own restart from the checkpoint does, a laser of player 1 in a hatch takes 10 off it, as one of
            player 0 does in another.
  three     the same with three games, so that input goes through the host to the other client.
  control   the same run with SF64_COOP_TEST_NO_LOCKSTEP: it must differ, or this test says nothing.
  pause     Start on either client pauses both on the same frame, Start on either resumes.
  save      the intro skip goes by the host's save, as the level start carries it (#191): with Corneria cleared there,
            the client's Start in the intro skips it on both games and both players fly; with it cleared only in the
            client's save, nobody's Start does anything.
  zero_seed a seed zeroed on both clients: no clock read, so they stay equal.
  reset     a client flew Corneria alone, took a hit on a wing and went back to the title through the reset (F4), in the
            same process: the session's level is equal from frame 0 on (#187).
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
  player_hits Corneria with pilots (#252): each human's own hits count, add up to at most the team's, same on both.
  scenes    Corneria to Granga with the client as Falco (#15): no scene shows a second Falco, his rescue goes to Slippy,
            gTeamShields[Falco] holds the client's shields while it flies, and no computer Falco joins the boss fight.
  allrange  Katina with the client as Falco (#15): Falco's all-range slot is a body on the client's ship, and no
            computer Falco flies while the client does.
  allrange_others the allrange check on Fortuna, Bolse, Sector Z and Venom 2 (#15).
  allrange_down_others the allrange_down check on Fortuna, Bolse, Sector Z and Venom 2 (#15).
  allrange_down Katina with the client as Falco, downed on purpose (#15): Falco's body waits away, gTeamShields[Falco]
            is -1, and no computer Falco takes over.
  audio     the plain run with the two clients at different audio sample counts, as a radio line follows the sound thread.
  release   the plain run on a Release build (--release-exe) and on Debug: all four clients have to hash alike.
  vehicles  Titania, Aquas and Macbeth, the Landmaster and Blue Marine levels: same hash, both drive where co-op has opened.
  vehicle_end a vehicle level to its end: pilots drive it live, one on each client, with gInvincible on both, through
            the boss to level complete. Same hash on every frame. Macbeth takes minutes, Titania about an hour.
  rates     the plain run at real time, with the two clients at different gInterpolationFPS.
  desync_sim src/port/coop/sync/desync.c alone, in a virtual network (tools/coop/desynctest), WP-23.
  desync    a bit flipped on one client, on rails and in all-range: both games name that frame, the players and the
            region, write a report, and the session ends; desync_report.py names the flipped value.
  desync_control the same with SF64_COOP_TEST_NO_DESYNC_CHECK: the hashes differ and nobody says so.
  desync_three a third game that stays in step hears which two differed, and writes its report too.
  handover  the handover (#189): after a desync both folders hold both reports, byte for byte, desync_report.py diff
            reads the two of one folder as it reads the originals, and each folder's copy of the other player's log
            runs to its `session closed`.
  bosses    the bosses and dogfighters go after the nearest human (#213): Area 6's Gorgon, Andross' face and Star Wolf
            in Venom 2, with player 1 the nearer one. Each run ends with a bit flipped, and the boss's or Star Wolf's
            place is read from the desync report's dump, so the check runs on an old build too. Hashes equal up to the
            poke; with src/port/coop/test/boss_trace.h the boss trace is alike on both games and shows player 1 kept.
            `inhale`: the face's inhale swallows player 1, chews and spits it out (its wings break), and player 0 flies
            on and is not hit meanwhile (owner's choice A for #213's option D).
  other_bosses the other bosses go after the nearer human too (#220), read from the boss trace, which the base build
            writes as well: `granga`, Corneria's Granga walks at and turns to player 1 on most frames that player 1 is
            the nearer; `shogun`, Sector Y's Shoguns keep player 1 often and turn toward the player they keep;
            `crusher`, Meteo's Crusher rings home in on player 1 too (SF64_COOP_BOSS_POKE puts the Crusher into its
            ring attack, which takes its shields shot away to reach); `sarumarine`, Zoness' Sarumarine balls fly at
            player 1; `bacoon`, Aquas' Bacoon looks at player 1. On rails player 0 holds the top left and player 1
            the bottom right, the nearer one. Hashes and boss traces equal on both games.
  log     the co-op log (#172): each game's folder holds its own log and the other player's, streamed during the
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
import struct
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
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
import view_check  # noqa: E402

IS_LINUX = platform.system() == "Linux"
BUTTON_A, BUTTON_B, BUTTON_START, BUTTON_L = 0x8000, 0x4000, 0x1000, 0x0020
BUTTON_C_LEFT = 0x0002  # gBoostButton, src/engine/fox_game.c
BOOST_AT, BOOST_FRAMES = 2000, 40  # feed frames of sc_boost
START_FRAME = 600
REPLAY_FRAMES = 30000  # longer than a short run: the games stop on `done`, not at the end of this. A game whose replay
# ends leaves lockstep there (its pads are its own again), so a longer run passes replay_frames to session()
INPUT_DELAY = 3  # COOP_LOCKSTEP_INPUT_DELAY
TIMEOUT_MS = 30000  # the join driver's transport timeout
STALL_SECONDS = 10  # COOP_LOCKSTEP_STALL_MS
SEEDS = "1111,2222,3333,4444,5555,6666"  # every level start of a run: the flight then depends on nothing but the input
JOIN_FRAME = 1700  # a little after both players fly: level frames, 1674 in NOTES.md, #142
SHORT_FRAMES = 2700
AIM_FRAMES = 4000  # Corneria's rails from the join on, long enough for some enemies to fire (#193)
LEVEL_COMPLETE, MAP, PLAY_PAUSE = 7, 4, 100  # PLAYERSTATE_LEVEL_COMPLETE, GSTATE_MAP, PLAY_PAUSE
PLAY_INIT, PLAY_UPDATE, LEVEL_INTRO = 1, 2, 2  # PLAY_INIT, PLAY_UPDATE, PLAYERSTATE_LEVEL_INTRO
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

    def __init__(self, args, folder, name, env, replay_frames=REPLAY_FRAMES):
        self.name = name
        self.run = replay_tool.make_run_dir(args.exe, f"lockstep-{name}")
        self.report_path = self.run / "join-report.txt"
        self.sync_report = self.run / "lockstep-report.txt"
        self.sync_log = self.run / "lockstep-log.txt"
        replay = self.run / "input.sfcr"
        join_test.write_replay(replay, replay_frames, join_test.SEEDS.get(name, join_test.SEEDS["client0"]))
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
            clients=1, client_envs=None, replay_frames=REPLAY_FRAMES):
    """A host and its clients through the join driver, on their way to a level. Returns them started: with one client as
    (host, client), with more as (host, client, client, ...). client_envs holds one more dict per client."""
    folder.mkdir(parents=True, exist_ok=True)
    port_file = folder / "port"
    env = dict(both or {})
    env["SF64_COOP_TEST_START_SEEDS"] = SEEDS
    host = Client(args, folder, "host", {**env, "SF64_COOP_JOIN_PORT_FILE": str(port_file),
                                         "SF64_COOP_JOIN_CLIENTS": str(clients), "SF64_COOP_JOIN_LEVEL": "0",
                                         "SF64_COOP_JOIN_START_FRAME": str(START_FRAME), **(host_env or {})},
                  replay_frames)
    host.start(config)
    deadline = time.monotonic() + 180
    while not (port_file.exists() and port_file.read_text(encoding="utf-8").strip()):
        if host.proc.poll() is not None or time.monotonic() >= deadline:
            host.close()  # a host left running keeps the build from linking Starship.exe
            raise Fail("the host did not report its port")
        time.sleep(0.2)
    port = port_file.read_text(encoding="utf-8").strip()
    if client_port is not None:
        port = client_port(port)
    games = [host]
    for n, pick in zip(range(clients), ("falco", "slippy", "peppy")):
        games.append(Client(args, folder, f"client{n}", {**env, "SF64_COOP_JOIN_CONNECT": f"127.0.0.1:{port}",
                                                         "SF64_COOP_JOIN_PICKS": pick, **(client_env or {}),
                                                         **((client_envs or [{}] * clients)[n])}, replay_frames))
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


AIM_LINE = re.compile(r"(shot|missile) level=(\d+) frame=(\d+) .*player=(-?\d+)$")


def aim_rows(path):
    """(kind, level, frame, player, line) of every line of an aim trace (src/port/coop/test/aim_trace.h)."""
    rows = []
    if path.exists():
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            match = AIM_LINE.match(line)
            if match:
                rows.append((match.group(1), int(match.group(2)), int(match.group(3)), int(match.group(4)), line))
    return rows


def sc_aim(args, folder):
    """Enemies aim at the nearest human (#193). The trace watches the shots and does not ask the aim code, so a build
    with every enemy aiming at player 0 fails here."""
    traces = {name: folder / f"{name}-aim.txt" for name in ("host", "client")}
    for path in traces.values():
        if path.exists():
            path.unlink()  # the trace appends
    host, client = run_short(args, folder, frames=AIM_FRAMES,
                             host_env={"SF64_COOP_AIM_TRACE": str(traces["host"])},
                             client_env={"SF64_COOP_AIM_TRACE": str(traces["client"])})
    frames = compare(host, client, AIM_FRAMES)
    ours, theirs = aim_rows(traces["host"]), aim_rows(traces["client"])
    # A game can run a little past `done` before it is stopped, so the shorter trace has to start the longer one.
    n = min(len(ours), len(theirs))
    for a, b in zip(ours[:n], theirs[:n]):
        expect(a == b, f"the aim traces differ: host `{a[4]}`, client `{b[4]}`")
    at = {player: sum(1 for row in ours if row[3] == player) for player in (-1, 0, 1)}
    expect(at[1] > 0, f"no enemy shot or missile flew at player 1: {at[0]} at player 0, {at[-1]} at nobody")
    return (f"same hash on {frames} frames; {at[1]} shots or missiles at player 1, {at[0]} at player 0, "
            f"{at[-1]} at nobody; {n} trace lines alike")


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


RETURN_FRAMES = 150  # COOP_RETURN_FRAMES, src/port/coop/game/players.c: decision 0007 rule 4
RETURN_SHIELDS = 255  # Play_GetMaxShields without three gold rings: full
RETURN_BOMBS = 3  # as player 0 gets after a death
FOX_BOMBS = 6  # poked into player 0 first, with `shields` of the plan, so a return that copies player 0's numbers shows
# Level frames; a poke lands after its frame (view_trace.h). Rails: the pool is set to 1, player 1 goes down twice, a
# life comes in while it waits at pool 0. All-range: the same after L (gDebugJumpToAllRange) at `jump`. Granga's fight
# takes about 140 of player 0's shields in the 1000 frames after `pool` there, so he starts it higher.
RETURN_PLANS = {
    "rails": {"pool": JOIN_FRAME + 50, "downs": (JOIN_FRAME + 100, JOIN_FRAME + 400), "life": JOIN_FRAME + 800,
              "frames": SHORT_FRAMES, "jump": None, "shields": 160},
    "all-range": {"pool": JOIN_FRAME + 650, "downs": (JOIN_FRAME + 700, JOIN_FRAME + 1000), "life": JOIN_FRAME + 1400,
                  "frames": JOIN_FRAME + 1600, "jump": JOIN_FRAME + 100, "shields": 230},
}


def return_pokes(plan):
    """SF64_COOP_VIEW_POKE for a plan: the same list on both games, so they stay one game."""
    pool = plan["pool"]
    pokes = [f"{pool}:0:lives:1", f"{pool}:0:shields:{plan['shields']}", f"{pool}:0:bombs:{FOX_BOMBS}"]
    pokes += [f"{down}:1:down:4" for down in plan["downs"]]
    pokes.append(f"{plan['life']}:0:lives:2")  # a life comes in: the waiting player takes one of the two
    return ",".join(pokes)


def run_return(args, folder, plan, pokes=None):
    """Both games through one plan, with sway (and L at plan['jump']). Returns the two, stopped."""
    folder.mkdir(parents=True, exist_ok=True)
    frames = plan["frames"]
    feed = folder / "feed.txt"
    jump = plan["jump"]
    write_feed(feed, frames + 16, press(sway, {(jump, 0)}, BUTTON_L) if jump is not None else sway)
    config = {"CVars": {"gDebugJumpToAllRange": 1}} if jump is not None else None
    pokes = pokes or return_pokes(plan)
    env = {"SF64_COOP_LOCKSTEP_FEED": str(feed), "SF64_COOP_LOCKSTEP_END": str(frames), "SF64_COOP_VIEW_POKE": pokes}
    traces = {name: folder / f"{name}-view.txt" for name in ("host", "client0")}
    for path in traces.values():
        if path.exists():
            path.unlink()
    host, client = session(args, folder, host_env={"SF64_COOP_VIEW_TRACE": str(traces["host"])},
                           client_env={"SF64_COOP_VIEW_TRACE": str(traces["client0"])}, both=env, config=config)
    try:
        wait_for([host, client], "done", 900)
    finally:
        host.close()
        client.close()
    return host, client, traces


def check_return(game, view_path, plan, section):
    """What one game shows of the plan, from its player trace and view trace. Returns a line for the summary."""
    rows = [r for r in player_trace.parse(game.run / "trace.txt") if r.level == 0 and len(r.players) > 1]
    by_frame = {r.frame: r for r in rows}
    view = view_check.read_trace(1, view_path).by_frame
    mode = player_trace.ALL_RANGE if section == "all-range" else player_trace.ON_RAILS
    found = []

    def parked_from(frame):
        later = [r for r in rows if r.frame > frame and r.players[1].parked]
        return later[0].frame if later else None

    def back_from(frame):
        later = [r for r in rows if r.frame > frame and not r.players[1].parked]
        return later[0].frame if later else None

    def pool(frame):
        return view[frame].players[0].lives if frame in view else None

    def due_from(frame):
        """A return is due on the first frame from `frame` on that player 0 is ACTIVE: in all-range a U-turn waits."""
        due = [r.frame for r in rows if r.frame >= frame and r.players[0].state == player_trace.ACTIVE]
        return due[0] if due else None

    for n, poke in enumerate(plan["downs"]):
        before = by_frame.get(poke)
        expect(before is not None and before.mode == mode and not before.players[1].parked
               and before.players[1].state == player_trace.ACTIVE,
               f"{game.name}: player 1 does not fly {section} at frame {poke}, where it is poked down: "
               f"{before.players[1] if before else 'no row'}")
        down = parked_from(poke)
        expect(down == poke + 1, f"{game.name}: player 1 went down at frame {down}, want {poke + 1}")
        back = back_from(down)
        # Drawn on the frame it goes down, as since WP-11 (NOTES.md, #9): Player_Update sets draw after Player_Down.
        out = [r for r in rows if down < r.frame < (back if back is not None else rows[-1].frame + 1)]
        drawn = [r.frame for r in out if r.players[1].draw != 0]
        expect(not drawn, f"{game.name}: player 1 drawn while out, first at frame {drawn[0] if drawn else None}")
        if n == 0:
            due = due_from(down + RETURN_FRAMES)
            expect(back is not None, f"{game.name}: player 1 went down at frame {down} and never came back "
                                     f"(pool {pool(down)}, trace to frame {rows[-1].frame})")
            expect(back == due, f"{game.name}: player 1 back at frame {back}, want {due} ({RETURN_FRAMES} after the "
                                f"down at {down}, on a frame player 0 is ACTIVE)")
            r = by_frame[back]
            p0, p1 = r.players[0], r.players[1]
            offset = (p1.x - p0.x, p1.y - p0.y, p1.z - p0.z)
            expect(abs(offset[0] - 250) < 0.2 and abs(offset[1] - 80) < 0.2 and abs(offset[2]) < 0.2,
                   f"{game.name}: player 1 back at {offset} from player 0, want (250, 80, 0)")
            expect(p1.shields == RETURN_SHIELDS and p1.state == player_trace.ACTIVE,
                   f"{game.name}: player 1 back with shields {p1.shields}, state {p1.state}")
            v = view[back].players
            expect(pool(back - 1) == 1 and pool(back) == 0,
                   f"{game.name}: pool {pool(back - 1)} before the return, {pool(back)} after, want 1 and 0")
            expect(v[1].bombs == RETURN_BOMBS and v[1].lives == 0,
                   f"{game.name}: player 1 back with {v[1].bombs} bombs and lives slot {v[1].lives}")
            expect(v[0].bombs == FOX_BOMBS, f"{game.name}: player 0 has {v[0].bombs} bombs, not the poked {FOX_BOMBS}")
            found.append(f"down {down}, back {back} ({back - down} out) beside player 0, shields {p1.shields}, "
                         f"bombs {v[1].bombs}, pool 1 -> 0")
        else:
            life = plan["life"]
            due = due_from(life + 1)
            expect(pool(down) == 0, f"{game.name}: pool {pool(down)} at the second down, want 0")
            expect(back == due, f"{game.name}: player 1 back at frame {back}, want {due}: a life came in after {life}")
            expect(back - down > RETURN_FRAMES, f"{game.name}: back after {back - down} frames at pool 0")
            expect(pool(back) == 1 and by_frame[back].players[1].shields == RETURN_SHIELDS,
                   f"{game.name}: pool {pool(back)} after the return, want 1 (2 came in, 1 spent)")
            found.append(f"down {down} at pool 0, out {back - down} frames until a life came in, back {back}, pool 2 -> 1")
    return "; ".join(found)


PLAYERSTATE_DOWN = 4  # include/sf64player.h
FOX_DOWN_PLAN = {"pool": JOIN_FRAME + 50, "downs": (JOIN_FRAME + 100,), "fox": JOIN_FRAME + 110, "life": None,
                 "frames": SHORT_FRAMES, "jump": None, "shields": 160}


def sc_return_fox_down(args, folder):
    """0007 rule 3 in a level not open for the refill (all of them, for now): player 0 down with a return waiting."""
    plan = FOX_DOWN_PLAN
    pokes = f"{plan['pool']}:0:lives:1,{plan['downs'][0]}:1:down:4,{plan['fox']}:0:down:4"
    host, client, traces = run_return(args, folder, plan, pokes)
    frames = compare(host, client, plan["frames"])
    lines = []
    for game in (host, client):
        rows = [r for r in player_trace.parse(game.run / "trace.txt") if r.level == 0 and len(r.players) > 1]
        restart = next((n for n in range(1, len(rows)) if rows[n].frame < rows[n - 1].frame), None)
        expect(restart is not None, f"{game.name}: the level did not restart after player 0 went down")
        before, after = rows[:restart], rows[restart:]
        view = view_check.read_trace(1, traces[game.name]).rows
        view_restart = next((n for n in range(1, len(view)) if view[n].frame < view[n - 1].frame), None)
        expect(view_restart is not None, f"{game.name}: the view trace has no restart")
        view_before, view_after = view[:view_restart], view[view_restart:]
        down = next((r.frame for r in before if r.frame > plan["downs"][0] and r.players[1].parked), None)
        fox = next((r.frame for r in before if r.frame > plan["fox"] and r.players[0].state == PLAYERSTATE_DOWN), None)
        expect(down == plan["downs"][0] + 1 and fox == plan["fox"] + 1,
               f"{game.name}: player 1 down at {down}, player 0 at {fox}")
        back = [r.frame for r in before if r.frame > down and not r.players[1].parked]
        expect(not back, f"{game.name}: player 1 came back at frame {back[0] if back else None} with player 0 down")
        last = before[-1].frame
        joined = next((r for r in after if not r.players[1].parked), None)
        flies = next((r for r in after if r.players[0].state == player_trace.ACTIVE), None)
        expect(joined is not None and flies is not None and joined.frame == flies.frame,
               f"{game.name}: after the restart player 1 joined at {joined.frame if joined else None}, player 0 flies "
               f"from {flies.frame if flies else None}")
        pools = (view_before[-1].players[0].lives, view_after[-1].players[0].lives, view_after[-1].players[1].lives)
        expect(pools == (0, 0, 0), f"{game.name}: pool {pools[0]} before the restart, {pools[1]} at the end, player "
                                   f"1's slot {pools[2]}; want 0, 0, 0 (player 0's death took the one life)")
        lines.append(f"player 1 down {down}, player 0 down {fox}, due {down + RETURN_FRAMES}, level restarted after "
                     f"frame {last}; player 1 out until then, joined with player 0 on restart frame {joined.frame}, "
                     f"pool 1 -> 0 by player 0's death only")
    expect(lines[0] == lines[1], f"the games differ: host {lines[0]}, client {lines[1]}")
    return f"same hash on {frames} frames; {lines[0]}"


def return_in(args, folder, section):
    plan = RETURN_PLANS[section]
    host, client, traces = run_return(args, folder, plan)
    frames = compare(host, client, plan["frames"])
    lines = [check_return(game, traces[game.name], plan, section) for game in (host, client)]
    expect(lines[0] == lines[1], f"the games saw different returns: host {lines[0]}, client {lines[1]}")
    return f"same hash on {frames} frames; {lines[0]}"


def sc_return(args, folder):
    """#14: a player 2-4 who goes down comes back after RETURN_FRAMES for a life from the pool, at full shields."""
    return return_in(args, folder, "rails")


def sc_return_all_range(args, folder):
    return return_in(args, folder, "all-range")
LASER_AT = 1900  # feed frame a laser item appears in front of player 1 (sc_laser)


def run_item(args, folder, spec):
    """Both games with SF64_COOP_LOCKSTEP_ITEM=spec. Returns (frames, [per game: placed, rows]) where a row is the dict of
    a `lasers` line with strength, wings and states as lists of ints, after hashes and lockstep logs matched."""
    host, client = run_short(args, folder, both={"SF64_COOP_LOCKSTEP_ITEM": spec}, frames=LASER_AT + 200)
    frames = compare(host, client, LASER_AT + 200)
    games = []
    for game in (host, client):
        report = game.sync()
        expect(join_test.said(report, "item"), f"{game.name}: the item was never placed")
        rows = []
        for row in join_test.said(report, "lasers"):
            rows.append({key: ([int(v) for v in row[key].split(",")] if key != "frame" else int(row[key]))
                         for key in ("frame", "strength", "wings", "states")})
        expect(rows, f"{game.name}: no `lasers` line")
        games.append((rows, join_test.said(report, "drop")))
    expect(games[0][0][1:] == games[1][0][1:] or games[0][0][-1] == games[1][0][-1],
           f"the two games saw different pickups: {games[0][0]} / {games[1][0]}")
    return frames, games


BROKEN, INTACT = 11, 22  # 10 * left + right wing state, WINGSTATE_BROKEN and WINGSTATE_INTACT


def sc_laser(args, folder):
    """#201: a laser item in front of player 1 alone. It has to count for player 1 on both clients: gLaserStrength[1] 0
    to 1, its wing health set, player 0 untouched, and the hashes equal."""
    frames, games = run_item(args, folder, f"{LASER_AT}:1:0:8")
    for n, (rows, _) in enumerate(games):
        first, last = rows[0], rows[-1]
        expect(first["strength"][1] == 0 and last["strength"][1] == 1,
               f"game {n}: player 1's lasers {first['strength'][1]} -> {last['strength'][1]}, want 0 -> 1")
        expect(last["strength"][0] == first["strength"][0], f"game {n}: player 0's lasers changed: {rows}")
        expect(first["wings"][1] == 0 and last["wings"][1] == 60,
               f"game {n}: player 1's wing health {first['wings'][1]} -> {last['wings'][1]}, want 0 -> 60")
    last = games[0][0][-1]
    return (f"same hash on {frames} frames; on both games player 1's lasers 0 -> 1 on frame {last['frame']}, its wings "
            f"{last['wings'][1]}, player 0's lasers stay {last['strength'][0]}")


def sc_laser_wing1(args, folder):
    """#201: player 1's wing is broken, it collects a laser item: the wings are repaired, the lasers are not raised, and
    Fox is untouched (the owner's rule: the collector decides)."""
    frames, games = run_item(args, folder, f"{LASER_AT}:1:0:2")
    for n, (rows, _) in enumerate(games):
        last = rows[-1]
        expect(last["states"][1] == INTACT, f"game {n}: player 1's wings were not repaired: {last}")
        expect(last["strength"] == [0, 0], f"game {n}: lasers changed with a wing to repair: {last}")
        expect(last["wings"][1] > 0, f"game {n}: player 1's wing health was not set: {last}")
        expect(last["states"][0] == INTACT and last["wings"][0] == rows[0]["wings"][0],
               f"game {n}: Fox's wings changed: {rows[0]} -> {last}")
    return f"same hash on {frames} frames; player 1's wings repaired, lasers stay 0, Fox untouched, on both games"


def sc_laser_wing0(args, folder):
    """#201: only Fox's wing is broken, player 1 collects a laser item: player 1's lasers 0 -> 1, Fox's wings stay
    broken (before, the item turned into a wing repair for Fox before anybody reached it)."""
    frames, games = run_item(args, folder, f"{LASER_AT}:1:0:1")
    for n, (rows, _) in enumerate(games):
        last = rows[-1]
        expect(last["strength"] == [0, 1], f"game {n}: lasers {last['strength']}, want 0,1")
        expect(last["states"][0] == BROKEN, f"game {n}: Fox's wings changed: {last}")
    return f"same hash on {frames} frames; player 1's lasers 0 -> 1, Fox's wings stay broken, on both games"


def sc_repair(args, folder):
    """#201: a wing repair item collected by player 1 repairs player 1's wings, not Fox's (both are broken)."""
    frames, games = run_item(args, folder, f"{LASER_AT}:1:1:3")
    for n, (rows, _) in enumerate(games):
        last = rows[-1]
        expect(last["states"][1] == INTACT, f"game {n}: player 1's wings were not repaired: {last}")
        expect(last["states"][0] == BROKEN, f"game {n}: Fox's wings were repaired: {last}")
    return f"same hash on {frames} frames; player 1's wings repaired, Fox's stay broken, on both games"


def sc_crate(args, folder):
    """#201: a supply crate bursts with Fox at twin lasers and player 1 at single: it drops lasers (anyone's need)."""
    frames, games = run_item(args, folder, f"{LASER_AT}:1:2:4")
    for n, (_, drops) in enumerate(games):
        expect(drops, f"game {n}: the crate dropped nothing")
        expect(drops[0]["lasers"] == "1", f"game {n}: the crate dropped item {drops[0]['id']}, not lasers")
    return f"same hash on {frames} frames; the crate dropped lasers on both games"

# ---- #210: who scores, where a life goes, the wing-broken event, the Saucerer (src/port/coop/test/score_probe.h) ----
SCORE_AT = 1900  # feed frame of the pokes on Corneria's rails (lives, wing), as LASER_AT
SCORE_ALL_RANGE_AT = JOIN_FRAME + 650  # kill: after L at JOIN_FRAME + 100, as return_all_range; both fly by then
SAUCERER_FROM = 600  # Katina: from here the poke waits for the first frame both players fly
LEVEL_KATINA = 16  # include/sf64level.h
EVID_ITEM_WING_REPAIR, EVID_FFF = 71, 4095  # include/sf64event.h; EVID_FFF is what ActorEvent_Load starts with
DMG_SRC_2, FALCO_SHOT = 2, 102  # include/sf64player.h; NPC_SHOT_ID + AI360_FALCO + 1, as CoopPlayers_DmgSource gives it
PLAYER_1_KILL, PLAYER_0_KILL = 11, 1  # COOP_DMG_SRC_PLAYER + 1 (src/port/coop/game/players.h); AI360_FOX + 1
SCORE_LINE = re.compile(r"(\w+)=(\S+)")


def steer(frame, player):
    """sway with the fire button never down: only the probe's enemies die by a player's hand."""
    _, x, y = sway(frame, player)
    return 0, x, y


def score_rows(path):
    """The score probe's report as [(kind, {field: text})], or [] without one (a build without score_probe.c)."""
    if not path.exists():
        return []
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        kind, _, rest = line.partition(" ")
        rows.append((kind, dict(SCORE_LINE.findall(rest))))
    return rows


def run_score(args, folder, probe, frames, level=None, jump=None):
    """Both games with SF64_COOP_SCORE_PROBE=probe, steered by `steer` (and L at `jump`). Returns (frames compared, the
    report rows), after hashes and lockstep logs matched and both games reported alike."""
    folder.mkdir(parents=True, exist_ok=True)
    feed = folder / "feed.txt"
    write_feed(feed, frames + 16, press(steer, {(jump, 0)}, BUTTON_L) if jump is not None else steer)
    config = {"CVars": {"gDebugJumpToAllRange": 1}} if jump is not None else None
    reports = {name: folder / f"{name}-score.txt" for name in ("host", "client")}
    for path in reports.values():
        if path.exists():
            path.unlink()
    env = {"SF64_COOP_LOCKSTEP_FEED": str(feed), "SF64_COOP_LOCKSTEP_END": str(frames), "SF64_COOP_SCORE_PROBE": probe}
    host_env = {"SF64_COOP_SCORE_REPORT": str(reports["host"])}
    if level is not None:
        host_env["SF64_COOP_JOIN_LEVEL"] = str(level)
    host, client = session(args, folder, host_env, {"SF64_COOP_SCORE_REPORT": str(reports["client"])}, env,
                           config=config)
    try:
        wait_for([host, client], "done", 900)
    finally:
        host.close()
        client.close()
    compared = compare(host, client, frames)
    rows = [score_rows(reports[name]) for name in ("host", "client")]
    expect(rows[0], "no score probe report (a build without src/port/coop/test/score_probe.c)")
    expect(rows[0] == rows[1], f"the two games saw different things: host {rows[0]}, client {rows[1]}")
    for kind, row in rows[0]:
        expect(kind != "error", f"the probe said: error {row}")
    return compared, rows[0]


def fly_rows(rows):
    """Every `players` line of a report says player 0 and player 1 fly at the poke (ACTIVE 3 or U_TURN 5, unparked)."""
    for kind, row in rows:
        if kind == "players":
            expect(all(state in ("3", "5") for state in row["states"].split(",")) and row["parked"] == "0,0",
                   f"a poke with the players not both flying: {row}")


def sc_kill(args, folder):
    """#210, decision 0008 4b: a kill by any human scores as one by player 0 does, DMG_SRC_2 and computer wingmen
    score nothing, and the hit-count drop (14: lasers) follows a kill of player 1 in all-range."""
    frames, rows = run_score(args, folder, f"kill:{SCORE_ALL_RANGE_AT}:1", SCORE_ALL_RANGE_AT + 200,
                             jump=JOIN_FRAME + 100)
    fly_rows(rows)
    kills = {int(row["source"]): row for kind, row in rows if kind == "kill"}
    expect(set(kills) == {PLAYER_1_KILL, DMG_SRC_2, FALCO_SHOT, PLAYER_0_KILL}, f"kills seen: {sorted(kills)}")
    texts = []
    for source, gained, dropped in ((PLAYER_1_KILL, 1, 1), (DMG_SRC_2, 0, 0), (FALCO_SHOT, 0, 0),
                                    (PLAYER_0_KILL, 1, 0)):
        row = kills[source]
        hits = [int(v) for v in row["hits"].split(",")]
        drop = [int(v) for v in row["drop"].split(",")]
        lasers = [int(v) for v in row["lasers"].split(",")]
        expect(row["gone"] == "1", f"the enemy of source {source} did not die: {row}")
        expect(hits[1] - hits[0] == gained,
               f"a kill of source {source}: gHitCount {hits[0]} -> {hits[1]}, want +{gained}")
        if source == PLAYER_1_KILL:
            expect(drop == [14, 0] and lasers[1] - lasers[0] == 1,
                   f"player 1's kill at drop counter 14: counter {drop[0]} -> {drop[1]}, laser items {lasers[0]} -> "
                   f"{lasers[1]}; want 14 -> 0 and one more")
        texts.append(f"source {source} +{hits[1] - hits[0]}")
    return (f"same hash on {frames} frames; on both games {', '.join(texts)} (player 1, DMG_SRC_2, Falco, player 0); "
            "player 1's kill dropped lasers at the counter's 14")


def sc_lives(args, folder):
    """#210, decision 0007 rule 2: the sixth gold ring and a 1-up taken by player 1 go to the pool, not slot 1."""
    frames, rows = run_score(args, folder, f"lives:{SCORE_AT}:1", SCORE_AT + 200)
    fly_rows(rows)
    lives = [row for kind, row in rows if kind == "lives"]
    expect(lives, "no `lives` line")
    first, last = lives[0], lives[-1]
    expect(first["rings"] == "5", f"the ring count was not set: {first}")
    expect(last["rings"] == "6", f"player 1 did not take the sixth ring: {last}")
    pool, slot = (int(first["pool"]), int(last["pool"])), (int(first["slot"]), int(last["slot"]))
    expect(pool[1] == pool[0] + 2 and slot[1] == slot[0],
           f"pool {pool[0]} -> {pool[1]}, player 1's slot {slot[0]} -> {slot[1]}; want the pool +2 (ring and 1-up), "
           "the slot unchanged")
    return (f"same hash on {frames} frames; on both games the sixth ring and a 1-up of player 1: pool {pool[0]} -> "
            f"{pool[1]}, player 1's slot stays {slot[1]}")


def sc_wing(args, folder):
    """#210: EVC_WING_BROKEN fires when player 1's wing is broken and player 0's is whole, and not with every wing
    whole: CO S39 turns into a wing repair item, or despawns."""
    frames, rows = run_score(args, folder, f"wing:{SCORE_AT}:1", SCORE_AT + 200)
    fly_rows(rows)
    events = {}
    for kind, row in rows:
        if kind == "event":
            events.setdefault(row["n"], []).append(int(row["type"]))
    expect(set(events) == {"1", "2"}, f"events seen: {events}")
    expect(EVID_ITEM_WING_REPAIR in events["1"],
           f"player 1's broken wing did not fire EVC_WING_BROKEN: event 1 went through {events['1']}")
    expect(events["2"] == [EVID_FFF] * len(events["2"]), f"with every wing whole the event fired: {events['2']}")
    return (f"same hash on {frames} frames; on both games player 1's broken wing made CO S39 a wing repair "
            f"(type {EVID_ITEM_WING_REPAIR}), with every wing whole it stayed {EVID_FFF} and despawned")


def sc_saucerer(args, folder):
    """#210, decision 0008 1a: Katina's Saucerer takes damage from player 1's shot, as from player 0's."""
    frames, rows = run_score(args, folder, f"saucerer:{SAUCERER_FROM}:1", SAUCERER_FROM + 600, level=LEVEL_KATINA)
    fly_rows(rows)
    shots = [row for kind, row in rows if kind == "shot"]
    hatches = [[int(v) for v in row["hp"].split(",")] for kind, row in rows if kind == "hatch"]
    expect(len(shots) == 2, f"shots placed: {shots}")
    expect(hatches, "no `hatch` line: the Saucerer was not put into its fight")
    expect(hatches[-1][1] == 90, f"player 0's shot did not hit the second hatch: {hatches}")
    expect(hatches[-1][0] == 90, f"player 1's shot did not hurt the Saucerer: hatches {hatches[0]} -> {hatches[-1]}")
    return (f"same hash on {frames} frames; on both games player 1's laser took the first hatch {hatches[0][0]} -> "
            f"{hatches[-1][0]}, player 0's the second {hatches[0][1]} -> {hatches[-1][1]}, from frame "
            f"{shots[0]['frame']} (the fight poked in)")

# Name, LevelId (include/sf64level.h), and whether co-op has opened it: there player 1 drives too, elsewhere it stays
# parked (CoopPlayers_SectionSupported, src/port/coop/game/players.c).
VEHICLE_LEVELS = (("Titania", 12, True), ("Aquas", 13, True), ("Macbeth", 11, True))
VEHICLE_FRAMES = 6000  # past each level's intro and well into the drive
STATE_ACTIVE = 3  # PLAYERSTATE_ACTIVE


def vehicle_story(game):
    """From a game's trace: frames on which player 0 was active, the forms it had then, and frames player 1 was not
    parked."""
    rows = player_trace.parse(game.run / "trace.txt")
    active = [r for r in rows if r.players and r.players[0].state == STATE_ACTIVE]
    forms = sorted({r.players[0].form for r in active})
    joined = sum(1 for r in rows if len(r.players) > 1 and not r.players[1].parked)
    return len(active), forms, joined


TORPEDO_PERIOD = 150  # frames between player 1's torpedoes on Aquas; one flies for up to 300 (D_i3_801C4454)
TORPEDO_RE = re.compile(r" torpedo=(-?\d+):(\d+):(\d+):(-?[\d.]+),(-?[\d.]+),(-?[\d.]+)")


def sway_torpedo(frame, player):
    """sway, and player 1 fires a torpedo (B) every TORPEDO_PERIOD frames. Player 0 never does, so the one torpedo
    slot is free for player 1."""
    button, x, y = sway(frame, player)
    if player == 1 and frame >= JOIN_FRAME and (frame - JOIN_FRAME) % TORPEDO_PERIOD == TORPEDO_PERIOD // 2:
        button |= BUTTON_B
    return button, x, y


def torpedo_story(game):
    """From a game's trace: frames player 1's torpedo flew, frames it homed on a target, and the longest way one
    torpedo went (from the first frame it showed to the last of an unbroken run)."""
    flew = homed = 0
    longest = 0.0
    start = last = None
    for line in (game.run / "trace.txt").read_text().splitlines():
        m = TORPEDO_RE.search(line)
        if not m or int(m.group(1)) != 1:
            start = None
            continue
        flew += 1
        homed += int(m.group(2)) != 0 and int(m.group(3)) != 0
        last = tuple(float(v) for v in m.groups()[3:])
        start = start or last
        longest = max(longest, math.dist(start, last))
    return flew, homed, longest


def sc_vehicles(args, folder):
    """The Landmaster and Blue Marine levels with two clients (WP-18, #16), the three levels side by side. Both steer
    and fire from the join frame on. Hashes equal on every frame. In a level co-op has opened, player 1
    drives along with player 0; in one it has not opened it stays parked (decision 0009, item 6: V2 until the level is
    proven). All three are open since #16.
    On Aquas player 1 also fires torpedoes: one has to fly and at least one has to home on a target."""
    only = getattr(args, "levels", None)
    levels = [entry for entry in VEHICLE_LEVELS if not only or entry[0].lower() in only]

    def one(entry):
        name, level, opened = entry
        script = sway_torpedo if name == "Aquas" else sway
        host, client = run_short(args, folder / name.lower(), script=script, frames=VEHICLE_FRAMES,
                                 host_env={"SF64_COOP_JOIN_LEVEL": str(level)})
        frames = compare(host, client, VEHICLE_FRAMES)
        active, forms, joined = vehicle_story(client)
        expect(active > 1000, f"{name}: player 0 drove on only {active} frames")
        if opened:
            expect(joined > 1000, f"{name}: player 1 drove on only {joined} frames")
            moved = flown(client)
            expect(moved[1] > 500, f"{name}: player 1 did not steer, moved {moved[1]:.0f}")
        else:
            expect(joined == 0, f"{name}: player 1 was not parked on {joined} frames")
        text = f"{name} {frames} frames, player 0 active on {active}, player 1 on {joined}, form {forms}"
        if script is sway_torpedo:
            flew, homed, longest = torpedo_story(client)
            expect(flew > 30 and longest > 1000, f"{name}: player 1's torpedo flew on {flew} frames, {longest:.0f} far")
            expect(homed > 0, f"{name}: player 1's torpedo never homed ({flew} frames in flight)")
            text += f", player 1's torpedo flew on {flew} frames (up to {longest:.0f} far), homed on {homed}"
        return text

    with ThreadPoolExecutor(max_workers=len(levels)) as pool:
        futures = [pool.submit(one, entry) for entry in levels]
    return "same hash; " + "; ".join(future.result() for future in futures)


# ---- #213: the bosses and the dogfighters go after the nearest human ----
#
# Each run flies one level with gInvincible on both games, player 0 steered away and player 1 left near where the
# fight is, and ends with a bit of player 2 flipped on the client at BOSS_POKE[name]: both games then write a desync
# report whose dump holds the state of that frame as hashed. What a boss went after is read from the host's dump, so the
# same check runs on any build, an old one included (the base build fails it). A build with
# src/port/coop/test/boss_trace.h also writes the boss trace, and its frames up to the poke are checked too.
BOSS_RUNS = {  # name: level, frame of the poke (state hash numbering), what the dump is checked for
    "gorgon": (3, 9800),
    "andross": (9, 2900),
    "dogfight": (19, 4100),
    "inhale": (9, 3300),
}
OBJ_BOSS_A6_GORGON, OBJ_BOSS_AND_ANDROSS, OBJ_ACTOR_ALLRANGE = 302, 320, 197  # include/sf64object.h
OBJ_ACTIVE = 2
AI360_WOLF, AI360_ANDREW = 4, 7  # Star Wolf, gActors[4] to [7] in Venom 2
# Offsets in the structs as hashed (x86-64): Object.pos (include/sf64object.h), Player.cam.eye, pos and trueZpos
# (include/sf64player.h).
OBJECT_POS, PLAYER_EYE, PLAYER_POS, PLAYER_TRUE_Z = 0x04, 0x40, 0x74, 0x138
BOSS_TRACE_LINE = re.compile(r"(\w+)=(\S+)")


GORGON_NUDGE = 9200  # level frame: Area 6's Gorgon comes at about 9430; debris knocks the ships about before that
ANDROSS_WALLS = 1560  # level frame: the tunnel ends and the face is up from about 1520
INHALE_BOOST = 2190  # level frame: the face's first inhale (state 17) runs from about 2170 in the andross script
INHALE_MASH = 2300  # from here player 1 presses A every other frame: a spat-out player leaves the mouth on presses


def boss_script(name):
    """Player 0 turns away (all-range) or holds the left wall (rails) once player 1 has joined. Player 1 keeps its stick
    still, so it stays where it joined, near the middle, where the fight comes. In Area 6 it drifts with the debris, so
    just before the Gorgon it goes right for 30 frames and left for 15: from any place on the path that ends it well
    away from the left wall (a ship crosses about 30 a frame). Andross: see below."""
    def script(frame, player):
        if name in ("andross", "inhale"):
            # The face's punches knock ships about and its inhale pulls player 0 to the middle, so both hold a wall:
            # player 0 the left one and the top, player 1 the right one at the height it joined, nearer the face.
            # Not before the tunnel ends, or both scrape its walls the whole way and break their wings.
            if frame < ANDROSS_WALLS:
                return 0, 0, 0
            if player == 0:
                return 0, -70, -60
            if name == "inhale" and INHALE_BOOST <= frame < INHALE_BOOST + 60:
                return BUTTON_C_LEFT, 70, 0  # a boost takes player 1 400 ahead, into the mouth first
            if name == "inhale" and frame >= INHALE_MASH and frame % 2 == 0:
                return BUTTON_A, 70, 0
            return 0, 70, 0
        if frame < JOIN_FRAME + 100:
            return 0, 0, 0
        if player == 0:
            return 0, -70, 0
        if name == "gorgon" and GORGON_NUDGE <= frame < GORGON_NUDGE + 30:
            return 0, 70, 0
        if name == "gorgon" and GORGON_NUDGE + 30 <= frame < GORGON_NUDGE + 45:
            return 0, -70, 0
        return 0, 0, 0
    return script


def vec(data, offset):
    return struct.unpack_from("<3f", data, offset)


def dump_state(report):
    """{players: [(pos x, y, trueZpos, eye x, y)], bosses: {slot: (id, pos)}, wolves: {slot: pos}} of a desync report."""
    state = {"players": [], "bosses": {}, "wolves": {}, "wings": {}}
    for chunk in report.chunks:
        data = bytes(chunk.data)
        if chunk.name == "gPlayer":
            x, y, _ = vec(data, PLAYER_POS)
            eye = vec(data, PLAYER_EYE)
            state["players"].append((x, y, struct.unpack_from("<f", data, PLAYER_TRUE_Z)[0], eye[0], eye[1]))
        elif chunk.name in ("gLeftWingHealth", "gRightWingHealth"):
            state["wings"][(chunk.name, chunk.index)] = struct.unpack_from("<i", data, 0)[0]
        elif chunk.name == "gBosses" and data[0] != 0:
            state["bosses"][chunk.index] = (struct.unpack_from("<H", data, 2)[0], vec(data, OBJECT_POS))
        elif (chunk.name == "gActors" and data[0] == OBJ_ACTIVE and struct.unpack_from("<H", data, 2)[0] ==
              OBJ_ACTOR_ALLRANGE and AI360_WOLF <= chunk.index <= AI360_ANDREW):
            state["wolves"][chunk.index] = vec(data, OBJECT_POS)
    return state


def boss_trace(path):
    """The boss trace as a list of {field: text}, or [] without one (a build that has no boss_trace.c)."""
    if not path.exists():
        return []
    return [dict(BOSS_TRACE_LINE.findall(line)) for line in path.read_text(encoding="utf-8").splitlines() if line]


def boss_run(args, folder, name):
    """One run of BOSS_RUNS: returns (the host's dump state, the host's boss trace)."""
    level, poke = BOSS_RUNS[name]
    folder.mkdir(parents=True, exist_ok=True)
    feed = folder / "feed.txt"
    frames = poke + 400
    write_feed(feed, frames + 16, boss_script(name))
    traces = {game: folder / f"{game}-boss.txt" for game in ("host", "client")}
    for path in traces.values():
        if path.exists():
            path.unlink()  # the trace appends
    env = {"SF64_COOP_LOCKSTEP_FEED": str(feed), "SF64_COOP_LOCKSTEP_END": str(frames)}
    host, client = session(args, folder, {"SF64_COOP_JOIN_LEVEL": str(level), "SF64_COOP_BOSS_TRACE": str(traces["host"])},
                           {"SF64_COOP_BOSS_TRACE": str(traces["client"]), "SF64_COOP_LOCKSTEP_POKE": str(poke),
                            "SF64_COOP_HASH_POKE_PLAYER": "1"}, env, config={"CVars": {"gInvincible": 1}})
    try:
        wait_for([host, client], "over", 900, "after the flipped bit")
    finally:
        host.close()
        client.close()
    # Every frame of the level before the poke hashes alike on both games. The poke counts level frames, as rows() does.
    names, _, ours = host.rows()
    _, _, theirs = client.rows()
    expect(min(len(ours), len(theirs)) >= poke, f"{name}: only {min(len(ours), len(theirs))} level frames")
    difference = join_test.first_difference(names, ours[:poke], theirs[:poke])
    expect(difference is None, f"{name}: the hash logs differ at {difference}")
    found = own_reports(host)
    expect(len(found) == 1, f"{name}: the host wrote {len(found)} desync reports")
    report = desync_report.Report(found[0])
    expect(report.header.get("frame") == str(poke) and not report.verify(), f"{name}: the host's dump is not of {poke}")
    # The trace has a line per frame from the level start on, and after the poke the two games part (they play on
    # after the session ends), so only the lines well before the poke have to be alike. Those are the ones checked.
    trace, theirs_trace = boss_trace(traces["host"])[:poke - 40], boss_trace(traces["client"])[:poke - 40]
    expect(trace == theirs_trace, f"{name}: the boss traces differ before the poke")
    return dump_state(report), trace, poke


def nearer(a, b, point):
    """Which of a and b (x, y) is nearer to point (x, y), as 0 or 1."""
    return 0 if math.dist(a, point) <= math.dist(b, point) else 1


def check_boss_dump(name, state):
    """What the dump says the boss or Star Wolf went after. Returns a line."""
    p0, p1 = state["players"][0], state["players"][1]
    if name == "inhale":
        # Spat out, the swallowed player's wings break (Andross_AndAndross_Update, state 16): its wing health is 0.
        wings = {player: (state["wings"].get(("gLeftWingHealth", player)), state["wings"].get(("gRightWingHealth", player)))
                 for player in (0, 1)}
        text = f"wing health of player 0 {wings[0]}, of player 1 {wings[1]}"
        expect(wings[1] == (0, 0) and wings[0][0] > 0 and wings[0][1] > 0, f"inhale: player 1 was not spat out: {text}")
        return text
    if name == "dogfight":
        wolves = state["wolves"]
        expect(wolves, "dogfight: no Star Wolf in the dump")
        at = {slot: (math.dist(pos, p0[:3]), math.dist(pos, p1[:3])) for slot, pos in wolves.items()}
        on1 = [slot for slot, (d0, d1) in at.items() if d1 < d0]
        text = ", ".join(f"gActors[{slot}] {d0:.0f} from player 0, {d1:.0f} from player 1" for slot, (d0, d1) in at.items())
        expect(len(on1) * 2 > len(at), f"dogfight: Star Wolf stays with player 0: {text}")
        return text
    boss = next((pos for sid, pos in state["bosses"].values()
                 if sid == (OBJ_BOSS_A6_GORGON if name == "gorgon" else OBJ_BOSS_AND_ANDROSS)), None)
    expect(boss is not None, f"{name}: no boss in the dump")
    # The Gorgon warps to the camera of whom it follows; Andross' face goes to that player's ship.
    a, b = (p0[3:5], p1[3:5]) if name == "gorgon" else (p0[:2], p1[:2])
    text = (f"boss at x {boss[0]:.0f}, y {boss[1]:.0f}; player 0 {a[0]:.0f},{a[1]:.0f}, "
            f"player 1 {b[0]:.0f},{b[1]:.0f}")
    expect(math.dist(a, b) > 500, f"{name}: the players are not apart: {text}")
    expect(nearer(a, b, boss[:2]) == 1, f"{name}: the boss is with player 0: {text}")
    return text


def boss_trace_story(name, trace):
    """From the boss trace: frames on which the boss (or a Star Wolf) kept player 1, and on which it kept player 0."""
    kept = {0: 0, 1: 0}
    for row in trace:
        if name == "dogfight":
            values = [row[k].split(",")[8] for k in row if k in ("a4", "a5", "a6", "a7")]
        else:
            values = [row["b0"].split(",")[7]] if "b0" in row else []
        for value in values:
            if value in ("0", "1"):
                kept[int(value)] += 1
    return kept


def inhale_story(trace):
    """From the boss trace of `inhale`: player 1 swallowed, chewed, spat out and flying again, player 0 flying and not
    hit by the face meanwhile. Returns a line."""
    def player(row, i):
        values = row.get(f"p{i}", "").split(",")
        return values if len(values) >= 12 else None

    mouth = [n for n, row in enumerate(trace) if player(row, 1) and player(row, 1)[0] == "11"]
    expect(mouth, "inhale: player 1 was never in the mouth (PLAYERSTATE_ANDROSS_MOUTH)")
    first, last = mouth[0], mouth[-1]
    expect(last - first + 1 == len(mouth), "inhale: player 1 left the mouth and came back")
    spat = [n for n in mouth if player(trace[n], 1)[10] == "1"]
    expect(spat, "inhale: player 1 was never spat out (csState 1)")
    expect(last + 1 < len(trace) and player(trace[last + 1], 1)[0] == "3",
           "inhale: player 1 did not fly again after the mouth")
    hits = []
    nearest = None
    before = int(player(trace[first - 1], 0)[9]) if first > 0 else 0
    for n in range(first, last + 1):
        p0 = player(trace[n], 0)
        expect(p0[0] == "3" and p0[1] == "0", f"inhale: player 0 was not flying at {trace[n]['frame']}: state {p0[0]}")
        if int(p0[9]) > before:  # Player_ApplyDamage sets hitTimer to 20, gInvincible or not; it counts down after
            hits.append(f"{trace[n]['frame']} ({'an effect' if p0[11] == '0' else 'a hitbox, direction ' + p0[11]})")
        before = int(p0[9])
        if "b0" in trace[n]:
            b = [float(v) for v in trace[n]["b0"].split(",")[2:5]]
            d = math.dist(b, (float(p0[2]), float(p0[3]), float(p0[4])))
            nearest = d if nearest is None else min(nearest, d)
    # A bump is the face's hitbox (Player_CollisionCheck, direction 1 to 4). The inhale's debris fly back out once it
    # ends, as they do upstream when the inhale swallows nobody, and may hit a flying player 0: an effect, direction 0,
    # reported and not counted as a bump.
    bumps = [hit for hit in hits if "hitbox" in hit]
    expect(not bumps, f"inhale: the face bumped player 0 while player 1 was in the mouth, at {', '.join(bumps)}; "
                      f"nearest the face {nearest:.0f}")
    return (f"player 1 in the mouth on {len(mouth)} frames ({trace[first]['frame']} to {trace[last]['frame']}), spat out "
            f"on {len(spat)}, then flying; player 0 flying throughout, not bumped by the face, nearest it {nearest:.0f}; "
            f"other hits on player 0 meanwhile: {', '.join(hits) or 'none'}")


def sc_bosses(args, folder):
    """#213: Area 6's Gorgon, Andross' face and Star Wolf in Venom 2 go after player 1 when it is the nearer human,
    and the face's inhale swallows, chews and spits out player 1 while player 0 flies on unhit, with the hashes equal on
    both games up to the poke. --levels picks some of gorgon,andross,dogfight,inhale."""
    only = getattr(args, "levels", None)
    lines = []
    for name in BOSS_RUNS:
        if only and name not in only:
            continue
        state, trace, frames = boss_run(args, folder / name, name)
        text = check_boss_dump(name, state)
        expect(trace, f"{name}: no boss trace (a build without src/port/coop/test/boss_trace.c)")
        if name == "inhale":
            lines.append(f"{name}: same hash on {frames} frames; {text}; {inhale_story(trace)}")
            continue
        kept = boss_trace_story(name, trace)
        expect(kept[1] > kept[0], f"{name}: kept player 1 on {kept[1]} frames, player 0 on {kept[0]}")
        lines.append(f"{name}: same hash on {frames} frames; {text}; kept player 1 on {kept[1]} frames, player 0 on "
                     f"{kept[0]}")
    return "; ".join(lines)


# ---- #220: the other bosses go after the nearest human ----
#
# Each run flies one level with gInvincible on both games and reads the boss trace (src/port/coop/test/boss_trace.h,
# in every build since #213, so the base build runs the same check and fails it): where the boss is, where it faces or
# goes, and where each player is, frame by frame. The two games' traces and hash logs must be alike up to the end.
# Frames of the plans are level frames (the feed's), as in BOSS_RUNS.


GRANGA_CIRCLE = JOIN_FRAME + 476  # level frame: player 1, flying straight, passes the Granga's start about here


def granga_script(frame, player):
    """Corneria: player 0 presses L (gDebugJumpToAllRange) at JOIN_FRAME + 100, and once the cut to all-range is over
    it circles to the left, about 4500 from the middle of the Granga's ground; player 1 flies straight into the middle
    and circles there, so on most frames of the fight it is the nearer one."""
    if frame == JOIN_FRAME + 100 and player == 0:
        return BUTTON_L, 0, 0
    if frame < JOIN_FRAME + 380:
        return 0, 0, 0
    if player == 0:
        return 0, -70, 0
    return (0, 70, 0) if frame >= GRANGA_CIRCLE else (0, 0, 0)


def rails_script(frame, player):
    """A rails level: from JOIN_FRAME + 100 player 0 holds the top left and player 1 the bottom right, so the two are
    about 2200 apart across the path and player 1, nearer the water or the ground where these bosses are, is the
    nearer one."""
    if frame < JOIN_FRAME + 100:
        return 0, 0, 0
    return (0, -70, -60) if player == 0 else (0, 70, 60)


# Meteo's Crusher fires its rings (states 9 and 10) only once its shields are shot away, and nobody shoots here: as it
# reaches its fight (state 3) SF64_COOP_BOSS_POKE puts it into state 10, the rings, which it then repeats (Meteo_
# MeCrusher_Update, states 9 and 10).
CRUSHER_POKE = "0:3:10:40"
OTHER_BOSS_RUNS = {  # name: level, frames, script, CVars beyond gInvincible, environment of both games
    "granga": (0, JOIN_FRAME + 100 + 3900, granga_script, {"gDebugJumpToAllRange": 1}, {}),
    "shogun": (5, JOIN_FRAME + 100 + 3900, granga_script, {"gDebugJumpToAllRange": 1}, {}),  # Sector Y's all-range
    "crusher": (1, 19000, rails_script, {}, {"SF64_COOP_BOSS_POKE": CRUSHER_POKE}),  # Meteo: ~14500 after the intro
    "sarumarine": (8, 19000, rails_script, {}, {}),  # Zoness: the Sarumarine comes about 14000 frames after the intro
    "bacoon": (13, 9000, rails_script, {}, {}),  # Aquas: the Bacoon is close to where the players join
}
OBJ_BOSS_AQ_BACOON = 318  # include/sf64object.h
GRANGA_WALKS = (1, 2, 3)  # GRANGA_FORWARD_MISSILE, _BACKWARDS_LASER_PLASMA, _FORWARD_LASER_PLASMA: it walks at its target
FACING, NOT_FACING = 20.0, 40.0  # degrees: the boss faces a player within FACING, and the other one not within NOT_FACING
TURNING = 0.3  # degrees a frame: less is not a turn


def other_boss_run(args, folder, name):
    """One run of OTHER_BOSS_RUNS: both games to the plan's last frame. Returns (the host's boss trace, frames)."""
    level, frames, script, cvars, both = OTHER_BOSS_RUNS[name]
    folder.mkdir(parents=True, exist_ok=True)
    feed = folder / "feed.txt"
    write_feed(feed, frames + 16, script)
    traces = {game: folder / f"{game}-boss.txt" for game in ("host", "client")}
    for path in traces.values():
        if path.exists():
            path.unlink()  # the trace appends
    env = {"SF64_COOP_LOCKSTEP_FEED": str(feed), "SF64_COOP_LOCKSTEP_END": str(frames), **both}
    host, client = session(args, folder, {"SF64_COOP_JOIN_LEVEL": str(level), "SF64_COOP_BOSS_TRACE": str(traces["host"])},
                           {"SF64_COOP_BOSS_TRACE": str(traces["client"])}, env,
                           config={"CVars": {"gInvincible": 1, **cvars}})
    try:
        wait_for([host, client], "done", 900)
    finally:
        host.close()
        client.close()
    compared = compare(host, client, frames)
    # A game runs on a little past `done` before it is stopped, by a number of frames that differs from run to run: the
    # lines both traces hold are compared, and the checks read only the plan's frames, so they count the same each run.
    ours, theirs = boss_trace(traces["host"]), boss_trace(traces["client"])
    n = min(len(ours), len(theirs))
    expect(n >= frames - 40, f"{name}: only {n} lines of boss trace")
    expect(ours[:n] == theirs[:n], f"{name}: the boss traces differ")
    return ours[:min(n, frames)], compared


def facing_story(trace, slot, states, boss_y=None):
    """How the boss in `slot`, in one of `states`, turns (its rot y) on the frames that player 0 and player 1 lie to
    different sides of where it faces: toward player 1 (`turn1`) or toward player 0 (`turn0`). A boss turns at a
    capped rate, so a player that circles close by is seldom right in front of it; the way it turns shows whom it goes
    after. Also: the frames it faces one player within FACING and not the other within NOT_FACING, the frames player 1
    is the nearer, and its kept slot per value."""
    def turn(a):
        return (a + 180.0) % 360.0 - 180.0

    count = {"walk": 0, "near1": 0, "face1": 0, "face0": 0, "turn1": 0, "turn0": 0}
    kept = {}
    last = None
    for row in trace:
        b = row.get(slot, "").split(",")
        if "p1" not in row or len(b) < 8 or int(b[1]) not in states:
            last = None
            continue
        pos = (float(b[2]), float(b[3]) if boss_y is None else boss_y, float(b[4]))
        yaw = float(b[6])
        players = [[float(v) for v in row[f"p{i}"].split(",")[2:5]] for i in (0, 1)]
        dist = [math.dist(pos, p) for p in players]
        want = [turn(math.degrees(math.atan2(p[0] - pos[0], p[2] - pos[2])) - yaw) for p in players]
        count["walk"] += 1
        count["near1"] += dist[1] < dist[0]
        count["face1"] += abs(want[1]) < FACING and abs(want[0]) > NOT_FACING
        count["face0"] += abs(want[0]) < FACING and abs(want[1]) > NOT_FACING
        if last is not None and abs(turn(yaw - last)) > TURNING and (want[0] > 0) != (want[1] > 0):
            count["turn0" if (turn(yaw - last) > 0) == (want[0] > 0) else "turn1"] += 1
        kept[b[7]] = kept.get(b[7], 0) + 1
        last = yaw
    return count, kept


def kept_turns(trace, slots):
    """For the bosses in `slots` (any state): on the frames each keeps player 0 or player 1, how often it turns toward
    that player and how often toward the other one, as in facing_story. Returns {kept: [toward 0, toward 1]} and the
    frames a boss was there."""
    def turn(a):
        return (a + 180.0) % 360.0 - 180.0

    turns = {"0": [0, 0], "1": [0, 0]}
    frames = 0
    for slot in slots:
        last = None
        for row in trace:
            b = row.get(slot, "").split(",")
            if "p1" not in row or len(b) < 8:
                last = None
                continue
            frames += 1
            pos, yaw = (float(b[2]), float(b[4])), float(b[6])
            players = [[float(v) for v in row[f"p{i}"].split(",")[2:5]] for i in (0, 1)]
            want = [turn(math.degrees(math.atan2(p[0] - pos[0], p[2] - pos[1])) - yaw) for p in players]
            if (b[7] in turns and last is not None and abs(turn(yaw - last)) > TURNING and
                    (want[0] > 0) != (want[1] > 0)):
                turns[b[7]][0 if (turn(yaw - last) > 0) == (want[0] > 0) else 1] += 1
            last = yaw
    return turns, frames


def shot_story(trace, prefix):
    """The shots of the boss trace with this prefix (`e` Crusher rings, `o` Sarumarine balls), each followed from the
    first frame its slot shows it to the last: [shots that came nearer player 0 than player 1, the other way round]."""
    near = [0, 0]
    tracks = {}  # slot: [nearest to player 0, nearest to player 1, last row]
    for n, row in enumerate(trace):
        if "p1" not in row:
            continue
        players = [[float(v) for v in row[f"p{i}"].split(",")[2:5]] for i in (0, 1)]
        for key, value in row.items():
            if not (key.startswith(prefix) and key[len(prefix):].isdigit()):
                continue
            pos = [float(v) for v in value.split(",")[1:4]]
            dist = [math.dist(pos, p) for p in players]
            track = tracks.get(key)
            if track is None or track[2] != n - 1:  # a new shot in this slot
                if track is not None:
                    near[0 if track[0] <= track[1] else 1] += 1
                track = [dist[0], dist[1], n]
            track = [min(track[0], dist[0]), min(track[1], dist[1]), n]
            tracks[key] = track
    for track in tracks.values():
        near[0 if track[0] <= track[1] else 1] += 1
    return near


def bacoon_story(trace):
    """Frames on which Aquas' Bacoon looks (its head's yaw, clamped by the game to 40 either way) within FACING of
    player 1 and not within NOT_FACING / 2 of player 0, and the other way round."""
    def turn(a):
        return (a + 180.0) % 360.0 - 180.0

    face = [0, 0]
    frames = 0
    for row in trace:
        boss = next((row[k].split(",") for k in row if re.fullmatch(r"b\d+", k) and
                     row[k].startswith(f"{OBJ_BOSS_AQ_BACOON},")), None)
        if boss is None or "aq" not in row or "p1" not in row or not (4 <= int(boss[1]) < 16):
            continue
        frames += 1
        pos = (float(boss[2]), float(boss[4]))
        yaw = float(row["aq"].split(",")[0])
        players = [[float(v) for v in row[f"p{i}"].split(",")[2:5]] for i in (0, 1)]
        off = [abs(turn(yaw - math.degrees(math.atan2(p[0] - pos[0], p[2] - pos[1])))) for p in players]
        for i in (0, 1):
            face[i] += off[i] < FACING / 2 and off[1 - i] > NOT_FACING / 2
    return face, frames


def sc_other_bosses(args, folder):
    """#220: the bosses other than Area 6's and Andross go after the nearer human. granga: Corneria's Granga walks at
    and turns its guns to player 1 when player 1 is the nearer. --levels picks some of OTHER_BOSS_RUNS."""
    only = getattr(args, "levels", None)
    lines = []
    for name in OTHER_BOSS_RUNS:
        if only and name not in only:
            continue
        trace, frames = other_boss_run(args, folder / name, name)
        if name == "granga":
            count, kept = facing_story(trace, "b0", GRANGA_WALKS)
            text = (f"{count['walk']} walking frames, player 1 the nearer on {count['near1']}; it turns toward player 1 "
                    f"on {count['turn1']}, toward player 0 on {count['turn0']}; faces player 1 on {count['face1']}, "
                    f"player 0 on {count['face0']}; kept {', '.join(f'{k}: {v}' for k, v in sorted(kept.items()))}")
            expect(count["turn1"] > 2 * count["turn0"], f"granga: the Granga goes after player 0: {text}")
            expect(count["near1"] * 2 > count["walk"], f"granga: player 1 was not the nearer one: {text}")
            lines.append(f"granga: same hash on {frames} frames; {text}")
        if name == "shogun":
            # Each Shogun picks its target among its kept human and the wingmen, so it often goes after a wingman: what
            # counts is that it keeps player 1 often, the nearer one, and turns toward whom it keeps.
            turns, seen = kept_turns(trace, ("b0", "b1", "b2"))
            kept1 = sum(1 for row in trace for slot in ("b0", "b1", "b2")
                        if len(row.get(slot, "").split(",")) >= 8 and row[slot].split(",")[7] == "1")
            text = (f"{seen} Shogun frames, player 1 kept on {kept1}; keeping player 1 it turns toward player 1 on "
                    f"{turns['1'][1]}, toward player 0 on {turns['1'][0]}; keeping player 0 toward player 0 on "
                    f"{turns['0'][0]}, toward player 1 on {turns['0'][1]}")
            expect(kept1 * 4 > seen, f"shogun: the Shoguns seldom keep player 1: {text}")
            # Chasing a wingman it turns either way, so the margin is smaller than the Granga's.
            expect(turns["1"][1] * 2 > turns["1"][0] * 3,
                   f"shogun: a Shogun that keeps player 1 does not go after it: {text}")
            lines.append(f"shogun: same hash on {frames} frames; {text}")
        if name in ("crusher", "sarumarine"):
            near = shot_story(trace, "e" if name == "crusher" else "o")
            what = "rings" if name == "crusher" else "balls"
            kept = {}
            for row in trace:
                b = row.get("b0", "").split(",")
                if len(b) >= 8:
                    kept[b[7]] = kept.get(b[7], 0) + 1
            text = (f"{sum(near)} {what}, {near[1]} came nearest player 1, {near[0]} player 0; boss kept "
                    f"{', '.join(f'{k}: {v}' for k, v in sorted(kept.items()))}")
            expect(sum(near) >= 4, f"{name}: too few {what} to tell: {text}")
            # The Crusher's rings start at its two sides and each homes in on the human nearest to it: about half go
            # to each side. The Sarumarine's balls fly where its kept human is.
            want = near[1] * 3 >= sum(near) if name == "crusher" else near[1] > 2 * near[0]
            expect(want, f"{name}: the {what} go after player 0: {text}")
            lines.append(f"{name}: same hash on {frames} frames; {text}")
        if name == "bacoon":
            face, seen = bacoon_story(trace)
            text = f"{seen} frames it looks about, at player 1 on {face[1]}, at player 0 on {face[0]}"
            expect(face[1] > 2 * face[0], f"bacoon: the Bacoon looks at player 0: {text}")
            lines.append(f"bacoon: same hash on {frames} frames; {text}")
    return "; ".join(lines)


# A level, its frames from the join to well past level complete with the pilot and gInvincible (#16): Titania reached
# level complete at frame 29169 in the first run, Macbeth at 9523, Aquas at 7240 (the join is close to its boss), so
# there is room for a slower fight. Macbeth's boss
# kills player 0 outright when the train reaches the end of its track first (Macbeth_Actor207_Update, state 18-19), and
# with gInvincible that tank is never downed: it climbs on and the game crashes later. A slower pilot fails there.
VEHICLE_END_LEVELS = (("Macbeth", 11, 14000), ("Titania", 12, 34000), ("Aquas", 13, 14000))


def sc_vehicle_end(args, folder):
    """A vehicle level to its end with two players (#16, #181). Each client's pilot (src/port/coop/test/pilot.c) drives
    its own vehicle, and gInvincible on both games keeps them going, so what is tested is the level, the boss and the
    end with two vehicles, not the pilot. Hashes and inputs equal on every frame, player 0 reaches level complete, and
    player 1 drives most of the way. The menu replay is made longer than the run: a game whose replay ends leaves
    lockstep."""
    only = getattr(args, "levels", None)
    texts = []
    for name, level, frames in VEHICLE_END_LEVELS:
        if only and name.lower() not in only:
            continue
        env = {"SF64_COOP_LOCKSTEP_PILOT": "1", "SF64_COOP_PILOT_LOG": "pilot-log.txt",
               "SF64_COOP_LOCKSTEP_END": str(frames)}
        host, client = session(args, folder / name.lower(), {"SF64_COOP_JOIN_LEVEL": str(level)}, None, env,
                               config={"CVars": {"gInvincible": 1}}, replay_frames=frames + 20000)
        try:
            wait_for([host, client], "done", max(args.level_seconds, frames * 0.25), f"in {name}")
        finally:
            host.close()
            client.close()
        compared = compare(host, client, frames)
        rows = player_trace.parse(client.run / "trace.txt")
        complete = next((r.frame for r in rows if r.level == level and r.players and
                         r.players[0].state == LEVEL_COMPLETE), None)
        expect(complete is not None, f"{name}: player 0 never reached level complete in {frames} frames")
        active, _, joined = vehicle_story(client)
        expect(joined > frames // 2, f"{name}: player 1 drove on only {joined} of {frames} frames")
        texts.append(f"{name} {compared} frames, level complete at {complete}, player 1 drove on {joined}")
    expect(texts, "no level chosen")
    return "same hash; " + "; ".join(texts)


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


def fly(args, folder, extra_env=None):
    """Pilots fly Corneria, one on each client. Returns (host, client, frames to the map and its tail)."""
    env = {"SF64_COOP_LOCKSTEP_PILOT": "1", "SF64_COOP_PILOT_LOG": "pilot-log.txt", **(extra_env or {})}
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


# Corneria's rows in sScenes (src/port/coop/game/team.c): the fourth is Falco's rescue, S59 with S60 and S61.
CO_RESCUE_SCENE = 3
TEAM_FALCO, TEAM_SLIPPY = 1, 2


def team_lines(game, level=0):
    """The team trace of a game (src/port/coop/test/team_trace.h), lines in `level` (Corneria) while in play, as
    dicts."""
    rows = []
    for line in (game.run / "team.txt").read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("#"):
            continue
        # p1 and scenes came with #15; a build from before reads as no player 1 and no choice, so it fails on its ships.
        # ai and body came with the all-range part of #15; before it, a wingman read as nobody flying.
        # shown came with the wingman bar of a human with the shield upgrade; before it, the bar drew the held value.
        # radio came with the rescue lines of decision 0013; before it, no line was logged.
        row = {"p1": "-1", "scenes": ",".join(["0"] * 16), "ai": "0,0,0", "body": "0,0,0", "shown": "0,0,0",
               "radio": "0,0",
               **dict(field.split("=", 1) for field in line.split())}
        if row["level"] == str(level) and row["gs"] == "7":  # GSTATE_PLAY
            rows.append({k: [int(v) for v in value.split(",")] if "," in value else value for k, value in row.items()})
    return rows


SCENES_FRAMES = 8500  # Corneria's rails and the start of Granga: the rescue (S59) starts near 5000 in the pilots'
# flight, Granga near 7140 (docs/coop/NOTES.md)


def fly_for(args, folder, frames, extra_env):
    """Pilots fly Corneria, one on each client, for `frames` lockstep frames. Returns (host, client). A whole level
    is not asked for: a borrowed rescue can cost Slippy (0012 rule 4), and the pilots do not always win Granga then."""
    env = {"SF64_COOP_LOCKSTEP_PILOT": "1", "SF64_COOP_PILOT_LOG": "pilot-log.txt", **extra_env}
    folder.mkdir(parents=True, exist_ok=True)
    host, client = session(args, folder, both=env)
    deadline = time.monotonic() + args.level_seconds
    try:
        while not (host.sync_log.exists() and len(log_rows(host)) >= frames and len(log_rows(client)) >= frames):
            for game in (host, client):
                expect(game.proc.poll() is None, f"{game.name} ended with code {game.proc.returncode}")
            expect(time.monotonic() < deadline, f"not {frames} frames after {args.level_seconds} s")
            time.sleep(2)
    finally:
        client.close()
        host.close()
    return host, client


HITS_FRAMES = 3500  # Corneria's rails to past the first enemy groups, before the rescue


def score_rows(game):
    """(frame-ordered) [(gHitCount, [gPlayerScores...])] from the player trace's score= field (#252)."""
    rows = []
    for line in (game.run / "trace.txt").read_text(encoding="utf-8", errors="replace").splitlines():
        field = next((f for f in line.split() if f.startswith("score=")), None)
        if field:
            team, _, own = field[6:].partition(":")
            rows.append((int(team), [int(v) for v in own.split(",")]))
    return rows


def sc_player_hits(args, folder):
    """#252: pilots fly Corneria, one on each client. Each human's own kills go to gPlayerScores[index] next to the
    team's gHitCount: both players score, their sum never passes the team count, and both games agree (the hash)."""
    frames = HITS_FRAMES
    host, client = fly_for(args, folder, frames, {})
    equal = compare(host, client, frames)
    found = []
    for game in (host, client):
        rows = score_rows(game)
        expect(rows, f"{game.name}: no score= field in the player trace")
        team, own = rows[-1]
        expect(len(own) >= 2, f"{game.name}: one player only in the trace")
        expect(own[0] > 0 and own[1] > 0, f"{game.name}: a player scored nothing: team {team}, players {own}")
        over = [r for r in rows if sum(r[1]) > r[0]]
        expect(not over, f"{game.name}: players {over[0][1] if over else 0} add up past the team's {over[0][0] if over else 0}")
        found.append(f"{game.name}: team {team}, players {own}")
    expect(score_rows(host)[-1] == score_rows(client)[-1], "host and client end on different counts")
    return f"{frames} frames of Corneria, {equal} frames equal; " + "; ".join(found)


# Decision 0013, Corneria's Falco rescue flown by Slippy: Falco's lines (CO S60:7, :25, S59:44) do not play; Slippy's
# call for help (2062) and Peppy's warning to Slippy (2061) do, and Slippy's thanks (2336) if he comes through.
CO_RESCUE_FALCO_LINES = {2166, 2167, 2337}
CO_RESCUE_SLIPPY_CALLS = {2062, 2061}
CO_RESCUE_SLIPPY_THANKS = 2336
CO_FOX_ASKS_FALCO = 2185  # CO S14:19; with Slippy in Falco's lead, Peppy's 20267 plays instead


def sc_scenes(args, folder):
    """#15: the client flies Falco. No scene shows a second Falco, his rescue goes to Slippy, and gTeamShields[Falco]
    is the human's shields on every frame he flies (decisions 0006 rule 1, 0012)."""
    frames = SCENES_FRAMES
    host, client = fly_for(args, folder, frames, {"SF64_COOP_TEAM_TRACE": "team.txt"})
    equal = compare(host, client, frames)
    found = []
    for game in (host, client):
        lines = team_lines(game)
        expect(lines, f"{game.name}: no Corneria lines in the team trace")
        falco = [l for l in lines if l["ships"][0] > 0]
        expect(not falco, f"{game.name}: a scene shows Falco on {len(falco)} lines, first at frame {falco[0]['frame'] if falco else 0}")
        rescue = [l for l in lines if l["scenes"][CO_RESCUE_SCENE] != 0]
        expect(rescue, f"{game.name}: Falco's rescue (S59) never chose a flyer")
        expect(rescue[0]["scenes"][CO_RESCUE_SCENE] == TEAM_SLIPPY,
               f"{game.name}: Falco's rescue went to team {rescue[0]['scenes'][CO_RESCUE_SCENE]}, not Slippy")
        flying = [l for l in lines if int(l["p1"]) >= 0]
        expect(flying, f"{game.name}: player 1 never flew")
        wrong = [l for l in flying if l["team"][0] != min(max(int(l["p1"]), 1), 255)]
        expect(not wrong, f"{game.name}: gTeamShields[Falco] {wrong[0]['team'][0] if wrong else 0} while player 1 "
                          f"has {wrong[0]['p1'] if wrong else 0}, frame {wrong[0]['frame'] if wrong else 0}")
        shields = sorted({int(l["p1"]) for l in flying})
        # Boss fight (0006 rule 3): no computer Falco beside the human, the computer wingmen as before.
        boss = [l for l in lines if l["ai"][0] > 0]
        expect(not boss, f"{game.name}: a computer Falco flies at the boss from frame {boss[0]['frame'] if boss else 0}")
        others = [l for l in lines if l["ai"][1] + l["ai"][2] > 0]
        expect(others, f"{game.name}: no computer wingman flew at the boss in {frames} frames")
        # Decision 0013: Fox's question to Falco at the waterfall lead (CO S14:19) does not play with Falco human.
        asked = [l for l in lines if l["radio"][0] == CO_FOX_ASKS_FALCO]
        expect(not asked, f"{game.name}: Fox asks Falco where he goes ({CO_FOX_ASKS_FALCO}) at frame "
                          f"{asked[0]['frame'] if asked else 0}")
        # Decision 0013: the borrowed rescue's radio, from the choice to the boss fight.
        window = [l for l in lines if int(rescue[0]["frame"]) <= int(l["frame"]) < int(others[0]["frame"])]
        heard = {l["radio"][0] for l in window}
        falco_lines = sorted(heard & CO_RESCUE_FALCO_LINES)
        expect(not falco_lines, f"{game.name}: Falco's rescue lines {falco_lines} played with Slippy flying the rescue")
        missing = sorted(CO_RESCUE_SLIPPY_CALLS - heard)
        expect(not missing, f"{game.name}: the rescue's lines {missing} never played (heard {sorted(heard)})")
        saved = window[-1]["team"][1] > 0
        expect(not saved or CO_RESCUE_SLIPPY_THANKS in heard,
               f"{game.name}: Slippy came through the rescue but his thanks ({CO_RESCUE_SLIPPY_THANKS}) never played")
        found.append(f"{game.name}: {len(flying)} lines with player 1 flying, shields {shields[0]}..{shields[-1]}, "
                     f"Falco's rescue to Slippy from frame {rescue[0]['frame']} with lines "
                     f"{sorted(heard & (CO_RESCUE_SLIPPY_CALLS | {CO_RESCUE_SLIPPY_THANKS}))}"
                     f"{'' if saved else ' (Slippy downed, no thanks due)'}, computer wingmen at the boss from "
                     f"frame {others[0]['frame']} and no Falco among them")
    return f"{frames} frames of Corneria to Granga, {equal} frames equal; " + "; ".join(found)


ALLRANGE_LEVEL = 16  # LEVEL_KATINA: all-range from the start
ALLRANGE_FRAMES = 4000


def allrange_run(args, folder, level, name):
    """#15: on all-range `level` the client flies Falco. While it flies, Falco's slot holds a body on the client's
    ship and no computer Falco flies; Slippy and Peppy fly as before (decision 0006 rule 3, all-range)."""
    frames = ALLRANGE_FRAMES
    host, client = run_short(args, folder, frames=frames, host_env={"SF64_COOP_JOIN_LEVEL": str(level)},
                             both={"SF64_COOP_TEAM_TRACE": "team.txt"})
    equal = compare(host, client, frames)
    found = []
    for game in (host, client):
        lines = team_lines(game, level)
        expect(lines, f"{game.name}: no {name} lines in the team trace")
        # PLAYERSTATE_ACTIVE, DOWN and U_TURN (player 0 turns at the map's edge), player 1 flying
        play = [l for l in lines if l["p0"] in ("3", "4", "5") and int(l["p1"]) >= 0]
        expect(play, f"{game.name}: player 1 never flew while player 0 was active")
        ai = [l for l in play if l["ai"][0] > 0]
        expect(not ai, f"{game.name}: a computer Falco flies beside player 1 on {len(ai)} of {len(play)} lines, "
                       f"first at frame {ai[0]['frame'] if ai else 0}")
        off = [l for l in play if l["body"][0] != 1]
        expect(not off, f"{game.name}: Falco's body is not on player 1 (body={off[0]['body'][0] if off else 0}) on "
                        f"{len(off)} of {len(play)} lines, first at frame {off[0]['frame'] if off else 0}")
        others = [l for l in play if l["ai"][1] + l["ai"][2] > 0]
        expect(others, f"{game.name}: Slippy and Peppy never flew")
        found.append(f"{game.name}: {len(play)} lines in play, Falco's body on player 1 in all of them, "
                     f"computer Slippy or Peppy in {len(others)}")
    return f"{frames} frames of {name}, {equal} frames equal; " + "; ".join(found)


def sc_allrange(args, folder):
    """#15: allrange_run on Katina."""
    return allrange_run(args, folder, ALLRANGE_LEVEL, "Katina")


# The other all-range levels with wingmen (include/sf64level.h). Katina is `allrange`.
ALLRANGE_OTHERS = ((14, "Fortuna"), (17, "Bolse"), (18, "Sector Z"), (19, "Venom 2"))


def sc_allrange_others(args, folder):
    """#15: allrange_run on each other all-range level; every level runs, and the failures are named together."""
    done, failed = [], []
    for level, name in ALLRANGE_OTHERS:
        try:
            done.append(allrange_run(args, folder / name.replace(" ", "_"), level, name))
        except Exception as e:  # noqa: BLE001, a level's failure is reported with the others
            failed.append(f"{name}: {e}")
    expect(not failed, " | ".join(failed) + (" | passed: " + " | ".join(done) if done else ""))
    return " | ".join(done)


ALLRANGE_DOWN_AFTER = 1500  # frames of player 0 active on Katina before the client's shields drop to 0


def allrange_down_run(args, folder, level, name):
    """#15: on all-range `level` the client flies Falco and goes down (SF64_COOP_TEAM_DOWN). From then on Falco's body
    waits in the retreat state, gTeamShields[Falco] is -1, no computer Falco flies and the body is never somewhere the
    client is not (decision 0006 rules 1 and 3, all-range). Where the client returns in place (#205), the body is on it
    again from the first frame it flies."""
    frames = ALLRANGE_FRAMES
    down = f"{level},{ALLRANGE_DOWN_AFTER},1"
    host, client = run_short(args, folder, frames=frames, host_env={"SF64_COOP_JOIN_LEVEL": str(level)},
                             both={"SF64_COOP_TEAM_TRACE": "team.txt", "SF64_COOP_TEAM_DOWN": down})
    equal = compare(host, client, frames)
    found = []
    for game in (host, client):
        marks = [l for l in (game.run / "team.txt").read_text(encoding="utf-8").splitlines() if l.startswith("# down")]
        expect(marks, f"{game.name}: the client's shields were never dropped")
        at = int(marks[0].split()[2].split("=")[1])
        lines = team_lines(game, level)
        play = [l for l in lines if l["p0"] in ("3", "4", "5")]  # PLAYERSTATE_ACTIVE, DOWN, U_TURN
        before = [l for l in play if int(l["frame"]) < at and int(l["p1"]) >= 0]
        expect(before and all(l["body"][0] == 1 for l in before),
               f"{game.name}: Falco's body was not on player 1 before it went down")
        # Every line after the down, scenes and player 0's own down included, up to a level reload (gPlayState
        # PLAY_INIT, or PLAYERSTATE_INIT): the reload clears the actors and brings everyone back, as the game does.
        after = [l for l in lines if int(l["frame"]) > at]
        restart = [i for i, l in enumerate(after) if l["ps"] == "1" or l["p0"] == "1"]
        after = after[:restart[0]] if restart else after
        expect(after, f"{game.name}: no lines in play after frame {at}")
        ai = [l for l in after if l["ai"][0] > 0]
        expect(not ai, f"{game.name}: a computer Falco flies after player 1 went down, first at frame "
                       f"{ai[0]['frame'] if ai else 0}")
        bad = [l for l in after if l["body"][0] == 3]
        expect(not bad, f"{game.name}: Falco's body is somewhere player 1 is not, first at frame "
                        f"{bad[0]['frame'] if bad else 0}")
        away = [l for l in after if l["body"][0] == 2]
        expect(away, f"{game.name}: Falco's body never went away after frame {at}")
        # The body stays away while player 1 stays out. Where the return in place is in (#205, decision 0007: a downed
        # player comes back after 150 frames) the first line with player 1 flying again ends the away window, and from
        # there on the body is on player 1 again with its shields in the team (not away, not a computer Falco).
        later = [l for l in after if int(l["frame"]) >= int(away[0]["frame"])]
        back = next((i for i, l in enumerate(later) if int(l["p1"]) >= 0), len(later))
        settled, returned = later[:back], later[back:]
        wrong = [l for l in settled if l["body"][0] != 2 or l["team"][0] != -1]
        expect(not wrong, f"{game.name}: after going away at frame {away[0]['frame']}, line at frame "
                          f"{wrong[0]['frame'] if wrong else 0} has body={wrong[0]['body'][0] if wrong else 0} "
                          f"team={wrong[0]['team'][0] if wrong else 0} p1={wrong[0]['p1'] if wrong else 0}")
        wrong = [l for l in returned if l["body"][0] != 1 or l["team"][0] < 0]
        expect(not wrong, f"{game.name}: player 1 flies again from frame {returned[0]['frame'] if returned else 0}, "
                          f"line at frame {wrong[0]['frame'] if wrong else 0} has body={wrong[0]['body'][0] if wrong else 0} "
                          f"team={wrong[0]['team'][0] if wrong else 0} p1={wrong[0]['p1'] if wrong else 0}")
        found.append(f"{game.name}: down at frame {at}, body away from frame {away[0]['frame']} on "
                     f"{len(settled)} lines, gTeamShields[Falco] -1 there, no computer Falco"
                     + (f"; player 1 back from frame {returned[0]['frame']}, body on it again on {len(returned)} lines"
                        if returned else ""))
    return f"{frames} frames of {name}, {equal} frames equal; " + "; ".join(found)


def sc_allrange_down(args, folder):
    """#15: allrange_down_run on Katina."""
    return allrange_down_run(args, folder, ALLRANGE_LEVEL, "Katina")


def sc_allrange_down_others(args, folder):
    """#15: allrange_down_run on each other all-range level; every level runs, and the failures are named together."""
    done, failed = [], []
    for level, name in ALLRANGE_OTHERS:
        try:
            done.append(allrange_down_run(args, folder / name.replace(" ", "_"), level, name))
        except Exception as e:  # noqa: BLE001, a level's failure is reported with the others
            failed.append(f"{name}: {e}")
    expect(not failed, " | ".join(failed) + (" | passed: " + " | ".join(done) if done else ""))
    return " | ".join(done)


ALLRANGE_RINGS_AFTER = 1500  # frames of player 0 active on Katina before the client gets the rings and its shields
RINGS_SHIELDS = 300  # of the 383 the upgrade allows
SHIELD_MAX_UPGRADED = 383  # Play_GetMaxShields with three gold rings (src/engine/fox_play.c)


def sc_teambar(args, folder):
    """The bar of a human-flown wingman, with the team's shield upgrade. On Katina the client flies Falco and gets the
    three gold rings and 300 of 383 shields (SF64_COOP_TEAM_RINGS). gTeamShields[Falco] stays held to 255 (decision 0006
    rule 1), so a bar that drew it would be full; the pause, level start and level clear rows and the radio portrait
    draw Falco's share of the larger gauge instead, 300 * 255 / 383 = 199, and the same on both clients
    (CoopTeam_DisplayShields)."""
    frames = ALLRANGE_FRAMES
    rings = f"{ALLRANGE_LEVEL},{ALLRANGE_RINGS_AFTER},1,{RINGS_SHIELDS}"
    host, client = run_short(args, folder, frames=frames, host_env={"SF64_COOP_JOIN_LEVEL": str(ALLRANGE_LEVEL)},
                             both={"SF64_COOP_TEAM_TRACE": "team.txt", "SF64_COOP_TEAM_RINGS": rings})
    equal = compare(host, client, frames)
    found = []
    for game in (host, client):
        marks = [l for l in (game.run / "team.txt").read_text(encoding="utf-8").splitlines() if l.startswith("# rings")]
        expect(marks, f"{game.name}: the rings and the shields were never poked")
        at = int(marks[0].split()[2].split("=")[1])
        after = [l for l in team_lines(game, ALLRANGE_LEVEL) if int(l["frame"]) > at and int(l["p1"]) >= 1]
        above = [l for l in after if int(l["p1"]) > 255]
        expect(above, f"{game.name}: player 1 never held more than 255 shields after frame {at}")
        held = [l for l in above if l["team"][0] != 255]
        expect(not held, f"{game.name}: gTeamShields[Falco] is {held[0]['team'][0] if held else 0} at frame "
                         f"{held[0]['frame'] if held else 0}, not held to 255")
        wrong = [l for l in after if abs(l["shown"][0] - max(1, int(l["p1"]) * 255 // SHIELD_MAX_UPGRADED)) > 1]
        expect(not wrong, f"{game.name}: at frame {wrong[0]['frame'] if wrong else 0} player 1 has "
                          f"{wrong[0]['p1'] if wrong else 0} of {SHIELD_MAX_UPGRADED} and Falco's bar draws "
                          f"{wrong[0]['shown'][0] if wrong else 0}, want "
                          f"{int(wrong[0]['p1']) * 255 // SHIELD_MAX_UPGRADED if wrong else 0}")
        found.append(f"{game.name}: rings at frame {at}, {len(above)} lines with more than 255 shields (the bar draws "
                     f"{above[0]['shown'][0]} for {above[0]['p1']}, the held value is {above[0]['team'][0]}), "
                     f"{len(after)} lines in all right")
    return f"{frames} frames of Katina, {equal} frames equal; " + "; ".join(found)


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


def level_trace(game, frames):
    """The first `frames` rows of the trace in Corneria: about one per lockstep frame, as both start with the level."""
    return [r for r in player_trace.parse(game.run / "trace.txt") if r.level == 0][:frames]


def both_fly(row):
    return row.players[0].state == player_trace.ACTIVE and not any(p.parked for p in row.players)


def sc_save(args, folder):
    """Saves are each client's own, and the level start carries the host's word on the level (#191). Corneria cleared on
    the host only: the client's Start in the intro skips it, Play_Init runs again on both games on the same frame, and
    both players fly. Cleared on the client only: neither Start does anything, the intro goes on."""
    frames, skip = 900, 300
    host, client = run_short(args, folder / "host_cleared", press(idle, {(skip, 1)}), frames=frames,
                             host_env={"SF64_COOP_LOCKSTEP_CLEARED": "1"})
    equal = compare(host, client, frames)
    for game in (host, client):
        inits = [i for i, state in enumerate(play_states(game)[:frames]) if state == PLAY_INIT]
        expect(inits == [skip + 1], f"{game.name}: Play_Init on lockstep frames {inits}, want [{skip + 1}]: "
                                    f"the client's Start did not skip the intro")
        rows = level_trace(game, frames)
        intro = sum(1 for r in rows if r.players[0].state == LEVEL_INTRO)
        expect(intro <= skip + 2, f"{game.name}: player 0 in the intro on {intro} frames, the skip was at {skip}")
        flying = next((n for n, r in enumerate(rows) if both_fly(r)), None)
        expect(flying is not None, f"{game.name}: after the skip the two players never flew together")
    host, client = run_short(args, folder / "client_cleared", press(idle, {(skip, 0), (skip + 60, 1)}), frames=frames,
                             client_env={"SF64_COOP_LOCKSTEP_CLEARED": "1"})
    kept = compare(host, client, frames)
    for game in (host, client):
        states = set(play_states(game)[1:frames])
        expect(states == {PLAY_UPDATE}, f"{game.name}: play states {sorted(states)} after frame 0, want only "
                                        f"{PLAY_UPDATE}: a Start did something in the intro")
        rows = level_trace(game, frames)
        expect(len(rows) >= frames - 2 and all(r.players[0].state == LEVEL_INTRO for r in rows[2:]),
               f"{game.name}: player 0 left the intro within {frames} frames with the host's save not cleared")
    return (f"host's save cleared: the client's Start skipped the intro on both, Play_Init again on frame {skip + 1}, "
            f"both fly {flying} frames into the level, same hash on {equal} frames; client's save cleared only: Start "
            f"from either did nothing, same hash on {kept} frames")


def sc_zero_seed(args, folder):
    """A zero seed on both clients on one frame: upstream's re-seed reads the clock and parts them."""
    env = {"SF64_COOP_LOCKSTEP_ZERO_SEED": "1000:1"}
    host, client = run_short(args, folder / "seed1", frames=1500, both=env)
    equal = compare(host, client, 1500)
    env = {"SF64_COOP_LOCKSTEP_ZERO_SEED": "1000:3"}
    host, client = run_short(args, folder / "seed3", frames=1500, both=env)
    return f"seeds 1 and 3 zeroed on both clients: same hash on {equal} and {compare(host, client, 1500)} frames"


RESET_FLY_FRAMES = 600  # the client's own flight before the reset
RESET_START_FRAME = 2400  # the host starts once the client is back at the title: boot, title, level, reset, title
RESET_FRAMES = 900


def sc_reset(args, folder):
    """A client that played a level alone in the same process, with damage, then reset to the title: what that level
    left must not part the games at the session's level start (#187)."""
    client_env = {"SF64_COOP_TEST_START_LEVEL": "0", "SF64_COOP_TEST_START_SKIP_INTRO": "1",
                  "SF64_COOP_TEST_START_TITLE_FRAMES": "40", "SF64_COOP_TEST_START_RESET": str(RESET_FLY_FRAMES)}
    host, client = run_short(args, folder, frames=RESET_FRAMES,
                             host_env={"SF64_COOP_JOIN_START_FRAME": str(RESET_START_FRAME)}, client_env=client_env)
    _, rows = hash_check.read_log(client.run / "state-hash.txt")
    running = join_test.said(client.report(), "level_running")
    expect(running, "the client never got to the session's level")
    first = int(running[0]["frame"])
    before = [row[1] for row in rows if row[0] < first]
    # Its own level is the first stretch in GSTATE_PLAY; the session's level start is in GSTATE_PLAY too.
    start = next((i for i, state in enumerate(before) if state == replay_tool.GS_PLAY), len(before))
    end = next((i for i in range(start, len(before)) if before[i] != replay_tool.GS_PLAY), len(before))
    alone = end - start
    expect(alone >= RESET_FLY_FRAMES, f"the client played {alone} frames before the session, want {RESET_FLY_FRAMES}")
    titles = sum(1 for state in before[end:] if state == replay_tool.GS_TITLE)
    expect(titles >= hash_check.WHOLE_PATH_FRAMES,
           f"the client was at the title for {titles} frames between its own level and the session's")
    frames = compare(host, client, RESET_FRAMES)
    return (f"a client that flew {alone} frames alone and reset to the title: same hash on {frames} frames of the "
            f"session's level")


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
    "aim": sc_aim,
    "boost": sc_boost,
    "return": sc_return,
    "return_all_range": sc_return_all_range,
    "return_fox_down": sc_return_fox_down,
    "laser": sc_laser,
    "laser_wing1": sc_laser_wing1,
    "laser_wing0": sc_laser_wing0,
    "repair": sc_repair,
    "crate": sc_crate,
    "kill": sc_kill,
    "lives": sc_lives,
    "wing": sc_wing,
    "saucerer": sc_saucerer,
    "three": sc_three,
    "control": sc_control,
    "pause": sc_pause,
    "save": sc_save,
    "zero_seed": sc_zero_seed,
    "reset": sc_reset,
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
    "scenes": sc_scenes,
    "player_hits": sc_player_hits,
    "allrange": sc_allrange,
    "allrange_down": sc_allrange_down,
    "allrange_others": sc_allrange_others,
    "allrange_down_others": sc_allrange_down_others,
    "teambar": sc_teambar,
    "audio": sc_audio,
    "release": sc_release,
    "rates": sc_rates,
    "desync": sc_desync,
    "desync_control": sc_desync_control,
    "desync_three": sc_desync_three,
    "handover": sc_handover,
    "log": sc_log,
    "vehicles": sc_vehicles,
    "vehicle_end": sc_vehicle_end,
    "bosses": sc_bosses,
    "other_bosses": sc_other_bosses,
}
LONG = ("full", "scenes", "allrange", "allrange_down", "allrange_others", "allrange_down_others", "teambar",
        "audio", "release", "rates", "vehicles", "vehicle_end", "bosses", "other_bosses")


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
    ap.add_argument("--levels", type=lambda t: t.lower().split(","),
                    help="for `vehicles` and `vehicle_end`: some of titania,aquas,macbeth (default all); for "
                         "`bosses`: some of gorgon,andross,dogfight,inhale; for `other_bosses`: some of "
                         + ",".join(OTHER_BOSS_RUNS))
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
    root = Path(args.out).resolve() if args.out else replay_tool.ROOT / "local" / f"test-lockstep-{time.strftime('%Y%m%d-%H%M%S')}"
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
