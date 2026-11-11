#!/usr/bin/env python3
"""The campaign in co-op, scripted (WP-32, #28): two real game clients per level, a pilot on each, the levels in the
order of a route, the state hashes compared on every frame, and one summary at the end.

    python tools/coop/campaign.py                         # every campaign level, the route `all`; unattended
    python tools/coop/campaign.py --route owner           # the route the owner and a friend played on 2026-11-09
    python tools/coop/campaign.py --levels meteo,bolse    # only these, in this order
    python tools/coop/campaign.py --lock <file>           # hold an flock on <file> for each level's session (Linux)
    python tools/coop/campaign.py --list

Each level is a session of its own: a host and a client through the join driver (src/port/coop/test/join_driver.h),
the level started with SF64_COOP_JOIN_LEVEL as the lobby's "Start at" does (#211), and SF64_COOP_LOCKSTEP_PILOT on both,
so each client's pilot (src/port/coop/test/pilot.h) flies its own player and its pad goes the road a pad takes:
stamped, delayed, sent, read back from lockstep. So a level started this way carries no route state: lives, score,
lasers, team shields and the map's medals start as for a new game, and a route here is only the order of the runs.
gInvincible is on in both games unless --mortal, and but for Corneria (MORTAL): what is tested is the level, its boss
and its end with two players, not the pilot. A player can go down where it is off, and the summary counts it. Zoness and
Venom/Andross also run with gInfiniteBombs (BOMBS), as their bosses need bombs, and their summary lines say so.

A level passes when both games stay equal on every frame, hash log and input log (lockstep_test.compare: every region,
no desync report), from the first frame in the level to the map or ending after it, and player 0 reached level
complete. The cameras differ: each game looks through its own player. A level the pilot cannot finish within its frame
cap is reported as that, with the frames that were compared.

The games run at the speed of games that meet over sockets (replay.MEETING_SPEED), silent and without a window. A whole
route takes hours. Exit code 0 when every level passed.
"""
import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import join_test  # noqa: E402
import lockstep_test  # noqa: E402
import player_trace  # noqa: E402
import replay as replay_tool  # noqa: E402

# LevelId, include/sf64level.h; the names of hash_check.py's VIEW_LEVELS.
LEVELS = {
    "corneria": 0, "meteo": 1, "sector-x": 2, "area-6": 3, "sector-y": 5, "venom-1": 6, "solar": 7, "zoness": 8,
    "venom-andross": 9, "macbeth": 11, "titania": 12, "aquas": 13, "fortuna": 14, "katina": 16, "bolse": 17,
    "sector-z": 18, "venom-2": 19,
}
# The map's paths (sPaths, src/overlays/ovl_menu/fox_map.c:455-480): blue is Corneria to Bolse and Venom 1, red is
# Corneria to Area 6 and Venom 2; both end with Andross. `owner` is the route played by hand on 2026-11-09 (issue #2).
# `all` is blue, then what red and the other paths add.
ROUTES = {
    "blue": ("corneria", "meteo", "fortuna", "sector-x", "titania", "bolse", "venom-1", "venom-andross"),
    "red": ("corneria", "sector-y", "aquas", "zoness", "sector-z", "area-6", "venom-2", "venom-andross"),
    "owner": ("corneria", "meteo", "fortuna", "solar", "macbeth", "area-6", "venom-2", "venom-andross"),
    "all": ("corneria", "meteo", "fortuna", "sector-x", "titania", "bolse", "venom-1", "venom-andross", "sector-y",
            "aquas", "zoness", "sector-z", "area-6", "venom-2", "katina", "solar", "macbeth"),
}
FRAMES = 26000  # lockstep frames a level may take at most: Corneria took about 11200 to the map, Sector X 11529 to
FRAMES_BY_LEVEL = {"titania": 60000, "sector-y": 36000, "area-6": 40000}  # level complete; Titania's Goras 29169
# in #16's run and past 40000 in #28's first, Sector Y's last Shogun and Area 6's Gorgon longer than 24000 in #181's
# views --pilot
# Levels flown without gInvincible: Corneria, as lockstep_test.py's `full` flies it. With gInvincible on both games,
# Granga lay still with a leg shot off and its backpack at 110 for 20000 frames in #28's first run, out of the pilot's
# reach (src/port/coop/test/pilot.h); without it the same pilot takes Corneria to the map.
MORTAL = {"corneria"}
# Levels flown with gInfiniteBombs as well (owner's choice of 2026-11-11, campaign run): their bosses take bombs where
# the pilot's shots do not reach or do not count. Zoness' Sarumarine takes only bombs on its parts until they are down
# (src/overlays/ovl_i3/fox_zo.c), and Andross' hands rest wide of the path or under its floor, within a bomb's burst but
# out of a shot's reach (src/overlays/ovl_i6/fox_andross.c). The port's cheat keeps player 0 at 9 bombs
# (src/port/mods/PortEnhancements.c), a simulation CVar the two games agree on (src/port/coop/session/sim_cvars.c); the
# pilot bombs with player 0 there (src/port/coop/test/pilot_aim.c). What is tested is still the level, its boss and its
# end with two players.
BOMBS = {"zoness", "venom-andross"}
TAIL = 60  # frames kept after the game leaves the level
END_TAIL = 1800  # or after level complete, whichever comes first
LEVEL_COMPLETE, DOWN, GS_PLAY = 7, 4, 7


