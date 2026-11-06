#!/usr/bin/env python3
"""Tests for the co-op menu (WP-25): the menu layer without a game, then the real menu in real games.

    python tools/coop/menu_test.py                  # build coop_menu_test, run sim and udp; no ROM needed
    python tools/coop/menu_test.py --game           # and game: three real games, unattended; needs a build with assets
    python tools/coop/menu_test.py --only game --no-build --exe <file>
    python tools/coop/menu_test.py --asan           # sim and udp under AddressSanitizer and UBSan; not on Windows
    python tools/coop/menu_test.py --list

sim runs src/port/coop/ui/menu.c on the real session and transport core in a virtual network: that is where the exact
checks are, of what can be chosen, of status and of ping against the transport's own numbers. udp does a shorter run over
real sockets. game starts real games with SF64_COOP_TEST_MENU (src/port/coop/test/menu_driver.h), clicks the real ImGui
widgets through ImGui's input queue and reads back what the window holds.

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
import replay  # noqa: E402

BOOT_SECONDS = 180  # three games with software rendering on a busy machine
STEP_SECONDS = 60
TIMEOUT_MS = 5000  # the transport's default, src/port/coop/net/transport.h
# Sets a CVar the handshake compares (src/port/coop/session/sim_cvars.c), and the name the player reads for it.
ODD_CVAR, ODD_CVAR_NAME = "gInvincible", "Invincible"


class Fail(Exception):
    pass


def expect(cond, what):
    if not cond:
        raise Fail(what)


class Game:
    """One real game with the menu driver on, in its own folder."""

    def __init__(self, exe, name, out, cvars=None):
        self.name = name
        self.run = replay.make_run_dir(exe, f"menu-{name}")
        self.commands = self.run / "menu-commands.txt"
        self.state = self.run / "menu-state.txt"
        self.offset = 0
        self.items = {}
        self.texts = {}
        config = {"CVars": {"gOpenMenuBar": 1, **(cvars or {})}, "Window": {"Width": 1024, "Height": 768}}
        (self.run / "starship.cfg.json").write_text(json.dumps(config, indent=4), encoding="utf-8")
        env = replay.game_env(self.run)
        env["SF64_COOP_TEST_MENU"] = str(self.run)
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

    def wait(self, test, what, seconds=STEP_SECONDS):
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
            time.sleep(0.1)

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


SCENARIOS = {"sim": scenario_program("sim"), "udp": scenario_program("udp"), "game": scenario_game}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--only", help="comma separated scenario names")
    ap.add_argument("--list", action="store_true", help="print the scenario names and stop")
    ap.add_argument("--game", action="store_true", help="also run game")
    ap.add_argument("--exe", help="the game, for game; default: the newest build")
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
    names = args.only.split(",") if args.only else ["sim", "udp"] + (["game"] if args.game else [])
    unknown = [n for n in names if n not in SCENARIOS]
    if unknown:
        print(f"RESULT: FAILED, unknown scenario {', '.join(unknown)}; --list shows them")
        return 2

    build_dir = args.build_dir or (Path("build/x64") if IS_WINDOWS else Path("build-cmake"))
    build_dir = build_dir if build_dir.is_absolute() else ROOT / build_dir
    out = args.out or ROOT / "local" / "test-menu" / time.strftime("%Y%m%d-%H%M%S")
    out.mkdir(parents=True, exist_ok=True)

    program = None
    if any(n != "game" for n in names):
        if not args.no_build:
            if shutil.which("cmake") is None:
                print("RESULT: FAILED, cmake not found")
                return 1
            build_log = out / "build.log"
            with open(build_log, "w", encoding="utf-8") as log:
                code = subprocess.run(["cmake", "--build", str(build_dir), "--config", args.config, "--target",
                                       target(args.asan)], stdout=log, stderr=subprocess.STDOUT).returncode
            if code != 0:
                tail = build_log.read_text(encoding="utf-8", errors="replace").strip().splitlines()[-8:]
                print("\n".join(tail))
                print(f"RESULT: FAILED, {target(args.asan)} did not build (log: {build_log})")
                return 1
        program = find_exe(build_dir, args.config, args.asan)
        if program is None:
            print(f"RESULT: FAILED, no {target(args.asan)} in {build_dir}")
            return 1

    lines = []
    failed = []
    for name in names:
        started = time.monotonic()
        exe = replay.find_exe(args.exe) if name == "game" else program
        try:
            line = f"ok      {name}: {SCENARIOS[name](args, exe, out)}"
        except Fail as e:
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
