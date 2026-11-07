#!/usr/bin/env python3
"""Tests for the co-op menu (WP-25): the menu layer without a game, then the real menu in real games.

    python tools/coop/menu_test.py                  # build coop_menu_test, run sim and udp; no ROM needed
    python tools/coop/menu_test.py --game           # and the real games, every game_ scenario
    python tools/coop/menu_test.py --only game_level --no-build --exe <file>
    python tools/coop/menu_test.py --asan           # sim and udp under AddressSanitizer and UBSan; not on Windows
    python tools/coop/menu_test.py --list

sim runs src/port/coop/ui/menu.c on the real session and transport core in a virtual network: that is where the exact
checks are, of what can be chosen, of status and of ping against the transport's own numbers. udp does a shorter run over
real sockets. game starts real games with SF64_COOP_TEST_MENU (src/port/coop/test/menu_driver.h), clicks the real ImGui
widgets through ImGui's input queue and reads back what the window holds. game_level does the same up to the level
start and then compares the state hash logs frame by frame, as join_test.py does; game_level_map has a host that went
through the menus to the map first. Their games play a replay with no input after that, so no controller reaches them,
each from other seeds, and report the level through SF64_COOP_JOIN_WATCH. game_platform reads what a real menu shows for
a platform refusal, on the host's side and the joining player's, and runs SF64_COOP_TEST_ANY_PLATFORM. Two games on one
PC never differ in platform, so the other side there is coop_session_test, which says what it is told to. game_leave
has player 2 leave during the level, which ends the session for everyone under lockstep (#160) while every game keeps
running, then the same games hold a second session, start again and keep every player past the time-out (#132).
game_level and game_leave print the longest gap between two session ticks (menu-stall.txt).

Prints one line per scenario and a RESULT line, keeps logs in local/test-menu/<stamp>/, exits 0 only when everything
passed. Each game gets its own folder under local/ with its own SHIP_HOME, removed when the run passed.
"""
import argparse
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
IS_WINDOWS = platform.system() == "Windows"
sys.path.insert(0, str(Path(__file__).resolve().parent))
import join_test  # noqa: E402
import net_test  # noqa: E402
import replay  # noqa: E402
import session_test  # noqa: E402

BOOT_SECONDS = 180  # three games with software rendering on a busy machine
STEP_SECONDS = 60
TIMEOUT_MS = 10000  # COOP_MENU_TIMEOUT_MS, src/port/coop/ui/menu.h
# Sets a CVar the handshake compares (src/port/coop/session/sim_cvars.c), and the name the player reads for it.
ODD_CVAR, ODD_CVAR_NAME = "gInvincible", "Invincible"
OTHER_PLATFORM = "Other platform"
PLATFORM_TEXT = rf"Different platforms: host .+, joining player {OTHER_PLATFORM}\."
LEVEL_SECONDS = 600  # from the click to enough frames in the level, three games on a busy machine
# Longer than any run: the menu takes as many frames as the machine draws while the clicks go by. Nothing here reads the
# end of the replay, the games stop once the level has run long enough.
LEVEL_REPLAY_FRAMES = 30000


class Fail(Exception):
    pass


def expect(cond, what):
    if not cond:
        raise Fail(what)


