#!/usr/bin/env python3
"""Tests for the co-op session: handshake (WP-21), characters (WP-34) and level start (WP-24). A virtual network, then
real processes over UDP.

    python tools/coop/session_test.py                  # build coop_session_test, run every scenario
    python tools/coop/session_test.py --only sim,rom   # chosen scenarios
    python tools/coop/session_test.py --list
    python tools/coop/session_test.py --no-build --build-dir <dir>
    python tools/coop/session_test.py --asan           # the same under AddressSanitizer and UBSan; not on Windows

No ROM and no game data are needed; identities are given on the command line of the test program. Prints one line
per scenario and a RESULT line, keeps every log in local/test-session/<stamp>/, exits 0 only when everything passed.
Whether the scenarios can fail at all is checked by session_mutants.py.
"""
import argparse
import platform
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
IS_WINDOWS = platform.system() == "Windows"
sys.path.insert(0, str(Path(__file__).resolve().parent))
from net_test import SEED, Fail, Run, chaos, expect, read  # noqa: E402
from run_folder import make_run_folder  # noqa: E402

# Bits of CoopIdentityDiff, src/port/coop/session/identity.h.
DIFF_BUILD, DIFF_ROM, DIFF_CVARS, DIFF_PLATFORM = 1, 2, 4, 8
END_REFUSED = 1  # COOP_SESSION_END_REFUSED
EAGER = 3  # game messages each side pushes past the session in a refused handshake
REFUSE_STARTED = 1 << 9  # COOP_SESSION_REFUSE_STARTED
LEVEL, PHASE = 5, 1  # what the bots start: TEST_PHASE in coop_session_test.c
LEVEL_VALUES = 87  # COOP_LEVEL_START_VALUES


def pair(run, host_args=(), client_args=(), via=None, seconds=60, seed=SEED):
    """One host and one client, optionally through the chaos relay. Returns both results."""
    common = ["--max-seconds", seconds]
    host, port, host_out = run.host(*common, *host_args)
    info = None
    if via:
        port, info = run.relay(port, via, seed)
    client, client_out = run.client(port, *common, *client_args)
    run.wait(client, seconds + 10, "client")
    run.wait(host, seconds + 10, "host")
    return read(host_out), read(client_out), info


def events(result, kind):
    return [e for e in result["events"] if e["type"] == kind]


def damaged_runs(run, play, tries=8):
    """A handshake is only a few datagrams, so a relay run may well lose none, and a pass on an undamaged link proves
    nothing. Tries seeds until the relay did drop something; every try before must pass as well. play(run, seed)
    returns the results and then the relay's info."""
    for n in range(tries):
        sub = Run(run.exe, run.folder / f"try{n}")
        try:
            *results, info = play(sub, SEED + n)
            stats = chaos.stats(info)
        finally:
            sub.stop_all()
        yield results
        if stats["c2s"]["dropped"] + stats["s2c"]["dropped"] > 0:
            run.lost = stats["c2s"]["dropped"] + stats["s2c"]["dropped"]
            run.tries = n + 1
            return
    raise Fail(f"the relay dropped nothing in {tries} runs, so this pass would prove nothing")


def damaged_pair(run, host_args, client_args, via):
    return damaged_runs(run, lambda sub, seed: pair(sub, host_args, client_args, via, seed=seed))


def refused(run, host_args, client_args, diff, needle, via=None):
    """Both sides refused with the same text, which names what differs."""
    if via:
        for h, c in damaged_pair(run, host_args, client_args, via):
            detail = check_refused(h, c, diff, needle)
        return f"{detail} (relay dropped {run.lost}, try {run.tries})"
    # Without game messages on the wire the check that none got through could not fail. Not on a relay: the refusal
    # would queue behind them and could run out of linger time.
    h, c, _ = pair(run, [*host_args, "--eager", EAGER], [*client_args, "--eager", EAGER])
    detail = check_refused(h, c, diff, needle)
    expect(h["ignored"] + c["ignored"] > 0, "nothing was held back, so this pass proves nothing")
    return detail


def check_refused(h, c, diff, needle):
    hr, cr = events(h, "refused"), events(c, "refused")
    expect(len(hr) == 1 and not events(h, "joined"), f"host events: {h['events']}")
    expect(len(cr) == 1 and not events(c, "joined"), f"client events: {c['events']}")
    expect(hr[0]["diff"] == diff and cr[0]["diff"] == diff, f"reasons {hr[0]['diff']}/{cr[0]['diff']}, want {diff}")
    expect(hr[0]["text"] == cr[0]["text"], "host and client got different texts")
    expect(needle in cr[0]["text"], f"text does not say what differs: {cr[0]['text']!r}")
    ended = events(c, "ended")
    expect(len(ended) == 1 and ended[0]["end"] == END_REFUSED, f"client did not end with the refusal: {c['events']}")
    expect(h["received"] == 0 and c["received"] == 0, "a game message passed a refused handshake")
    return cr[0]["text"].splitlines()[0]


