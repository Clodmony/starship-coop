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
  unlimited the flight of steer with no frame limit on either game (#287): the games run as fast as they can, and
            the client's frames before it joins are the same however fast that is. Hashes equal. On a Release
            build a client that joined in its first frames left the host at level frame 4 to 9 (voice player).
  aim       enemies aim at the nearest human (#193): on Corneria's rails some enemy shots or missiles fly at player 1,
            the two games log the same ones (src/port/coop/test/aim_trace.h), and the hashes stay equal.
  return    a player 2-4 who goes down comes back (decision 0007 rule 4, #14): player 1 is forced down on Corneria's
            rails, is out for exactly RETURN_FRAMES frames, comes back beside player 0 with full shields, 3 bombs and
            one life less in the pool, gLifeCount[0]; down again with the pool at 0 it stays out until a life comes in.
            The two games hash alike on every frame.
  return_all_range the same in Corneria's all-range section, where a return also waits for player 0 to be ACTIVE.
  return_fox_down player 1 goes down, and player 0 goes down before player 1 is due back: the return is dropped,
            the level restarts as for one player, player 0's death spends the life and player 1 joins again for free.
            The level is closed for the refill of player 0 (SF64_COOP_TEST_FOX_REFILL_OFF), as a level not yet open is.
  fox_refill player 0 would go down on Corneria's rails with lives in the pool (decision 0007 rule 3): a life refills
            it where it flies (full shields, 3 bombs, single laser), the pool drops by one, player 1 flies on untouched,
            nothing fades or restarts; at pool 0 it goes down as before and player 1 parks. Same hash on every frame.
  fox_refill_all_range the same in Corneria's all-range section, during the fight with Granga.
  fox_lead_swap the spike of #183 (0007 rule 13), with SF64_COOP_TEST_LEAD_SWAP: at pool 0 player 0 would go down
            while player 1 flies, and player 1's ship takes slot 0: the path goes on, the client steers slot 0 and
            looks through it, the host's ship returns in slot 1 for a life, the lead swaps back, and with nobody
            left slot 0 goes down. An experiment; not in the default run.
  fox_lead_swap_all_range the same in Corneria's all-range section.
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
  save      the intro skip goes by the host's save, which every game holds in a session (decision 0009 item 8, #191),
            with real save files: with Corneria cleared in the host's and the client's blank, the host's Start in the
            intro skips it on both games and both players fly, and so does the client's; with it cleared only in the
            client's save, nobody's Start does anything.
  save_own  the control for the session's save: a client that keeps its own save, one byte apart from the host's,
            parts from it at frame 0 in region globals, and desync_report.py names gSaveFile.planet.
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
  results   the level clear status screen out of Corneria (#252): the scene holds until both pressed A, a press
            before 2 s does not count; then again with player 1 idle: it goes on alone after 30 s. Same on both.
  guests    Corneria, client as Falco, Slippy and Peppy down (#15, 0012 B4): Katt flies Falco's scenes.
  map_saves two PCs with two saves (decision 0009 item 8): the host's blank, the client's with every level cleared
            and its own sound settings. Pilots fly Corneria to its end, the map writes the save, and the games go on
            through the map into the next level: equal on every frame. Then the client leaves: both save files are
            byte for byte as before, each game's save in memory is its file's again, and in the session both held the
            host's progress with their own sound settings. About 15 minutes.
  map_saves_host_full the same with the host's save all cleared and the client's blank.
  map_saves_equal the same with one save on both, all cleared.
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
            the boss to level complete. Same hash on every frame. By hand, not in test_all.py. All three took 23
            minutes with other tests running, Titania alone about 8 (level complete at frame 36223, limit 42000, #266).
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
            `dogfight` checks the picks (#266): each time a Star Wolf member that chases Fox picks a human, it takes the
            nearer one (PICK_MARGIN), and player 1 is picked at least once; the frames each human is kept on are
            printed, not checked, as they follow which members chase Fox when. `inhale`: the face's inhale swallows
            player 1, chews and spits it out (its wings break), and player 0 flies on and is not hit meanwhile
            (owner's choice A for #213's option D). By hand, not in test_all.py: about 2 minutes.
  other_bosses the other bosses go after the nearer human too (#220), read from the boss trace, which the base build
            writes as well: `granga`, Corneria's Granga walks at and turns to player 1 on most frames that player 1 is
            the nearer; `shogun`, Sector Y's Shoguns keep player 1 often and turn toward the player they keep;
            `crusher`, Meteo's Crusher rings home in on player 1 too (SF64_COOP_BOSS_POKE puts the Crusher into its
            ring attack, which takes its shields shot away to reach); `sarumarine`, Zoness' Sarumarine balls fly at
            player 1; `bacoon`, Aquas' Bacoon looks at player 1; `spyborg`, Sector X's Spyborg goes after player 1
            across the path, `goras`, Titania's Goras goes after the player it keeps (pilots on both), and `vulkain`,
            Solar's Vulkain turns its head to player 1 (campaign run). On
            rails player 0 holds the top left and player 1 the bottom right, the nearer one. Hashes and boss traces
            equal on both games. `macbeth` (wave 7): Macbeth's train end, actor 207, goes to and faces the tank that
            is the nearer when each of its states starts; pilots drive both Landmasters and keep them apart in the
            fight (SF64_COOP_PILOT_LANES), player 1 on the train's side. The build before it fails as "goes to and
            faces player 0 while player 1 is nearer". About 5 minutes.
  w7macbeth_ram Macbeth's train end at the end of the track (#220, the owner's choice of 2026-11-12): it rams player 0
            (states 18 and 19), and that ends the try as upstream: player 0 goes down at the ram, not refilled under
            it (decision 0007's refill would buy only the next ram until the pool is gone). Pilots in lanes, no
            gInvincible, a pool of 90, SF64_COOP_MA_TRACK_END to reach the track's end at the fight's start. By hand,
            about 3 minutes.
  sy_robot_spread Sector Y's robots spread some shots around the human they aim at (#273): on the rails player 0
            holds the top left and player 1 flies near the middle. The aim trace's syrobot lines give each robot
            shot's start and direction: a plain one flies at the nearest human, a spread one at a point at most 150
            from it in x and y, and those with player 1 the nearer are spread around it. Hashes equal on all 9500
            frames. By hand, not in test_all.py: about a minute.
  eels    Aquas' boss fight with both marines firing, so the eels of the Bacoon's pods (actor 255) are shot and
            explode piece by piece. src/port/coop/test/smoke_trace.h names every smoke puff: each puff of an eel has to
            lie in the fight, not at a position read from outside the eel's history (the desync of a real Windows and
            Linux session, NOTES.md 2026-11-11). Hashes and smoke traces equal on both games.
  player_names two players with names of their own (#257, #258), typed as a player might ("Captain Fox", "sam.ok!"):
            the session cleans them to CAPTAIN and SAM.OK!, the host shares both, and by the level start each game
            holds both. Each game marks the other player's ship with its name (CAPTAIN on the client, SAM.OK! on the
            host) and never its own, and the hashes are equal on every frame.
  macbeth_bombs Macbeth with two Landmasters (#272): a bomb's fire and a state-16 beam of the train's end, placed on
            one tank at a time by src/port/coop/test/mabomb_probe.h, hit that tank, player 1 as well as player 0, and
            no other; on a build without the fix they hit player 0 only. Hashes and the two probe logs equal.
  log     the co-op log (#172): each game's folder holds its own log and the other player's, streamed during the
            session; the hash stays equal with streaming on; nothing personal is in the files; coop_log.py merges them.
  survive_gaps_route #267: Corneria parts and starts again, pilots fly it to the map and into the next level, which
            parts 200 frames in and starts again with the route it carries; flown to its end, the map and the level
            after. Every frame of both restarted runs equal, and every game holds the same route and save at every
            level and map (the driver's `route` lines). About 5 minutes.
  survive_gaps_map #267: the games part on the map after Corneria: Corneria starts again from the session's own level
            start, the map's changes to the route and the save dropped, equal from its frame 0.
  survive_gaps_map_lost the same with a client that stops sending input on the map: it comes back with the session id.
  survive_gaps_checkpoints #267: a restart from the game's own checkpoint, one session per level (--levels; by default
            the 11 rails levels' own rings, put where player 2 flies, and Katina and Bolse; andross by name), flown
            on to the level's end. About 20 minutes.
  survive_gaps_venom_jump #267: a restart in Venom 2's second part, which Venom 1 jumps into: the restart point a
            jump leaves, flown on into the level after.
  survive_gaps_four #267: four games; one lost, the level starts again for three; a second lost, again for two.

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
import make_save  # noqa: E402
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
MAP_FRAMES = 3000  # the map after Corneria, on its way to the next level with A every 30 frames: room enough
GSTATE_PLAY = 7
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
            clients=1, client_envs=None, replay_frames=REPLAY_FRAMES, saves=None):
    """A host and its clients through the join driver, on their way to a level. Returns them started: with one client as
    (host, client), with more as (host, client, client, ...). client_envs holds one more dict per client. saves maps a
    game's name (host, client0, ...) to a save file that goes into its folder before it starts: a game holds a blank
    save otherwise (it writes gDefaultSave at boot), and a real pair of PCs holds two different ones. A save is a file
    or its bytes (make_save.save_file()); None is no file."""
    folder.mkdir(parents=True, exist_ok=True)
    port_file = folder / "port"
    env = dict(both or {})
    env["SF64_COOP_TEST_START_SEEDS"] = SEEDS
    host = Client(args, folder, "host", {**env, "SF64_COOP_JOIN_PORT_FILE": str(port_file),
                                         "SF64_COOP_JOIN_CLIENTS": str(clients), "SF64_COOP_JOIN_LEVEL": "0",
                                         "SF64_COOP_JOIN_START_FRAME": str(START_FRAME), **(host_env or {})},
                  replay_frames)
    if saves and saves.get("host") is not None:
        put_save(saves["host"], host.run)
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
                                                         "SF64_COOP_JOIN_PICKS": pick,
                                                         "SF64_COOP_JOIN_CONNECT_FRAME": str(replay_tool.CLIENT_JOIN_FRAME),
                                                         **(client_env or {}),
                                                         **((client_envs or [{}] * clients)[n])}, replay_frames))
        if saves and saves.get(f"client{n}") is not None:
            put_save(saves[f"client{n}"], games[-1].run)
        games[-1].start(client_config if client_config is not None else config)
    return tuple(games)


def put_save(save, folder):
    """A save file into a game's folder, where the game reads it (default.sav, libultraship os_eeprom.cpp)."""
    path = folder / "default.sav"
    if isinstance(save, (bytes, bytearray)):
        path.write_bytes(bytes(save))
    else:
        shutil.copy2(save, path)


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


def run_short(args, folder, script=sway, frames=SHORT_FRAMES, host_env=None, client_env=None, both=None, seconds=900,
              saves=None):
    """Both games fed by script, to frame `frames`. Returns the two, stopped. saves: see session()."""
    folder.mkdir(parents=True, exist_ok=True)
    feed = folder / "feed.txt"
    write_feed(feed, frames + 16, script)
    env = {"SF64_COOP_LOCKSTEP_FEED": str(feed), "SF64_COOP_LOCKSTEP_END": str(frames), **(both or {})}
    host, client = session(args, folder, host_env, client_env, env, saves=saves)
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


def sc_unlimited(args, folder):
    """steer with no frame limit (#287). A Release game plays 1100 frames a second and more, so a client that joins as
    soon as it can has played a few title frames when the host's level start arrives, and the two part at level frame 4
    to 9 (the voice player of the sound thread, NOTES.md #287). With a limit of 8 the client had played 290. The session
    ends, both games play on alone and no `done` comes: this waits two minutes, a run takes seconds."""
    host, client = run_short(args, folder, both={"SF64_COOP_TEST_SPEED": "max"}, seconds=120)
    frames = compare(host, client, SHORT_FRAMES)
    moved = flown(client)
    expect(moved[0] > 500 and moved[1] > 500, f"a player did not fly: moved {moved}")
    return f"no frame limit: same hash on {frames} frames, moved {moved[0]:.0f} and {moved[1]:.0f}; {played(host)}"


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


def run_return(args, folder, plan, pokes=None, both=None, level=None, script=None):
    """Both games through one plan, with sway (and L at plan['jump']). Returns the two, stopped."""
    folder.mkdir(parents=True, exist_ok=True)
    frames = plan["frames"]
    feed = folder / "feed.txt"
    jump = plan["jump"]
    script = script or sway
    write_feed(feed, frames + 16, press(script, {(jump, 0)}, BUTTON_L) if jump is not None else script)
    config = {"CVars": {"gDebugJumpToAllRange": 1}} if jump is not None else None
    pokes = pokes or return_pokes(plan)
    env = {"SF64_COOP_LOCKSTEP_FEED": str(feed), "SF64_COOP_LOCKSTEP_END": str(frames), "SF64_COOP_VIEW_POKE": pokes,
           **(both or {})}
    traces = {name: folder / f"{name}-view.txt" for name in ("host", "client0")}
    for path in traces.values():
        if path.exists():
            path.unlink()
    host_env = {"SF64_COOP_VIEW_TRACE": str(traces["host"])}
    if level is not None:
        host_env["SF64_COOP_JOIN_LEVEL"] = str(level)
    host, client = session(args, folder, host_env=host_env,
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
    """0007 rule 3 in a level not open for the refill: player 0 down with a return waiting. Corneria is open since the
    refill was built, so the run closes it (SF64_COOP_TEST_FOX_REFILL_OFF, src/port/coop/test/switches.h)."""
    plan = FOX_DOWN_PLAN
    pokes = f"{plan['pool']}:0:lives:1,{plan['downs'][0]}:1:down:4,{plan['fox']}:0:down:4"
    host, client, traces = run_return(args, folder, plan, pokes, both={"SF64_COOP_TEST_FOX_REFILL_OFF": "1"})
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


# Decision 0007 rule 3, Corneria (rule 12): player 0 is poked down twice with lives in the pool, then once at pool 0.
# `end` leaves the last one FOX_REFILL_TAIL frames: long enough to see player 0 go down and player 1 park, short of
# the fade and the game over that follow (Cutscene_KillPlayer comes 20 frames or more after the down, the fade 70
# after that).
FOX_REFILL_TAIL = 40
FOX_REFILL_POOL = 2
FOX_REFILL_MERCY = 30  # COOP_RETURN_MERCY_FRAMES
FOX_REFILL_DOWN_LINES = (20090, 20091, 20092, 20318)  # what Cutscene_KillPlayer has the team say
FOX_REFILL_SFX_DOWN = 0x0900C010  # NA_SE_ARWING_DOWN, include/sfx.h: what Player_Down plays
FOX_REFILL_PLANS = {
    "rails": {"pool": JOIN_FRAME + 50, "downs": (JOIN_FRAME + 100, JOIN_FRAME + 400), "last": JOIN_FRAME + 900,
              "jump": None, "shields": 255},
    "all-range": {"pool": JOIN_FRAME + 650, "downs": (JOIN_FRAME + 700, JOIN_FRAME + 1000), "last": JOIN_FRAME + 1400,
                  "jump": JOIN_FRAME + 100, "shields": 255},
}


# The other levels: name -> (LevelId of include/sf64level.h, section, the first frame both players fly with this
# file's seeds and feed; read from a run's player trace). One plan shape for all: the pool is set 20 frames after that,
# with both at full shields, player 0 is poked down after 60 and 260 frames and, at pool 0, after 460.
FOX_REFILL_LEVELS = {
    "meteo": (1, "rails", 853), "sector_x": (2, "rails", 343), "area_6": (3, "rails", 542),
    "sector_y": (5, "rails", 611), "venom_1": (6, "rails", 302), "solar": (7, "rails", 631),
    "zoness": (8, "rails", 272), "fortuna": (14, "all-range", 266), "katina": (16, "all-range", 262),
    "bolse": (17, "all-range", 272), "sector_z": (18, "all-range", 802), "venom_2": (19, "all-range", 622),
    # The Landmaster and the Blue Marine: their own down sites (Tank_UpdateOnRails, Player_UpdateOnRails).
    "macbeth": (11, "rails", 264), "titania": (12, "rails", 602), "aquas": (13, "rails", 546),
}
# radioDamageTimer of a poke. It counts down in every state and the down is only tested while player 0 is ACTIVE, so in
# all-range it has to outlast a U-turn (about 110 frames) that a poke can fall into.
FOX_REFILL_TIMER = {"rails": 4, "all-range": 150}


def fox_refill_level_plan(name):
    level, section, join = FOX_REFILL_LEVELS[name]
    return {"level": level, "section": section, "pool": join + 20, "downs": (join + 60, join + 260),
            "last": join + 460, "jump": None, "timer": FOX_REFILL_TIMER[section]}


def fox_refill_pokes(plan):
    pool = plan["pool"]
    pokes = [f"{pool}:0:lives:{FOX_REFILL_POOL}", f"{pool}:0:bombs:{FOX_BOMBS}", f"{pool}:1:bombs:{FOX_BOMBS}",
             f"{pool}:1:shields:255"]
    pokes.append(f"{pool}:0:shields:{RETURN_SHIELDS}")
    pokes += [f"{down}:0:down:{plan.get('timer', 4)}" for down in plan["downs"] + (plan["last"],)]
    return ",".join(pokes)


def fox_refill_check(game, view_path, plan, section):
    """What one game shows of a fox_refill plan. Returns a line for the summary."""
    rows = [r for r in player_trace.parse(game.run / "trace.txt")
            if r.level == plan.get("level", 0) and len(r.players) > 1]
    by_frame = {r.frame: r for r in rows}
    trace = view_check.read_trace(1, view_path)
    view = trace.by_frame
    mode = player_trace.ALL_RANGE if section == "all-range" else player_trace.ON_RAILS
    frames = [r.frame for r in rows]
    expect(frames == sorted(set(frames)), f"{game.name}: the level restarted (the trace's frames go back)")
    found = []
    timer = plan.get("timer", 4)

    def refill_from(frame):
        """The first frame after `frame` on which the pool is one lower than on the frame before."""
        return next((f for f in sorted(view) if f > frame and f - 1 in view
                     and view[f].players[0].lives == view[f - 1].players[0].lives - 1), None)

    for n, poke in enumerate(plan["downs"]):
        # The poke is seen on the first frame player 0's ACTIVE update runs: the next one on the rails, after a U-turn
        # in all-range, where the down is not tested.
        at = refill_from(poke)
        pool = FOX_REFILL_POOL - n
        expect(at is not None and at <= poke + timer,
               f"{game.name}: no refill within {timer} frames of the poke at {poke} (pool then "
               f"{view[poke].players[0].lives if poke in view else None}, next change at {at})")
        expect(section != "rails" or at == poke + 1, f"{game.name}: refilled at {at}, want {poke + 1}")
        expect(by_frame[at].mode == mode, f"{game.name}: frame {at} is not {section}")
        before, after = view[at - 1].players, view[at].players
        expect((before[0].lives, after[0].lives) == (pool, pool - 1),
               f"{game.name}: pool {before[0].lives} -> {after[0].lives} at frame {at}, want {pool} -> {pool - 1}: "
               f"player 0 was not refilled (state {after[0].state}, shields {after[0].shields})")
        # Solar's heat takes a point on the same frame (Play_UpdateLevel, after the players).
        expect(RETURN_SHIELDS - 1 <= after[0].shields <= RETURN_SHIELDS and after[0].bombs == RETURN_BOMBS
               and after[0].state == 3,
               f"{game.name}: player 0 refilled with shields {after[0].shields}, bombs {after[0].bombs}, state "
               f"{after[0].state} at frame {at}")
        expect(by_frame[at].lasers[0] == 0, f"{game.name}: player 0's lasers {by_frame[at].lasers} after the refill")
        expect(view[at].guns[0] == (0.0, 0.0), f"{game.name}: player 0's guns {view[at].guns[0]} after the refill")
        # Player 1 is not touched: where it was plus a frame's flight, its own shields and bombs.
        p1b, p1a = by_frame[at - 1].players[1], by_frame[at].players[1]
        expect(not p1a.parked and p1a.state == player_trace.ACTIVE and p1a.draw != 0,
               f"{game.name}: player 1 parked or not flying at the refill, frame {at}")
        expect(abs(p1a.x - p1b.x) < 80 and abs(p1a.y - p1b.y) < 80,
               f"{game.name}: player 1 moved from {p1b.x, p1b.y} to {p1a.x, p1a.y} at the refill")
        # Its shields move by a frame's drain or heal at most (2, Player_UpdateShields): Granga hits it too.
        expect(after[1].bombs == FOX_BOMBS and abs(after[1].shields - before[1].shields) <= 2,
               f"{game.name}: player 1 has {after[1].bombs} bombs, shields {before[1].shields} -> {after[1].shields}")
        # Player 0 stays where it flies, and the path goes on.
        p0b, p0a = by_frame[at - 1].players[0], by_frame[at].players[0]
        expect(abs(p0a.x - p0b.x) < 80 and abs(p0a.y - p0b.y) < 80,
               f"{game.name}: player 0 moved from {p0b.x, p0b.y} to {p0a.x, p0a.y} at the refill")
        if section == "rails":
            later = by_frame[at + 30]
            expect(later.pathvel > 0 and later.path > by_frame[at].path,
                   f"{game.name}: the path stopped at the refill: {by_frame[at].path} -> {later.path}")
        found.append(f"refilled at {at}, pool {pool} -> {pool - 1}")

    # From the first poke to the last: player 0 never down, player 1 never parked, no fade, no down theme or line.
    first = plan["downs"][0]
    last = next((r.frame for r in rows if r.frame > plan["last"] and r.players[0].state == PLAYERSTATE_DOWN), None)
    expect(last is not None, f"{game.name}: player 0 did not go down after the last poke at {plan['last']}, at pool 0")
    span = [r for r in rows if first <= r.frame < last]
    bad = [(r.frame, r.players[0].state) for r in span if r.players[0].state not in player_trace.FLYING]
    expect(not bad, f"{game.name}: player 0 left its flight at {bad[:1]}")
    parked = [r.frame for r in span if r.players[1].parked]
    expect(not parked, f"{game.name}: player 1 parked at frame {parked[:1]} while player 0 was refilled")
    lines = sorted({r.radio_id for r in span if r.radio_id in FOX_REFILL_DOWN_LINES})
    expect(not lines, f"{game.name}: the team said {lines}, the lines of a player down")
    vspan = [v for v in trace.rows if first <= v.frame < last]
    faded = [v.frame for v in vspan if v.fill[3] != 0 and v.fill[:3] == (0, 0, 0)]
    expect(not faded, f"{game.name}: the screen faded to black at frame {faded[:1]}")
    theme = [v.frame for v in vspan if any(sfx == FOX_REFILL_SFX_DOWN for sfx, _ in v.sfx)]
    expect(not theme, f"{game.name}: the down sound played at frame {theme[:1]}")

    # At pool 0 the original rule: player 0 goes down, player 1 parks on the same frame, the life is not spent yet.
    row, v = by_frame.get(last), view.get(last)
    expect(row is not None and v is not None, f"{game.name}: no row at frame {last}")
    expect(v.players[0].lives == 0 and row.players[0].state == PLAYERSTATE_DOWN and row.players[1].parked,
           f"{game.name}: at pool {v.players[0].lives}, frame {last}: player 0 state {row.players[0].state}, player 1 "
           f"parked {row.players[1].parked}; want pool 0, DOWN and parked")
    expect("frames" not in plan or last < plan["frames"],
           f"{game.name}: the last down at {last} is past the {plan.get('frames')} frames compared")
    found.append(f"down at {last} at pool 0, player 1 parked with it")
    return "; ".join(found)


def fox_refill_in(args, folder, section, both=None):
    plan = dict(FOX_REFILL_PLANS[section])
    plan["frames"] = plan["last"] + FOX_REFILL_TAIL
    host, client, traces = run_return(args, folder, plan, fox_refill_pokes(plan), both=both)
    frames = compare(host, client, plan["frames"])
    lines = [fox_refill_check(game, traces[game.name], plan, section) for game in (host, client)]
    expect(lines[0] == lines[1], f"the games saw different refills: host {lines[0]}, client {lines[1]}")
    return f"same hash on {frames} frames; {lines[0]}; player 1 flew on, no fade, no restart"


def sc_fox_refill(args, folder):
    """Decision 0007 rule 3 on Corneria's rails: a life refills player 0 in place, the team flies on."""
    return fox_refill_in(args, folder, "rails")


def sc_fox_refill_all_range(args, folder):
    return fox_refill_in(args, folder, "all-range")


# A long flight with a deep pool: the feed does not dodge, so player 0 loses its shields to what the level throws at it
# (enemy fire, collisions, the boss) and is refilled each time. Frames from the level's start. Nothing here pokes
# player 0 down: every refill follows a hit of the level's own.
FOX_REFILL_LONG_POOL = 90
FOX_REFILL_LONG_WEAK = 24  # times player 0's shields are set to 1 (the view trace takes 32 pokes)
FOX_REFILL_LONG_FRAMES = {"rails": 9000, "all-range": 4000}
FOX_REFILL_LONG_MORE = {"sector_z": 4300}  # the first hit on player 0 in Sector Z comes at 4145; the level ends at 4380
FOX_REFILL_LONG_LEVELS = {"corneria": (0, "rails", JOIN_FRAME), **FOX_REFILL_LEVELS}


def fox_refill_long_story(game, view_path, level, end):
    """From one game, up to frame `end`: (refills of player 0, returns of player 1, frames by level mode, last frame)."""
    rows = [r for r in player_trace.parse(game.run / "trace.txt")
            if r.level == level and len(r.players) > 1 and r.frame < end]
    frames = [r.frame for r in rows]
    expect(frames == sorted(set(frames)), f"{game.name}: the level restarted (the trace's frames go back)")
    down = [(r.frame, r.players[0].state) for r in rows if r.players[0].state == PLAYERSTATE_DOWN]
    expect(not down, f"{game.name}: player 0 went down at frame {down[:1]} with lives in the pool")
    view = view_check.read_trace(1, view_path).by_frame
    by_frame = {r.frame: r for r in rows}
    refills, returns = [], []
    for f in sorted(view):
        if f - 1 not in view or f not in by_frame or f - 1 not in by_frame:
            continue
        if view[f].players[0].lives == view[f - 1].players[0].lives - 1:
            came_back = by_frame[f - 1].players[1].parked and not by_frame[f].players[1].parked
            (returns if came_back else refills).append(f)
            if not came_back:
                p = view[f].players[0]
                expect(p.state == 3 and p.shields >= RETURN_SHIELDS - 1 and p.bombs == RETURN_BOMBS,
                       f"{game.name}: a life went at frame {f} with player 0 in state {p.state}, shields {p.shields}")
    modes = {}
    for r in rows:
        modes[r.mode] = modes.get(r.mode, 0) + 1
    return refills, returns, modes, rows[-1].frame


def sc_fox_refill_long(args, folder):
    """Decision 0007 rule 12: a long flight per level (--levels, default all open ones) in which player 0 is refilled
    whenever the level itself takes its last shields, the two games equal on every frame."""
    names = args.levels or list(FOX_REFILL_LONG_LEVELS)
    done = []
    for name in names:
        expect(name in FOX_REFILL_LONG_LEVELS, f"no level {name}: {', '.join(FOX_REFILL_LONG_LEVELS)}")
        level, section, join = FOX_REFILL_LONG_LEVELS[name]
        plan = {"frames": FOX_REFILL_LONG_MORE.get(name, FOX_REFILL_LONG_FRAMES[section]), "jump": None}
        # And its shields are set to 1 now and then, so the next thing that hits it takes the last of them: the
        # level's own hit, with the timers a real hit leaves, on whatever frame it comes.
        step = (plan["frames"] - join - 100) // FOX_REFILL_LONG_WEAK
        pokes = ",".join([f"{join + 20}:0:lives:{FOX_REFILL_LONG_POOL}"] +
                         [f"{join + 100 + n * step}:0:shields:1" for n in range(FOX_REFILL_LONG_WEAK)])
        host, client, traces = run_return(args, folder / name, plan, pokes, level=level)
        frames = compare(host, client, plan["frames"])
        stories = [fox_refill_long_story(game, traces[game.name], level, plan["frames"]) for game in (host, client)]
        expect(stories[0][:3] == stories[1][:3], f"{name}: the games differ: host {stories[0]}, client {stories[1]}")
        refills, returns, modes, _ = stories[0]
        expect(refills, f"{name}: player 0 never lost its shields in {plan['frames']} frames, so nothing was shown")
        where = ", ".join(f"{count} frames {'all-range' if mode == player_trace.ALL_RANGE else 'on rails'}"
                          for mode, count in sorted(modes.items()))
        done.append(f"{name}: same hash on {frames} frames ({where}); player 0 refilled {len(refills)} times (first "
                    f"{refills[0]}, last {refills[-1]}), never down, no restart; player 1 returned {len(returns)} times")
    return " | ".join(done)


def sc_fox_refill_levels(args, folder):
    """Decision 0007 rule 12: the same on the other levels that are open for the refill, or on --levels."""
    names = args.levels or list(FOX_REFILL_LEVELS)
    done = []
    for name in names:
        expect(name in FOX_REFILL_LEVELS, f"no level {name}: {', '.join(FOX_REFILL_LEVELS)}")
        plan = fox_refill_level_plan(name)
        plan["frames"] = plan["last"] + (plan["timer"] if plan["section"] == "all-range" else FOX_REFILL_TAIL)
        host, client, traces = run_return(args, folder / name, plan, fox_refill_pokes(plan), level=plan["level"])
        frames = compare(host, client, plan["frames"])
        lines = [fox_refill_check(game, traces[game.name], plan, plan["section"]) for game in (host, client)]
        expect(lines[0] == lines[1], f"{name}: the games saw different refills: host {lines[0]}, client {lines[1]}")
        done.append(f"{name}: same hash on {frames} frames; {lines[0]}")
    return " | ".join(done)


# Spike #183 (decision 0007 rule 13), with SF64_COOP_TEST_LEAD_SWAP on both games: at pool 0 player 0 is poked down
# while player 1 flies, so player 1's ship takes slot 0 (`swap`). A life comes in and the host's ship returns in slot 1
# (`life`). Slot 0 is poked down again at pool 0: the lead goes back (`back`). The last poke finds nobody else flying,
# so slot 0 goes down for good (`last`).
FOX_LEAD_SHIELDS = 135  # poked into player 1 first, so the ship in slot 0 after the swap is known by them
FOX_LEAD_BOMBS = 5
# radioDamageTimer of the last poke: it counts down in every state and the down is only tested while ACTIVE, so in
# all-range it has to outlast a U-turn (about 110 frames) that the poke can fall into.
FOX_LEAD_LAST_TIMER = 150
FOX_LEAD_PLANS = {
    "rails": {"pool": JOIN_FRAME + 50, "swap": JOIN_FRAME + 100, "life": JOIN_FRAME + 400, "back": JOIN_FRAME + 600,
              "last": JOIN_FRAME + 800, "jump": None},
    "all-range": {"pool": JOIN_FRAME + 650, "swap": JOIN_FRAME + 700, "life": JOIN_FRAME + 1000,
                  "back": JOIN_FRAME + 1200, "last": JOIN_FRAME + 1400, "jump": JOIN_FRAME + 100},
}


def fox_lead_level_plan(name):
    """The swap on another level: only where player 0 flies the rails, where a poke is seen on the next frame."""
    level, section, join = FOX_REFILL_LEVELS[name]
    expect(section == "rails", f"{name}: the lead swap plan is for the rails")
    # sway is silent before JOIN_FRAME, Corneria's; here it starts when both fly in this level.
    return {"level": level, "pool": join + 20, "swap": join + 160, "life": join + 360, "back": join + 560,
            "last": join + 760, "jump": None, "script": lambda frame, player: sway(frame + JOIN_FRAME - join, player)}


def fox_lead_pokes(plan):
    pool = plan["pool"]
    return ",".join([f"{pool}:0:lives:0", f"{pool}:0:bombs:{FOX_BOMBS}", f"{pool}:1:bombs:{FOX_LEAD_BOMBS}",
                     f"{plan['swap'] - 1}:1:shields:{FOX_LEAD_SHIELDS}", f"{plan['swap'] - 1}:0:shields:200",
                     f"{plan['swap']}:0:down:4", f"{plan['life']}:0:lives:1", f"{plan['back'] - 1}:0:shields:200",
                     f"{plan['back'] - 1}:1:shields:200", f"{plan['back']}:0:down:4",
                     f"{plan['last']}:0:down:{FOX_LEAD_LAST_TIMER}"])


def fox_lead_check(game, view_path, plan, section):
    """What one game shows of a fox_lead plan. Returns a line for the summary (the same on both games)."""
    rows = [r for r in player_trace.parse(game.run / "trace.txt")
            if r.level == plan.get("level", 0) and len(r.players) > 1]
    by_frame = {r.frame: r for r in rows}
    view = view_check.read_trace(1, view_path).by_frame
    seat = 0 if game.name == "host" else 1
    frames = [r.frame for r in rows]
    expect(frames == sorted(set(frames)), f"{game.name}: the level restarted (the trace's frames go back)")

    def active_from(frame):
        later = [r.frame for r in rows if r.frame >= frame and r.players[0].state == player_trace.ACTIVE]
        return later[0] if later else None

    def local(frame):
        return view[frame].local

    # The swap: slot 0 holds player 1's ship, slot 1 is down, each game looks through the slot its person flies.
    at = active_from(plan["swap"] + 1)
    expect(at is not None and at in view, f"{game.name}: player 0 not ACTIVE after the poke at {plan['swap']}")
    before, after = view[at - 1].players, view[at].players
    expect(local(at - 1) == seat, f"{game.name}: local index {local(at - 1)} before the swap, want {seat}")
    expect(after[0].state == 3 and not after[0].parked and after[1].parked,
           f"{game.name}: at frame {at} slot 0 state {after[0].state}, slot 1 parked {after[1].parked}: no lead swap")
    # Solar's heat takes a point from slot 0 on the frame of the swap (Play_UpdateLevel, after the players).
    expect(0 <= before[1].shields - after[0].shields <= 1 and after[0].bombs == FOX_LEAD_BOMBS
           and after[1].bombs == FOX_BOMBS,
           f"{game.name}: after the swap slot 0 has shields {after[0].shields} (player 1 had {before[1].shields}), "
           f"bombs {after[0].bombs}/{after[1].bombs}")
    expect(after[0].lives == 0, f"{game.name}: pool {after[0].lives} after the swap, want 0")
    expect(local(at) == 1 - seat, f"{game.name}: local index {local(at)} after the swap, want {1 - seat}")
    expect(view[at].view == 0, f"{game.name}: looks through {view[at].view} after the swap, want slot 0")
    p1b, p0a = by_frame[at - 1].players[1], by_frame[at].players[0]
    expect(abs(p0a.x - p1b.x) < 80 and abs(p0a.y - p1b.y) < 80,
           f"{game.name}: slot 0 at {p0a.x, p0a.y} after the swap, player 1 was at {p1b.x, p1b.y}")
    found = [f"lead swap at {at}: slot 0 flies player 1's ship ({after[0].shields} shields), slot 1 down"]
    if section == "rails":
        later = by_frame[at + 60]
        expect(later.pathvel > 0 and later.path > by_frame[at].path,
               f"{game.name}: the path stopped at the swap: {by_frame[at].path} -> {later.path}")
        # Who steers slot 0: the two feeds steer against each other (sway), so slot 0's sideways movement goes with
        # player 0's stick before the swap and against it after.

        steer_of = plan.get("script", sway)

        def follows(first, last):
            total = 0.0
            for f in range(first, last):
                if f in by_frame and f + 1 in by_frame:
                    total += (by_frame[f + 1].players[0].x - by_frame[f].players[0].x) * steer_of(f, 0)[1]
            return total

        own, other = follows(at - 90, at - 1), follows(at + 2, at + 92)
        expect(own * other < 0, f"{game.name}: slot 0's steering did not change hands at the swap ({own:.0f} before, "
                                f"{other:.0f} after)")
        found.append("slot 0 follows the client's stick from then on, the path goes on")

    # A life comes in: the host's ship returns in slot 1, for that life.
    back = next((r.frame for r in rows if r.frame > at and not r.players[1].parked), None)
    due = active_from(max(plan["life"] + 1, at + RETURN_FRAMES))
    expect(back is not None and back == due, f"{game.name}: slot 1 back at {back}, want {due}")
    expect(view[back].players[1].shields >= RETURN_SHIELDS - 1 and view[back].players[0].lives == 0,  # Solar: heat
           f"{game.name}: slot 1 back with {view[back].players[1].shields} shields, pool {view[back].players[0].lives}")
    expect(local(back) == 1 - seat, f"{game.name}: local index {local(back)} after the return")
    found.append(f"the host's ship returned in slot 1 at {back} for the life")

    # The lead goes back at the second poke.
    again = active_from(plan["back"] + 1)
    expect(again is not None and view[again].players[1].parked and view[again].players[0].state == 3
           and local(again) == seat,
           f"{game.name}: at frame {again} slot 1 parked {view[again].players[1].parked}, slot 0 state "
           f"{view[again].players[0].state}, local index {local(again)}; want a second swap")
    found.append(f"swapped back at {again}")

    # Nobody else flies: slot 0 goes down as in the original.
    end = next((r.frame for r in rows if r.frame > plan["last"] and r.players[0].state == PLAYERSTATE_DOWN), None)
    expect(end is not None, f"{game.name}: slot 0 did not go down after the last poke at {plan['last']}; want DOWN, "
                            "nobody left to take the lead")
    row = by_frame[end]
    expect(row.players[0].state == PLAYERSTATE_DOWN and row.players[1].parked,
           f"{game.name}: at frame {end} slot 0 state {row.players[0].state}; want DOWN, nobody left to take the lead")
    span = [r for r in rows if plan["swap"] <= r.frame < end]
    bad = [(r.frame, r.players[0].state) for r in span if r.players[0].state not in player_trace.FLYING]
    expect(not bad, f"{game.name}: slot 0 left its flight at {bad[:1]}")
    expect(end < plan["frames"], f"{game.name}: the last down at {end} is past the {plan['frames']} frames compared")
    found.append(f"down for good at {end}")
    return "; ".join(found)


def fox_lead_in(args, folder, section):
    plan = dict(FOX_LEAD_PLANS[section])
    # In all-range the last down can wait for a U-turn to end, so the run goes on until past it.
    plan["frames"] = plan["last"] + (FOX_LEAD_LAST_TIMER if section == "all-range" else FOX_REFILL_TAIL)
    host, client, traces = run_return(args, folder, plan, fox_lead_pokes(plan), both={"SF64_COOP_TEST_LEAD_SWAP": "1"})
    frames = compare(host, client, plan["frames"])
    lines = [fox_lead_check(game, traces[game.name], plan, section) for game in (host, client)]
    expect(lines[0] == lines[1], f"the games saw different swaps: host {lines[0]}, client {lines[1]}")
    return f"same hash on {frames} frames; {lines[0]}"


def sc_fox_lead_swap(args, folder):
    """Spike #183: the lead role passes to a survivor at pool 0, on Corneria's rails. An experiment, not a rule. With
    --levels, the same on the rails of those levels instead (FOX_REFILL_LEVELS)."""
    if not args.levels:
        return fox_lead_in(args, folder, "rails")
    done = []
    for name in args.levels:
        expect(name in FOX_REFILL_LEVELS, f"no level {name}: {', '.join(FOX_REFILL_LEVELS)}")
        plan = fox_lead_level_plan(name)
        plan["frames"] = plan["last"] + FOX_REFILL_TAIL
        host, client, traces = run_return(args, folder / name, plan, fox_lead_pokes(plan),
                                          both={"SF64_COOP_TEST_LEAD_SWAP": "1"}, level=plan["level"],
                                          script=plan["script"])
        frames = compare(host, client, plan["frames"])
        lines = [fox_lead_check(game, traces[game.name], plan, "rails") for game in (host, client)]
        expect(lines[0] == lines[1], f"{name}: the games saw different swaps: host {lines[0]}, client {lines[1]}")
        done.append(f"{name}: same hash on {frames} frames; {lines[0]}")
    return " | ".join(done)


def sc_fox_lead_swap_all_range(args, folder):
    return fox_lead_in(args, folder, "all-range")


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


# How much nearer the human a Star Wolf did not pick may be at the pick before it counts as a wrong pick (#266). The trace
# line is written after the frame's moves, so who is nearer can change between the pick and the line: in the dogfight
# trace a Star Wolf moves up to 79 units a frame and a ship up to 47, and the difference of two distances then moves by
# at most 2 * 79 + 47 + 47 = 252.
PICK_MARGIN = 250.0


def dogfight_picks(trace):
    """The picks of the Star Wolf members that chase Fox, from the boss trace of `dogfight`: each frame on which a member's
    kept slot (the last number of its aN field) goes from empty (-1) to player 0 or 1 while both can be picked. Returns
    [(frame, member, human, (distance to player 0, distance to player 1))]. #213 keeps a pick until the AI's next choice,
    so the picks are where the rule shows. How many frames a member holds a player does not: it depends on which members
    chase Fox at all (Leon, Pigma and Andrew chase a wingman first) and on where the fight has got to by then."""
    picks, last = [], {}
    for row in trace:
        members = {key: row[key].split(",") for key in row if re.fullmatch(r"a\d+", key)}
        players = [row.get(f"p{i}", "").split(",") for i in (0, 1)]
        # Who a Star Wolf may pick (CoopAim_CanAimAt): player 0 always, player 1 when it is not parked and in state 3
        # or 5 (PLAYERSTATE_ACTIVE, PLAYERSTATE_U_TURN). Only then is "the nearer of the two" what it chose from.
        both = len(players[0]) >= 5 and len(players[1]) >= 5 and players[1][1] == "0" and players[1][0] in ("3", "5")
        for key, fields in members.items():
            if both and last.get(key) == "-1" and fields[8] in ("0", "1"):
                where = [float(v) for v in fields[2:5]]
                distances = tuple(math.dist(where, [float(v) for v in p[2:5]]) for p in players)
                picks.append((row["frame"], key, int(fields[8]), distances))
        last = {key: fields[8] for key, fields in members.items()}
    return picks


def check_dogfight_picks(trace):
    """Every Star Wolf pick went to the nearer human (within PICK_MARGIN), and player 1 was picked at least once (a
    Star Wolf that only ever goes after player 0 never picks it). Returns a line."""
    picks = dogfight_picks(trace)
    expect(picks, "dogfight: no pick of a Star Wolf in the trace (a kept slot going from -1 to a player)")
    for frame, key, human, distances in picks:
        expect(distances[human] <= distances[1 - human] + PICK_MARGIN,
               f"dogfight: {key} kept player {human} at frame {frame}, the farther one: {distances[0]:.0f} from player 0, "
               f"{distances[1]:.0f} from player 1")
    on1 = sum(1 for pick in picks if pick[2] == 1)
    expect(on1, f"dogfight: no Star Wolf picked player 1 in {len(picks)} picks")
    return f"{len(picks)} picks, all to the nearer human, {on1} of them player 1"


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
        if name == "dogfight":
            # Not the frames each human was kept on (#266): that moved from 3528 and 2573 (#213) to 1819 and 2574
            # with every pick right, and it counts only the members that chase Fox on that frame (NOTES.md, #266). The
            # rule is in the picks.
            lines.append(f"{name}: same hash on {frames} frames; {text}; {check_dogfight_picks(trace)}; kept player 1 on "
                         f"{kept[1]} frames, player 0 on {kept[0]}")
            continue
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
    # Campaign run, 2026-11-11 (#220's leftovers): Sector X's Spyborg and Solar's Vulkain.
    "spyborg": (2, 13000, rails_script, {}, {}),
    "vulkain": (7, 12000, rails_script, {}, {}),
    # Titania: Goras waits in state 3 until it is shot, so pilots on both clients drive the Landmasters (the feed is
    # not played then) and bring it into its fight, states 7 to 12, from about 7800 frames in. Since the pilot reads
    # Goras' boxes right (#262, CoopPilot_GorasTarget) it beats Goras and the level ends at about frame 11760, so the run
    # stops before that: the map's frames hold no Goras for the trace the check counts.
    "goras": (12, 11000, rails_script, {}, {"SF64_COOP_LOCKSTEP_PILOT": "1"}),
    # Macbeth's train end (wave 7, #220): pilots on both clients (the feed is not played then) drive the Landmasters to
    # the fight, from about level frame 6400 on, and there keep apart (SF64_COOP_PILOT_LANES): player 0 on the right of
    # the path, player 1 on the left, the side of the train over which the train's end hovers between its attacks. They
    # do not aim at it, so it is not shot down and the run ends before the track does (the ram of states 18 and 19).
    "macbeth": (11, 12000, rails_script, {}, {"SF64_COOP_LOCKSTEP_PILOT": "1", "SF64_COOP_PILOT_LOG": "pilot-log.txt",
                                             "SF64_COOP_PILOT_LANES": "60,-60"}),
}
OBJ_BOSS_SX_SPYBORG, OBJ_BOSS_TI_GORAS, OBJ_BOSS_SO_VULKAIN = 303, 306, 315  # include/sf64object.h
SPYBORG_CHASES = (1, 2, 3, 4, 8, 9, 85)  # SectorX_SxSpyborg_Update's states that move it to its target's x and y
GORAS_CHASES = range(7, 13)  # Titania_80193DF0's states that move Goras to its target's x
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


def other_boss_chase_story(trace, boss_id, states, axes):
    """A boss in a state that moves it to its target across the path (`states`; `axes` 1 for x, 2 for x and y): the
    frames it is nearer player 1 than player 0 on those axes, the other way round, and its kept slot per value."""
    near = [0, 0]
    kept = {}
    for row in trace:
        boss = next((row[k].split(",") for k in row if re.fullmatch(r"b\d+", k) and
                     row[k].startswith(f"{boss_id},")), None)
        if boss is None or "p1" not in row or int(boss[1]) not in states:
            continue
        pos = [float(v) for v in boss[2:2 + axes]]
        players = [[float(v) for v in row[f"p{i}"].split(",")[2:2 + axes]] for i in (0, 1)]
        dist = [math.dist(pos, p) for p in players]
        near[0 if dist[0] < dist[1] else 1] += 1
        kept[boss[7]] = kept.get(boss[7], 0) + 1
    return near, kept


def other_boss_kept_story(trace, boss_id, states, apart=300.0):
    """A boss in a state that moves it to its target's x (`states`), on frames the two players are more than `apart`
    from each other across the path: per kept slot, the frames it is nearer the player it keeps and the frames it is
    nearer the other one ({kept: [nearer kept, nearer other]}; an empty slot, -1, counts against player 0, upstream's
    target)."""
    follow = {}
    for row in trace:
        boss = next((row[k].split(",") for k in row if re.fullmatch(r"b\d+", k) and
                     row[k].startswith(f"{boss_id},")), None)
        if boss is None or "p1" not in row or int(boss[1]) not in states:
            continue
        xs = [float(row[f"p{i}"].split(",")[2]) for i in (0, 1)]
        if abs(xs[0] - xs[1]) <= apart:
            continue
        kept = int(boss[7])
        target = kept if kept in (0, 1) else 0
        mine, other = abs(float(boss[2]) - xs[target]), abs(float(boss[2]) - xs[1 - target])
        follow.setdefault(kept, [0, 0])[0 if mine < other else 1] += 1
    return follow


def other_boss_vulkain_story(trace):
    """Frames on which Solar's Vulkain turns its head (yaw from its body, clamped by the game to 40 either way) within
    FACING / 2 of player 1 and not within NOT_FACING / 2 of player 0, and the other way round, while the head moves."""
    def turn(a):
        return (a + 180.0) % 360.0 - 180.0

    face = [0, 0]
    frames = 0
    last = None
    for row in trace:
        boss = next((row[k].split(",") for k in row if re.fullmatch(r"b\d+", k) and
                     row[k].startswith(f"{OBJ_BOSS_SO_VULKAIN},")), None)
        if boss is None or "so" not in row or "p1" not in row:
            last = None
            continue
        yaw = float(row["so"].split(",")[0])
        if last is None or abs(turn(yaw - last)) < 0.01 and abs(turn(yaw)) < 0.01:
            last = yaw
            continue  # the head at rest
        last = yaw
        frames += 1
        pos = (float(boss[2]), float(boss[4]))
        body = float(boss[6])
        players = [[float(v) for v in row[f"p{i}"].split(",")[2:5]] for i in (0, 1)]
        off = [abs(turn(body + yaw - math.degrees(math.atan2(p[0] - pos[0], p[2] - pos[1])))) for p in players]
        for i in (0, 1):
            face[i] += off[i] < FACING / 2 and off[1 - i] > NOT_FACING / 2
    return face, frames


# Macbeth's train end (wave 7, #220): actor 207 (Macbeth_Actor207_Update, src/overlays/ovl_i5/fox_ma.c), the end of the
# train in the Landmaster fight at the end of Macbeth, with a kept slot of its own (the `ma=` field of the boss trace).
W7MACBETH_GOES = (5, 7, 15)  # its states that move it across the path to its target's x
W7MACBETH_FACES = (4, 5, 6)  # its states in which Macbeth_Actor207_FacePlayer turns it to its target on every frame
W7MACBETH_APART = 300.0  # the players more than this apart across the path, or nobody is the nearer by much


def w7macbeth_story(trace):
    """Macbeth's train end, from the boss trace. Each time it enters one of W7MACBETH_GOES or W7MACBETH_FACES, the human
    then nearer to it (as CoopAim_Nearest measures: x, y and trueZpos, on the row before) is the one it should go after
    in that state. Per that nearer player: the frames of W7MACBETH_GOES on which its x is nearer that player's than the
    other's and the other way round (`goes`), and the frames of W7MACBETH_FACES on which it faces (rot y within FACING)
    that player and not the other (within NOT_FACING) and the other way round (`faces`); only frames with the players
    more than W7MACBETH_APART apart across the path. Also its kept slot per value, over those states."""
    def turn(a):
        return (a + 180.0) % 360.0 - 180.0

    goes = {0: [0, 0], 1: [0, 0]}
    faces = {0: [0, 0], 1: [0, 0]}
    picks = {0: 0, 1: 0}
    kept = {}
    state = None
    pick = None
    last = None
    for row in trace:
        m = row.get("ma", "").split(",")
        if "p1" not in row or len(m) < 8:
            state = None
            last = None
            continue
        now = int(m[1])
        pos = [float(v) for v in m[2:5]]
        players = [[float(v) for v in row[f"p{i}"].split(",")[2:5]] for i in (0, 1)]
        if now != state and now in W7MACBETH_GOES + W7MACBETH_FACES:
            at = last if last is not None else (pos, players)
            dist = [math.dist(at[0], p) for p in at[1]]
            pick = 1 if dist[1] < dist[0] else 0
            picks[pick] += 1
        state = now
        last = (pos, players)
        if now not in W7MACBETH_GOES + W7MACBETH_FACES:
            continue
        kept[m[7]] = kept.get(m[7], 0) + 1
        if abs(players[0][0] - players[1][0]) <= W7MACBETH_APART:
            continue
        if now in W7MACBETH_GOES:
            mine, other = abs(pos[0] - players[pick][0]), abs(pos[0] - players[1 - pick][0])
            goes[pick][0 if mine < other else 1] += 1
        if now in W7MACBETH_FACES:
            yaw = float(m[6])
            off = [abs(turn(yaw - math.degrees(math.atan2(p[0] - pos[0], p[2] - 130.0 - pos[2])))) for p in players]
            if off[pick] < FACING and off[1 - pick] > NOT_FACING:
                faces[pick][0] += 1
            elif off[1 - pick] < FACING and off[pick] > NOT_FACING:
                faces[pick][1] += 1
    return goes, faces, picks, kept


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
        if name == "spyborg":
            near, kept = other_boss_chase_story(trace, OBJ_BOSS_SX_SPYBORG, SPYBORG_CHASES, 2)
            text = (f"{sum(near)} frames it goes after its target, nearer player 1 on {near[1]}, player 0 on {near[0]}; "
                    f"kept {', '.join(f'{k}: {v}' for k, v in sorted(kept.items()))}")
            expect(sum(near) >= 300, f"{name}: too few frames of the chase to tell: {text}")
            expect(near[1] > 2 * near[0], f"{name}: the boss goes after player 0: {text}")
            lines.append(f"{name}: same hash on {frames} frames; {text}")
        if name == "goras":
            # Two pilots: either player can be the nearer one, so the check reads whom Goras keeps.
            follow = other_boss_kept_story(trace, OBJ_BOSS_TI_GORAS, GORAS_CHASES)
            text = (f"chase frames with the players apart, per kept slot (nearer the kept player, nearer the other): "
                    f"{', '.join(f'{k}: {v[0]}, {v[1]}' for k, v in sorted(follow.items())) or 'none'}")
            mine = sum(v[0] for v in follow.values())
            other = sum(v[1] for v in follow.values())
            expect(mine + other >= 300, f"goras: too few frames of the chase to tell: {text}")
            expect(sum(follow.get(1, [0, 0])) >= 100, f"goras: Goras seldom keeps player 1: {text}")
            expect(mine > 2 * other, f"goras: Goras does not go after the player it keeps: {text}")
            lines.append(f"goras: same hash on {frames} frames; {text}")
        if name == "vulkain":
            face, seen = other_boss_vulkain_story(trace)
            text = f"{seen} frames its head moves, at player 1 on {face[1]}, at player 0 on {face[0]}"
            expect(sum(face) >= 30, f"vulkain: too few frames of the Vulkain's head to tell: {text}")
            expect(face[1] > 2 * face[0], f"vulkain: the Vulkain turns its head to player 0: {text}")
            lines.append(f"vulkain: same hash on {frames} frames; {text}")
        if name == "macbeth":
            goes, faces, picks, kept = w7macbeth_story(trace)
            text = (f"states picked with player 1 the nearer {picks[1]}, player 0 {picks[0]}; going across the path "
                    f"with player 1 the nearer it goes to player 1 on {goes[1][0]}, to player 0 on {goes[1][1]}, with "
                    f"player 0 the nearer to player 0 on {goes[0][0]}, to player 1 on {goes[0][1]}; facing, with player 1 "
                    f"the nearer, player 1 on {faces[1][0]}, player 0 on {faces[1][1]}, with player 0 the nearer, player "
                    f"0 on {faces[0][0]}, player 1 on {faces[0][1]}; kept "
                    f"{', '.join(f'{k}: {v}' for k, v in sorted(kept.items()))}")
            # Player 1 drives the train's side of the path, so it is mostly the nearer one when a state starts. What
            # counts is that the train's end goes after whoever is the nearer then: player 1 there, and player 0 when
            # player 0 is (seldom here, so that half is not asked to be large).
            expect(sum(goes[1]) >= 100 and sum(faces[1]) >= 50,
                   f"macbeth: too few frames with player 1 the nearer to tell: {text}")
            wrong = [what for what, v in (("goes to", goes[1]), ("faces", faces[1])) if not v[0] > 2 * v[1]]
            expect(not wrong, f"macbeth: the train's end {' and '.join(wrong)} player 0 while player 1 is nearer: {text}")
            expect(goes[0][0] + faces[0][0] >= 2 * (goes[0][1] + faces[0][1]),
                   f"macbeth: the train's end goes after player 1 while player 0 is nearer: {text}")
            lines.append(f"macbeth: same hash on {frames} frames; {text}")
    return "; ".join(lines)


# ---- sy_robot_spread (#273) ----
# Some of Sector Y's robots spread their shot (SectorY_SyRobot_Update, iwork[19] != 0): the point they aim at is moved
# by RAND_FLOAT_CENTERED(300.0f) in x and in y, (Rand_ZeroOne() - 0.5f) * 300 (include/macros.h), and Rand_ZeroOne is
# in [0, 1) (src/sys/sys_math.c): each axis by -150 up to just under 150, uniform, mean 0, root mean square 86.6.
W8_SYROBOT_LEVEL = 5  # LEVEL_SECTOR_Y, campaign.py's "sector-y"
W8_SYROBOT_STEER = 650  # both players fly from level frame 611 in Sector Y with this file's seeds (FOX_REFILL_LEVELS)
W8_SYROBOT_FRAMES = 9500  # robot 89 (level script 89, the last that spreads) fires at about level frame 8500
W8_SYROBOT_BOUND = 150.0
W8_SYROBOT_TOLERANCE = 2.0  # how far a shot aimed straight at a player may miss it here: the trace's printed digits
W8_SYROBOT_MIN_AT_1 = 6  # spread shots with player 1 the nearer, fewer is too few to tell
W8_SYROBOT_LINE = re.compile(r"syrobot level=(\d+) frame=(\d+) slot=(\d+) script=(\d+) spread=(-?\d+) start=(\S+) "
                             r"dir=(\S+)((?: p\d=\S+)+)$")


def w8_syrobot_script(frame, player):
    """Sector Y's rails: from W8_SYROBOT_STEER player 0 holds the top left; player 1 never steers and flies where it
    joined, near the middle of the path (250, 80). The robots that spread their shots fire from the middle (level
    scripts 66, 22 and 89), so player 1 is the nearer one for most of their shots."""
    if frame < W8_SYROBOT_STEER or player != 0:
        return 0, 0, 0
    return 0, -70, -60


def w8_syrobot_rows(path):
    """The syrobot lines of an aim trace (src/port/coop/test/aim_trace.h): one dict per robot shot."""
    rows = []
    if not path.exists():
        return rows
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = W8_SYROBOT_LINE.match(line)
        if not match:
            continue
        players = {}
        for part in match.group(8).split():
            key, value = part.split("=")
            values = value.split(",")
            players[int(key[1:])] = ([float(v) for v in values[:3]], values[3] == "1")
        rows.append({"level": int(match.group(1)), "frame": int(match.group(2)), "script": int(match.group(4)),
                     "spread": int(match.group(5)), "start": [float(v) for v in match.group(6).split(",")],
                     "dir": [float(v) for v in match.group(7).split(",")], "players": players, "line": line})
    return rows


def w8_syrobot_nearest(row):
    """The player the shot aims at by the rule of src/port/coop/game/aim.c: of the players that may be aimed at, the
    nearest to the shot's start, a tie to the lower index."""
    best, best_dist = 0, None
    for i in sorted(row["players"]):
        pos, flies = row["players"][i]
        if not flies:
            continue
        dist = sum((a - b) ** 2 for a, b in zip(pos, row["start"]))
        if best_dist is None or dist < best_dist:
            best, best_dist = i, dist
    return best


def w8_syrobot_miss(row, player):
    """Where the shot crosses the depth (trueZpos) of `player`, less that player's x and y: the offset of the point the
    robot aimed at from that player. None for a shot that does not fly along z."""
    (px, py, pz), _ = row["players"][player]
    sx, sy, sz = row["start"]
    dx, dy, dz = row["dir"]
    if abs(dz) < 1e-6:
        return None
    t = (pz - sz) / dz
    return sx + t * dx - px, sy + t * dy - py


def w8_syrobot_spread_text(misses):
    if not misses:
        return "none"
    xs, ys = [m[0] for m in misses], [m[1] for m in misses]
    rms = math.sqrt(sum(v * v for v in xs + ys) / (2 * len(misses)))
    return (f"x {min(xs):.0f} to {max(xs):.0f} (mean {sum(xs) / len(xs):.0f}), y {min(ys):.0f} to {max(ys):.0f} "
            f"(mean {sum(ys) / len(ys):.0f}), root mean square {rms:.0f}")


def sc_sy_robot_spread(args, folder):
    """#273: Sector Y's robots spread a shot around the human it aims at. On the rails player 0 holds the top left and
    player 1 flies in the middle, near the robots that spread. Each robot shot is read from the aim trace's syrobot
    lines: a plain one flies at the nearest human, a spread one at a point at most W8_SYROBOT_BOUND from it in x and y,
    and the spread shots at player 1 are spread: none straight at it, root mean square at least a fifth of the bound."""
    folder.mkdir(parents=True, exist_ok=True)
    feed = folder / "feed.txt"
    write_feed(feed, W8_SYROBOT_FRAMES + 16, w8_syrobot_script)
    traces = {game: folder / f"{game}-aim.txt" for game in ("host", "client")}
    for path in traces.values():
        if path.exists():
            path.unlink()  # the trace appends
    env = {"SF64_COOP_LOCKSTEP_FEED": str(feed), "SF64_COOP_LOCKSTEP_END": str(W8_SYROBOT_FRAMES)}
    host, client = session(args, folder, {"SF64_COOP_JOIN_LEVEL": str(W8_SYROBOT_LEVEL),
                                          "SF64_COOP_AIM_TRACE": str(traces["host"])},
                           {"SF64_COOP_AIM_TRACE": str(traces["client"])}, env, config={"CVars": {"gInvincible": 1}})
    try:
        wait_for([host, client], "done", 1200)
    finally:
        host.close()
        client.close()
    frames = compare(host, client, W8_SYROBOT_FRAMES)
    ours, theirs = w8_syrobot_rows(traces["host"]), w8_syrobot_rows(traces["client"])
    # A game runs on a little past `done`: the lines both traces hold are compared, the checks read the plan's frames.
    n = min(len(ours), len(theirs))
    for a, b in zip(ours[:n], theirs[:n]):
        expect(a["line"] == b["line"], f"the robot shot lines differ: host `{a['line']}`, client `{b['line']}`")
    rows = [row for row in ours[:n] if row["level"] == W8_SYROBOT_LEVEL and row["frame"] <= W8_SYROBOT_FRAMES]
    expect(rows, "no robot shot in the trace (a build without the syrobot line of aim_trace.h writes none)")
    problems, text = w8_syrobot_story(rows)
    expect(not problems, f"{'; '.join(problems)}. {text}")
    return f"same hash on {frames} frames; {text}"


def w8_syrobot_story(rows):
    """The checks of sy_robot_spread on its robot shots: (the problems found, the summary)."""
    plain = [row for row in rows if row["spread"] == 0]
    spread = [row for row in rows if row["spread"] != 0]
    worst = 0.0
    wide = []  # plain shots that miss the nearest human
    for row in plain:
        miss = w8_syrobot_miss(row, w8_syrobot_nearest(row))
        if miss is not None:
            worst = max(worst, abs(miss[0]), abs(miss[1]))
            if max(abs(miss[0]), abs(miss[1])) > W8_SYROBOT_TOLERANCE:
                wide.append(f"frame {row['frame']} misses player {w8_syrobot_nearest(row)} by {miss[0]:.1f}, "
                            f"{miss[1]:.1f}")
    at = {0: [], 1: []}
    off = []  # spread shots that are not around the nearest human
    for row in spread:
        aimed = w8_syrobot_nearest(row)
        miss = w8_syrobot_miss(row, aimed)
        if miss is None:
            continue
        if max(abs(miss[0]), abs(miss[1])) > W8_SYROBOT_BOUND + W8_SYROBOT_TOLERANCE:
            off.append(f"frame {row['frame']} misses player {aimed} by {miss[0]:.0f}, {miss[1]:.0f}")
            continue
        at.setdefault(aimed, []).append(miss)
    by_command = {value: sum(1 for row in spread if row["spread"] == value)
                  for value in sorted({row["spread"] for row in spread})}
    text = (f"{len(rows)} robot shots: {len(plain)} plain, all within {worst:.2f} of the nearest human; {len(spread)} "
            f"spread (iwork[19] {', '.join(f'{k}: {v}' for k, v in by_command.items()) or 'none'}), {len(at[1])} "
            f"around player 1, the nearer: {w8_syrobot_spread_text(at[1])}; {len(at[0])} around player 0, the nearer: "
            f"{w8_syrobot_spread_text(at[0])}")
    problems = []
    if wide:
        problems.append(f"{len(wide)} plain shots miss the nearest human ({'; '.join(wide)})")
    if off:
        problems.append(f"{len(off)} of {len(spread)} spread shots are not around the nearest human, more than "
                        f"{W8_SYROBOT_BOUND:.0f} off in x or y ({'; '.join(off)})")
    if len(at[1]) < W8_SYROBOT_MIN_AT_1:
        problems.append(f"too few spread shots around player 1 to tell, {len(at[1])}")
    straight = sum(1 for m in at[1] if abs(m[0]) <= W8_SYROBOT_TOLERANCE and abs(m[1]) <= W8_SYROBOT_TOLERANCE)
    if straight:
        problems.append(f"{straight} of the {len(at[1])} spread shots with player 1 the nearer fly straight at it")
    if at[1] and math.sqrt(sum(m[0] ** 2 + m[1] ** 2 for m in at[1]) / (2 * len(at[1]))) < W8_SYROBOT_BOUND / 5:
        problems.append("the spread around player 1 is too small")
    every = at[0] + at[1]
    if every and any(abs(sum(m[axis] for m in every) / len(every)) >= W8_SYROBOT_BOUND / 2 for axis in (0, 1)):
        problems.append("the spread is not centred on the human aimed at")
    return problems, text


# Macbeth's ram at the end of the track (wave 7, #220; the owner's choice of 2026-11-12): once the track's stage reaches 4
# the train's end rams player 0 (Macbeth_Actor207_Update, states 18 and 19) and zeroes its shields at every touch. That
# ends the try as upstream: player 0 goes down, past decision 0007's refill (CoopPlayers_RefillFox). The run: pilots on
# both clients, in lanes apart once the fight is on, without gInvincible (it keeps a hit from taking a tank down), a pool of
# W7MACBETH_RAM_POOL lives so that both reach the fight, and SF64_COOP_MA_TRACK_END, which gives the track's stage 4 the
# first time the train's end is in state 4, so it rams at once.
W7MACBETH_RAM_FRAMES = 8000  # the fight starts at about level frame 6400; the ram and player 0's down come within 400
W7MACBETH_RAM_POOL = 90
W7MACBETH_RAM_WAIT = 600  # frames from the ram's start (state 19) by which player 0 has to be down


def w7macbeth_ram_story(boss_rows, view):
    """From the host's boss trace and view trace: the frame the train's end starts its ram (state 19), the frames on which
    player 0 was refilled from then on (a life gone from the pool while it stays ACTIVE and player 1 does not come back),
    the first frame it is DOWN, and the pool when the ram starts. Only up to the level's start again (frames go back)."""
    start = next((int(r["frame"]) for r in boss_rows if r.get("ma", "").split(",")[1:2] == ["19"]), None)
    if start is None:
        return None, [], None, None
    rows = []
    for row in view.rows:
        if row.frame < start:
            continue
        if rows and row.frame <= rows[-1].frame:
            break  # the level started again
        rows.append(row)
    refills = []
    for before, now in zip(rows, rows[1:]):
        p, q = before.players[0], now.players[0]
        came_back = len(now.players) > 1 and before.players[1].parked and not now.players[1].parked
        if q.lives == p.lives - 1 and q.state == player_trace.ACTIVE and not came_back:
            refills.append(now.frame)
    down = next((r.frame for r in rows if r.players[0].state == PLAYERSTATE_DOWN), None)
    return start, refills, down, (rows[0].players[0].lives if rows else None)


def sc_w7macbeth_ram(args, folder):
    """#220, the owner's choice of 2026-11-12: Macbeth's train end rams player 0 at the end of the track, and that ends the
    try as upstream: player 0 goes down at the ram, once, and is not refilled under it (decision 0007's refill would buy
    only the next ram, about every 31 frames, until the pool is gone). Hashes equal on both games."""
    folder.mkdir(parents=True, exist_ok=True)
    frames = W7MACBETH_RAM_FRAMES
    traces = {name: folder / f"{name}-boss.txt" for name in ("host", "client")}
    views = {name: folder / f"{name}-view.txt" for name in ("host", "client")}
    for path in [*traces.values(), *views.values()]:
        if path.exists():
            path.unlink()  # the traces append
    both = {"SF64_COOP_LOCKSTEP_PILOT": "1", "SF64_COOP_PILOT_LOG": "pilot-log.txt", "SF64_COOP_PILOT_LANES": "60,-60",
            "SF64_COOP_MA_TRACK_END": "1", "SF64_COOP_VIEW_POKE": f"100:0:lives:{W7MACBETH_RAM_POOL}",
            "SF64_COOP_LOCKSTEP_END": str(frames)}
    host, client = session(args, folder, {"SF64_COOP_JOIN_LEVEL": "11", "SF64_COOP_BOSS_TRACE": str(traces["host"]),
                                          "SF64_COOP_VIEW_TRACE": str(views["host"])},
                           {"SF64_COOP_BOSS_TRACE": str(traces["client"]), "SF64_COOP_VIEW_TRACE": str(views["client"])},
                           both, config={"CVars": {}})
    try:
        wait_for([host, client], "done", 900)
    finally:
        host.close()
        client.close()
    compared = compare(host, client, frames)
    stories = [w7macbeth_ram_story(boss_trace(traces[name]), view_check.read_trace(1, views[name]))
               for name in ("host", "client")]
    expect(stories[0] == stories[1], f"w7macbeth_ram: the games differ: host {stories[0]}, client {stories[1]}")
    start, refills, down, pool = stories[0]
    expect(start is not None, f"w7macbeth_ram: the train's end never started its ram (state 19) in {frames} frames")
    text = (f"the ram starts at frame {start} with {pool} lives in the pool; player 0 refilled under it "
            f"{len(refills)} times{f' (first {refills[0]}, last {refills[-1]})' if refills else ''}, down at "
            f"{down if down is not None else 'no frame'}")
    expect(not refills, f"w7macbeth_ram: player 0 is refilled under the ram, so it drains the pool: {text}")
    expect(down is not None and down - start <= W7MACBETH_RAM_WAIT,
           f"w7macbeth_ram: player 0 does not go down at the ram: {text}")
    return f"same hash on {compared} frames; {text}"


EELS_FRAMES = 9000  # Aquas from the join: the Bacoon lets its eels out from about level frame 5600 on
EEL_PUFF_SCALE = 3.0  # the smoke of an eel's piece (Aquas_Actor255_Update); nothing else in Aquas spawns this size here
EEL_REACH = 6000.0  # an eel swims between the Bacoon and the marines, about 3000 apart along the path
SMOKE_LINE = re.compile(r"smoke frame=(\d+) level=(\d+) slot=(\d+) pos=([-\d.]+),([-\d.]+),([-\d.]+) scale=([-\d.]+) "
                        r"path=([-\d.]+)")


def eels_script(frame, player):
    """Aquas: both marines stay where they join, near the middle of the path, and fire their lasers every other frame.
    The eels swim at player 0, into the fire."""
    if frame < JOIN_FRAME + 100:
        return 0, 0, 0
    return (BUTTON_A if frame % 2 == player else 0), 0, 0


def smoke_trace(path):
    """The smoke trace (src/port/coop/test/smoke_trace.h) as (frame, level, slot, x, y, z, scale, path) rows."""
    if not path.exists():
        return []
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        m = SMOKE_LINE.match(line)
        if m:
            rows.append((int(m[1]), int(m[2]), int(m[3])) + tuple(float(m[i]) for i in range(4, 9)))
    return rows


def sc_eels(args, folder):
    """The eels of Aquas' boss die where they are. An eel (actor 255) that is shot explodes piece by piece, each piece
    with a smoke puff at a place from the eel's history of positions. The port's iwork is unsigned, so for the eel of
    pod 0 the index into that history was not wrapped and the place was read from before the array: memory that a
    Windows and a Linux build lay out differently, which parted a real session (NOTES.md, 2026-11-11). Read from the
    smoke trace, so one build on one PC shows it: every eel puff lies in the fight."""
    folder.mkdir(parents=True, exist_ok=True)
    feed = folder / "feed.txt"
    write_feed(feed, EELS_FRAMES + 16, eels_script)
    traces = {game: folder / f"{game}-smoke.txt" for game in ("host", "client")}
    for path in traces.values():
        if path.exists():
            path.unlink()  # the trace appends
    env = {"SF64_COOP_LOCKSTEP_FEED": str(feed), "SF64_COOP_LOCKSTEP_END": str(EELS_FRAMES)}
    host, client = session(args, folder, {"SF64_COOP_JOIN_LEVEL": "13", "SF64_COOP_SMOKE_TRACE": str(traces["host"])},
                           {"SF64_COOP_SMOKE_TRACE": str(traces["client"])}, env, config={"CVars": {"gInvincible": 1}})
    try:
        wait_for([host, client], "done", 900)
    finally:
        host.close()
        client.close()
    compared = compare(host, client, EELS_FRAMES)
    ours, theirs = smoke_trace(traces["host"]), smoke_trace(traces["client"])
    expect(ours, "no smoke trace (a build before this scenario has none)")
    # A game runs on a little past `done` before it is stopped: compare what both hold.
    n = min(len(ours), len(theirs))
    expect(ours[:n] == theirs[:n], "the smoke traces differ")
    puffs = [row for row in ours[:n] if row[1] == 13 and row[6] == EEL_PUFF_SCALE]
    expect(len(puffs) >= 20, f"only {len(puffs)} eel puffs of {n} puffs: too few eels died to tell")
    # z runs against the path: a puff in the fight has z near -path.
    stray = [row for row in puffs if abs(row[5] + row[7]) > EEL_REACH or abs(row[3]) > EEL_REACH]
    if stray:
        frame, _, _, x, y, z, _, path = stray[0]
        raise Fail(f"{len(stray)} of {len(puffs)} eel puffs lie outside the fight, the first at level frame {frame}: "
                   f"({x:.0f}, {y:.0f}, {z:.0f}) with the path at {path:.0f}")
    return f"same hash on {compared} frames; {len(puffs)} eel puffs of {n} puffs, all in the fight"


# A level, its frames from the join to well past level complete with the pilot and gInvincible (#16): Titania reached
# level complete at frame 29169 in the first run, Macbeth at 9523, Aquas at 7240 (the join is close to its boss), so
# there is room for a slower fight. Titania's moved (#266): level complete came at 43591 on a later build, and at 36223
# on `coop` at 33bd5fe6 (this scenario and campaign.py alike; NOTES.md, "the campaign run"), past the 34000 frames it
# had been given. Why it moves is a guess (Goras follows the tank lined up with it, #220). Its limit is now 42000, about
# 16 % above the latest run as 34000 was above the first, so a fight as long as the 43591 one fails here.
# Macbeth's boss kills player 0 outright when the train reaches the end of its track first (Macbeth_Actor207_Update,
# state 18-19), and with gInvincible that tank is never downed: it climbs on and the game crashes later. A slower pilot
# fails there.
VEHICLE_END_LEVELS = (("Macbeth", 11, 14000), ("Titania", 12, 42000), ("Aquas", 13, 14000))


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


def fly(args, folder, extra_env=None, saves=None, tail=MAP_TAIL, next_level=None, before_close=None, client_env=None):
    """Pilots fly Corneria, one on each client. Returns (host, client, frames to the map and its tail of `tail`
    frames). With next_level, the games go on through the map into the next level, which player 0's A every 30 frames
    picks (lockstep_driver.h), and the frames run to `next_level` frames into it. host.at_map and host.at_next hold the
    rows where the map and that level began. before_close(host, client) runs while both games still run. saves: see
    session(); client_env goes to the client alone."""
    env = {"SF64_COOP_LOCKSTEP_PILOT": "1", "SF64_COOP_PILOT_LOG": "pilot-log.txt", **(extra_env or {})}
    folder.mkdir(parents=True, exist_ok=True)
    host, client = session(args, folder, both=env, saves=saves, client_env=client_env)
    deadline = time.monotonic() + args.level_seconds
    at_map = at_next = None
    try:
        while True:
            rows = log_rows(host) if host.sync_log.exists() else []
            if at_map is None:
                at_map = next((i for i, row in enumerate(rows) if int(row[1]) == MAP), None)
            if at_map is not None and at_next is None and next_level is not None:
                at_next = next((i for i in range(at_map, len(rows)) if int(rows[i][1]) == GSTATE_PLAY), None)
            if next_level is None and at_map is not None and len(rows) >= at_map + tail:
                break
            if at_next is not None and len(rows) >= at_next + next_level:
                break
            expect(at_map is not None or len(rows) < FLY_FRAMES, f"no map in {FLY_FRAMES} frames")
            expect(at_map is None or at_next is not None or next_level is None or len(rows) < at_map + MAP_FRAMES,
                   f"no level after {MAP_FRAMES} frames of the map")
            for game in (host, client):
                expect(game.proc.poll() is None, f"{game.name} ended with code {game.proc.returncode} at {len(rows)}")
            # A desync ends the lockstep log for good and the games run on alone: do not wait for rows that cannot come.
            parted = sorted(host.run.glob("coop-desync-*-player1.txt"))
            expect(not parted, f"the games parted: {parted[0].name if parted else ''}, the log ends at {len(rows)} rows, "
                               f"the map began at row {at_map}")
            expect(time.monotonic() < deadline, f"no map after {args.level_seconds} s, at frame {len(rows)}")
            time.sleep(2)
        host.at_map, host.at_next = at_map, at_next
        if before_close is not None:
            before_close(host, client)
    finally:
        client.close()
        host.close()
    return host, client, (at_next + next_level) if next_level is not None else (at_map + tail)


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
            # A desync stops lockstep, and the session starts the level again (the session survives).
            wait_for([host, client], "again", SURVIVE_AGAIN_SECONDS, "after the desync: the level did not start again")
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
    # then stops the level (WP-23), each game has to say where, and the session starts the level again.
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
        # guests came with Katt and Bill flying a taken wingman's scene (0012 B4); before it, nobody.
        row = {"p1": "-1", "scenes": ",".join(["0"] * 16), "ai": "0,0,0", "body": "0,0,0", "shown": "0,0,0",
               "radio": "0,0", "guests": "0,0",
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


def trace_hit_rows(game):
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
        rows = trace_hit_rows(game)
        expect(rows, f"{game.name}: no score= field in the player trace")
        team, own = rows[-1]
        expect(len(own) >= 2, f"{game.name}: one player only in the trace")
        expect(own[0] > 0 and own[1] > 0, f"{game.name}: a player scored nothing: team {team}, players {own}")
        over = [r for r in rows if sum(r[1]) > r[0]]
        expect(not over, f"{game.name}: players {over[0][1] if over else 0} add up past the team's {over[0][0] if over else 0}")
        found.append(f"{game.name}: team {team}, players {own}")
    expect(trace_hit_rows(host)[-1] == trace_hit_rows(client)[-1], "host and client end on different counts")
    return f"{frames} frames of Corneria, {equal} frames equal; " + "; ".join(found)


RESULTS_AT = JOIN_FRAME + 100      # the probe turns the level clear screen on
RESULTS_READY_FROM = 60             # src/port/coop/game/score.c: presses count from here
RESULTS_HOLD_AT = 150               # the scene holds from here
RESULTS_TIMEOUT = 900               # and goes on alone after this many held frames
RESULTS_EARLY = RESULTS_AT + 20     # player 1's press before 2 s: not counted
RESULTS_HOST = RESULTS_AT + 90      # player 0 ready
RESULTS_CLIENT = RESULTS_AT + 400   # player 1 ready, well into the hold


def results_runs(rows):
    """The `results` lines as [(frame, holding, (ready 0, ready 1))], and the frames the screen went on and off."""
    held, on, off = [], None, None
    for kind, row in rows:
        if kind != "results":
            continue
        if "on" in row:
            if row["on"] == "1":
                on = int(row["frame"])
            else:
                off = int(row["frame"])
        else:
            held.append((int(row["frame"]), int(row["holding"]), tuple(int(v) for v in row["ready"].split(","))))
    return held, on, off


def sc_results(args, folder):
    """#252: the level clear status screen holds the scene in a session until every human pressed A or B, presses in
    its first 2 s do not count, and it goes on alone after 30 s. The probe turns the screen on in Corneria, as a
    level's end does. Both games report the same frames, and their hashes match through the hold."""
    def idle(frame, player):
        return 0, 0, 0

    texts = []
    both = press(idle, {(RESULTS_EARLY, 1), (RESULTS_HOST, 0), (RESULTS_CLIENT, 1)}, BUTTON_A)
    frames = RESULTS_CLIENT + 100
    compared, rows = run_results(args, folder / "pressed", both, frames)
    held, on, off = results_runs(rows)
    expect(on == RESULTS_AT, f"the screen went on at {on}, want {RESULTS_AT}")
    ready0 = next((f for f, _, r in held if r[0]), None)
    ready1 = next((f for f, _, r in held if r[1]), None)
    start = next((f for f, h, _ in held if h), None)
    end = next((f for f, h, _ in held if start is not None and f > start and not h), None)
    # Report lines come one frame after the update that changed them (the probe reads in GamePreUpdate).
    expect(ready0 == RESULTS_HOST + 1, f"player 0 counted ready at {ready0}, pressed at {RESULTS_HOST}")
    expect(ready1 == RESULTS_CLIENT + 1, f"player 1 counted ready at {ready1}, pressed at {RESULTS_CLIENT} "
                                          f"(and early at {RESULTS_EARLY}, which must not count)")
    expect(start is not None and abs(start - (RESULTS_AT + RESULTS_HOLD_AT)) <= 2,
           f"the hold began at {start}, want about {RESULTS_AT + RESULTS_HOLD_AT}")
    expect(end == RESULTS_CLIENT + 1, f"the hold ended at {end}, want the frame after player 1's press")
    expect(off == end + 30, f"the probe's screen went off at {off}")
    texts.append(f"both pressed: held {start} to {end}, {compared} frames equal")

    one = press(idle, {(RESULTS_HOST, 0)}, BUTTON_A)
    frames = RESULTS_AT + RESULTS_HOLD_AT + RESULTS_TIMEOUT + 100
    compared, rows = run_results(args, folder / "timeout", one, frames)
    held, on, off = results_runs(rows)
    start = next((f for f, h, _ in held if h), None)
    end = next((f for f, h, _ in held if start is not None and f > start and not h), None)
    expect(start is not None and end is not None, f"no hold, or it never ended: {held}")
    expect(abs((end - start) - RESULTS_TIMEOUT) <= 2, f"held {end - start} frames with player 1 idle, want "
                                                       f"{RESULTS_TIMEOUT}")
    expect(not any(r[1] for _, _, r in held), "player 1 counted ready without a press")
    texts.append(f"player 1 idle: held {end - start} frames, {compared} frames equal")
    return "; ".join(texts)


# #252: a seat with no human holds nobody. Three games, the first client (player 2) leaves on purpose, and the level
# starts again for the other two with roster Fox,-,Slippy: seat 1 is empty (CoopPlayers_IsOut). The probe turns the
# level clear screen on in that second level; Fox and the player in seat 2 press, and the hold must end after the later
# press, not after the 900-frame limit.
def sc_results_hole(args, folder):
    folder.mkdir(parents=True, exist_ok=True)
    feed = folder / "feed.txt"
    pad = press(idle, {(RESULTS_HOST, 0), (RESULTS_CLIENT, 2)}, BUTTON_A)
    write_feed(feed, RESULTS_CLIENT + 200, pad)
    env = {"SF64_COOP_LOCKSTEP_FEED": str(feed), "SF64_COOP_LOCKSTEP_END": str(SURVIVE_FRAMES),
           "SF64_COOP_SCORE_PROBE": f"results:{RESULTS_AT}"}
    reports = {n: folder / f"{n}-score.txt" for n in ("host", "client0", "client1")}
    for path in reports.values():
        if path.exists():
            path.unlink()
    games = session(args, folder, both=env, clients=2,
                    client_envs=[{"SF64_COOP_JOIN_LEAVE_FILE": "leave", "SF64_COOP_SCORE_REPORT": str(reports["client0"])},
                                 {"SF64_COOP_SCORE_REPORT": str(reports["client1"])}],
                    host_env={"SF64_COOP_SCORE_REPORT": str(reports["host"])})
    host, goes, stays = games
    try:
        wait_for(list(games), "done", 900, "before the player left")
        (goes.run / "leave").write_text("", encoding="utf-8")
        survive_wait([host, stays], "again", 1, SURVIVE_AGAIN_SECONDS, "after the player left")
        deadline = time.monotonic() + 600
        while not any(k == "results" and r.get("on") == "0" for k, r in score_rows(reports["client1"])):
            expect(time.monotonic() < deadline, "the results screen never went off in the second level")
            expect(stays.proc.poll() is None and host.proc.poll() is None, "a game ended before the screen went off")
            time.sleep(0.5)
        survive_no_end([host, stays])
    finally:
        for game in games:
            game.close()
    expect(player_of(goes) == 2 and player_of(stays) == 3, f"seats: the one who left is player {player_of(goes)}, the "
                                                        f"other {player_of(stays)}; the test needs the hole at seat 1")
    roster = [r for _, r in survive_starts(host)]
    expect(len(roster) == 2 and roster[1].split(",")[1] == "-" and roster[1].split(",")[2] != "-",
           f"the second level start's roster is {roster}, want a hole at player 2")
    rows = [score_rows(reports[n]) for n in ("host", "client1")]
    expect(rows[0] == rows[1], "the two games that stayed saw different things")
    held, on, off = results_runs(rows[0])
    start = next((f for f, h, _ in held if h), None)
    end = next((f for f, h, _ in held if start is not None and f > start and not h), None)
    expect(start is not None and end is not None, f"no hold, or it never ended: {held}")
    expect(end == RESULTS_CLIENT + 1, f"the hold ended at {end}, want the frame after the press of the player in seat "
                                      f"3 at {RESULTS_CLIENT} (the empty seat must not hold it, nor the 900-frame limit)")
    expect(not any(r[1] for _, _, r in held), "the empty seat counted ready")
    return f"seat 2 empty after a leave: held {start} to {end}, the screen did not wait for the empty seat"


def run_results(args, folder, script, frames):
    """run_score with the `results` probe and a feed of its own: nothing but the presses in `script`."""
    folder.mkdir(parents=True, exist_ok=True)
    feed = folder / "feed.txt"
    write_feed(feed, frames + 16, script)
    reports = {name: folder / f"{name}-score.txt" for name in ("host", "client")}
    for path in reports.values():
        if path.exists():
            path.unlink()
    env = {"SF64_COOP_LOCKSTEP_FEED": str(feed), "SF64_COOP_LOCKSTEP_END": str(frames),
           "SF64_COOP_SCORE_PROBE": f"results:{RESULTS_AT}"}
    host, client = session(args, folder, {"SF64_COOP_SCORE_REPORT": str(reports["host"])},
                           {"SF64_COOP_SCORE_REPORT": str(reports["client"])}, env)
    try:
        wait_for([host, client], "done", 900)
    finally:
        host.close()
        client.close()
    compared = compare(host, client, frames)
    rows = [score_rows(reports[name]) for name in ("host", "client")]
    expect(rows[0], "no score probe report (a build without the results probe)")
    expect(rows[0] == rows[1], f"the two games saw different things: host {rows[0]}, client {rows[1]}")
    return compared, rows[0]


# Decision 0013, Corneria's Falco rescue flown by Slippy: Falco's lines (CO S60:7, :25, S59:44) do not play; Slippy's
# call for help (2062) and Peppy's warning to Slippy (2061) do, and Slippy's thanks (2336) if he comes through.
CO_RESCUE_FALCO_LINES = {2166, 2167, 2337}
CO_RESCUE_SLIPPY_CALLS = {2062, 2061}
CO_RESCUE_SLIPPY_THANKS = 2336
CO_FOX_ASKS_FALCO = 2185  # CO S14:19; with Slippy in Falco's lead, Peppy's 20267 plays instead


GUESTS_FRAMES = 5600  # Corneria past the choice of Falco's rescue (near frame 5000)
TEAM_PEPPY = 3
TEAM_KATT = 4


def sc_guests(args, folder):
    """#15, decision 0012 B4: the client flies Falco, and Slippy and Peppy are down early (SF64_COOP_TEAM_POKE), so no
    computer wingman is free. Falco's scenes go to Katt, who flies them in her own ship, and no Falco shows."""
    frames = GUESTS_FRAMES
    poke = f"0,100,{TEAM_SLIPPY},-1,{TEAM_PEPPY},-1"  # Corneria, Slippy and Peppy down
    host, client = fly_for(args, folder, frames, {"SF64_COOP_TEAM_TRACE": "team.txt", "SF64_COOP_TEAM_POKE": poke})
    equal = compare(host, client, frames)
    found = []
    for game in (host, client):
        lines = team_lines(game)
        expect(lines, f"{game.name}: no Corneria lines in the team trace")
        falco = [l for l in lines if l["ships"][0] > 0]
        expect(not falco, f"{game.name}: a scene shows Falco, first at frame {falco[0]['frame'] if falco else 0}")
        rescue = [l for l in lines if l["scenes"][CO_RESCUE_SCENE] != 0]
        expect(rescue, f"{game.name}: Falco's rescue (S59) never chose a flyer")
        expect(rescue[0]["scenes"][CO_RESCUE_SCENE] == TEAM_KATT,
               f"{game.name}: Falco's rescue went to team {rescue[0]['scenes'][CO_RESCUE_SCENE]}, not Katt")
        katt = [l for l in lines if l["guests"][0] > 0]
        expect(katt, f"{game.name}: Katt never flew in her own ship")
        found.append(f"{game.name}: Falco's rescue to Katt from frame {rescue[0]['frame']}, Katt's ship on "
                     f"{len(katt)} lines from frame {katt[0]['frame']}, no Falco")
    return f"{frames} frames of Corneria, {equal} frames equal; " + "; ".join(found)


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
        away = [l for l in after if l["body"][0] == 2]
        expect(away, f"{game.name}: Falco's body never went away after frame {at}")
        # The window while player 1 is out: from the down to the first line (after the body went away) with its team
        # value above 0 again. Where the return in place is in (#205, decision 0007: a downed player comes back after 150
        # frames) the window ends there; where it is not, the window is the rest of the level. In the window no computer
        # Falco flies, scenes out of play included, and the body stays away.
        up = next((i for i, l in enumerate(after)
                   if int(l["frame"]) > int(away[0]["frame"]) and l["team"][0] > 0), len(after))
        window, rest = after[:up], after[up:]
        ai = [l for l in window if l["ai"][0] > 0]
        expect(not ai, f"{game.name}: a computer Falco flies after player 1 went down, first at frame "
                       f"{ai[0]['frame'] if ai else 0}")
        bad = [l for l in window if l["body"][0] == 3]
        expect(not bad, f"{game.name}: Falco's body is somewhere player 1 is not, first at frame "
                        f"{bad[0]['frame'] if bad else 0}")
        settled = [l for l in window if int(l["frame"]) >= int(away[0]["frame"])]
        wrong = [l for l in settled if l["body"][0] != 2 or l["team"][0] != -1]
        expect(not wrong, f"{game.name}: after going away at frame {away[0]['frame']}, line at frame "
                          f"{wrong[0]['frame'] if wrong else 0} has body={wrong[0]['body'][0] if wrong else 0} "
                          f"team={wrong[0]['team'][0] if wrong else 0} p1={wrong[0]['p1'] if wrong else 0}")
        # After the return: while player 1 flies in play its body is on it again and no computer Falco flies beside it.
        # Out of play (a scene while player 0 is in a standby state and player 1 is parked) the game's own Falco may fly the
        # scene (decision 0006 B3, the intro and level-clear scenes keep the game's wingman): that is not checked.
        returned = [l for l in rest if l["p0"] in ("3", "4", "5") and int(l["p1"]) >= 1]
        wrong = [l for l in returned if l["body"][0] != 1 or l["team"][0] < 0 or l["ai"][0] > 0]
        expect(not wrong, f"{game.name}: player 1 flies again, line at frame {wrong[0]['frame'] if wrong else 0} has "
                          f"body={wrong[0]['body'][0] if wrong else 0} ai={wrong[0]['ai'][0] if wrong else 0} "
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


MAP_SAVES_NEXT = 300  # frames into the level after the map: its level start and a little of it
OWN_VOLUMES = (40, 50, 60)  # a game's own music, voice and sfx volumes, which it keeps when it takes the host's save
OWN_SOUND_MODE = 1  # mono


def save_lines(game, word):
    """The saves of the join driver's `save_session` or `save_after` lines, as bytes (256 each)."""
    return [bytes.fromhex(values["save"]) for values in join_test.said(game.report(), word)]


def progress(save):
    """The bytes of a save that are the session's (src/port/coop/game/save.c): all but the settings, the language bytes
    and the checksum."""
    data = bytearray(save[:make_save.DATA_SIZE])
    data[make_save.SETTINGS] = b"\0" * 4
    data[make_save.LANGUAGES] = b"\0" * 2
    return bytes(data)


def leave_and_check_saves(host, client, files):
    """The client leaves the session (SF64_COOP_JOIN_LEAVE_FILE), the host sees it go, and both end their session.
    Then: every game's default.sav is byte for byte what it was before the session (files: name -> bytes), its save in
    memory after the session is that file's save again, and in the session each game held the host's progress with its
    own settings. Returns a line about it."""
    (client.run / "leave").write_text("now\n", encoding="utf-8")
    deadline = time.monotonic() + 120
    while not all(save_lines(game, "save_after") for game in (host, client)):
        for game in (host, client):
            expect(game.proc.poll() is None, f"{game.name} ended with code {game.proc.returncode} before `save_after`")
        expect(time.monotonic() < deadline, "no `save_after` from both games within 120 s of the client's leaving")
        time.sleep(1)
    for game in (host, client):
        now = (game.run / "default.sav").read_bytes()
        expect(not make_save.check(now, game.name), f"{game.name}: {make_save.check(now, game.name)}")
        expect(now == files[game.name], f"{game.name}'s default.sav changed in the session: planets "
                                        f"{make_save.planets(files[game.name])} before, {make_save.planets(now)} after")
        after = save_lines(game, "save_after")[-1]
        expect(after == now[:make_save.SAVE_SIZE], f"{game.name}: its save in memory after the session is not its "
                                                   f"file's: planets {make_save.planets(after)}")
        held = save_lines(game, "save_session")
        expect(len(held) == 1, f"{game.name}: {len(held)} `save_session` lines")
        expect(progress(held[0]) == progress(files["host"]), f"{game.name}: in the session it held planets "
                                                             f"{make_save.planets(held[0])}, the host's are "
                                                             f"{make_save.planets(files['host'])}")
        expect(make_save.settings(held[0]) == make_save.settings(files[game.name]),
               f"{game.name}: in the session its sound settings were {make_save.settings(held[0])}, its own are "
               f"{make_save.settings(files[game.name])}")
        expect(held[0][make_save.LANGUAGES] == files[game.name][make_save.LANGUAGES], f"{game.name}: language bytes")
    return (f"after the session both default.sav are byte for byte as before and back in memory; in it both held the "
            f"host's planets {make_save.planets(files['host'])}, the client its own sound settings "
            f"{make_save.settings(files['client0'])}")


def map_saves_run(args, folder, host_save, client_save, what):
    """Pilots fly Corneria to its end, the map writes the save (src/overlays/ovl_menu/fox_map.c, Map_Setup...), and
    the games go on through the map into the next level. host_save and client_save are save files, or None for none:
    the game writes gDefaultSave at boot. The hashes and lockstep logs must be equal on every frame, from the level
    start through the map into the next level; then leave_and_check_saves."""
    blank = make_save.save_file()
    files = {"host": host_save or blank, "client0": client_save or blank}
    leave = {"SF64_COOP_JOIN_LEAVE_FILE": "leave"}  # in the game's own folder, where it runs
    checked = []
    host, client, frames = fly(args, folder, saves={"host": host_save, "client0": client_save}, next_level=MAP_SAVES_NEXT,
                               client_env=leave,
                               before_close=lambda h, c: checked.append(leave_and_check_saves(h, c, files)))
    equal = compare(host, client, frames)
    return (f"{what}: equal on {equal} frames, Corneria to the map at {host.at_map}, the next level at {host.at_next} "
            f"and {MAP_SAVES_NEXT} frames of it; {checked[0]}")


def sc_map_saves(args, folder):
    """Two PCs hold two different saves, as a real pair does: the host's is blank, the client's has every level
    cleared and its own sound settings. The map reads the save (which planets are played and cleared decides what it
    shows and when: a planet played for the first time gets a stage of its own, D_menu_801CD980), so before the host's
    save went to the clients the first real Windows and Linux session parted 41 frames into the map after Corneria
    (route line 15 of gTexturedLines started on one game only, and the random numbers). Both games must stay one game
    through the map into the next level, and neither save file may change."""
    client = make_save.save_file(clear=make_save.slots("all"), volumes=OWN_VOLUMES, sound_mode=OWN_SOUND_MODE)
    return map_saves_run(args, folder, None, client, "host blank, client all cleared")


def sc_map_saves_host_full(args, folder):
    """map_saves the other way round: the host's save has every level cleared and its own sound settings, the client's
    is blank."""
    host = make_save.save_file(clear=make_save.slots("all"), volumes=OWN_VOLUMES, sound_mode=OWN_SOUND_MODE)
    return map_saves_run(args, folder, host, None, "host all cleared, client blank")


def sc_map_saves_equal(args, folder):
    """map_saves with the same save on both, every level cleared."""
    both = make_save.save_file(clear=make_save.slots("all"))
    return map_saves_run(args, folder, both, both, "both all cleared")


def sc_save_own(args, folder):
    """The control for the session's save (decision 0009 item 8): the client keeps its own save
    (SF64_COOP_JOIN_KEEP_OWN_SAVE), which differs from the host's in one byte, Corneria played. That has to show at
    frame 0 as a desync in region globals, and desync_report.py has to name gSaveFile.planet: a save that drifted is
    a named difference, not a map that parts later. It is also a recovery that cannot help: the level starts again, the
    client keeps its own save again and the games part at frame 0 again, so the session gives up and ends as it did
    before there was a recovery (SYNC_QUICK_PARTS, lockstep_game.c), with two level starts and no third."""
    one_byte = make_save.save_file(played=make_save.slots("corneria"))
    folder.mkdir(parents=True, exist_ok=True)
    feed = folder / "feed.txt"
    write_feed(feed, 400, idle)
    env = {"SF64_COOP_LOCKSTEP_FEED": str(feed), "SF64_COOP_LOCKSTEP_END": "300"}
    host, client = session(args, folder, both=env, client_env={"SF64_COOP_JOIN_KEEP_OWN_SAVE": "1"},
                           saves={"host": None, "client0": one_byte})
    try:
        survive_wait([host, client], "over", 2, 600, "with the client on its own save")
        wait_for([host, client], "alive", 120, "after the session ended: a game hangs")
        time.sleep(2)
    finally:
        host.close()
        client.close()
    reports = {}
    for game in (host, client):
        found = own_reports(game)
        expect(len(found) == 2, f"{game.name} wrote {len(found)} desync reports, want one per level start")
        reports[game.name] = desync_report.Report(found[0])
        header = reports[game.name].header
        expect(header.get("frame") == "0", f"{game.name}: {found[0].name} says frame {header.get('frame')}, want 0")
        expect(header.get("regions differing") == "globals", f"{game.name}: regions {header.get('regions differing')}")
        problems = reports[game.name].verify()
        expect(not problems, f"{game.name}: {problems[0] if problems else ''}")
        expect([run for run, _ in survive_starts(game)] == [1, 2], f"{game.name}: level starts {survive_starts(game)}")
        ended = [line for _, _, line in game.report() if line.startswith("ended")]
        expect(ended and "out of step at frame 0" in ended[0], f"{game.name}: {ended[:1]}")
        expect(any("parted again at once, so the session is over" in line for line in ended), f"{game.name}: {ended}")
        expect(not join_test.said(game.sync(), "again") or len(join_test.said(game.sync(), "again")) == 1,
               f"{game.name}: the level started again more than once")
    done = report_diff(own_reports(host)[0], own_reports(client)[0])
    first = next((line.strip() for line in done.stdout.splitlines() if "differs" in line and ":" in line), None)
    expect(first is not None and first.startswith("globals: gSaveFile.planet differs in 1 of"),
           f"desync_report.py diff did not name gSaveFile.planet: {done.stdout[-400:]}")
    return (f"a save one byte apart parts the games at frame 0, in globals: `{first}`; the level started again once, "
            f"parted at frame 0 again, and the session ended there")


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


def check_skipped(game, frames, skip):
    """Start at `skip` skipped the intro on this game: Play_Init again on the next frame, and both players fly."""
    inits = [i for i, state in enumerate(play_states(game)[:frames]) if state == PLAY_INIT]
    expect(inits == [skip + 1], f"{game.name}: Play_Init on lockstep frames {inits}, want [{skip + 1}]: the Start did "
                                f"not skip the intro")
    rows = level_trace(game, frames)
    intro = sum(1 for r in rows if r.players[0].state == LEVEL_INTRO)
    expect(intro <= skip + 2, f"{game.name}: player 0 in the intro on {intro} frames, the skip was at {skip}")
    flying = next((n for n, r in enumerate(rows) if both_fly(r)), None)
    expect(flying is not None, f"{game.name}: after the skip the two players never flew together")
    return flying


def sc_save(args, folder):
    """The intro skip goes by the host's save, which every game holds in a session (decision 0009 item 8, #191), with
    real save files that differ at the session start. Corneria cleared in the host's save, the client's blank: the
    host's Start in the intro skips it on both games, Play_Init runs again on both on the same frame and both players
    fly; the same with the client's Start. Cleared in the client's save only: neither Start does anything."""
    frames, skip = 900, 300
    cleared = make_save.save_file(clear=make_save.slots("corneria"))
    found = []
    for who, player in (("host", 0), ("client", 1)):
        host, client = run_short(args, folder / f"host_cleared_{who}_start", press(idle, {(skip, player)}),
                                 frames=frames, saves={"host": cleared, "client0": None})
        equal = compare(host, client, frames)
        flying = [check_skipped(game, frames, skip) for game in (host, client)]
        found.append(f"the {who}'s Start skipped the intro on both, Play_Init again on frame {skip + 1}, both fly "
                     f"{flying[0]} frames into the level, same hash on {equal} frames")
    host, client = run_short(args, folder / "client_cleared", press(idle, {(skip, 0), (skip + 60, 1)}), frames=frames,
                             saves={"host": None, "client0": cleared})
    kept = compare(host, client, frames)
    for game in (host, client):
        states = set(play_states(game)[1:frames])
        expect(states == {PLAY_UPDATE}, f"{game.name}: play states {sorted(states)} after frame 0, want only "
                                        f"{PLAY_UPDATE}: a Start did something in the intro")
        rows = level_trace(game, frames)
        expect(len(rows) >= frames - 2 and all(r.players[0].state == LEVEL_INTRO for r in rows[2:]),
               f"{game.name}: player 0 left the intro within {frames} frames with the host's save not cleared")
    return (f"host's save cleared, client's blank: {found[0]}; {found[1]}; client's save cleared only: Start from "
            f"either did nothing, same hash on {kept} frames")


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
    # The client joins once it is back at the title after its own flight: from its frame RESET_FLY_FRAMES + 100 on, with
    # 100 title frames in a row. The host's frame count says nothing about that when the games run fast, as the host is
    # thousands of frames on while the client boots, and neither does its own: the boot logo waits 1000 ms of the clock,
    # 240 frames on Debug at a limit of 8 and 1750 with no limit (#287).
    client_env = {"SF64_COOP_TEST_START_LEVEL": "0", "SF64_COOP_TEST_START_SKIP_INTRO": "1",
                  "SF64_COOP_TEST_START_TITLE_FRAMES": "40", "SF64_COOP_TEST_START_RESET": str(RESET_FLY_FRAMES),
                  "SF64_COOP_JOIN_CONNECT_FRAME": str(RESET_FLY_FRAMES + 100), "SF64_COOP_JOIN_CONNECT_TITLE": "100"}
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
        # With nobody else in the session there is nothing to start again: the host keeps the lost player's slot for
        # COOP_SESSION_REJOIN_MS, a client tries to come back to its host until the connection times out, and then the
        # session ends as it always did.
        deadline = time.monotonic() + TIMEOUT_MS / 1000 + 45
        while not [line for _, _, line in stays.report() if line.startswith("ended")]:
            expect(stays.proc.poll() is None, f"{stays.name} is gone")
            expect(time.monotonic() < deadline, f"{stays.name}'s session never ended: "
                                                f"{[line for _, _, line in stays.report()][-3:]}")
            time.sleep(0.5)
        over = time.monotonic() - started
        said = [line for _, _, line in stays.report() if line.startswith(("left", "ended", "rejoin", "recover"))]
        tried = [line.split(" ")[0] for line in said]
        expect(tried[:2] == (["rejoin", "ended"] if leaver == "host" else ["left", "recover"]),
               f"{stays.name}: {said}")
        expect(not join_test.said(stays.sync(), "again"), f"{stays.name}: a level started again with nobody there")
        frames_alive = len(join_test.said(stays.sync(), "alive"))
        time.sleep(3)
        expect(len(join_test.said(stays.sync(), "alive")) > frames_alive, f"{stays.name} does not play on")
        return (f"{stays.name} stopped lockstep after {ended:.0f} s, waited for the other ({' '.join(tried)}), ended "
                f"the session after {over:.0f} s with `{said[-1]}`, and played on")
    finally:
        host.close()
        client.close()


def sc_left(args, folder):
    return lost_player(args, folder, "client", "host", lambda game: game.stop(kill=True))


def sc_host_left(args, folder):
    return lost_player(args, folder, "host", "client", lambda game: game.stop(kill=True))


def sc_stall(args, folder):
    """A client that keeps its connection and stops sending input: nothing but lockstep can see that. The host drops
    it as a lost player (its slot is kept), the client sees the host close and comes back with the session id, and
    the level starts again for both (the session survives)."""
    folder.mkdir(parents=True, exist_ok=True)
    feed = folder / "feed.txt"
    write_feed(feed, 3 * SHORT_FRAMES, sway)
    mute = JOIN_FRAME + 50
    games = session(args, folder, client_env={"SF64_COOP_LOCKSTEP_MUTE": str(mute)},
                    both={"SF64_COOP_LOCKSTEP_FEED": str(feed), "SF64_COOP_LOCKSTEP_END": str(SURVIVE_FRAMES)})
    host, client = games
    try:
        wait_for([host, client], "done", 900, "before the stall")
        started = time.monotonic()
        wait_for([host], "over", STALL_SECONDS + 900, "after the stall")
        ended = time.monotonic() - started
        survive_wait(games, "again", 1, SURVIVE_AGAIN_SECONDS, "after the stall: the level did not start again")
        survive_wait(games, "done", 2, 600, "in the level that started again")
        survive_no_end(games)
        over = int(join_test.said(host.sync(), "over")[0]["frame"])
        expect(mute <= over <= mute + 2 * INPUT_DELAY, f"the host ran to frame {over}, input stopped at {mute}")
    finally:
        host.close()
        client.close()
    parted = [line for _, _, line in host.report() if line.startswith("parted")]
    expect(parted and "reason=2" in parted[0] and "stopped sending input" in parted[0], f"host: {parted}")
    left = join_test.said(host.report(), "left")
    expect(left and left[0]["away"] == "1", f"host: the slot of the player it dropped was not kept: {left}")
    recover = [line for _, _, line in host.report() if line.startswith("recover")]
    expect(recover and "Player 2 lost the connection" in recover[0], f"host: {recover}")
    expect(join_test.said(client.report(), "rejoin"), "the client did not try to come back")
    back = [v for v in join_test.said(client.report(), "joined") if v.get("rejoined") == "1"]
    expect(len(back) == 1 and back[0]["player"] == "1", f"the client did not come back as player 2: {back}")
    for game in games:
        expect(survive_starts(game) == [(1, "Fox,Falco,-,-"), (2, "Fox,Falco,-,-")],
               f"{game.name}: level starts {survive_starts(game)}")
    frames = survive_compare(games, 2, SURVIVE_FRAMES)
    no_desync(games)
    return (f"the host stopped at frame {over} after {ended:.0f} s and dropped player 2 with its slot kept; the client "
            f"came back with the session id, and the level that started again is equal on {frames} frames")


def sc_frozen(args, folder):
    """A client that stops dead for longer than the host waits for input, and then goes on: a connection that dropped
    and came back. The host drops the player with its slot kept, the thawed client finds its connection closed, comes
    back with the session id as the player it was, and the level starts again for both (the session survives)."""
    expect(IS_LINUX, "needs SIGSTOP")

    def freeze(game):
        os.kill(game.proc.pid, signal.SIGSTOP)  # the game this script started

    folder.mkdir(parents=True, exist_ok=True)
    feed = folder / "feed.txt"
    write_feed(feed, 3 * SHORT_FRAMES, sway)
    games = session(args, folder, both={"SF64_COOP_LOCKSTEP_FEED": str(feed),
                                        "SF64_COOP_LOCKSTEP_END": str(SURVIVE_FRAMES)})
    host, client = games
    try:
        wait_for([host, client], "done", 900, "before the freeze")
        freeze(client)
        started = time.monotonic()
        wait_for([host], "over", TIMEOUT_MS / 1000 + 30, "after the freeze")
        ended = time.monotonic() - started
        wait_for([host], "alive", 120, "after the freeze: the host hangs")
        os.kill(client.proc.pid, signal.SIGCONT)
        survive_wait(games, "again", 1, SURVIVE_AGAIN_SECONDS, "after the thaw: the level did not start again")
        survive_wait(games, "done", 2, 600, "in the level that started again")
        survive_no_end(games)
    finally:
        os.kill(client.proc.pid, signal.SIGCONT)
        host.close()
        client.close()
    left = join_test.said(host.report(), "left")
    expect(len(left) == 1 and left[0]["away"] == "1", f"host: the lost player's slot was not kept: {left}")
    recover = [line for _, _, line in host.report() if line.startswith("recover")]
    expect(recover and "reason=1" in recover[0] and "Player 2 lost the connection. Waiting for it to come back (15 s)"
           in recover[0], f"host: {recover}")
    expect(join_test.said(client.report(), "rejoin"), "the client did not try to come back")
    back = [v for v in join_test.said(client.report(), "joined") if v.get("rejoined") == "1"]
    expect(len(back) == 1 and back[0]["player"] == "1", f"the client did not come back as player 2: {back}")
    for game in games:
        expect(survive_starts(game) == [(1, "Fox,Falco,-,-"), (2, "Fox,Falco,-,-")],
               f"{game.name}: level starts {survive_starts(game)}")
    frames = survive_compare(games, 2, SURVIVE_FRAMES)
    no_desync(games)
    return (f"the host stopped lockstep after {ended:.0f} s and kept player 2's slot; the thawed client came back "
            f"with the session id, and the level that started again is equal on {frames} frames")


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


def check_desync(host, client, poke, mode, pair=(1, 2), clients=None, ended_later=False):
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
        expect(over, f"{game.name}: lockstep did not stop")
        frame = int(over[0]["frame"])
        expect(poke <= frame <= poke + DESYNC_SEEN_WITHIN, f"{game.name}: over at frame {frame}, poke at {poke}")
        # The session does not end: every game says where the games parted, or hears from the host that the level
        # starts again, and takes the second level start (the session survives).
        said = [line for _, _, line in game.report() if line.startswith(("parted", "recover"))]
        expect(said, f"{game.name} says nothing about the desync")
        ended = [line for _, _, line in game.report() if line.startswith(("ended", "leaving"))]
        expect(ended_later or not ended, f"{game.name}'s session ended: {ended[:1]}")
        if game in (host, client):
            parted = [line for line in said if line.startswith("parted")]
            expect(parted, f"{game.name}: {said}")
            expect(f"out of step at frame {poke} of Corneria ({mode})" in parted[0], f"{game.name}: {parted[0]}")
            expect("report for the developers is in" in parted[0], f"{game.name}: no report named: {parted[0]}")
        else:
            # A game outside the pair waits the grace out, and the host's word comes first: it stops on that, and its
            # report comes from there (NOTES.md, 2026-11-07, #21).
            expect("The games parted" in said[0] or "out of step" in said[0], f"{game.name}: {said[0]}")
        expect(join_test.said(game.sync(), "again"), f"{game.name}: the level did not start again")
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


def poke_session(args, folder, poke, jump=None, both=None, client_envs=None, clients=1, after=None):
    """A session with sway, a bit of player 2 flipped on the first client at `poke`, into all-range first when `jump`
    says when player 1 presses L (CVar gDebugJumpToAllRange on every game). Waits for lockstep to stop and the level to
    start again (the session survives), or for `done` when `both` turns the check off. after(games) runs before the
    games are stopped. Returns the games, stopped."""
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
            wait_for(list(games), "again", SURVIVE_AGAIN_SECONDS, "after the desync: the level did not start again")
        if after is not None:
            after(games)
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
    because it is done, not because its time is up; and the session still ends, with both games playing on. A desync
    alone ends no session any more (the level starts again), so the client leaves the level that started again
    (SF64_COOP_JOIN_LEAVE_FILE): the report of the desync is what crosses then."""
    poke = JOIN_FRAME + 300

    def leave(games):
        (games[1].run / "leave").write_text("", encoding="utf-8")
        survive_wait(games, "over", 2, 120, "after the client left the level that started again")
        wait_for(list(games), "alive", 120, "after the session ended: a game hangs")

    envs = [{"SF64_COOP_LOCKSTEP_POKE": str(poke), "SF64_COOP_HASH_POKE_PLAYER": "1",
             "SF64_COOP_JOIN_LEAVE_FILE": "leave"}]
    host, client = poke_session(args, folder, poke, client_envs=envs, after=leave)
    found = check_desync(host, client, poke, "rails", ended_later=True)
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


NAMES_TYPED_258 = ("Captain Fox", "sam.ok!")  # what the host and the client type
NAMES_HELD_258 = "CAPTAIN,SAM.OK!,-,-"  # what every game holds after the session cleaned them (session.h)


def tag_rows_258(path):
    """(frame, {player: name}) of every line of a view trace that has `tags` (src/port/coop/test/view_trace.h)."""
    rows = []
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        fields = dict(kv.split("=", 1) for kv in line.split() if "=" in kv)
        if "tags" not in fields or "frame" not in fields:
            continue
        tags = {} if fields["tags"] == "-" else {
            int(g.split(":", 1)[0]): g.split(":", 1)[1].replace("_", " ") for g in fields["tags"].split(",")}
        rows.append((int(fields["frame"]), tags))
    return rows


def sc_player_names(args, folder):
    """#257, #258: see the list at the top."""
    view = {"SF64_COOP_VIEW_TRACE": "view-trace.txt"}
    host, client = run_short(args, folder, host_env={"SF64_COOP_JOIN_NAME": NAMES_TYPED_258[0]},
                             client_env={"SF64_COOP_JOIN_NAME": NAMES_TYPED_258[1]}, both=view)
    frames = compare(host, client, SHORT_FRAMES)
    no_desync([host, client])
    marks = {}
    for game, own, other, want in ((host, 0, 1, "SAM.OK!"), (client, 1, 0, "CAPTAIN")):
        said = join_test.said(game.report(), "names")
        expect(said and said[-1]["names"] == NAMES_HELD_258, f"{game.name} holds the names {said[-1] if said else None}")
        start = int(join_test.said(game.report(), "level_start")[0]["frame"])
        expect(any(int(s["frame"]) <= start and s["names"] == NAMES_HELD_258 for s in said),
               f"{game.name} did not hold both names by the level start at frame {start}: {said}")
        rows = tag_rows_258(game.run / "view-trace.txt")
        marked = [tags[other] for _, tags in rows if other in tags]
        wrong = sorted({mark for mark in marked if mark != want})
        own_marks = sum(1 for _, tags in rows if own in tags)
        expect(marked and not wrong and own_marks == 0,
               f"{game.name}: player {other} marked on {len(marked)} of {len(rows)} frames, other marks {wrong}, its own "
               f"player on {own_marks}")
        marks[game.name] = f"{want} on {len(marked)} of {len(rows)} frames"
    return (f"both hold {NAMES_HELD_258}; host marks {marks['host']}, client marks {marks['client0']}; same hash on "
            f"{frames} frames")


# ---- The session survives (#189 and its sibling): a desync or a lost player starts the level again ----

SURVIVE_FRAMES = 600  # frames of the level after a restart that have to hash alike: the intro, from frame 0 on
SURVIVE_AGAIN_SECONDS = 120  # a level that starts again does so within a second or two; a build without it never
SURVIVE_REASONS = {"lost": 1, "left": 2, "desync": 3}  # CoopRecoverReason, src/port/coop/session/session.h


def survive_run_rows(game, run):
    """(names, rows) of a game's hash log from the first frame of its lockstep run `run` on (1 is the first level start
    of the session), without their frame numbers: the join driver says `level_running` once per level start."""
    names, rows = hash_check.read_log(game.run / "state-hash.txt")
    running = join_test.said(game.report(), "level_running")
    expect(len(running) >= run, f"{game.name} got to the level {len(running)} times, want {run}")
    first = int(running[run - 1]["frame"])
    return names, [r[1:] for r in rows if r[0] >= first]


def survive_run_log(game, run):
    """The lines of a game's lockstep log that belong to run `run`."""
    lines = game.sync_log.read_text(encoding="utf-8").splitlines()
    marks = [i for i, line in enumerate(lines) if line.startswith("# run ")]
    starts = [0] + [i + 1 for i in marks]
    ends = marks + [len(lines)]
    expect(len(starts) >= run, f"{game.name}'s lockstep log holds {len(starts)} runs, want {run}")
    return lines[starts[run - 1]:ends[run - 1]]


def survive_wait(games, word, count, seconds, what):
    """Until every game's lockstep report says `word` at least `count` times."""
    deadline = time.monotonic() + seconds
    while True:
        pending = [game for game in games if len(join_test.said(game.sync(), word)) < count]
        if not pending:
            return
        for game in pending:
            expect(game.proc.poll() is None, f"{game.name} ended with code {game.proc.returncode} before `{word}` {what}")
        expect(time.monotonic() < deadline,
               f"no `{word}` number {count} from {', '.join(g.name for g in pending)} after {seconds} s {what}")
        time.sleep(0.5)


def survive_compare(games, run, frames):
    """The games' hash logs and lockstep logs of run `run` are equal on `frames` frames from its frame 0. Returns the
    number of frames compared."""
    names, ours = survive_run_rows(games[0], run)
    logs = survive_run_log(games[0], run)
    for other in games[1:]:
        _, theirs = survive_run_rows(other, run)
        n = min(len(ours), len(theirs))
        expect(n >= frames, f"run {run}: only {n} frames of {games[0].name} and {other.name} to compare, want {frames}")
        difference = join_test.first_difference(names, ours[:frames], theirs[:frames])
        expect(difference is None, f"run {run}: the hash logs of {games[0].name} and {other.name} differ at {difference}")
        theirs_log = survive_run_log(other, run)
        m = min(len(logs), len(theirs_log), frames)
        for i in range(m):
            expect(logs[i] == theirs_log[i], f"run {run}: the lockstep logs differ at frame {i}: {logs[i]} against "
                                             f"{theirs_log[i]}")
    return frames


def survive_starts(game):
    """The level starts a game took, as (run, roster) from its join report."""
    return [(int(v["run"]), v["roster"]) for v in join_test.said(game.report(), "level_start")]


def survive_no_end(games):
    for game in games:
        ended = [line for _, _, line in game.report() if line.startswith("ended")]
        expect(not ended, f"{game.name}'s session ended: {ended[-1] if ended else ''}")
        expect(game.proc.poll() is None, f"{game.name} is gone")


def survive_first_difference(games, poke):
    """#189: every game of the pair wrote the same `first difference` line into its own co-op log, each folder holds
    it in its copy of the other player's log too, each report ends with it, and it starts as the line of
    desync_report.py diff on the two report files does. Returns the line."""
    needle = f"first difference at frame {poke}, players 1 and 2: "
    lines = {}
    for game in games:
        player = player_of(game)
        logs = coop_logs(game)
        for holder, path in sorted(logs.items()):
            found = [line.format() for line in coop_log.read_lines(path, holder) if needle in line.format()]
            expect(len(found) == 1, f"{game.name}'s copy of player {holder}'s log says `{needle}` {len(found)} times")
            lines[(game.name, holder)] = found[0].split(needle, 1)[1]
            category = [line.category for line in coop_log.read_lines(path, holder) if needle in line.format()]
            expect(category == ["desync"], f"{game.name}: the line is in category {category}")
        report = own_reports(game)
        expect(len(report) == 1, f"{game.name} wrote {len(report)} desync reports")
        tail = report[0].read_text(encoding="utf-8", errors="replace").splitlines()[-1]
        expect(tail == f"# {needle}{lines[(game.name, player)]}", f"{game.name}'s report ends with `{tail[:200]}`")
    expect(len(set(lines.values())) == 1, f"the games wrote different lines: {sorted(set(lines.values()))}")
    line = next(iter(lines.values()))
    done = report_diff(own_reports(games[0])[0], own_reports(games[1])[0])
    tool = next((text.strip() for text in done.stdout.splitlines() if " differs in " in text), "")
    head = line.split("; word at", 1)[0]
    expect(tool.startswith(head), f"the game says `{head}`, desync_report.py diff says `{tool}`")
    return line


def sc_survive_desync(args, folder):
    """A bit of player 2 flipped on the client: both games find the desync, and the session does not end. Both stop
    lockstep, the host sends its save and the level start again (run 2), both enter the level from it, and the level
    is equal on both from its frame 0. Both co-op logs name the first differing field (#189)."""
    folder.mkdir(parents=True, exist_ok=True)
    poke = JOIN_FRAME + 300
    feed = folder / "feed.txt"
    write_feed(feed, poke + 16, sway)
    env = {"SF64_COOP_LOCKSTEP_FEED": str(feed), "SF64_COOP_LOCKSTEP_END": str(SURVIVE_FRAMES)}
    games = session(args, folder, both=env,
                    client_envs=[{"SF64_COOP_LOCKSTEP_POKE": str(poke), "SF64_COOP_HASH_POKE_PLAYER": "1"}])
    try:
        survive_wait(games, "over", 1, 900, "after the flipped bit")
        survive_wait(games, "again", 1, SURVIVE_AGAIN_SECONDS, "after the desync: the level did not start again")
        survive_wait(games, "done", 2, 600, "in the level that started again")
        survive_no_end(games)
        time.sleep(2)  # the co-op log's stream: the last lines to the other folder
    finally:
        for game in games:
            game.close()
    host, client = games
    for game in games:
        over = join_test.said(game.sync(), "over")
        expect(poke <= int(over[0]["frame"]) <= poke + DESYNC_SEEN_WITHIN,
               f"{game.name}: lockstep stopped at frame {over[0]['frame']}, poke at {poke}")
        parted = [line for _, _, line in game.report() if line.startswith("parted")]
        expect(parted and f"out of step at frame {poke} of Corneria (rails)" in parted[0]
               and "report for the developers is in" in parted[0], f"{game.name}: {parted[:1]}")
        recover = join_test.said(game.report(), "recover")
        expect(recover and all(int(v["reason"]) == SURVIVE_REASONS["desync"] for v in recover),
               f"{game.name}: recover {recover}")
        said = [line for _, _, line in game.report() if line.startswith("recover")]
        expect("The games parted. Starting the level again together." in said[0], f"{game.name}: {said[0]}")
        expect(survive_starts(game) == [(1, "Fox,Falco,-,-"), (2, "Fox,Falco,-,-")],
               f"{game.name}: level starts {survive_starts(game)}")
        expect(len(own_reports(game)) == 1, f"{game.name} wrote {len(own_reports(game))} desync reports, want the one "
                                            "of the flipped bit and none in the level that started again")
    # No checkpoint was reached, so the level starts again from its start (a checkpoint: survive_checkpoint).
    for game in games:
        second = survive_level_values(game, 2)
        expect(not any(second[SURVIVE_CHECKPOINT_FLAG:]), f"{game.name}: level start 2 carries a checkpoint: "
                                                  f"{second[SURVIVE_CHECKPOINT_FLAG:]}")
        again = survive_run_state(game, 2)
        expect(again["load"] == 0 and again["saved_load"] == 0 and again["path"] == 0 and again["allrange"] == 0,
               f"{game.name}: run 2 did not begin at the level's start: {again}")
    expect(not join_test.said(host.report(), "restart_at_checkpoint"), "the host took a checkpoint it never reached")
    line = survive_first_difference(games, poke)
    frames = survive_compare(games, 2, SURVIVE_FRAMES)
    return (f"poke at {poke}: both games stopped lockstep, took level start 2 from the level's start and hash alike on its first {frames} "
            f"frames; no session ended; both logs say `{line}`, as desync_report.py diff does")


def sc_survive_three(args, folder):
    """Three games, and one client leaves the level on purpose (SF64_COOP_JOIN_LEAVE_FILE, the menu's Leave with its
    handover). The session does not end for the other two: the host answers the one who goes, tells the one who stays,
    and starts the level again for the two of them, without the player who left; the level is equal on both from its
    frame 0. The one who left plays on alone."""
    folder.mkdir(parents=True, exist_ok=True)
    feed = folder / "feed.txt"
    write_feed(feed, 3 * SHORT_FRAMES, sway)
    env = {"SF64_COOP_LOCKSTEP_FEED": str(feed), "SF64_COOP_LOCKSTEP_END": str(SURVIVE_FRAMES)}
    games = session(args, folder, both=env, clients=2, client_envs=[{}, {"SF64_COOP_JOIN_LEAVE_FILE": "leave"}])
    host, stays, goes = games
    try:
        wait_for(list(games), "done", 900, "before the player left")
        (goes.run / "leave").write_text("", encoding="utf-8")
        survive_wait([host, stays], "again", 1, SURVIVE_AGAIN_SECONDS, "after the player left: the level did not start again")
        survive_wait([host, stays], "done", 2, 600, "in the level that started again")
        survive_no_end([host, stays])
        wait_for([goes], "alive", 120, "after leaving: the game hangs")
    finally:
        for game in games:
            game.close()
    gone = player_of(goes)
    # The two clients join in either order, so who is player 2 and who player 3 comes from the first level start.
    roster = survive_starts(host)[0][1].split(",")
    expect(sorted(roster) == ["-", "Falco", "Fox", "Slippy"], f"the first level start's roster is {roster}")
    kept = list(roster)
    kept[gone - 1] = "-"
    for game in (host, stays):
        expect(survive_starts(game) == [(1, ",".join(roster)), (2, ",".join(kept))],
               f"{game.name}: level starts {survive_starts(game)}, want the second without player {gone}")
        recover = [line for _, _, line in game.report() if line.startswith("recover")]
        expect(recover and f"Player {gone} left. Starting the level again together." in recover[0],
               f"{game.name}: {recover}")
    expect(len(survive_starts(goes)) == 1, f"{goes.name} took {len(survive_starts(goes))} level starts")
    expect(not join_test.said(goes.sync(), "again"), f"{goes.name}: the level started again for the one who left")
    frames = survive_compare([host, stays], 2, SURVIVE_FRAMES)
    no_desync(games)
    return (f"player {gone} left a level of three: the host and player {player_of(stays)} took level start 2 without "
            f"it and hash alike on its first {frames} frames; the one who left plays on alone")



SURVIVE_CHECKPOINT_FLAG = 171  # the level start value that says the level starts at a checkpoint (level_start.c)
SURVIVE_ITEM_CHECKPOINT = 3  # SF64_COOP_LOCKSTEP_ITEM kind: the ring a rails level saves its checkpoint at
SURVIVE_FORTUNA = 14  # LEVEL_FORTUNA: its checkpoint is Star Wolf's arrival (src/overlays/ovl_i4/fox_fo.c:300)


def survive_level_values(game, run):
    """The values of the level start of `run` in a game's join report, as numbers."""
    starts = join_test.said(game.report(), "level_start")
    expect(len(starts) >= run, f"{game.name} took {len(starts)} level starts, want {run}")
    return [int(v) for v in starts[run - 1]["values"].split(",")]


def survive_run_state(game, run):
    """The lockstep driver's `run_state` line of `run`: where that run's level began."""
    states = [v for v in join_test.said(game.sync(), "run_state") if v["run"] == str(run)]
    expect(len(states) == 1, f"{game.name} has {len(states)} run_state lines of run {run}")
    return {key: int(value) for key, value in states[0].items()}


def survive_checkpoint_run(args, folder, both, host_env=None, after=300):
    """A session that reaches a checkpoint in its level; `after` frames later a bit of player 2 is flipped on the
    client. Returns (games stopped, the frame of the checkpoint, the frame of the flipped bit)."""
    folder.mkdir(parents=True, exist_ok=True)
    feed = folder / "feed.txt"
    write_feed(feed, 6000, sway)
    env = {"SF64_COOP_LOCKSTEP_FEED": str(feed), "SF64_COOP_LOCKSTEP_END": str(SURVIVE_FRAMES), **both}
    games = session(args, folder, both=env, host_env=host_env,
                    client_envs=[{"SF64_COOP_LOCKSTEP_POKE_AFTER": str(after), "SF64_COOP_HASH_POKE_PLAYER": "1"}])
    try:
        survive_wait(games, "over", 1, 900, "after the flipped bit behind the checkpoint")
        survive_wait(games, "again", 1, SURVIVE_AGAIN_SECONDS, "after the desync: the level did not start again")
        survive_wait(games, "done", 2, 600, "in the level that started again")
        survive_no_end(games)
        time.sleep(2)
    finally:
        for game in games:
            game.close()
    reached = [join_test.said(game.sync(), "checkpoint") for game in games]
    expect(all(reached), "a game never reached a checkpoint")
    expect(reached[0][0] == reached[1][0], f"the games reached different checkpoints: {reached[0][0]}, {reached[1][0]}")
    at = int(reached[0][0]["frame"])
    return games, reached[0][0], at + after


def survive_check_checkpoint(games, checkpoint, poke, level, mode, saved_hits=True):
    """After a desync behind a checkpoint both games took a level start that begins at the host's checkpoint, began
    there, and are equal from the first frame of that run on. Returns a line. saved_hits: the level's setup takes its
    hits from the checkpoint's gSavedHitCount; Andross takes them from D_ctx_80177CA4 instead
    (src/engine/fox_play.c:4710, saved at the brain's arrival, src/overlays/ovl_i6/fox_andross.c:1120), and then the
    run only has to begin with the same hits on every game."""
    host, client = games
    for game in games:
        over = int(join_test.said(game.sync(), "over")[0]["frame"])
        expect(poke <= over <= poke + DESYNC_SEEN_WITHIN, f"{game.name}: lockstep stopped at {over}, poke at {poke}")
        expect([run for run, _ in survive_starts(game)] == [1, 2], f"{game.name}: level starts {survive_starts(game)}")
        first, second = survive_level_values(game, 1), survive_level_values(game, 2)
        expect(first[SURVIVE_CHECKPOINT_FLAG] == 0 and not any(first[SURVIVE_CHECKPOINT_FLAG:]),
               f"{game.name}: the first level start carries a checkpoint: {first[SURVIVE_CHECKPOINT_FLAG:]}")
        expect(second[SURVIVE_CHECKPOINT_FLAG] == 1, f"{game.name}: the second level start does not begin at the checkpoint: "
                                             f"{second[SURVIVE_CHECKPOINT_FLAG:]}")
        expect(second[SURVIVE_CHECKPOINT_FLAG + 1] == int(checkpoint["load"]) and
               second[SURVIVE_CHECKPOINT_FLAG + 4] == int(checkpoint["allrange"]) and
               second[SURVIVE_CHECKPOINT_FLAG + 2] == int(checkpoint["hits"]),
               f"{game.name}: the level start's checkpoint {second[SURVIVE_CHECKPOINT_FLAG:]} is not the one reached, "
               f"{checkpoint}")
        began, again = survive_run_state(game, 1), survive_run_state(game, 2)
        # A level's start: no checkpoint, and the path at 0 or behind it (Macbeth's tank begins at -770).
        expect(began["load"] == 0 and began["saved_load"] == 0 and began["path"] <= 0 and began["allrange"] == 0,
               f"{game.name}: the first run did not begin at the level's start: {began}")
        expect(again["level"] == level and again["mode"] == mode, f"{game.name}: run 2 is in {again}")
        expect(again["saved_load"] == int(checkpoint["load"]) and again["allrange"] == int(checkpoint["allrange"]),
               f"{game.name}: run 2 does not hold the checkpoint: {again}, want {checkpoint}")
        if int(checkpoint["load"]) != 0:
            # On rails the level's setup puts the players at the saved path progress (src/engine/fox_play.c:4734), and
            # five frames in they are a little past it.
            expect(again["load"] >= int(checkpoint["load"]) and
                   0 <= again["path"] - int(checkpoint["path"]) < 400,
                   f"{game.name}: run 2 did not begin at the checkpoint's place: {again}, want {checkpoint}")
        expect(not saved_hits or again["hits"] == int(checkpoint["hits"]),
               f"{game.name}: run 2 began with {again['hits']} hits, the checkpoint held {checkpoint['hits']}")
        expect(again["lives"] == began["lives"], f"{game.name}: the restart cost a life: {again['lives']}")
    expect(join_test.said(host.report(), "restart_at_checkpoint"), "the host did not take its checkpoint")
    expect(survive_run_state(host, 2) == survive_run_state(client, 2),
           f"the games began run 2 apart: {survive_run_state(host, 2)}, {survive_run_state(client, 2)}")
    frames = survive_compare(games, 2, SURVIVE_FRAMES)
    line = survive_first_difference(games, poke)
    again = survive_run_state(host, 2)
    return (f"checkpoint at frame {checkpoint['frame']} (object index {checkpoint['load']}, path {checkpoint['path']}, "
            f"all-range {checkpoint['allrange']}, {checkpoint['hits']} hits), poke at {poke}: both games took level "
            f"start 2 at the checkpoint, began there (object index {again['load']}, path {again['path']}, all-range "
            f"{again['allrange']}) and hash alike on its first {frames} frames; `{line}`")


def sc_survive_checkpoint(args, folder):
    """A desync after a checkpoint, on rails: the level starts again at the checkpoint (the owner's rule of
    2026-11-11). A checkpoint ring is put in front of player 2 on Corneria's rails (the lockstep driver's item), the
    game's own code saves the checkpoint when player 2 flies through it, and 300 frames later a bit is flipped on the
    client. Both games then take a level start that carries the host's checkpoint, begin the level there as the game
    does after a death, without a life taken, and are equal from that run's frame 0 on."""
    ring = JOIN_FRAME + 200
    games, checkpoint, poke = survive_checkpoint_run(
        args, folder, {"SF64_COOP_LOCKSTEP_ITEM": f"{ring}:1:{SURVIVE_ITEM_CHECKPOINT}"})
    # The driver holds the ring back until the level has loaded 40 objects: the game's own restart reads that many back.
    expect(ring <= int(checkpoint["frame"]) and int(checkpoint["load"]) > 40,
           f"the ring from frame {ring} on gave no rails checkpoint: {checkpoint}")
    return survive_check_checkpoint(games, checkpoint, poke, 0, 0)


def sc_survive_checkpoint_allrange(args, folder):
    """The same in an all-range level, with the game's own checkpoint: on Fortuna it is Star Wolf's arrival
    (gAllRangeCheckpoint, src/overlays/ovl_i4/fox_fo.c:300). 300 frames later a bit is flipped on the client; the
    level starts again with Star Wolf there, on both games alike."""
    games, checkpoint, poke = survive_checkpoint_run(args, folder, {}, host_env={"SF64_COOP_JOIN_LEVEL": str(SURVIVE_FORTUNA)})
    expect(int(checkpoint["allrange"]) == 1 and int(checkpoint["load"]) == 0,
           f"Fortuna's checkpoint is not the all-range one: {checkpoint}")
    return survive_check_checkpoint(games, checkpoint, poke, SURVIVE_FORTUNA, 1)


SURVIVE_MISSION_NUMBER = 89  # the level start value that holds gMissionNumber (level_start.c: 87 values, then the
SURVIVE_TOTAL_HITS = 88      # level's hits, gTotalHits, gMissionNumber)


def sc_survive_next_level(args, folder):
    """A desync in the level after the map: the restart point is the level the host's game entered by itself. Pilots
    fly Corneria to its end and through the map into the next level, and 200 frames into it a bit is flipped on the
    client. The host had kept that level's start when its game went into it (`entering` in its join report), so both
    games take a level start for that level, with the mission number and the total hits the campaign had built, and
    are equal from its frame 0 on. Corneria is not flown again."""
    folder.mkdir(parents=True, exist_ok=True)
    env = {"SF64_COOP_LOCKSTEP_PILOT": "1", "SF64_COOP_PILOT_LOG": "pilot-log.txt",
           "SF64_COOP_LOCKSTEP_END": str(SURVIVE_FRAMES)}
    games = session(args, folder, both=env, client_envs=[{"SF64_COOP_LOCKSTEP_POKE_IN_LEVEL": "2:200",
                                                          "SF64_COOP_HASH_POKE_PLAYER": "1"}])
    host, client = games
    try:
        survive_wait(games, "over", 1, args.level_seconds, "after the flipped bit in the level after the map")
        survive_wait(games, "again", 1, SURVIVE_AGAIN_SECONDS, "after the desync: the level did not start again")
        survive_wait(games, "done", 2, 600, "in the level that started again")
        survive_no_end(games)
        time.sleep(2)
    finally:
        for game in games:
            game.close()
    poked = join_test.said(client.sync(), "poke_in_level")
    expect(len(poked) == 1 and poked[0]["level"] != "0", f"the bit was not flipped in the level after the map: {poked}")
    poke, level = int(poked[0]["frame"]), int(poked[0]["level"])
    entering = join_test.said(host.report(), "entering")
    expect(entering and int(entering[-1]["level"]) == level, f"the host kept no restart point for level {level}: "
                                                              f"{entering}")
    for game in games:
        over = int(join_test.said(game.sync(), "over")[0]["frame"])
        expect(poke <= over <= poke + DESYNC_SEEN_WITHIN, f"{game.name}: lockstep stopped at {over}, poke at {poke}")
        starts = join_test.said(game.report(), "level_start")
        expect([(v["run"], v["level"]) for v in starts] == [("1", "0"), ("2", str(level))],
               f"{game.name}: level starts {[(v['run'], v['level']) for v in starts]}, want Corneria and then level "
               f"{level}")
        first, second = survive_level_values(game, 1), survive_level_values(game, 2)
        expect(first[SURVIVE_MISSION_NUMBER] == 0 and second[SURVIVE_MISSION_NUMBER] == 1,
               f"{game.name}: the mission number is {first[SURVIVE_MISSION_NUMBER]} in level start 1 and "
               f"{second[SURVIVE_MISSION_NUMBER]} in level start 2, want 0 and 1")
        expect(second[SURVIVE_TOTAL_HITS] > 0, f"{game.name}: level start 2 carries no hits of Corneria")
        expect(not any(second[SURVIVE_CHECKPOINT_FLAG:]), f"{game.name}: level start 2 carries a checkpoint")
        again = survive_run_state(game, 2)
        expect(again["level"] == level and again["load"] == 0 and again["path"] == 0 and again["hits"] == 0,
               f"{game.name}: run 2 did not begin at the start of level {level}: {again}")
    expect(survive_level_values(host, 2) == survive_level_values(client, 2), "the games took different level starts")
    frames = survive_compare(games, 2, SURVIVE_FRAMES)
    line = survive_first_difference(games, poke)
    total = survive_level_values(host, 2)[SURVIVE_TOTAL_HITS]
    return (f"Corneria to the map and into level {level}; poke at frame {poke}, 200 frames into it: both games took "
            f"level start 2 for level {level} with mission number 1 and {total} total hits, began at its start and "
            f"hash alike on its first {frames} frames; `{line}`")


# ---- #272: Macbeth's bomb fire and state-16 beams hit every tank (lane macbeth-bombs) ----
#
# src/port/coop/test/mabomb_probe.h places a bomb of the train's end, or a beam of its state 16, where one tank will be,
# while every other tank is at least 250 across the track, and logs whom the hazard's own update hit. Each kind runs for
# each tank in turn, W8_MABOMB_ROUNDS times. Upstream both tests look at player 0 only (Macbeth_MaBombDrop_Update,
# Macbeth_Effect380_Update in src/overlays/ovl_i5/fox_ma.c), so on a build without the fix of #272 a hazard on player 1
# hits nobody, while the cases on player 0 hit it on every build: they show the probe reaches its target.
W8_MABOMB_FRAMES = 4500  # level frames: the tanks part from JOIN_FRAME on (sway), 12 cases fit well after that
W8_MABOMB_CASES = ("bomb", "beam")  # sMaBombCases in src/port/coop/test/mabomb_probe.c
W8_MABOMB_WORDS = {"bomb": "under a bomb's fire", "beam": "in a beam"}
W8_MABOMB_ROUNDS = 3  # MABOMB_ROUNDS there
W8_MABOMB_PROBE = "mabomb.txt"  # in each game's run folder
W8_MABOMB_LINE = re.compile(r"mabomb frame=(\d+) case=(\w+) target=(\d+) hit=(\d+) first=(\S+) lost=(\d) dx=(\S+)")


def w8_mabomb_rows(path):
    """The probe's case lines as dicts, and whether it wrote its last line."""
    rows, done = [], False
    if not path.exists():
        return rows, done
    for line in path.read_text(encoding="utf-8").splitlines():
        match = W8_MABOMB_LINE.match(line)
        if match:
            rows.append({"frame": int(match.group(1)), "case": match.group(2), "target": int(match.group(3)),
                         "hit": int(match.group(4)), "first": [int(v) for v in match.group(5).split(",")],
                         "lost": match.group(6) == "1", "dx": float(match.group(7))})
        elif line.startswith("mabomb done"):
            done = True
    return rows, done


def w8_mabomb_story(rows):
    """(case, target) -> [cases that ran to their end, of those the ones that hit the target, the ones that hit another
    tank]. A lost case (the hazard gone early, as at a restart) does not count."""
    story = {(case, target): [0, 0, 0] for case in W8_MABOMB_CASES for target in (0, 1)}
    for row in rows:
        counts = story.setdefault((row["case"], row["target"]), [0, 0, 0])
        if row["lost"]:
            continue
        counts[0] += 1
        counts[1] += bool(row["hit"] & (1 << row["target"]))
        counts[2] += bool(row["hit"] & ~(1 << row["target"]))
    return story


def sc_macbeth_bombs(args, folder):
    """Macbeth with two Landmasters (#272): a bomb's fire and a state-16 beam of the train's end, placed on one tank by
    src/port/coop/test/mabomb_probe.h, hit that tank, player 1 as well as player 0, and no other. Both games sway as in
    `vehicles`, with their shields held (SF64_COOP_TRACE_SHIELDS) so that nobody goes down between cases; hashes and the
    two probe logs equal on every frame."""
    host, client = run_short(args, folder, script=sway, frames=W8_MABOMB_FRAMES,
                             host_env={"SF64_COOP_JOIN_LEVEL": "11"},
                             both={"SF64_COOP_MABOMB_PROBE": W8_MABOMB_PROBE, "SF64_COOP_TRACE_SHIELDS": "255,255"},
                             seconds=1800)
    frames = compare(host, client, W8_MABOMB_FRAMES)
    rows, done = w8_mabomb_rows(host.run / W8_MABOMB_PROBE)
    expect(rows, "the probe placed nothing: a build without src/port/coop/test/mabomb_probe.c (#272), or the tanks "
                 "never parted by 250")
    expect((rows, done) == w8_mabomb_rows(client.run / W8_MABOMB_PROBE), "the two games' probe logs differ")
    wanted = len(W8_MABOMB_CASES) * 2 * W8_MABOMB_ROUNDS
    expect(done, f"the probe ran {len(rows)} of {wanted} cases in {frames} frames")
    story = w8_mabomb_story(rows)
    lost = sum(row["lost"] for row in rows)
    for (case, target), (ran, _, _) in sorted(story.items()):
        expect(ran >= W8_MABOMB_ROUNDS - 1, f"{case} on player {target}: only {ran} cases ran to their end "
                                            f"({lost} lost in all)")

    def hits(target):
        return ", ".join(f"{W8_MABOMB_WORDS[case]} on {story[(case, target)][1]} of {story[(case, target)][0]}"
                         for case in W8_MABOMB_CASES)

    expect(all(story[(case, 0)][1] == story[(case, 0)][0] for case in W8_MABOMB_CASES),
           f"player 0, the control, is not hit every time: {hits(0)}")
    expect(all(story[(case, 1)][1] == story[(case, 1)][0] for case in W8_MABOMB_CASES),
           f"player 1 is not hit: {hits(1)}; player 0, the control: {hits(0)}")
    others = sum(counts[2] for counts in story.values())
    expect(others == 0, f"a hazard hit a tank it was not placed on in {others} cases (the tanks were too close)")
    first = sorted({row["first"][row["target"]] for row in rows if not row["lost"]})
    return (f"same hash on {frames} frames; {len(rows)} cases, {lost} lost; player 1 hit {hits(1)}; player 0 hit "
            f"{hits(0)}; no hazard hit the other tank; frames to the hit {first}")


# ---- #267: the session survives beyond its first level and its first three players (survive_gaps_*) ----
#
# Decision 0014 on more than Corneria and three games: a level that started again and is then flown to its end, the
# map and the level after it; a restart while the map is up; the game's own checkpoints in the other levels; Venom's
# jump; four players. The lockstep driver's places (lockstep_driver.h: L<n> the n-th level a game entered in a run, M<n>
# the n-th map) say where a bit is flipped or the input stops, and its `route` lines what each game holds there.

SURVIVE_GAPS_TAIL = 300  # frames of the level after the map, or after a jump, that have to hash alike
SURVIVE_GAPS_ON_MAP = 120  # frames into the map at which it goes wrong: well inside the map's 600 or so
SURVIVE_GAPS_REPLAY = 150000  # frames of the empty replay: longer than any run here, as a game leaves lockstep at its end
SURVIVE_GAPS_ROUTE_FIELDS = ("state", "level", "phase", "mission", "planets", "mission_hits", "medals", "team", "paths",
                             "total", "clear", "status", "lives", "save")
SURVIVE_GAPS_ENDING = 8  # GSTATE_ENDING
SURVIVE_GAPS_GAME_OVER = 5  # GSTATE_GAME_OVER


def survive_gaps_routes(game, run=None):
    """The `route` lines of a game's lockstep report (lockstep_driver.h), of run `run` or of all runs."""
    return [r for r in join_test.said(game.sync(), "route") if run is None or r["run"] == str(run)]


def survive_gaps_route_state(route):
    """What a route line says of the campaign and the save, without where and when it was taken."""
    return {key: route[key] for key in SURVIVE_GAPS_ROUTE_FIELDS}


def survive_gaps_route_change(before, after):
    """The fields of two route lines that differ, as a short text."""
    return ", ".join(key for key in SURVIVE_GAPS_ROUTE_FIELDS if before[key] != after[key]) or "nothing"


def survive_gaps_run_frames(game, run):
    """How many lockstep frames of run `run` a game's lockstep log holds; 0 before that run began."""
    lines = game.sync_log.read_text(encoding="utf-8").splitlines() if game.sync_log.exists() else []
    marks = [i for i, line in enumerate(lines) if line.startswith("# run ")]
    starts = [0] + [i + 1 for i in marks]
    ends = marks + [len(lines)]
    return ends[run - 1] - starts[run - 1] if len(starts) >= run else 0


def survive_gaps_watch(games, done, seconds, what, again_max=None):
    """Until done() is true. Fails as soon as a game is gone, a session ended, or a level started again more often than
    again_max: then nothing that is waited for can come."""
    deadline = time.monotonic() + seconds
    while not done():
        for game in games:
            expect(game.proc.poll() is None, f"{game.name} ended with code {game.proc.returncode} {what}")
            ended = [line for _, _, line in game.report() if line.startswith("ended")]
            expect(not ended, f"{game.name}'s session ended {what}: {ended[0][:300] if ended else ''}")
            again = len(join_test.said(game.sync(), "again"))
            expect(again_max is None or again <= again_max,
                   f"{game.name}: the level started again {again} times {what}, want at most {again_max}")
        expect(time.monotonic() < deadline, f"nothing after {seconds:.0f} s {what}")
        time.sleep(1)


def survive_gaps_compare(games, run, frames=None):
    """The hash logs and lockstep logs of run `run` are equal on every game from that run's frame 0: on `frames` frames,
    or on all that the shortest log holds of the run. Returns the frames compared."""
    names, ours = survive_run_rows(games[0], run)
    logs = survive_run_log(games[0], run)
    counted = None
    for other in games[1:]:
        _, theirs = survive_run_rows(other, run)
        theirs_log = survive_run_log(other, run)
        n = frames if frames is not None else min(len(ours), len(theirs), len(logs), len(theirs_log))
        expect(min(len(ours), len(theirs), len(logs), len(theirs_log)) >= n,
               f"run {run}: only {min(len(ours), len(theirs))} hash rows and {min(len(logs), len(theirs_log))} lockstep "
               f"frames of {games[0].name} and {other.name}, want {n}")
        difference = join_test.first_difference(names, ours[:n], theirs[:n])
        expect(difference is None, f"run {run}: the hash logs of {games[0].name} and {other.name} differ at {difference}")
        for i in range(n):
            expect(logs[i] == theirs_log[i], f"run {run}: the lockstep logs of {games[0].name} and {other.name} differ at "
                                             f"frame {i}: {logs[i]} against {theirs_log[i]}")
        counted = n if counted is None else min(counted, n)
    return counted


def survive_gaps_same_routes(games, run, places):
    """Every game wrote the same `route` lines in run `run`, at `places` in that order. Returns the host's."""
    ours = survive_gaps_routes(games[0], run)
    expect([r["place"] for r in ours] == places, f"run {run}: {games[0].name} was at {[r['place'] for r in ours]}, want "
                                                  f"{places}")
    for other in games[1:]:
        theirs = survive_gaps_routes(other, run)
        for a, b in zip(ours, theirs):
            expect(a == b, f"run {run}, {a['place']}: {games[0].name} and {other.name} hold different routes: "
                           f"{survive_gaps_route_change(a, b)}")
        expect(len(theirs) == len(ours), f"run {run}: {other.name} was at {[r['place'] for r in theirs]}")
    return ours


def survive_gaps_first_differences(games, pokes):
    """#189's line for each flipped bit (frames `pokes`, players 1 and 2): once in each game's own co-op log, the same
    in all games. Returns the lines."""
    found = []
    for poke in pokes:
        needle = f"first difference at frame {poke}, players 1 and 2: "
        texts = set()
        for game in games:
            own = coop_logs(game)[player_of(game)]
            lines = [line.format() for line in coop_log.read_lines(own, player_of(game)) if needle in line.format()]
            expect(len(lines) == 1, f"{game.name}'s co-op log says `{needle}` {len(lines)} times")
            texts.add(lines[0].split(needle, 1)[1])
        expect(len(texts) == 1, f"the games wrote different lines for frame {poke}: {sorted(texts)}")
        found.append(texts.pop())
    return found


def sc_survive_gaps_route(args, folder):
    """#267 item 1: a level that started again is flown to its end, then the map and the next level, twice: once in
    the session's first level and once in the level after the map, which the session starts again with the route it
    carries. Pilots fly (lockstep_driver.h), with gInvincible on but in Corneria as tools/coop/campaign.py has it, set on
    the same frame of every game. A bit of player 2 is flipped on the client in Corneria (run 1), so Corneria starts
    again (run 2) and is flown to the map and into the next level, where a second bit is flipped 200 frames in; that
    level starts again (run 3) and is flown to its end, the map and SURVIVE_GAPS_TAIL frames of the level after.
    Every frame of runs 2 and 3 up to the flipped bit is equal, the `route` lines (what the map and the next level read:
    mission number, planets, medals, paths, total hits, clear flags, lives and the save's progress) are equal on both
    games at every level and map, and each level that started again began with the route and the save it was first
    entered with."""
    folder.mkdir(parents=True, exist_ok=True)
    env = {"SF64_COOP_LOCKSTEP_PILOT": "1", "SF64_COOP_PILOT_LOG": "pilot-log.txt", "SF64_COOP_LOCKSTEP_END": "900000",
           "SF64_COOP_LOCKSTEP_ROUTE": "1", "SF64_COOP_LOCKSTEP_CAMPAIGN_CHEATS": "1"}
    pokes = f"1:L1:{JOIN_FRAME + 300},2:L2:200"
    games = session(args, folder, both=env, replay_frames=SURVIVE_GAPS_REPLAY,
                    client_envs=[{"SF64_COOP_LOCKSTEP_POKE_AT": pokes, "SF64_COOP_HASH_POKE_PLAYER": "1"}])
    host, client = games

    def next_level_flown():
        after = [r for r in survive_gaps_routes(host, 3) if r["place"] == "L2"]
        return bool(after) and survive_gaps_run_frames(host, 3) >= int(after[0]["frame"]) + SURVIVE_GAPS_TAIL

    try:
        survive_gaps_watch(games, next_level_flown, 2 * args.level_seconds,
                           "on the way through two restarts, two levels and two maps", again_max=2)
        time.sleep(2)
    finally:
        for game in games:
            game.close()
    poked = join_test.said(client.sync(), "poke_at")
    expect([(p["run"], p["place"]) for p in poked] == [("1", "L1"), ("2", "L2")] and poked[0]["level"] == "0" and
           poked[1]["level"] != "0", f"the bits were not flipped in Corneria and in the level after the map: {poked}")
    poke1, poke2, level2 = int(poked[0]["frame"]), int(poked[1]["frame"]), int(poked[1]["level"])
    for game in games:
        starts = join_test.said(game.report(), "level_start")
        expect([(v["run"], v["level"]) for v in starts] == [("1", "0"), ("2", "0"), ("3", str(level2))],
               f"{game.name}: level starts {[(v['run'], v['level']) for v in starts]}, want Corneria twice, then "
               f"level {level2}")
        # A client hears of a desync twice: from its own check and from the host (SESSION_RECOVER).
        recover = join_test.said(game.report(), "recover")
        expect(len(recover) >= 2 and all(int(v["reason"]) == SURVIVE_REASONS["desync"] for v in recover),
               f"{game.name}: recover {recover}")
        overs = [int(v["frame"]) for v in join_test.said(game.sync(), "over")]
        expect(len(overs) == 2 and poke1 <= overs[0] <= poke1 + DESYNC_SEEN_WITHIN and
               poke2 <= overs[1] <= poke2 + DESYNC_SEEN_WITHIN, f"{game.name}: lockstep stopped at {overs}, the bits "
                                                                f"were flipped at {poke1} and {poke2}")
        expect(len(own_reports(game)) == 2, f"{game.name} wrote {len(own_reports(game))} desync reports, want 2")
        first, second, third = (survive_level_values(game, run) for run in (1, 2, 3))
        expect(second == first, f"{game.name}: Corneria did not start again from the session's own level start")
        expect(third[SURVIVE_MISSION_NUMBER] == 1 and third[SURVIVE_TOTAL_HITS] > 0 and
               not any(third[SURVIVE_CHECKPOINT_FLAG:]),
               f"{game.name}: level start 3 holds mission number {third[SURVIVE_MISSION_NUMBER]}, total hits "
               f"{third[SURVIVE_TOTAL_HITS]}, checkpoint {third[SURVIVE_CHECKPOINT_FLAG:]}")
        for run, level in ((2, 0), (3, level2)):
            began = survive_run_state(game, run)
            expect(began["level"] == level and began["load"] == 0 and began["path"] == 0 and began["hits"] == 0,
                   f"{game.name}: run {run} did not begin at the start of level {level}: {began}")
    expect(len(join_test.said(host.report(), "recover")) == 2, f"host: {join_test.said(host.report(), 'recover')}")
    expect(survive_level_values(host, 3) == survive_level_values(client, 3), "the games took different level starts")
    survive_gaps_same_routes(games, 1, ["L1"])
    run2 = survive_gaps_same_routes(games, 2, ["L1", "M1", "L2"])
    run3 = survive_gaps_same_routes(games, 3, ["L1", "M1", "L2"])
    run1 = survive_gaps_routes(host, 1)
    expect(survive_gaps_route_state(run2[0]) == survive_gaps_route_state(run1[0]),
           f"Corneria started again with another route: {survive_gaps_route_change(run1[0], run2[0])}")
    expect(survive_gaps_route_state(run3[0]) == survive_gaps_route_state(run2[2]),
           f"level {level2} started again with another route than it was entered with: "
           f"{survive_gaps_route_change(run2[2], run3[0])}")
    expect(run3[1]["mission"] != run2[1]["mission"] or run3[1]["save"] != run2[1]["save"],
           "the map after the second level holds what the map after Corneria held")
    frames2 = survive_gaps_compare(games, 2, poke2)
    frames3 = survive_gaps_compare(games, 3)
    expect(frames3 >= int(run3[2]["frame"]) + SURVIVE_GAPS_TAIL, f"run 3: {frames3} frames compared")
    lines = survive_gaps_first_differences(games, [poke1, poke2])
    no_more = [r for r in survive_gaps_routes(host) if int(r["run"]) > 3]
    expect(not no_more, f"a fourth run: {no_more[:1]}")
    return (f"Corneria parted at {poke1} and started again from the session's level start (run 2); pilots flew it to "
            f"the map (frame {run2[1]['frame']}) and into level {level2} ({run2[2]['frame']}), which parted at "
            f"{poke2} and started again (run 3) with mission number {run3[0]['mission']} and {run3[0]['total']} total "
            f"hits; flown to the map ({run3[1]['frame']}) and into level {run3[2]['level']} ({run3[2]['frame']}). Equal "
            f"on {frames2} frames of run 2 and {frames3} of run 3; the route lines equal on both games at all 7 places; "
            f"each level that started again began with the route and save of its first entry; the map after Corneria "
            f"changed {survive_gaps_route_change(run2[0], run2[1])}; `{lines[1]}`")


def survive_gaps_map_session(args, folder, client_env):
    """Pilots fly Corneria to the map, where client_env makes the client go wrong, and the level starts again. Returns
    the two games, stopped once both flew SURVIVE_FRAMES frames of the level that started again."""
    folder.mkdir(parents=True, exist_ok=True)
    env = {"SF64_COOP_LOCKSTEP_PILOT": "1", "SF64_COOP_PILOT_LOG": "pilot-log.txt",
           "SF64_COOP_LOCKSTEP_END": str(SURVIVE_FRAMES), "SF64_COOP_LOCKSTEP_ROUTE": "1"}
    games = session(args, folder, both=env, client_envs=[client_env])
    try:
        survive_gaps_watch(games, lambda: all(join_test.said(g.sync(), "again") for g in games), args.level_seconds,
                           "before the level started again from the map", again_max=1)
        survive_wait(games, "done", 2, 600, "in the level that started again")
        survive_no_end(games)
        time.sleep(2)
    finally:
        for game in games:
            game.close()
    return games


def survive_gaps_map_check(games):
    """What a restart from the map has to show on both games: Corneria again from the session's own level start, with
    the route and the save it began with, equal on its first SURVIVE_FRAMES frames. Returns (frames, the change the map
    had made to the route)."""
    host, client = games
    for game in games:
        starts = join_test.said(game.report(), "level_start")
        expect([(v["run"], v["level"]) for v in starts] == [("1", "0"), ("2", "0")],
               f"{game.name}: level starts {[(v['run'], v['level']) for v in starts]}, want Corneria twice")
        expect(survive_level_values(game, 2) == survive_level_values(game, 1),
               f"{game.name}: Corneria did not start again from the session's own level start")
        began = survive_run_state(game, 2)
        expect(began["level"] == 0 and began["load"] == 0 and began["path"] == 0 and began["hits"] == 0,
               f"{game.name}: run 2 did not begin at Corneria's start: {began}")
    expect(not join_test.said(host.report(), "restart_at_checkpoint"), "the host took a checkpoint on the map")
    run1 = survive_gaps_same_routes(games, 1, ["L1", "M1"])
    run2 = survive_gaps_same_routes(games, 2, ["L1"])
    expect(survive_gaps_route_state(run2[0]) == survive_gaps_route_state(run1[0]),
           f"Corneria started again with another route than it began with: {survive_gaps_route_change(run1[0], run2[0])}")
    return survive_compare(games, 2, SURVIVE_FRAMES), survive_gaps_route_change(run1[0], run1[1])


def sc_survive_gaps_map(args, folder):
    """#267 item 2: the games part while the map is up. Pilots fly Corneria to the map; SURVIVE_GAPS_ON_MAP frames into
    it a bit is flipped on the client (the hash's own target, the last actor slot, which the map hashes too). Decision
    0014 item 2 says the level the team last entered: both games take a level start for Corneria, the session's own,
    so the map's changes to the route and the save are dropped, and the level is equal from its frame 0 on."""
    games = survive_gaps_map_session(args, folder, {"SF64_COOP_LOCKSTEP_POKE_AT": f"1:M1:{SURVIVE_GAPS_ON_MAP}"})
    host, client = games
    poked = join_test.said(client.sync(), "poke_at")
    expect(len(poked) == 1 and poked[0]["place"] == "M1" and int(poked[0]["state"]) == MAP,
           f"the bit was not flipped on the map: {poked}")
    poke = int(poked[0]["frame"])
    for game in games:
        over = int(join_test.said(game.sync(), "over")[0]["frame"])
        expect(poke <= over <= poke + DESYNC_SEEN_WITHIN, f"{game.name}: lockstep stopped at {over}, poke at {poke}")
        recover = join_test.said(game.report(), "recover")
        expect(recover and all(int(v["reason"]) == SURVIVE_REASONS["desync"] for v in recover),
               f"{game.name}: recover {recover}")
    expect(len(join_test.said(host.report(), "recover")) == 1, f"host: {join_test.said(host.report(), 'recover')}")
    parted = next(line for _, _, line in host.report() if line.startswith("parted"))
    frames, changed = survive_gaps_map_check(games)
    line = survive_first_difference(games, poke)  # one report each: none in the level that started again
    said = parted.split("text=", 1)[1].split(" A report", 1)[0][:160]
    return (f"bit flipped {SURVIVE_GAPS_ON_MAP} frames into the map (frame {poke}): `{said}`; both games took level "
            f"start 2 for Corneria, the session's own, began it with the route it began with (the map had changed "
            f"{changed}) and hash alike on its first {frames} frames; `{line}`")


def sc_survive_gaps_map_lost(args, folder):
    """#267 item 2, a lost player: the client stops sending input SURVIVE_GAPS_ON_MAP frames into the map after
    Corneria, with its connection up (as `stall`). The host drops it as a lost player with its slot kept, the client
    sees the host close and comes back with the session id, and Corneria starts again for both from the session's own
    level start, as after a desync on the map."""
    games = survive_gaps_map_session(args, folder, {"SF64_COOP_LOCKSTEP_MUTE_AT": f"1:M1:{SURVIVE_GAPS_ON_MAP}"})
    host, client = games
    muted = join_test.said(client.sync(), "mute_at")
    expect(len(muted) == 1 and int(muted[0]["state"]) == MAP, f"the input did not stop on the map: {muted}")
    mute = int(muted[0]["frame"])
    over = int(join_test.said(host.sync(), "over")[0]["frame"])
    expect(mute <= over <= mute + 2 * INPUT_DELAY, f"the host ran to frame {over}, input stopped at {mute}")
    parted = [line for _, _, line in host.report() if line.startswith("parted")]
    expect(parted and "reason=2" in parted[0] and "stopped sending input" in parted[0], f"host: {parted}")
    left = join_test.said(host.report(), "left")
    expect(left and left[0]["away"] == "1", f"host: the slot of the player it dropped was not kept: {left}")
    recover = [line for _, _, line in host.report() if line.startswith("recover")]
    expect(recover and "Player 2 lost the connection" in recover[0], f"host: {recover}")
    back = [v for v in join_test.said(client.report(), "joined") if v.get("rejoined") == "1"]
    expect(len(back) == 1 and back[0]["player"] == "1", f"the client did not come back as player 2: {back}")
    for game in games:
        expect(survive_starts(game) == [(1, "Fox,Falco,-,-"), (2, "Fox,Falco,-,-")],
               f"{game.name}: level starts {survive_starts(game)}")
    frames, changed = survive_gaps_map_check(games)
    no_desync(games)
    return (f"input stopped {SURVIVE_GAPS_ON_MAP} frames into the map (frame {mute}); the host stopped at {over}, kept "
            f"player 2's slot, the client came back with the session id, both took Corneria's own level start again, "
            f"began it with the route it began with (the map had changed {changed}) and hash alike on its first "
            f"{frames} frames")


def sc_survive_gaps_four(args, folder):
    """#267 item 4, decision 0014 items 1 and 3 with four players: a lost connection starts the level again for those
    who are there once the host has waited 15 s. Four games in a level; one is killed (its connection is gone for good),
    and the level starts again for the three who stay, without it; then a second is killed, and it starts again for the
    two who are left. Each run is equal on every game that took it, on its first JOIN_FRAME + 300 frames."""
    folder.mkdir(parents=True, exist_ok=True)
    feed = folder / "feed.txt"
    end = JOIN_FRAME + 300
    write_feed(feed, 3 * SHORT_FRAMES, sway)
    env = {"SF64_COOP_LOCKSTEP_FEED": str(feed), "SF64_COOP_LOCKSTEP_END": str(end)}
    games = session(args, folder, both=env, clients=3)
    host, a, b, c = games
    try:
        wait_for(list(games), "done", 900, "with four players")
        c.stop(kill=True)
        survive_wait([host, a, b], "again", 1, SURVIVE_AGAIN_SECONDS, "after the first player was lost")
        survive_wait([host, a, b], "done", 2, 900, "in the level that started again for three")
        b.stop(kill=True)
        survive_wait([host, a], "again", 2, SURVIVE_AGAIN_SECONDS, "after the second player was lost")
        survive_wait([host, a], "done", 3, 900, "in the level that started again for two")
        survive_no_end([host, a])
    finally:
        for game in games:
            game.close()
    roster = survive_starts(host)[0][1].split(",")
    expect(sorted(roster) == ["Falco", "Fox", "Peppy", "Slippy"], f"the first level start's roster is {roster}")
    three, two = list(roster), list(roster)
    three[player_of(c) - 1] = "-"
    two[player_of(c) - 1] = two[player_of(b) - 1] = "-"
    want = [(1, ",".join(roster)), (2, ",".join(three)), (3, ",".join(two))]
    for game in (host, a):
        expect(survive_starts(game) == want, f"{game.name}: level starts {survive_starts(game)}, want {want}")
    expect(survive_starts(b) == want[:2], f"{b.name}: level starts {survive_starts(b)}, want {want[:2]}")
    recover = [line for _, _, line in host.report() if line.startswith("recover")]
    for i, lost in enumerate((c, b)):
        expect(len(recover) > i and "reason=1" in recover[i] and
               f"Player {player_of(lost)} lost the connection. Waiting for it to come back (15 s)" in recover[i],
               f"host: {recover}")
    gone = [v for v in join_test.said(host.report(), "left") if v["away"] == "0"]
    expect(len(gone) == 2, f"host: {len(gone)} players gone for good after the wait, want 2: "
                           f"{join_test.said(host.report(), 'left')}")
    frames1 = survive_compare(list(games), 1, end)
    frames2 = survive_compare([host, a, b], 2, end)
    frames3 = survive_compare([host, a], 3, end)
    no_desync([host, a])
    return (f"four games ({','.join(roster)}): player {player_of(c)} lost, the level started again for three "
            f"({want[1][1]}), then player {player_of(b)} lost and it started again for two ({want[2][1]}); equal on "
            f"{frames1} frames with four, {frames2} with three and {frames3} with two")


# Levels with a checkpoint of their own, found in the code and the level object tables of the US 1.0 ROM (#267,
# NOTES.md): (LevelId, phase, frame cap of the level, what the checkpoint is). A rails ring is OBJ_ITEM_CHECKPOINT at
# that index of the level's table (ItemCheckpoint_Update, src/engine/fox_enmy.c:2508); all-range: gAllRangeCheckpoint.
SURVIVE_GAPS_LEVELS = {
    "corneria": (0, 0, 26000, "ring, object 369"),
    "meteo": (1, 0, 26000, "ring, object 479"),
    "sector_x": (2, 0, 26000, "ring, object 275"),
    "area_6": (3, 0, 40000, "ring, object 250"),
    "sector_y": (5, 0, 36000, "ring, object 215"),
    "venom_1": (6, 0, 26000, "ring, object 1164"),
    "solar": (7, 0, 26000, "ring, object 92"),
    "zoness": (8, 0, 26000, "ring, object 271"),
    "macbeth": (11, 0, 26000, "ring, object 313"),
    "titania": (12, 0, 60000, "ring, object 350"),
    "aquas": (13, 0, 26000, "ring, object 189"),
    "katina": (16, 0, 26000, "the Saucerer's arrival, src/overlays/ovl_i4/fox_ka.c:943"),
    "bolse": (17, 0, 40000, "the core's shield down, src/overlays/ovl_i4/fox_bo.c:385 and :309"),
    "andross": (9, 1, 26000, "the brain's arrival, src/overlays/ovl_i6/fox_andross.c:1084"),
}


# What runs without --levels: every level the pilots take to its end from the checkpoint, the rails with their own
# ring put where player 2 flies. Andross' second part runs with --levels andross: there the pilots run out of lives in
# the brain fight (2026-11-12).
SURVIVE_GAPS_DEFAULT = [f"{name}@ring" for name in ("corneria", "meteo", "sector_x", "area_6", "sector_y", "venom_1",
                                                      "solar", "zoness", "macbeth", "titania", "aquas")] + ["katina",
                                                                                                             "bolse"]


def survive_gaps_checkpoint_in(args, folder, name):
    """One level of survive_gaps_checkpoints. Returns its line. `<level>@ring`: the level's own checkpoint ring is put
    across the path where player 2 flies (SF64_COOP_LOCKSTEP_RING_TO, its depth unchanged), as the pilot does not steer
    for rings: the checkpoint is still the level's own, at its object index and path. `<level>@testring`: a ring the
    driver makes in front of player 2 once the level has loaded 40 objects (item kind 3, as survive_checkpoint), which
    builds from before RING_TO have too (a run against the reference build)."""
    name, _, how = name.partition("@")
    level, phase, cap, what = SURVIVE_GAPS_LEVELS[name]
    if how == "ring":
        what = f"{what}, put where player 2 flies"
    elif how == "testring":
        what = f"a test ring from frame {JOIN_FRAME + 1000}"
    cvars = {} if name == "corneria" else {"gInvincible": 1}  # as tools/coop/campaign.py flies a level
    if name in ("zoness", "andross"):
        cvars["gInfiniteBombs"] = 1
    folder.mkdir(parents=True, exist_ok=True)
    env = {"SF64_COOP_LOCKSTEP_PILOT": "1", "SF64_COOP_PILOT_LOG": "pilot-log.txt", "SF64_COOP_LOCKSTEP_END": str(cap),
           "SF64_COOP_LOCKSTEP_ROUTE": "1"}
    if how == "ring":
        env["SF64_COOP_LOCKSTEP_RING_TO"] = "1"
    elif how == "testring":
        env["SF64_COOP_LOCKSTEP_ITEM"] = f"{JOIN_FRAME + 1000}:1:{SURVIVE_ITEM_CHECKPOINT}"
    games = session(args, folder, both=env, config={"CVars": cvars} if cvars else None, replay_frames=SURVIVE_GAPS_REPLAY,
                    host_env={"SF64_COOP_JOIN_LEVEL": str(level), "SF64_COOP_JOIN_PHASE": str(phase)},
                    client_envs=[{"SF64_COOP_LOCKSTEP_POKE_AFTER": "300", "SF64_COOP_HASH_POKE_PLAYER": "1"}])
    host, client = games

    def left_routes(run):
        # The map, or another level (a jump); not the same level set up again inside itself (Andross' brain, frame
        # 977 of a run: one frame of PLAY_INIT).
        return [r for r in survive_gaps_routes(host, run) if r["place"].startswith("M") or
                (r["place"] != "L1" and (r["level"], r["phase"]) != (str(level), str(phase)))]

    def run1_over():
        left = left_routes(1)
        expect(not left, f"{name}: the level ended at frame {left[0]['frame'] if left else 0} without a checkpoint")
        expect(not join_test.said(host.sync(), "done"), f"{name}: no checkpoint in {cap} frames")
        return all(join_test.said(g.sync(), "again") for g in games)

    def run2_left():
        # The map, the level a jump goes to, or what follows Andross: the ending.
        expect(len(join_test.said(host.sync(), "done")) < 2, f"{name}: the level did not end in {cap} frames of run 2")
        left = left_routes(2)
        if left:
            return survive_gaps_run_frames(host, 2) >= int(left[0]["frame"]) + 60
        rows = survive_run_log(host, 2) if survive_gaps_run_frames(host, 2) else []
        ending = [i for i, row in enumerate(rows) if row.split()[1] == str(SURVIVE_GAPS_ENDING)]
        return bool(ending) and len(rows) >= ending[0] + 60

    try:
        survive_gaps_watch(games, run1_over, args.level_seconds, f"in {name} before the checkpoint", again_max=1)
        survive_gaps_watch(games, run2_left, args.level_seconds, f"in {name} after the checkpoint", again_max=1)
        time.sleep(2)
    finally:
        for game in games:
            game.close()
    reached = [v for v in join_test.said(host.sync(), "checkpoint") if v["run"] == "1"]
    expect(reached, f"{name}: no checkpoint")
    poke = int(reached[0]["frame"]) + 300  # SF64_COOP_LOCKSTEP_POKE_AFTER: after the first checkpoint
    # The level start carries the checkpoint the host held when the games parted: the last one before the bit.
    before = [v for v in reached if int(v["frame"]) <= poke]
    theirs = [v for v in join_test.said(client.sync(), "checkpoint") if v["run"] == "1"][:len(before)]
    expect(theirs == before, f"{name}: the games reached different checkpoints: {before}, {theirs}")
    checkpoint = before[-1]
    mode = 0 if int(checkpoint["load"]) != 0 else 1
    line = survive_check_checkpoint(games, checkpoint, poke, level, mode, saved_hits=name != "andross")
    frames = survive_gaps_compare(games, 2)
    left = left_routes(2)
    # Out of lives, the game goes to its game over and on to the map: that is no end of the level.
    over = [i for i, row in enumerate(survive_run_log(host, 2)) if row.split()[1] == str(SURVIVE_GAPS_GAME_OVER)]
    expect(not over, f"{name} ({what}): the restart held, {line}, equal on all {frames} frames; but the team ran out of "
                     f"lives at frame {over[0] if over else 0} of run 2 (game over): the pilot did not end the level")
    end = (f"the map at frame {left[0]['frame']}" if left and left[0]["place"].startswith("M") else
           f"level {left[0]['level']} (a jump) at frame {left[0]['frame']}" if left else "the ending")
    return f"{name}{'@' + how if how else ''} ({what}): {line}; run 2 flew on to {end}, equal on all {frames} frames"


def sc_survive_gaps_checkpoints(args, folder):
    """#267 item 3: a restart from the game's own checkpoint in the levels beyond Corneria's rails (--levels, default
    SURVIVE_GAPS_DEFAULT; the names are those of SURVIVE_GAPS_LEVELS). Pilots fly the level from its start (gInvincible
    on but in Corneria, gInfiniteBombs in Zoness and Andross, as tools/coop/campaign.py); 300 frames after the first
    checkpoint a bit of player 2 is flipped on the client. Both games take a level start at the host's checkpoint, begin
    there as after a death with no life taken (survive_check_checkpoint), and fly on to the level's end, the map or the
    level a jump goes to (a game over is no end): equal on every frame of that run. One session per level; the failures
    are named together. `<level>@ring` and `<level>@testring`: survive_gaps_checkpoint_in."""
    names = args.levels or SURVIVE_GAPS_DEFAULT
    unknown = [name for name in names if name.partition("@")[0] not in SURVIVE_GAPS_LEVELS or
               name.partition("@")[2] not in ("", "ring", "testring")]
    expect(not unknown, f"no level {', '.join(unknown)}; the choices are {', '.join(SURVIVE_GAPS_LEVELS)}")
    lines, failed = [], []
    for name in names:
        try:
            lines.append(survive_gaps_checkpoint_in(args, folder / name, name))
            print(f"        {lines[-1]}", flush=True)
        except (Fail, join_test.Fail) as failure:
            failed.append(f"{name}: {failure}")
            print(f"        FAILED {name}: {failure}", flush=True)
    expect(not failed, "; ".join(failed))
    return f"{len(lines)} levels: " + " | ".join(lines)


def sc_survive_gaps_venom_jump(args, folder):
    """#267 item 3, Venom 2's second part: Venom 1 ends with a jump into it (gNextGameState with Venom 2, phase 2,
    src/overlays/ovl_i1/fox_ve1.c:2521), and the host takes its restart point there (CoopLevelStart_Entering), which no
    scenario had run. Pilots fly Venom 1 (gInvincible on both); 60 frames into the second part a bit is flipped on the
    client (the last actor slot: the player poke shows only for a player in flight). Both games take a level start for
    Venom 2, phase 2, begin it as the jump did, with the route it was entered with, and fly on into the level it jumps
    to: equal on every frame of that run."""
    folder.mkdir(parents=True, exist_ok=True)
    env = {"SF64_COOP_LOCKSTEP_PILOT": "1", "SF64_COOP_PILOT_LOG": "pilot-log.txt", "SF64_COOP_LOCKSTEP_END": "60000",
           "SF64_COOP_LOCKSTEP_ROUTE": "1"}
    games = session(args, folder, both=env, config={"CVars": {"gInvincible": 1}}, replay_frames=SURVIVE_GAPS_REPLAY,
                    host_env={"SF64_COOP_JOIN_LEVEL": "6"}, client_envs=[{"SF64_COOP_LOCKSTEP_POKE_AT": "1:L2:60"}])
    host, client = games

    def next_level_flown():
        after = [r for r in survive_gaps_routes(host, 2) if r["place"] == "L2"]
        return bool(after) and survive_gaps_run_frames(host, 2) >= int(after[0]["frame"]) + SURVIVE_GAPS_TAIL

    try:
        survive_gaps_watch(games, next_level_flown, 2 * args.level_seconds, "through Venom 1, its jump and a restart",
                           again_max=1)
        time.sleep(2)
    finally:
        for game in games:
            game.close()
    poked = join_test.said(client.sync(), "poke_at")
    expect(len(poked) == 1 and poked[0]["place"] == "L2" and poked[0]["level"] == "19",
           f"the bit was not flipped in Venom 2's second part: {poked}")
    poke = int(poked[0]["frame"])
    entering = join_test.said(host.report(), "entering")
    expect([(v["level"], v["phase"]) for v in entering][:1] == [("19", "2")],
           f"the host took no restart point at the jump: {entering}")
    for game in games:
        starts = join_test.said(game.report(), "level_start")
        expect([(v["run"], v["level"], v["phase"]) for v in starts] == [("1", "6", "0"), ("2", "19", "2")],
               f"{game.name}: level starts {[(v['run'], v['level'], v['phase']) for v in starts]}")
        over = int(join_test.said(game.sync(), "over")[0]["frame"])
        expect(poke <= over <= poke + DESYNC_SEEN_WITHIN, f"{game.name}: lockstep stopped at {over}, poke at {poke}")
    expect(survive_level_values(host, 2) == survive_level_values(client, 2), "the games took different level starts")
    run1 = survive_gaps_same_routes(games, 1, ["L1", "L2"])
    run2 = survive_gaps_same_routes(games, 2, ["L1", "L2"])
    expect(survive_gaps_route_state(run2[0]) == survive_gaps_route_state(run1[1]),
           f"Venom 2's second part started again with another route: {survive_gaps_route_change(run1[1], run2[0])}")
    frames = survive_gaps_compare(games, 2)
    line = survive_first_difference(games, poke)
    return (f"Venom 1 jumped into Venom 2's second part at frame {run1[1]['frame']} (the host's restart point: level 19, "
            f"phase 2); bit flipped at {poke}; both games took level start 2 there, began it with the route it was "
            f"entered with, and flew on into level {run2[1]['level']} (phase {run2[1]['phase']}, frame "
            f"{run2[1]['frame']}): equal on all {frames} frames; `{line}`")


SCENARIOS = {
    "sim": sc_sim,
    "desync_sim": sc_desync_sim,
    "steer": sc_steer,
    "unlimited": sc_unlimited,
    "aim": sc_aim,
    "boost": sc_boost,
    "return": sc_return,
    "return_all_range": sc_return_all_range,
    "return_fox_down": sc_return_fox_down,
    "fox_refill": sc_fox_refill,
    "fox_refill_all_range": sc_fox_refill_all_range,
    "fox_refill_levels": sc_fox_refill_levels,
    "fox_refill_long": sc_fox_refill_long,
    "fox_lead_swap": sc_fox_lead_swap,
    "fox_lead_swap_all_range": sc_fox_lead_swap_all_range,
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
    "results": sc_results,
    "results_hole": sc_results_hole,
    "guests": sc_guests,
    "allrange": sc_allrange,
    "allrange_down": sc_allrange_down,
    "allrange_others": sc_allrange_others,
    "allrange_down_others": sc_allrange_down_others,
    "teambar": sc_teambar,
    "map_saves": sc_map_saves,
    "map_saves_host_full": sc_map_saves_host_full,
    "map_saves_equal": sc_map_saves_equal,
    "save_own": sc_save_own,
    "audio": sc_audio,
    "release": sc_release,
    "rates": sc_rates,
    "desync": sc_desync,
    "desync_control": sc_desync_control,
    "desync_three": sc_desync_three,
    "handover": sc_handover,
    "log": sc_log,
    "survive_desync": sc_survive_desync,
    "survive_three": sc_survive_three,
    "survive_checkpoint": sc_survive_checkpoint,
    "survive_checkpoint_allrange": sc_survive_checkpoint_allrange,
    "survive_next_level": sc_survive_next_level,
    "survive_gaps_route": sc_survive_gaps_route,
    "survive_gaps_map": sc_survive_gaps_map,
    "survive_gaps_map_lost": sc_survive_gaps_map_lost,
    "survive_gaps_checkpoints": sc_survive_gaps_checkpoints,
    "survive_gaps_venom_jump": sc_survive_gaps_venom_jump,
    "survive_gaps_four": sc_survive_gaps_four,
    "vehicles": sc_vehicles,
    "vehicle_end": sc_vehicle_end,
    "bosses": sc_bosses,
    "other_bosses": sc_other_bosses,
    "sy_robot_spread": sc_sy_robot_spread,
    "w7macbeth_ram": sc_w7macbeth_ram,
    "eels": sc_eels,
    "player_names": sc_player_names,
    "macbeth_bombs": sc_macbeth_bombs,
}
LONG = ("survive_next_level", "survive_gaps_route", "survive_gaps_map", "survive_gaps_map_lost",
        "survive_gaps_checkpoints", "survive_gaps_venom_jump", "fox_refill_levels", "fox_refill_long", "fox_lead_swap", "fox_lead_swap_all_range", "full", "scenes", "allrange", "allrange_down", "allrange_others", "allrange_down_others", "teambar",
        "map_saves", "map_saves_host_full", "map_saves_equal", "audio", "release", "rates", "vehicles", "vehicle_end", "bosses", "other_bosses",
        "eels", "w7macbeth_ram")
LONG += ("sy_robot_spread",)  # Sector Y's rails, 9500 frames (#273)


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
                    help="for `fox_refill_levels`, `fox_refill_long` and `fox_lead_swap`: some of "
                         + ",".join(FOX_REFILL_LEVELS) + " (and corneria for fox_refill_long); "
                         "for `vehicles` and `vehicle_end`: some of titania,aquas,macbeth (default all); for "
                         "`bosses`: some of gorgon,andross,dogfight,inhale; for `other_bosses`: some of "
                         + ",".join(OTHER_BOSS_RUNS) + "; for `survive_gaps_checkpoints`: some of "
                         + ",".join(SURVIVE_GAPS_LEVELS))
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
