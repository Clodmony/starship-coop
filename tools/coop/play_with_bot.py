#!/usr/bin/env python3
"""Play co-op by hand with a bot (#370): one person hosts from a normal game window, a second game flies player 2.

    python tools/coop/play_with_bot.py                        # bot with no window, joins 127.0.0.1:46464
    python tools/coop/play_with_bot.py --settings-from <the starship.cfg.json your game uses>
    python tools/coop/play_with_bot.py --window --port 50000 --pick slippy

The bot is a second Starship process with its own folder under local/test-bot-<stamp>/ (its own SHIP_HOME, config,
save and logs), niced, silent, and with no window unless --window. It joins through the scripted join flow
(src/port/coop/test/join_driver.h): the same session messages the co-op window's Join sends, so the person's game
cannot tell the two apart. In a level the test pilot flies its player (SF64_COOP_LOCKSTEP_PILOT,
src/port/coop/test/lockstep_driver.h and pilot.h). It plays a replay with no input, so no controller of this PC reaches
it. It never reads or writes the person's own config, saves or mods: --settings-from only reads the file it is given,
and only the settings the handshake compares (src/port/coop/session/sim_cvars.c).

The person's game is not started or stopped from here. Until it hosts, each try of the bot ends after the join time-out
and the bot starts again, every few seconds. The bot stops when the session ends, when the handshake refuses it (the
reason is printed), or on Ctrl+C. Its logs stay in its folder; the large files (replay, archives) are removed then.

Not a test plan: the scripted checks of docs/coop/QA.md stay what they are. lockstep_test.py's `bot` scenario runs this
tool against a scripted stand-in for the person (the real co-op window, clicked by the menu driver), and checks that both
games are in lockstep and equal on every frame.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import join_test  # noqa: E402
import replay as replay_tool  # noqa: E402
from run_folder import make_run_folder  # noqa: E402

ROOT = replay_tool.ROOT
DEFAULT_PORT = 46464  # COOP_MENU_DEFAULT_PORT, src/port/coop/ui/menu.h
PICKS = ("falco", "slippy", "peppy")  # asked in this order, the next one after a refusal (join_driver.h)
NAME = "BOT"  # over its Arwing and in the window's player list (#258); COOP_NAME_CHARS has upper case letters
JOIN_TIMEOUT_MS = 10000  # COOP_MENU_TIMEOUT_MS, src/port/coop/ui/menu.h: the time-out a real join has
RETRY_SECONDS = 3  # before the next try while nobody hosts
NICE = 10
MINUTES = 60  # the no-input replay; past its end a pilot steps once a frame instead of twice (pilot.h)
FRAMES_PER_MINUTE = 30 * 60  # the game's frames at real time: 2700 took 90 s in lockstep_test.py's `bot` (#370)
SIM_CVARS = ROOT / "src" / "port" / "coop" / "session" / "sim_cvars.c"
CHECKPOINT_CVAR = "gCheckpoint"  # gCheckpoint.<level>.<field>, compared too (sim_cvars.c, CHECKPOINT_FIELDS)
BIG_FILES = ("input.sfcr", "sf64.o2r", "starship.o2r")


def sim_cvar_names(source=SIM_CVARS):
    """The CVars the handshake compares, from the table in sim_cvars.c, so the list here cannot fall behind it."""
    names = re.findall(r'^\s*\{\s*"(g\w+)",\s*"', source.read_text(encoding="utf-8"), re.MULTILINE)
    return names + [CHECKPOINT_CVAR]


def settings_from(path, names=None):
    """The compared settings of a game's config, read only: {name: value} of the top level of its "CVars". A config
    keeps a CVar with dots in its name as nested objects (gCheckpoint.0.Set), so gCheckpoint comes as one subtree."""
    names = names if names is not None else sim_cvar_names()
    cvars = json.loads(Path(path).read_text(encoding="utf-8")).get("CVars", {})
    return {name: cvars[name] for name in names if name in cvars}


def cvar_value(text):
    """--cvar's value: a number where it reads as one, else the text."""
    for kind in (int, float):
        try:
            return kind(text)
        except ValueError:
            pass
    return text


def nice_kwargs():
    """Popen arguments for a game below the person's own: nice on POSIX, below normal priority on Windows."""
    if sys.platform == "win32":
        return {"creationflags": subprocess.BELOW_NORMAL_PRIORITY_CLASS}
    return {"preexec_fn": lambda: os.nice(NICE)}


def cpu_seconds(proc):
    """User and system CPU seconds of a running process on Linux; None elsewhere or once it ended."""
    try:
        fields = Path(f"/proc/{proc.pid}/stat").read_text().rsplit(")", 1)[1].split()
        return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")
    except (OSError, IndexError, ValueError, AttributeError):
        return None