class Game(join_test.LevelLog):
    """One real game with the menu driver on, in its own folder. With seeds it starts from them, gets no input and logs
    what join_test.py compares; press_until as in join_test.write_replay."""

    def __init__(self, exe, name, out, cvars=None, seeds=None, press_until=0, env_extra=None):
        self.name = name
        self.run = replay.make_run_dir(exe, f"menu-{name}")
        self.commands = self.run / "menu-commands.txt"
        self.state = self.run / "menu-state.txt"
        self.report_path = self.run / "join-report.txt"
        self.offset = 0
        self.items = {}
        self.texts = {}
        config = {"CVars": {"gOpenMenuBar": 1, **(cvars or {})}, "Window": {"Width": 1024, "Height": 768}}
        (self.run / "starship.cfg.json").write_text(json.dumps(config, indent=4), encoding="utf-8")
        env = replay.game_env(self.run)
        env["SF64_COOP_TEST_MENU"] = str(self.run)
        # See join_test.py: on a quiet machine a Release game with no limit never showed a client's ping (#147).
        env["SF64_COOP_TEST_SPEED"] = replay.MEETING_SPEED
        env.update(env_extra or {})
        if seeds is not None:
            join_test.write_replay(self.run / "input.sfcr", LEVEL_REPLAY_FRAMES, seeds, press_until)
            env.update({"SF64_COOP_REPLAY_PLAY": str(self.run / "input.sfcr"),
                        "SF64_COOP_REPLAY_REPORT": str(self.run / "replay-report.txt"),
                        "SF64_COOP_HASH_LOG": str(self.run / "state-hash.txt"),
                        "SF64_COOP_JOIN_WATCH": "1", "SF64_COOP_JOIN_REPORT": str(self.report_path),
                        "SF64_COOP_LOCKSTEP_REPORT": str(self.run / "lockstep-report.txt")})
        if platform.system() == "Linux":
            for key, value in (("SDL_VIDEODRIVER", "offscreen"), ("SDL_AUDIODRIVER", "dummy"),
                               ("LIBGL_ALWAYS_SOFTWARE", "1"), ("GALLIUM_DRIVER", "llvmpipe")):
                env.setdefault(key, value)
        self.log = open(out / f"game-{name}.log", "wb")
        self.proc = subprocess.Popen([str(exe)], cwd=self.run, env=env, stdout=self.log, stderr=subprocess.STDOUT)

    def read(self):
        """Take in what the game wrote since the last look; only whole blocks count."""
        if not self.state.exists():
            return
        with open(self.state, "rb") as f:
            f.seek(self.offset)
            data = f.read()
        whole = data.rfind(b"\n") + 1
        items, texts, in_block = {}, {}, False
        consumed = 0
        at = 0
        for line in data[:whole].decode("utf-8", "replace").split("\n")[:-1]:
            at += len(line.encode("utf-8", "replace")) + 1
            # The driver writes in text mode, so a Windows game ends each line with CR LF.
            line = line.removesuffix("\r")
            if line.startswith("error "):
                raise Fail(f"{self.name}: the driver says: {line[6:]}")
            if line.startswith("frame "):
                items, texts, in_block = {}, {}, True
            elif line == "end" and in_block:
                self.items, self.texts, in_block = items, texts, False
                consumed = at
            elif line.startswith("item "):
                _, item, enabled, label = line.split(" ", 3)
                items[item] = (enabled == "1", label)
            elif line.startswith("text "):
                parts = line.split(" ", 2)
                texts[parts[1]] = parts[2] if len(parts) > 2 else ""
        self.offset += consumed

    def send(self, line):
        with open(self.commands, "a", encoding="utf-8") as f:
            f.write(line + "\n")

    def wait(self, test, what, seconds=STEP_SECONDS, every=0.1):
        end = time.monotonic() + seconds
        while True:
            self.read()
            result = test()
            if result:
                return result
            if self.proc.poll() is not None:
                raise Fail(f"{self.name}: the game ended with code {self.proc.returncode} while waiting for {what}")
            if time.monotonic() > end:
                raise Fail(f"{self.name}: no {what} after {seconds} s; texts {self.texts}, items {sorted(self.items)}")
            time.sleep(every)

    def wait_item(self, item, enabled=None, label=None, seconds=STEP_SECONDS):
        def test():
            found = self.items.get(item)
            return (found is not None and (enabled is None or found[0] == enabled) and
                    (label is None or label in found[1]))
        self.wait(test, f"widget {item} (enabled {enabled}, label {label})", seconds)

    def wait_text(self, text, pattern, seconds=STEP_SECONDS):
        return self.wait(lambda: re.search(pattern, self.texts.get(text, "")), f"text {text} matching {pattern}",
                         seconds)

    def click(self, item):
        self.wait_item(item)
        self.send(f"click {item}")

    def type(self, item, text):
        self.wait_item(item)
        self.send(f"type {item} {text}")

    def stop(self):
        if self.proc.poll() is None:
            # Only the process this script started.
            self.proc.terminate()
            try:
                self.proc.wait(timeout=20)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        self.log.close()