def joined(run, host_args=(), client_args=(), diff=0, via=None):
    if via:
        for h, c in damaged_pair(run, host_args, client_args, via):
            detail = check_joined(h, c, diff)
        return f"{detail} (relay dropped {run.lost}, try {run.tries})"
    h, c, _ = pair(run, host_args, client_args)
    return check_joined(h, c, diff)


def check_joined(h, c, diff):
    hj, cj = events(h, "joined"), events(c, "joined")
    expect(len(hj) == 1 and len(cj) == 1, f"not joined: host {h['events']}, client {c['events']}")
    expect(not events(h, "refused") and not events(c, "refused"), "refused although it should match")
    expect(hj[0]["diff"] == diff and cj[0]["diff"] == diff, f"warnings {hj[0]['diff']}/{cj[0]['diff']}, want {diff}")
    expect(hj[0]["text"] == cj[0]["text"], "host and client got different texts")
    expect(cj[0]["player"] == 1, f"client got player {cj[0]['player']}, want 1")
    expect(h["received"] == 1 and c["received"] == 1, "game messages did not pass after the welcome")
    expect(events(h, "left"), "host did not see the client leave")
    return cj[0]["text"] or "joined, no warnings"


def party(run, picks, via=None, seconds=60, seed=SEED, host_args=(), client_args=()):
    """A host and one client for each list of characters in picks. A client asks for the first and for the next one
    after each refusal, and stays until every player holds a character. Returns host, clients and the relay's info."""
    common = ["--max-seconds", seconds, "--players", len(picks) + 1]
    host, port, host_out = run.host("--clients", len(picks), *common, *host_args)
    info = None
    if via:
        port, info = run.relay(port, via, seed)
    clients = []
    for n, names in enumerate(picks):
        wishes = [arg for name in names for arg in ("--pick", name)]
        clients.append(run.client(port, *common, *wishes, *client_args, name=f"client{n}"))
    for n, (proc, _) in enumerate(clients):
        run.wait(proc, seconds + 10, f"client {n}")
    run.wait(host, seconds + 10, "host")
    return read(host_out), [read(out) for _, out in clients], info


def check_party(h, clients, refusals):
    """Every side saw the same full roster: Fox as player 0, each wingman once, each client in its own slot."""
    roster = h["roster"]
    expect(len(roster) == len(clients) + 1, f"host never saw everyone with a character: {roster}")
    expect(roster[0] == "Fox", f"player 0 is {roster[0]}")
    expect(sorted(roster[1:]) == ["Falco", "Peppy", "Slippy"], f"a wingman is missing or held twice: {roster}")
    for n, c in enumerate(clients):
        expect(c["roster"] == roster, f"client {n} has {c['roster']}, host {roster}")
    expect(len({events(c, "joined")[0]["player"] for c in clients}) == len(clients), "two clients in one slot")
    held = [events(c, "pick_refused") for c in clients]
    expect(sum(len(r) for r in held) == refusals, f"want {refusals} refused picks, got {held}")
    expect(len(events(h, "pick_refused")) == refusals, f"host refused {events(h, 'pick_refused')}")
    for c in clients:
        for r in events(c, "pick_refused"):
            expect(r["text"] == f"{r['character']} is already taken.", f"wrong text: {r['text']!r}")
        expect(not events(c, "ended"), f"a client was ended: {c['events']}")
    return " ".join(roster)


def level_values():
    """What the bots start from, as Level_Value in coop_session_test.c."""
    words = [(0x9E3779B1 * (i + 1)) & 0xFFFFFFFF for i in range(LEVEL_VALUES)]
    return [w - (1 << 32) if w >= 1 << 31 else w for w in words]


def level_party(run, picks, via=None, seed=SEED, client_args=()):
    """party(), with the host trying to start LEVEL as soon as every client has joined, and the clients staying until
    it has started."""
    return party(run, picks, via, seed=seed, host_args=["--start-level", LEVEL],
                 client_args=["--wait-level", 1, *client_args])


def check_level(h, clients):
    """Every side saw one level start, and the same one: the level, the values and the host's last roster."""
    start = h["level_start"]
    expect(start is not None, f"the host never started: {h['events']}")
    expect(start["level"] == LEVEL and start["phase"] == PHASE, f"level {start['level']} phase {start['phase']}")
    expect(start["values"] == level_values(), "the host's values are not the ones the bot gave it")
    expect(start["roster"][:len(clients) + 1] == h["roster"][:len(clients) + 1], f"host started from {start['roster']}")
    for side in [h, *clients]:
        expect(len(events(side, "level_start")) == 1, f"{side['role']}: {side['events']}")
    for n, c in enumerate(clients):
        expect(c["level_start"] == start, f"client {n} started from {c['level_start']}, host from {start}")
    return " ".join(name for name in start["roster"] if name)


