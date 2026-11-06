#!/usr/bin/env python3
"""Tests for the co-op session handshake (WP-21): a virtual network, then real processes over UDP.

    python tools/coop/session_test.py                  # build coop_session_test, run every scenario
    python tools/coop/session_test.py --only sim,rom   # chosen scenarios
    python tools/coop/session_test.py --list
    python tools/coop/session_test.py --no-build --build-dir <dir>

No ROM and no game data are needed; identities are given on the command line of the test program. Prints one line
per scenario and a RESULT line, keeps every log in local/test-session/<stamp>/, exits 0 only when everything passed.
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

# Bits of CoopIdentityDiff, src/port/coop/session/identity.h.
DIFF_BUILD, DIFF_ROM, DIFF_CVARS, DIFF_PLATFORM = 1, 2, 4, 8
END_REFUSED = 1  # COOP_SESSION_END_REFUSED


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


def damaged_pair(run, host_args, client_args, via, tries=8):
    """A handshake is only a few datagrams, so a relay run may well lose none, and a pass on an undamaged link proves
    nothing. Tries seeds until the relay did drop something; every try before must pass as well."""
    for n in range(tries):
        sub = Run(run.exe, run.folder / f"try{n}")
        try:
            h, c, info = pair(sub, host_args, client_args, via, seed=SEED + n)
            stats = chaos.stats(info)
        finally:
            sub.stop_all()
        yield h, c
        if stats["c2s"]["dropped"] + stats["s2c"]["dropped"] > 0:
            run.lost = stats["c2s"]["dropped"] + stats["s2c"]["dropped"]
            run.tries = n + 1
            return
    raise Fail(f"the relay dropped nothing in {tries} runs, so this pass would prove nothing")


def refused(run, host_args, client_args, diff, needle, via=None):
    """Both sides refused with the same text, which names what differs."""
    if via:
        for h, c in damaged_pair(run, host_args, client_args, via):
            detail = check_refused(h, c, diff, needle)
        return f"{detail} (relay dropped {run.lost}, try {run.tries})"
    h, c, _ = pair(run, host_args, client_args)
    return check_refused(h, c, diff, needle)


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
    expect(cj[0]["player"] == 1, f"client got player {cj[0]['player']}, want 1")
    expect(h["received"] == 1 and c["received"] == 1, "game messages did not pass after the welcome")
    expect(events(h, "left"), "host did not see the client leave")
    return cj[0]["text"] or "joined, no warnings"


# ---- Scenarios. Each returns the text for its summary line. ----

def sc_sim(run, args):
    out = run.folder / "sim.log"
    with open(out, "w", encoding="utf-8") as log:
        code = subprocess.run([str(run.exe), "sim"], stdout=log, stderr=subprocess.STDOUT, timeout=600).returncode
    text = out.read_text(encoding="utf-8").strip()
    expect(code == 0, text.splitlines()[-1] if text else f"exit code {code}")
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


SCENARIOS = {
    "sim": sc_sim,
    "match": sc_match,
    "build": sc_build,
    "rom": sc_rom,
    "cvar": sc_cvar,
    "platform": sc_platform,
    "two_clients": sc_two_clients,
    "match_bad_wifi": sc_match_bad_wifi,
    "refuse_hell": sc_refuse_hell,
}


def find_exe(build_dir, config):
    name = "coop_session_test.exe" if IS_WINDOWS else "coop_session_test"
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
    ap.add_argument("--out", type=Path, help="log folder, default local/test-session/<stamp>")
    args = ap.parse_args()

    if args.list:
        print("\n".join(SCENARIOS))
        return 0
    names = args.only.split(",") if args.only else list(SCENARIOS)
    unknown = [n for n in names if n not in SCENARIOS]
    if unknown:
        print(f"RESULT: FAILED, unknown scenario {', '.join(unknown)}; --list shows them")
        return 2

    build_dir = args.build_dir or (Path("build/x64") if IS_WINDOWS else Path("build-cmake"))
    build_dir = build_dir if build_dir.is_absolute() else ROOT / build_dir
    out = args.out or ROOT / "local" / "test-session" / time.strftime("%Y%m%d-%H%M%S")
    out.mkdir(parents=True, exist_ok=True)

    if not args.no_build:
        if shutil.which("cmake") is None:
            print("RESULT: FAILED, cmake not found")
            return 1
        build_log = out / "build.log"
        with open(build_log, "w", encoding="utf-8") as log:
            code = subprocess.run(["cmake", "--build", str(build_dir), "--config", args.config, "--target",
                                   "coop_session_test"], stdout=log, stderr=subprocess.STDOUT).returncode
        if code != 0:
            tail = build_log.read_text(encoding="utf-8", errors="replace").strip().splitlines()[-8:]
            print("\n".join(tail))
            print(f"RESULT: FAILED, coop_session_test did not build (log: {build_log})")
            return 1

    exe = find_exe(build_dir, args.config)
    if exe is None:
        print(f"RESULT: FAILED, no coop_session_test in {build_dir}")
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