def toggle_window(game):
    """Through the menu bar, as a player does. The entry has to close by itself: while it is open, it blocks the mouse
    in the window it just opened."""
    game.click("menu")
    game.wait_item("open")
    game.click("open")
    game.wait(lambda: "open" not in game.items, "the Co-op entry closing")


def join(game, port, player):
    game.type("address", "127.0.0.1")
    game.type("port", port)
    game.click("join")
    game.wait_text("status", rf"In the session at 127\.0\.0\.1:{port} as player {player}\.")


def ping_of(game, row):
    return int(game.wait_text(row, r"ping (\d+) ms").group(1))


def scenario_game(args, exe, out):
    games = []

    def start(name, cvars=None):
        games.append(Game(exe, name, out, cvars))
        return games[-1]

    try:
        host, a, b = start("host"), start("a"), start("b")

        # With no session the only thing in the game is the entry: no window until a player opens it.
        for game in (host, a, b):
            game.wait(lambda g=game: "menu" in g.items, "Co-op entry in the menu bar", BOOT_SECONDS)
            expect("status" not in game.texts, f"{game.name}: the window was open at the start")
            toggle_window(game)
            game.wait_text("status", r"Not in a session\.")

        # Host and join with nothing but the widgets.
        host.type("port", "0")
        host.click("host")
        port = host.wait_text("status", r"Hosting on port (\d+), 1 of 4 players\.").group(1)
        host.wait_text("player1", r"^Player 1 \(you\): Fox$")
        expect("pick-falco" not in host.items, "the host is offered characters")
        join(a, port, 2)
        join(b, port, 3)
        host.wait_text("status", r", 3 of 4 players\.")
        host.wait_text("player2", r"^Player 2: no character yet")
        b.wait_text("player2", r"^Player 2: no character yet$")
        b.wait_text("player3", r"^Player 3 \(you\): no character yet$")

        # A taken character cannot be chosen: player 2 takes Falco, player 3 clicks the greyed out button.
        a.wait_item("pick-falco", enabled=True, label="Falco")
        b.wait_item("pick-falco", enabled=True, label="Falco")
        a.click("pick-falco")
        a.wait_item("pick-falco", enabled=False, label="Falco (you)")
        host.wait_text("player2", r"^Player 2: Falco")
        b.wait_text("player2", r"^Player 2: Falco$")
        b.wait_item("pick-falco", enabled=False, label="Falco (player 2)")
        b.wait_item("pick-slippy", enabled=True)
        b.click("pick-falco")
        time.sleep(2)  # a pick that went out would be answered well within this, the sim run shows how
        for game in (host, a, b):
            game.read()
        expect(re.search(r"^Player 3 \(you\): no character yet$", b.texts["player3"]), "a taken character was chosen")
        expect("asking" not in b.items["pick-falco"][1], "a click on a taken character asked the host")
        expect("taken" not in host.texts.get("message", ""), "a click on a taken character reached the host")
        b.click("pick-slippy")
        b.wait_item("pick-slippy", enabled=False, label="Slippy (you)")
        a.wait_item("pick-slippy", enabled=False, label="Slippy (player 3)")
        host.wait_text("player3", r"^Player 3: Slippy")

        # Ping: the host has one for each client, a client one for the host.
        pings = [ping_of(host, "player2"), ping_of(host, "player3"), ping_of(a, "player1"), ping_of(b, "player1")]
        expect(all(0 < ms < TIMEOUT_MS for ms in pings), f"pings out of range: {pings}")
        expect(" ms" not in a.texts["player3"], "a client shows a ping to another client")

        # The session goes on with the window closed: longer than the time-out, and nobody drops.
        toggle_window(host)
        host.wait(lambda: "status" not in host.texts, "the window closing")
        time.sleep(TIMEOUT_MS / 1000 + 2)
        a.read()
        expect("player3" in a.texts and "as player 2" in a.texts["status"], "a client dropped with the window closed")
        toggle_window(host)
        host.wait_text("status", r", 3 of 4 players\.")

        # Leaving frees the character.
        a.click("leave")
        a.wait_text("status", r"Not in a session\.")
        host.wait_text("status", r", 2 of 4 players\.")
        host.wait_text("message", r"Player 2 left")
        b.wait_item("pick-falco", enabled=True, label="Falco")
        b.click("pick-falco")
        host.wait_text("player3", r"^Player 3: Falco")

        # A game with another CVar is refused, and the text says what differs: the identity comes from the running game.
        a.stop()
        odd = start("odd", {ODD_CVAR: 1})
        odd.wait(lambda: "menu" in odd.items, "Co-op entry in the menu bar", BOOT_SECONDS)
        toggle_window(odd)
        odd.type("address", "127.0.0.1")
        odd.type("port", port)
        odd.click("join")
        refusal = odd.wait_text("message", ODD_CVAR_NAME).string
        odd.wait_text("status", r"Not in a session\.")
        host.wait_text("message", ODD_CVAR_NAME)
        host.wait_text("status", r", 2 of 4 players\.")

        # The host goes: the client is told, and is back at the start.
        host.click("leave")
        host.wait_text("status", r"Not in a session\.")
        b.wait_text("status", r"Not in a session\.")
        b.wait_text("message", r"closed the connection")
        b.wait_item("join", enabled=True)
    except Fail:
        args.keep = True
        raise
    finally:
        for game in games:
            game.stop()
        if not args.keep:
            for game in games:
                shutil.rmtree(game.run, ignore_errors=True)
    return f"3 games: host, join, Falco taken and a click on it sent nothing, pings {pings} ms, refused: {refusal[:60]}"