def lock(path):
    """An flock on path, held until the returned file is closed; None where there is no fcntl."""
    if path is None:
        return None
    try:
        import fcntl
    except ImportError:
        print("campaign: --lock needs fcntl, which this system lacks; running without it")
        return None
    handle = open(path, "a")
    fcntl.flock(handle, fcntl.LOCK_EX)
    return handle


def lines_from(path, at):
    """New whole lines of a file another process writes. Returns (lines, new offset)."""
    if not path.exists():
        return [], at
    with open(path, "rb") as f:
        f.seek(at)
        data = f.read()
    whole = data[:data.rfind(b"\n") + 1]
    return whole.decode("utf-8", errors="replace").splitlines(), at + len(whole)


def players_of(game, level, players=2):
    """Per player, from a game's trace: (frames flown, times down, lowest shields)."""
    rows = [r for r in player_trace.parse(game.run / "trace.txt") if r.level == level]
    stories = []
    for i in range(players):
        slots = [r.players[i] for r in rows if len(r.players) > i]
        flown = sum(1 for s in slots if s.state in player_trace.FLYING and not s.parked)
        down = sum(1 for a, b in zip(slots, slots[1:]) if a.state != DOWN and b.state == DOWN)
        lowest = min((s.shields for s in slots if s.state in player_trace.FLYING and not s.parked), default=0)
        stories.append((flown, down, lowest))
    return stories


def desync_value(host, client):
    """What desync_report.py diff says about the two games' reports, or None."""
    reports = [sorted(lockstep_test.own_reports(game)) for game in (host, client)]
    if not all(reports):
        return None
    out = subprocess.run([sys.executable, str(Path(__file__).resolve().parent / "desync_report.py"), "diff",
                          str(reports[0][0]), str(reports[1][0])], capture_output=True, text=True)
    lines = [line for line in out.stdout.splitlines() if line.strip()]
    return "; ".join(lines[-4:]) if lines else None