# ---- Scenarios. Each returns the text for its summary line. ----

def sc_sim(run, args):
    out = run.folder / "sim.log"
    with open(out, "w", encoding="utf-8") as log:
        code = subprocess.run([str(run.exe), "sim"], stdout=log, stderr=subprocess.STDOUT, timeout=120).returncode
    text = out.read_text(encoding="utf-8").strip()
    lines = text.splitlines()
    # A sanitizer report ends in stack frames; this line says what happened.
    report = [line for line in lines if "ERROR: " in line or "runtime error: " in line]
    expect(code == 0, report[0] if report else lines[-1] if lines else f"exit code {code}")
    return text


def sc_match(run, args):
    return joined(run)


def sc_build(run, args):
    return refused(run, ["--build", "aaaa1111"], ["--build", "bbbb2222"], DIFF_BUILD,
                   "Different game builds: host aaaa1111, joining player bbbb2222")


def sc_rom(run, args):
    return refused(run, [], ["--rom", "94F1D5A7"], DIFF_ROM, "host US 1.0 (76924CA9), joining player US (94F1D5A7)")


def sc_cvar(run, args):
    return refused(run, ["--cvar", "gInfiniteLives=1"], [], DIFF_CVARS,
                   'Setting "Infinite lives" (gInfiniteLives): host 1, joining player 0.')


def sc_platform(run, args):
    text = joined(run, ["--platform", "Windows x86-64 MSVC 1944"], ["--platform", "Linux x86-64 GCC 13.2"],
                  DIFF_PLATFORM)
    expect(text.startswith("Warning: host runs Windows"), f"no platform warning: {text!r}")
    return text


def sc_refuse_hell(run, args):
    return refused(run, [], ["--cvar", "gInvincible=1"], DIFF_CVARS, '"Invincible"', via="hell")


def sc_match_bad_wifi(run, args):
    return joined(run, via="bad_wifi")


def sc_two_clients(run, args):
    seconds = 60
    host, port, host_out = run.host("--clients", 2, "--max-seconds", seconds)
    good, good_out = run.client(port, "--max-seconds", seconds, name="good")
    bad, bad_out = run.client(port, "--max-seconds", seconds, "--cvar", "gRapidFire=1", name="bad")
    for proc, what in ((good, "good client"), (bad, "bad client"), (host, "host")):
        run.wait(proc, seconds + 10, what)
    h, g, b = read(host_out), read(good_out), read(bad_out)
    expect(len(events(h, "joined")) == 1 and len(events(h, "refused")) == 1, f"host events: {h['events']}")
    expect(events(g, "joined") and g["received"] == 1, f"good client: {g['events']}")
    expect(events(b, "refused") and "Rapid fire" in events(b, "refused")[0]["text"], f"bad client: {b['events']}")
    return "one joined, one refused for Rapid fire"


def sc_characters(run, args):
    h, clients, _ = party(run, [["falco"], ["slippy"], ["peppy"]])
    detail = check_party(h, clients, 0)
    for c, name in zip(clients, ("Falco", "Slippy", "Peppy")):
        player = events(c, "joined")[0]["player"]
        expect(c["roster"][player] == name, f"player {player} holds {c['roster'][player]}, picked {name}")
    return detail


WISHES = ["falco", "slippy", "peppy"]
TAKEN = 3  # three clients walking down the same list: 0, 1 and 2 refusals, in whatever order they arrive


def sc_taken(run, args):
    h, clients, _ = party(run, [WISHES] * 3)
    return f"{check_party(h, clients, TAKEN)}, {TAKEN} picks refused"


def sc_taken_bad_wifi(run, args):
    for h, clients in damaged_runs(run, lambda sub, seed: party(sub, [WISHES] * 3, "bad_wifi", seed=seed)):
        detail = check_party(h, clients, TAKEN)
    return f"{detail} (relay dropped {run.lost}, try {run.tries})"


def sc_level_start(run, args):
    h, clients, _ = level_party(run, [WISHES] * 3)
    check_party(h, clients, TAKEN)
    return f"{check_level(h, clients)} on level {LEVEL}, {TAKEN} picks refused"


def sc_level_start_bad_wifi(run, args):
    def play(sub, seed):
        return level_party(sub, [WISHES] * 3, "bad_wifi", seed)
    for h, clients in damaged_runs(run, play):
        check_party(h, clients, TAKEN)
        detail = check_level(h, clients)
    return f"{detail} (relay dropped {run.lost}, try {run.tries})"