class Bot(join_test.LevelLog):
    """The bot's game in its own folder. start() starts it, poll() hands back what its reports said since the last look.
    extra_env is for a test (a hash log, an end frame); the person's run needs none."""

    name = "bot"

    def __init__(self, exe, address="127.0.0.1", port=DEFAULT_PORT, picks=PICKS, window=False, cvars=None,
                 minutes=MINUTES, extra_env=None, log=None):
        self.exe = Path(exe)
        self.run = make_run_folder(ROOT / "local", "test-bot-")
        for name in replay_tool.DATA_FILES:
            src = replay_tool.find_data(self.exe, name)
            if src is None:
                if name.endswith(".o2r"):
                    raise SystemExit(f"play_with_bot: {name} not found next to {self.exe}; "
                                     "run python tools/coop/build.py --assets first, or pass --exe")
                continue
            shutil.copy2(src, self.run / name)
        config = {"CVars": dict(cvars or {})}
        (self.run / "starship.cfg.json").write_text(json.dumps(config, indent=4), encoding="utf-8")
        replay = self.run / "input.sfcr"
        join_test.write_replay(replay, max(1, int(minutes * FRAMES_PER_MINUTE)), join_test.SEEDS["client0"])
        self.report_path = self.run / "join-report.txt"
        self.sync_report = self.run / "lockstep-report.txt"
        self.sync_log = self.run / "lockstep-log.txt"
        env = replay_tool.game_env(self.run, person_plays=window)
        if window:
            env.update(replay_tool.DRAWN)
            env["SDL_AUDIODRIVER"] = "dummy"  # one game on the speakers is enough
        # A limit above real time, as games that meet over sockets have (join_test.py): before the session the bot
        # does not spin a core on the title screen, and in it lockstep holds it to the person's game.
        env["SF64_COOP_TEST_SPEED"] = replay_tool.MEETING_SPEED
        env.update({
            "SF64_COOP_REPLAY_PLAY": str(replay),
            "SF64_COOP_REPLAY_REPORT": str(self.run / "replay-report.txt"),
            "SF64_COOP_JOIN_CONNECT": f"{address}:{port}",
            "SF64_COOP_JOIN_PICKS": ",".join(picks),
            "SF64_COOP_JOIN_REPORT": str(self.report_path),
            "SF64_COOP_JOIN_CONNECT_FRAME": str(replay_tool.CLIENT_JOIN_FRAME),
            "SF64_COOP_JOIN_NAME": NAME,
            "SF64_COOP_JOIN_TIMEOUT_MS": str(JOIN_TIMEOUT_MS),
            "SF64_COOP_LOCKSTEP_PILOT": "1",
            "SF64_COOP_LOCKSTEP_REPORT": str(self.sync_report),
        })
        env.update(extra_env or {})
        self.env = env
        self.proc = None
        self.log = open(log or (self.run / "game.log"), "ab")
        self.seen = 0
        self.tries = 0

    def start(self):
        """Starts the game, or starts it again after a try that reached nobody: the folder and settings stay."""
        self.stop()
        for path in (self.report_path, self.sync_report):
            if path.exists():
                path.unlink()
        self.seen = 0
        self.tries += 1
        self.proc = subprocess.Popen([str(self.exe)], cwd=self.run, env=self.env, stdout=self.log,
                                     stderr=subprocess.STDOUT, **nice_kwargs())

    def poll(self):
        """The join report's lines since the last call: (word, values, line)."""
        lines = self.report()
        new, self.seen = lines[self.seen:], len(lines)
        return new

    def sync(self):
        return join_test.read_report(self.sync_report)

    def stop(self):
        """Stops the bot's own process; only the one this object started."""
        if self.proc is not None and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()

    def close(self, tidy=True):
        """Stops it; with tidy the large files go and the logs stay."""
        self.stop()
        self.log.close()
        if tidy:
            for name in BIG_FILES:
                path = self.run / name
                if path.exists():
                    path.unlink()

    def cpu_seconds(self):
        return cpu_seconds(self.proc)


def steps(address, port, window):
    """What the person does, in their own game."""
    where = "on this PC" if address in ("127.0.0.1", "localhost") else f"at {address}"
    return f"""
In your own game ({where}), the same build as the bot:
  1. Press F1 for the menu bar, then Co-op > Co-op window.
  2. Port: {port}. Click "Host a game". The status line says "Hosting on port {port}, 1 of 2 players."
  3. Wait: the bot joins by itself within a few seconds and picks a character.
     Your window then lists "Player 2: {NAME}, Falco" (Slippy or Peppy if Falco is taken).
  4. "Start at": the level you want (Corneria is the one the pilot knows best). Click "Start the level".
  5. Fly. The bot flies player 2 {'in its own window' if window else 'with no window'}. It presses nothing in menus,
     on the map or in cutscenes: those are yours. At a level's results screen it never presses A, so the game goes
     on 30 s after you do. It is not invincible: when it goes down, it spends a life of the team's pool.
Ctrl+C here stops the bot. Your game keeps running: it waits up to 15 s for the lost player (decision 0014),
then, with nobody else in it, the session ends and you play on alone.
"""