def fly_level(args, folder, name):
    """One level, two clients, pilots on both. Returns a dict for the summary."""
    level = LEVELS[name]
    cap = FRAMES_BY_LEVEL.get(name, FRAMES) if args.frames is None else args.frames
    env = {"SF64_COOP_LOCKSTEP_PILOT": "1", "SF64_COOP_PILOT_LOG": "pilot-log.txt",
           "SF64_COOP_LOCKSTEP_END": str(cap)}
    cvars = {} if (args.mortal or name in MORTAL) else {"gInvincible": 1}
    if name in BOMBS:
        cvars["gInfiniteBombs"] = 1
    config = {"CVars": cvars} if cvars else None
    row = {"level": name, "id": level, "cap": cap, "folder": str(folder), "invincible": "gInvincible" in cvars,
           "bombs": name in BOMBS}
    began = time.monotonic()
    held = lock(args.lock)
    try:
        host, client = lockstep_test.session(args, folder, {"SF64_COOP_JOIN_LEVEL": str(level)}, None, env,
                                             config=config, replay_frames=cap + 20000)
        # Frames of the host's hash log (from boot, with game state and level on every row) for the end, lockstep
        # frames for the cap. The level complete may come in another level's id: Venom 1 sets Venom 2's at its exit
        # (src/overlays/ovl_i6/fox_andross.c) and goes on to Andross in the same session.
        complete = left = complete_level = None
        frames = hashed = 0
        at = hash_at = trace_at = 0
        why = None
        try:
            while why is None:
                time.sleep(3)
                lines, at = lines_from(host.sync_log, at)
                for line in lines:
                    parts = line.split()
                    if len(parts) >= 3:
                        frames = int(parts[0]) + 1
                lines, hash_at = lines_from(host.run / "state-hash.txt", hash_at)
                for line in lines:
                    parts = line.split()
                    if line.startswith("#") or len(parts) < 3:
                        continue
                    hashed = int(parts[0])
                    if (complete is not None and left is None and
                            (int(parts[1]) != GS_PLAY or int(parts[2]) != complete_level)):
                        left = hashed
                lines, trace_at = lines_from(host.run / "trace.txt", trace_at)
                for line in lines:
                    fields = dict(kv.split("=", 1) for kv in line.split() if "=" in kv)
                    if complete is None and "p0" in fields and fields["p0"].split(",")[0] == str(LEVEL_COMPLETE):
                        complete = hashed  # read just before the trace: no later than the real one
                        complete_level = int(fields["level"])
                        row["complete_level_frame"] = int(fields["frame"])
                        row["complete_level"] = complete_level
                if left is not None and hashed >= left + TAIL + 30:
                    why = "left the level"
                elif complete is not None and hashed >= complete + END_TAIL:
                    why = "past level complete"
                elif frames >= cap or join_test.said(host.sync(), "done"):
                    why = "frame cap"
                elif join_test.said(host.sync(), "over") or join_test.said(client.sync(), "over"):
                    why = "the session ended"
                elif host.proc.poll() is not None or client.proc.poll() is not None:
                    why = (f"a game ended by itself: host {host.proc.poll()}, client {client.proc.poll()}, "
                           f"at frame {frames}")
                elif time.monotonic() - began > args.level_seconds:
                    why = f"timeout after {args.level_seconds:.0f} s at frame {frames}"
        finally:
            client.close()
            host.close()
    finally:
        if held is not None:
            held.close()
    row.update(stopped=why, seconds=round(time.monotonic() - began), left=left)
    try:
        names, _, ours = host.rows()
        _, _, theirs = client.rows()
        n = min(len(ours), len(theirs))
        if left is not None:
            first = int(join_test.said(host.report(), "level_running")[0]["frame"])
            n = min(n, left + TAIL - first)
        row["frames"] = lockstep_test.compare(host, client, n)
        row["equal"] = True
    except (lockstep_test.Fail, join_test.Fail) as failure:
        row["equal"] = False
        row["difference"] = str(failure)
        row["desync"] = desync_value(host, client)
    views = {game.name: sorted({player for _, player in lockstep_test.view_of(game)}) for game in (host, client)}
    row["views"] = views
    try:
        row["players"] = players_of(client, level)
    except (OSError, ValueError, KeyError) as error:
        row["players"] = []
        row["trace_error"] = str(error)
    row["complete"] = row.get("complete_level_frame") is not None
    if not row["equal"]:
        row["result"] = f"hash difference: {row['difference']}" + (f" ({row['desync']})" if row.get("desync") else "")
    elif not row["complete"]:
        row["result"] = f"pilot cannot finish ({why}); equal on the {row['frames']} frames flown"
    else:
        row["result"] = "equal on every frame to the end"
    if not row["invincible"]:
        row["result"] += " (gInvincible off)"
    if row["bombs"]:
        row["result"] += " (gInfiniteBombs on)"
    if views.get("host") == views.get("client0"):
        row["result"] += "; both games looked through the same player"
    return row


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--route", choices=ROUTES, default="all", help="the levels and their order (default all)")
    p.add_argument("--levels", help="these levels in this order, in place of a route")
    p.add_argument("--list", action="store_true", help="print the routes and levels")
    p.add_argument("--mortal", action="store_true", help="without gInvincible, so a player can go down")
    p.add_argument("--frames", type=int, help=f"most lockstep frames per level (default {FRAMES}, Titania "
                   f"{FRAMES_BY_LEVEL['titania']})")
    p.add_argument("--level-seconds", type=float, default=7200, help="give up on a level after this long (default 7200)")
    p.add_argument("--lock", help="hold an flock on this file for each level's session, so that other tests on this "
                   "PC can take their turn in between (Linux)")
    p.add_argument("--exe", help="Starship executable (default: newest build found)")
    p.add_argument("--out", help="folder for the logs (default: a new one under local/)")
    args = p.parse_args()
    if args.list:
        for route, names in ROUTES.items():
            print(f"{route}: {', '.join(names)}")
        return 0
    names = args.levels.split(",") if args.levels else list(ROUTES[args.route])
    unknown = [name for name in names if name not in LEVELS]
    if unknown:
        sys.exit(f"campaign: no level {', '.join(unknown)}; the choices are {', '.join(LEVELS)}")
    args.exe = replay_tool.find_exe(args.exe)
    root = Path(args.out).resolve() if args.out else replay_tool.ROOT / "local" / f"test-campaign-{time.strftime('%Y%m%d-%H%M%S')}"
    root.mkdir(parents=True, exist_ok=True)
    print(f"campaign: {len(names)} levels: {', '.join(names)}; gInvincible {'off' if args.mortal else 'on'}; "
          f"exe {args.exe}; logs {root}", flush=True)
    rows = []
    for name in names:
        try:
            row = fly_level(args, root / name, name)
        except (lockstep_test.Fail, join_test.Fail, OSError) as failure:
            row = {"level": name, "equal": False, "complete": False, "players": [], "seconds": 0,
                   "result": f"no session: {failure}"}
        rows.append(row)
        downs = ",".join(str(d) for _, d, _ in row["players"]) or "-"
        print(f"campaign: {name}: {row['result']}; {row.get('frames', 0)} frames compared, level complete at level "
              f"frame {row.get('complete_level_frame')}, downs {downs}, {row['seconds']} s", flush=True)
        (root / "summary.json").write_text(json.dumps(rows, indent=1), encoding="utf-8")
    print("campaign: summary (level, frames compared, level complete at level frame, downs per player, result)")
    for row in rows:
        downs = ",".join(str(d) for _, d, _ in row["players"]) or "-"
        print(f"campaign:   {row['level']:14} {row.get('frames', 0):6} {str(row.get('complete_level_frame')):>6} "
              f"{downs:>5}  {row['result']}")
    passed = sum(1 for row in rows if row["equal"] and row["complete"])
    print(f"campaign: {passed}/{len(rows)} levels equal on every frame to the end; logs {root}")
    return 0 if passed == len(rows) else 1


if __name__ == "__main__":
    sys.exit(main())