def scenario_game_platform(args, exe, out):
    run = net_test.Run(args.session_program, out / "game_platform")
    games = []

    def start(name, env_extra=None):
        games.append(Game(exe, name, out, env_extra=env_extra))
        return games[-1]

    def bot_client(name, port, *options):
        """A client that stays until --max-seconds or the end of its session. Returns its process and result file."""
        return run.client(port, "--max-seconds", 10, *options, name=name)

    try:
        plain, passing = start("plain"), start("passing", {"SF64_COOP_TEST_ANY_PLATFORM": "1"})
        for game in games:
            game.wait(lambda g=game: "menu" in g.items, "Co-op entry in the menu bar", BOOT_SECONDS)
            toggle_window(game)
            game.wait_text("status", r"Not in a session\.")
        plain.type("port", "0")
        plain.click("host")
        port = plain.wait_text("status", r"Hosting on port (\d+), 1 of 4 players\.").group(1)

        # Only the build may differ here, so a bot with this game's build then differs in the platform alone.
        probe, result = bot_client("probe", port, "--build", "probe")
        run.wait(probe, STEP_SECONDS, "the probe")
        said = session_test.events(net_test.read(result), "refused")
        expect(len(said) == 1 and said[0]["diff"] == session_test.DIFF_BUILD, f"the probe got {said}")
        build = re.search(r"host (\S+), joining player probe", said[0]["text"]).group(1)
        other = ["--build", build, "--platform", OTHER_PLATFORM]

        # What the host's window shows.
        client, result = bot_client("client", port, *other)
        run.wait(client, STEP_SECONDS, "the refused client")
        said = session_test.events(net_test.read(result), "refused")
        expect(len(said) == 1 and said[0]["diff"] == session_test.DIFF_PLATFORM, f"the client got {said}")
        host_view = plain.wait_text("message", rf"^A player was refused\. \| {PLATFORM_TEXT}").string
        plain.wait_text("status", r", 1 of 4 players\.")

        # What the joining player's window shows.
        plain.click("leave")
        plain.wait_text("status", r"Not in a session\.")
        bot, bot_port, _ = run.host("--max-seconds", STEP_SECONDS, *other, name="host")
        plain.type("address", "127.0.0.1")
        plain.type("port", bot_port)
        plain.click("join")
        joiner_view = plain.wait_text("message", r"Different platforms: host Other platform, joining player ").string
        plain.wait_text("status", r"Not in a session\.")
        run.wait(bot, STEP_SECONDS + 10, "the bot host")

        # The test switch, from a real game's environment: the same client gets in, and the window says what differs.
        passing.type("port", "0")
        passing.click("host")
        port = passing.wait_text("status", r"Hosting on port (\d+), 1 of 4 players\.").group(1)
        client, result = bot_client("passed", port, *other)
        passing.wait_text("status", r", 2 of 4 players\.")
        passing.wait_text("message", rf"^Player 2 joined\. \| {PLATFORM_TEXT}")
        client.wait(timeout=STEP_SECONDS)  # it runs out of time and exits with 1: a real host sends it no game message
        said = session_test.events(net_test.read(result), "joined")
        expect(len(said) == 1 and said[0]["diff"] == session_test.DIFF_PLATFORM, f"the client got {said}")
    except (Fail, net_test.Fail):
        args.keep = True
        raise
    finally:
        run.stop_all()
        for game in games:
            game.stop()
        if not args.keep:
            for game in games:
                shutil.rmtree(game.run, ignore_errors=True)
    return f"2 games and bots: host read '{host_view}'; joining player read '{joiner_view}'; the switch let it in"