def describe(word, values, line):
    """One line for the person about a line of the bot's join report, or None."""
    if word == "joined":
        return f"The bot joined as player {int(values.get('player', 0)) + 1}."
    if word == "pick":
        return f"The bot picked {values.get('character', '?')}."
    if word == "pick_refused":
        return f"{values.get('character', '?')} is taken; the bot asks for the next one."
    if word == "level_start":
        return "The level starts."
    if word == "level_running":
        return f"In the level (level {values.get('level', '?')}): the pilot flies."
    if word == "left":
        return f"A player left: {line.split('text=', 1)[-1]}"
    if word == "recover":
        return f"The session starts the level again: {line.split('text=', 1)[-1]}"
    return None


def refusal_text(bot):
    """The whole reason of a refusal: the handshake's text runs over several lines (identity.c, one per difference),
    up to the report's next line of its own."""
    time.sleep(0.5)  # the rest of the text is written in the same frame; give the file a moment
    text = bot.report_path.read_text(encoding="utf-8", errors="replace")
    reason = text.split("refused text=", 1)[-1]
    reason = re.split(r"\n(?:ended|left|session|joined) ", reason)[0]
    return " ".join(part.strip() for part in reason.splitlines() if part.strip())


def watch(bot, out=print, retry_seconds=RETRY_SECONDS, stop_when=None):
    """Runs the bot until its session ends, the handshake refuses it, or stop_when() is true. Returns (code, reason):
    0 the session ended or stop_when, 2 refused, 1 the game ended by itself."""
    joined = False
    waiting_said = False
    bot.start()
    while True:
        if stop_when is not None and stop_when():
            return 0, "stopped"
        for word, values, line in bot.poll():
            text = describe(word, values, line)
            if text:
                out(text)
            if word == "joined":
                joined = True
            elif word == "refused":
                reason = refusal_text(bot)
                out(f"Your game refused the bot: {reason}")
                if "setting" in reason.lower() or "differ" in reason.lower():
                    out("If a game setting differs, pass --settings-from <the starship.cfg.json your game uses>, "
                        "or --cvar NAME=VALUE.")
                return 2, f"refused: {reason}"
            elif word == "ended":
                reason = line.split("text=", 1)[-1]
                if joined:
                    out(f"The session ended: {reason}")
                    return 0, f"ended: {reason}"
                if not waiting_said:
                    out(f"Nobody hosts on the bot's address yet ({reason}). It tries again every few seconds.")
                    waiting_said = True
                bot.stop()
                time.sleep(retry_seconds)
                bot.start()
                break
        if bot.proc.poll() is not None:
            out(f"The bot's game ended by itself with code {bot.proc.returncode}; its log is in {bot.run}.")
            return 1, f"exited {bot.proc.returncode}"
        time.sleep(0.5)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--exe", help="Starship executable: the same build as the person's game (default: newest build "
                                  "in this checkout)")
    ap.add_argument("--address", default="127.0.0.1", help="the host's address (default 127.0.0.1, this PC)")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"the host's port (default {DEFAULT_PORT})")
    ap.add_argument("--pick", default=",".join(PICKS),
                    help="characters to ask for in order, comma-separated (default falco,slippy,peppy)")
    ap.add_argument("--window", action="store_true", help="the bot's game opens a window and draws (no sound)")
    ap.add_argument("--settings-from", metavar="CONFIG",
                    help="read the settings the handshake compares from this starship.cfg.json (read only)")
    ap.add_argument("--cvar", action="append", default=[], metavar="NAME=VALUE",
                    help="one more setting for the bot's config; may repeat")
    ap.add_argument("--minutes", type=float, default=MINUTES,
                    help=f"how long the bot can play before its no-input replay runs out (default {MINUTES})")
    ap.add_argument("--print-steps", action="store_true", help="print the steps for the person and exit")
    args = ap.parse_args()
    if args.print_steps:
        print(steps(args.address, args.port, args.window))
        return 0
    picks = [p.strip().lower() for p in args.pick.split(",") if p.strip()]
    for pick in picks:
        if pick not in PICKS:
            ap.error(f"--pick: {pick} is none of {', '.join(PICKS)}")
    cvars = settings_from(args.settings_from) if args.settings_from else {}
    for item in args.cvar:
        name, sep, value = item.partition("=")
        if not sep or not name:
            ap.error(f"--cvar {item}: want NAME=VALUE")
        cvars[name] = cvar_value(value)
    exe = replay_tool.find_exe(args.exe)
    bot = Bot(exe, args.address, args.port, picks, args.window, cvars, args.minutes)
    print(f"Bot: {exe}")
    print(f"Its folder: {bot.run} (own config, save and logs; nice {NICE}; "
          f"{'a window' if args.window else 'no window'}, no sound)")
    if cvars:
        print(f"Settings for the handshake: {', '.join(sorted(cvars))}")
    print(steps(args.address, args.port, args.window))
    code = 1
    try:
        code, _ = watch(bot, lambda text: print(text, flush=True))
    except KeyboardInterrupt:
        print("Stopping the bot.")
        code = 0
    finally:
        bot.close()
    print(f"Logs: {bot.run}")
    return code


if __name__ == "__main__":
    sys.exit(main())