def sc_unpicked_holds_start(run, args):
    h, clients, _ = level_party(run, [["falco"]], client_args=["--pick-delay-ms", 500])
    expect(h["start_refused"] > 0, "the host never had to wait, so this pass proves nothing")
    return f"{check_level(h, clients)}, host turned down {h['start_refused']} times until the pick"


def sc_late_client(run, args):
    common = ["--max-seconds", 60]
    host, port, host_out = run.host("--clients", 2, "--start-level", LEVEL, "--start-with", 1, *common)
    first, first_out = run.client(port, *common, "--pick", "falco", "--players", 2, "--wait-level", 1, name="client0")
    run.wait(first, 70, "client 0")
    late, late_out = run.client(port, *common, name="client1")
    run.wait(late, 70, "client 1")
    run.wait(host, 70, "host")
    h, c = read(host_out), read(late_out)
    hr, cr = events(h, "refused"), events(c, "refused")
    expect(len(hr) == 1 and len(cr) == 1 and not events(c, "joined"), f"client 1: {c['events']}")
    expect(hr[0]["diff"] == REFUSE_STARTED and cr[0]["diff"] == REFUSE_STARTED, f"reasons {hr} {cr}")
    expect(hr[0]["text"] == cr[0]["text"], "host and client got different texts")
    expect(events(c, "ended")[0]["end"] == END_REFUSED, f"client did not end with the refusal: {c['events']}")
    expect(read(first_out)["level_start"]["level"] == LEVEL, "client 0 did not start the level")
    return cr[0]["text"]


SCENARIOS = {
    "sim": sc_sim,
    "match": sc_match,
    "build": sc_build,
    "rom": sc_rom,
    "cvar": sc_cvar,
    "platform": sc_platform,
    "two_clients": sc_two_clients,
    "characters": sc_characters,
    "taken": sc_taken,
    "level_start": sc_level_start,
    "unpicked_holds_start": sc_unpicked_holds_start,
    "late_client": sc_late_client,
    "match_bad_wifi": sc_match_bad_wifi,
    "refuse_hell": sc_refuse_hell,
    "taken_bad_wifi": sc_taken_bad_wifi,
    "level_start_bad_wifi": sc_level_start_bad_wifi,
}


def target(asan):
    return "coop_session_test_asan" if asan else "coop_session_test"


def find_exe(build_dir, config, asan=False):
    name = target(asan) + (".exe" if IS_WINDOWS else "")
    for path in (build_dir / "coop-session-test" / name, build_dir / "coop-session-test" / config / name):
        if path.exists():
            return path
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--only", help="comma separated scenario names")
    ap.add_argument("--list", action="store_true", help="print the scenario names and stop")
    ap.add_argument("--build-dir", type=Path, help="default: build/x64 on Windows, build-cmake elsewhere")
    ap.add_argument("--config", default="Debug", choices=["Debug", "Release", "RelWithDebInfo"])
    ap.add_argument("--no-build", action="store_true", help="use the coop_session_test already built")
    ap.add_argument("--asan", action="store_true", help="use coop_session_test_asan: AddressSanitizer and UBSan")
    ap.add_argument("--out", type=Path, help="log folder, default local/test-session/<stamp>")
    args = ap.parse_args()

    if args.list:
        print("\n".join(SCENARIOS))
        return 0
    if args.asan and IS_WINDOWS:
        print("RESULT: FAILED, --asan needs GCC or Clang; MSVC has no UBSan")
        return 2
    names = args.only.split(",") if args.only else list(SCENARIOS)
    unknown = [n for n in names if n not in SCENARIOS]
    if unknown:
        print(f"RESULT: FAILED, unknown scenario {', '.join(unknown)}; --list shows them")
        return 2

    build_dir = args.build_dir or (Path("build/x64") if IS_WINDOWS else Path("build-cmake"))
    build_dir = build_dir if build_dir.is_absolute() else ROOT / build_dir
    out = args.out or make_run_folder(ROOT / "local" / "test-session")
    out.mkdir(parents=True, exist_ok=True)

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

    exe = find_exe(build_dir, args.config, args.asan)
    if exe is None:
        print(f"RESULT: FAILED, no {target(args.asan)} in {build_dir}")
        return 1

    lines = []
    failed = []
    for name in names:
        run = Run(exe, out / name)
        started = time.monotonic()
        try:
            line = f"ok      {name}: {SCENARIOS[name](run, args)}"
        except Fail as e:
            line = f"FAILED  {name}: {e}"
            failed.append(name)
        except Exception as e:  # a broken test must not read as a pass
            line = f"FAILED  {name}: {type(e).__name__}: {e}"
            failed.append(name)
        finally:
            run.stop_all()
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