def in_level(game):
    """The driver's line for the first frame after Play_Init, None before that."""
    running = join_test.said(game.report(), "level_running")
    return running[0] if running else None


def frames_logged(game):
    with open(game.run / "state-hash.txt", "rb") as f:
        return sum(1 for line in f if not line.startswith(b"#"))  # the log holds notes too, such as "# draws"


def longest_stall(game):
    """The longest gap between two session ticks with a session open, in ms (src/port/coop/test/menu_driver.h)."""
    path = game.run / "menu-stall.txt"
    lines = path.read_text(encoding="utf-8").split() if path.exists() else []
    return int(lines[-3]) if lines else 0


def open_windows(games):
    for game in games:
        game.wait(lambda g=game: "menu" in g.items, "Co-op entry in the menu bar", BOOT_SECONDS)
        toggle_window(game)
        game.wait_text("status", r"Not in a session\.")


def host_session(host):
    host.type("port", "0")
    host.click("host")
    return host.wait_text("status", r"Hosting on port (\d+), .* players\.").group(1)


def check_level(games, characters):
    """After the click: every game goes to the level and stays in the session, and the logs say it is the same level.
    Returns the frames compared and the host's report of the level."""
    host, *clients = games
    for game in games:
        game.wait_text("level", r"The level has started", LEVEL_SECONDS)
    for game in games:
        game.wait(lambda g=game: in_level(g), "frame in the level", LEVEL_SECONDS)
    expect("start" not in host.items, "the host can start a level twice")
    for game in clients:
        expect("pick-peppy" not in game.items, "a client is offered characters in the level")

    for game in games:
        first = int(in_level(game)["frame"])
        game.wait(lambda g=game, f=first: frames_logged(g) > f + join_test.FOUND_IN_LEVEL + 20,
                  "frames enough in the level", LEVEL_SECONDS, every=2)
    host.wait_text("status", rf", {len(games)} of 4 players\.")
    for player, game in enumerate(clients, 2):
        game.wait_text("status", rf" as player {player}\.")

    for game in games:
        game.stop()
    running = in_level(host)
    for game in clients:
        join_test.check_running(game, running)
    expect(running["level"] == str(join_test.LEVEL_CORNERIA), f"the host went to level {running['level']}")
    expect(running["players"] == str(len(games)), f"{running['players']} players in the level")
    expect(running["characters"] == characters, f"the game holds {running['characters']}")
    return join_test.check_same_level(host, clients), running


def level_run(args, games, body):
    """body with the games running; they are stopped whatever it does, and their folders go if it passed."""
    try:
        return body()
    except (Fail, join_test.Fail):
        args.keep = True
        raise
    finally:
        for game in games:
            game.stop()
        if not args.keep:
            for game in games:
                shutil.rmtree(game.run, ignore_errors=True)


def scenario_game_level(args, exe, out):
    """Host, join and pick with nothing but the widgets, then the Start button into a level on every game."""
    host, a, b = games = [Game(exe, name, out, seeds=join_test.SEEDS[seeds])
                          for name, seeds in (("host", "host"), ("a", "client0"), ("b", "client1"))]

    def body():
        open_windows(games)
        port = host_session(host)
        host.wait_item("start", enabled=True)
        join(a, port, 2)
        join(b, port, 3)
        host.wait_text("status", r", 3 of 4 players\.")

        # Only the host has a Start button, and it stays grey while anyone who joined holds nothing.
        host.wait_item("start", enabled=False)
        a.wait_text("level", r"Waiting for the host")
        expect("start" not in a.items and "start" not in b.items, "a client has a Start button")
        host.click("start")
        time.sleep(2)  # a level start that went out would be here well within this, the sim run shows how
        for game in games:
            game.read()
        expect(re.search(r"Waiting for the host", a.texts["level"]), "a click on the grey button reached a client")
        expect("level" not in host.texts and "start" in host.items, "a click on the grey button started the level")
        a.click("pick-falco")
        host.wait_text("player2", r"^Player 2: Falco")
        time.sleep(1)
        host.read()
        expect(not host.items["start"][0], "Start lit up with player 3 holding nothing")
        b.wait_item("pick-slippy", enabled=True)
        b.click("pick-slippy")
        host.wait_text("player3", r"^Player 3: Slippy")
        host.wait_item("start", enabled=True)

        host.click("start")
        frames, running = check_level(games, "Fox,Falco,Slippy,-")
        stall = max(longest_stall(game) for game in games)
        return (f"3 games from Start: same hash on {frames} frames, {running['characters']}, seeds {running['seeds']}, "
                f"longest stall {stall} ms")

    return level_run(args, games, body)


def scenario_game_level_map(args, exe, out):
    """A host that pressed A through the menus, a client still on the title screen."""
    host = Game(exe, "host", out, seeds=join_test.SEEDS["host"], press_until=join_test.MENU_START_FRAME - 60)
    a = Game(exe, "a", out, seeds=join_test.SEEDS["client0"])
    games = [host, a]

    def body():
        open_windows(games)
        port = host_session(host)
        join(a, port, 2)
        a.click("pick-peppy")
        host.wait_text("player2", r"^Player 2: Peppy")
        host.wait(lambda: frames_logged(host) > join_test.MENU_START_FRAME, "frames enough to reach the map",
                  LEVEL_SECONDS, every=2)
        host.wait_item("start", enabled=True)
        host.click("start")
        frames, _ = check_level(games, "Fox,Peppy,-,-")
        _, before, _ = host.rows()
        states = {row[1] for row in before[-30:]}
        expect(states != {join_test.GSTATE_TITLE}, f"the host was still on the title screen: game state {states}")
        return f"host came from game state {sorted(states)}, client from the title screen: same hash on {frames} frames"

    return level_run(args, games, body)


def start_three(games, port):
    """Players 2 and 3 join and pick Falco and Slippy, the host clicks Start, and every game gets to the level."""
    host, a, b = games
    join(a, port, 2)
    join(b, port, 3)
    a.wait_item("pick-falco", enabled=True)
    a.click("pick-falco")
    host.wait_text("player2", r"^Player 2: Falco")
    b.wait_item("pick-slippy", enabled=True)
    b.click("pick-slippy")
    host.wait_text("player3", r"^Player 3: Slippy")
    host.wait_item("start", enabled=True)
    host.click("start")
    for game in games:
        game.wait_text("level", r"The level has started", LEVEL_SECONDS)


def scenario_game_leave(args, exe, out):
    """A player leaves during a level, then the same three games hold a second session and start a level again (#132)."""
    host, a, b = games = [Game(exe, name, out, seeds=join_test.SEEDS[seeds])
                          for name, seeds in (("host", "host"), ("a", "client0"), ("b", "client1"))]

    def body():
        open_windows(games)
        start_three(games, host_session(host))
        for game in games:
            game.wait(lambda g=game: in_level(g), "frame in the level", LEVEL_SECONDS)

        # Player 2 leaves in the level. Every frame needs every player, so the session ends for everyone (#160,
        # docs/coop/PROTOCOL.md, End): the host says who left, player 3 reads that the host closed, and every game
        # goes on alone in its level.
        frames = {game.name: frames_logged(game) for game in games}
        a.click("leave")
        a.wait_text("status", r"Not in a session\.")
        host.wait_text("message", r"Player 2 left, so the session is over")
        host.wait_text("status", r"Not in a session\.")
        b.wait_text("status", r"Not in a session\.")
        b.wait_text("message", r"closed the connection")
        for game in games:
            game.wait(lambda g=game: frames_logged(g) > frames[g.name] + 60, "frames after the leave", STEP_SECONDS,
                      every=1)
        for game in games:
            game.wait(lambda g=game: "level" not in g.texts, "the level line gone after the session")

        # The same games hold a second session, in the level they are in, and start again. The join watch reports
        # level_running once per run, so the second start is checked from the menu and the frames: every game says it
        # started, keeps going and keeps all three players past the time-out.
        start_three(games, host_session(host))
        frames = {game.name: frames_logged(game) for game in games}
        time.sleep(TIMEOUT_MS / 1000 + 2)  # longer than the time-out: a player kept in by mistake would drop now
        for game in games:
            game.wait(lambda g=game: frames_logged(g) > frames[g.name] + 60, "frames after the second start",
                      STEP_SECONDS, every=1)
        host.wait_text("status", r", 3 of 4 players\.")
        a.wait_text("status", r" as player 2\.")
        b.wait_text("status", r" as player 3\.")
        stall = max(longest_stall(game) for game in games)
        return f"3 games: player 2 left in the level and the session ended for all, then a second session and level " \
               f"held past the time-out; longest stall {stall} ms"

    return level_run(args, games, body)


def target(asan):
    return "coop_menu_test_asan" if asan else "coop_menu_test"


def find_exe(build_dir, config, asan=False):
    name = target(asan) + (".exe" if IS_WINDOWS else "")
    for path in (build_dir / "coop-menu-test" / name, build_dir / "coop-menu-test" / config / name):
        if path.exists():
            return path
    return None


def scenario_program(mode):
    def run(args, exe, out):
        result = subprocess.run([str(exe), mode], capture_output=True, text=True, timeout=120, encoding="utf-8",
                                errors="replace")
        (out / f"{mode}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        line = (result.stdout.strip().splitlines() or [f"no output, exit code {result.returncode}"])[-1]
        expect(result.returncode == 0 and line.startswith("PASS"), line)
        return line
    return run


GAME_SCENARIOS = {"game": scenario_game, "game_level": scenario_game_level, "game_level_map": scenario_game_level_map,
                  "game_platform": scenario_game_platform, "game_leave": scenario_game_leave}
SCENARIOS = {"sim": scenario_program("sim"), "udp": scenario_program("udp"), **GAME_SCENARIOS}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--only", help="comma separated scenario names")
    ap.add_argument("--list", action="store_true", help="print the scenario names and stop")
    ap.add_argument("--game", action="store_true", help="also run the real games")
    ap.add_argument("--exe", help="the game, for the real games; default: the newest build")
    ap.add_argument("--build-dir", type=Path, help="default: build/x64 on Windows, build-cmake elsewhere")
    ap.add_argument("--config", default="Debug", choices=["Debug", "Release", "RelWithDebInfo"])
    ap.add_argument("--no-build", action="store_true", help="use the coop_menu_test already built")
    ap.add_argument("--asan", action="store_true", help="use coop_menu_test_asan: AddressSanitizer and UBSan")
    ap.add_argument("--keep", action="store_true", help="keep the games' folders after a run that passed")
    ap.add_argument("--out", type=Path, help="log folder, default local/test-menu/<stamp>")
    args = ap.parse_args()

    if args.list:
        print("\n".join(SCENARIOS))
        return 0
    if args.asan and IS_WINDOWS:
        print("RESULT: FAILED, --asan needs GCC or Clang; MSVC has no UBSan")
        return 2
    names = args.only.split(",") if args.only else ["sim", "udp"] + (list(GAME_SCENARIOS) if args.game else [])
    unknown = [n for n in names if n not in SCENARIOS]
    if unknown:
        print(f"RESULT: FAILED, unknown scenario {', '.join(unknown)}; --list shows them")
        return 2

    build_dir = args.build_dir or (Path("build/x64") if IS_WINDOWS else Path("build-cmake"))
    build_dir = build_dir if build_dir.is_absolute() else ROOT / build_dir
    out = args.out or ROOT / "local" / "test-menu" / time.strftime("%Y%m%d-%H%M%S")
    out.mkdir(parents=True, exist_ok=True)

    program = None
    targets = []
    if any(n not in GAME_SCENARIOS for n in names):
        targets.append(target(args.asan))
    if "game_platform" in names:
        targets.append(session_test.target(False))
    if targets and not args.no_build:
        if shutil.which("cmake") is None:
            print("RESULT: FAILED, cmake not found")
            return 1
        build_log = out / "build.log"
        with open(build_log, "w", encoding="utf-8") as log:
            code = subprocess.run(["cmake", "--build", str(build_dir), "--config", args.config, "--target", *targets],
                                  stdout=log, stderr=subprocess.STDOUT).returncode
        if code != 0:
            tail = build_log.read_text(encoding="utf-8", errors="replace").strip().splitlines()[-8:]
            print("\n".join(tail))
            print(f"RESULT: FAILED, {' '.join(targets)} did not build (log: {build_log})")
            return 1
    if any(n not in GAME_SCENARIOS for n in names):
        program = find_exe(build_dir, args.config, args.asan)
        if program is None:
            print(f"RESULT: FAILED, no {target(args.asan)} in {build_dir}")
            return 1
    if "game_platform" in names:
        args.session_program = session_test.find_exe(build_dir, args.config)
        if args.session_program is None:
            print(f"RESULT: FAILED, no {session_test.target(False)} in {build_dir}")
            return 1

    lines = []
    failed = []
    for name in names:
        started = time.monotonic()
        exe = replay.find_exe(args.exe) if name in GAME_SCENARIOS else program
        try:
            line = f"ok      {name}: {SCENARIOS[name](args, exe, out)}"
        except (Fail, join_test.Fail, net_test.Fail) as e:
            line = f"FAILED  {name}: {e}"
            failed.append(name)
        except Exception as e:  # a broken test must not read as a pass
            line = f"FAILED  {name}: {type(e).__name__}: {e}"
            failed.append(name)
        line += f" ({time.monotonic() - started:.1f} s)"
        print(line, flush=True)
        lines.append(line)

    result = f"RESULT: {len(names) - len(failed)}/{len(names)} scenarios passed"
    print(result)
    print(f"logs: {out}")
    (out / "summary.txt").write_text("\n".join(lines + [result]) + "\n", encoding="utf-8")
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
