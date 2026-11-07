#!/usr/bin/env python3
"""Tests for the Steam relay transport (WP-26): a fake Steam in a virtual network, then a mock Steam library.

    python tools/coop/steam_test.py                  # build coop_steam_test, run every scenario
    python tools/coop/steam_test.py --only lan,hell  # chosen scenarios
    python tools/coop/steam_test.py --list
    python tools/coop/steam_test.py --no-build --build-dir <dir>
    python tools/coop/steam_test.py --asan           # the same under AddressSanitizer and UBSan; not on Windows

None of this needs Steam, the Steamworks SDK, the game or a ROM; the build directory only has to be configured
(python tools/coop/build.py). Prints one line per scenario and a RESULT line, keeps every log in
local/test-steam/<stamp>/, and exits 0 only when everything passed.

This shows the transport keeps its promises on the rules Steam documents. It cannot show that Steam behaves that way,
or that the struct offsets in steam_backend_flat.c are right: only a real run can, on two PCs with two accounts:

    python tools/coop/steam_test.py --live host --library <steam library>
    python tools/coop/steam_test.py --live client --library <steam library> --connect <SteamID the host printed>

Each side prints its SteamID and a RESULT line. A real run uses the Steam account logged in on that PC. The result
files hold SteamIDs, so keep them out of issues and PRs.

Checks follow net_test.py: end state and counts, and each lossy scenario must show that the network really did damage.
"""
import argparse
import json
import os
import platform
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
IS_WINDOWS = platform.system() == "Windows"
PROFILES = ROOT / "tools" / "coop" / "chaos" / "profiles.json"
MOCK_ID_BASE = 76561190000000000  # as in mock_steam_api.c: the mock's UDP port is the id minus this
TIMEOUT_MS = 2000  # the keep-alive timeout in the scenarios that test it


class Fail(Exception):
    pass


def expect(ok, what):
    if not ok:
        raise Fail(what)


def link_args(profile):
    """The sim's link options from the shared impairment profiles. The fake has no bandwidth limit."""
    p = json.loads(PROFILES.read_text(encoding="utf-8"))[profile]
    return ["--latency-ms", p["latency_ms"], "--jitter-ms", p["jitter_ms"], "--loss", p["loss"],
            "--burst-enter", p["burst_enter"], "--burst-exit", p["burst_exit"], "--burst-loss", p["burst_loss"],
            "--reorder", p["reorder"], "--duplicate", p["duplicate"]]


class Run:
    """One scenario: its folder, and every process it started, so they can all be stopped at the end."""

    def __init__(self, exe, folder, args):
        self.exe = exe
        self.folder = folder
        self.args = args
        folder.mkdir(parents=True, exist_ok=True)
        self.procs = []

    def sim(self, name, *extra, seeds=None):
        """Runs one sim scenario. Returns its PASS text and its stats, if it printed any."""
        log_path = self.folder / "sim.log"
        cmd = [self.exe, "sim", "--scenario", name, "--seeds", seeds or self.args.seeds, *extra]
        with open(log_path, "w", encoding="utf-8") as log:
            code = subprocess.run([str(c) for c in cmd], stdout=log, stderr=subprocess.STDOUT,
                                  timeout=self.args.timeout).returncode
        lines = log_path.read_text(encoding="utf-8", errors="replace").strip().splitlines()
        result = [line for line in lines if line.startswith("sim: ")]
        expect(code == 0 and result and result[-1].startswith("sim: PASS"),
               result[-1] if result else (lines[-1] if lines else f"exit code {code}"))
        stats = [line for line in lines if line.startswith("stats: ")]
        return result[-1][len("sim: PASS "):], json.loads(stats[-1][len("stats: "):]) if stats else None

    def start(self, name, cmd, env=None):
        log = open(self.folder / f"{name}.log", "w", encoding="utf-8")
        proc = subprocess.Popen([str(c) for c in cmd], stdout=log, stderr=subprocess.STDOUT, cwd=self.folder,
                                env={**os.environ, **(env or {})})
        log.close()
        self.procs.append(proc)
        return proc

    def wait(self, proc, seconds, what):
        try:
            proc.wait(timeout=seconds)
        except subprocess.TimeoutExpired:
            raise Fail(f"{what} still running after {seconds} s")
        expect(proc.returncode == 0, f"{what} exited with {proc.returncode}")

    def stop_all(self):
        for proc in self.procs:
            if proc.poll() is None:
                proc.kill()
                proc.wait()


# ---- Scenarios on the fake. Each returns the text for its summary line. ----

def plain(name, *extra, profile=None, seeds=None):
    def scenario(run, args):
        link = link_args(profile) if profile else []
        return run.sim(name, *link, *extra, seeds=seeds)[0]
    return scenario


def summary(s):
    return (f"dropped {s['dropped']} of {s['seen']}, duplicated {s['duplicated']}, resends {s['resent']}, "
            f"unreliable through {s['unreliable_through']}, rtt {s['rtt_ms']} ms")


def sc_lan(run, args):
    _, s = run.sim("profile", *link_args("lan"))
    expect(s["dropped"] == 0, "the lan profile dropped messages")
    expect(s["unreliable_through"] == 2 * 600 * int(args.seeds), "lost unreliable messages on a clean link")
    return summary(s)


def lossy(profile):
    def scenario(run, args):
        _, s = run.sim("profile", *link_args(profile))
        expect(s["dropped"] > 0, "the network dropped nothing, so this pass would prove nothing")
        expect(s["resent"] > 0, "Steam resent nothing, so the reliable lane was not tested")
        return summary(s)
    return scenario


def sc_bad_wifi(run, args):
    _, s = run.sim("profile", *link_args("bad_wifi"))
    expect(s["dropped"] > 0 and s["duplicated"] > 0, "the network dropped or duplicated nothing")
    expect(s["unreliable_through"] < 2 * 600 * int(args.seeds), "no unreliable message was lost")
    return summary(s)


def sc_transatlantic(run, args):
    _, s = run.sim("profile", *link_args("transatlantic"))
    # 90 ms each way, plus 0 to 8.
    expect(170 <= s["rtt_ms"] <= 260, f"rtt {s['rtt_ms']} ms, expected about 190")
    return summary(s)


def sc_hell(run, args):
    _, s = run.sim("profile", *link_args("hell"))
    expect(s["dropped"] > 0 and s["duplicated"] > 0, "the network dropped or duplicated nothing")
    return summary(s)


def outage(direction):
    def scenario(run, args):
        return run.sim("outage", *link_args("good_wifi"), "--direction", direction)[0]
    return scenario


# ---- Scenarios on the mock library: the real backend, in real processes. ----

def mock_library(exe, old=False):
    return exe.parent / ("mock-old" if old else "mock") / "libsteam_api.so"


def mock_id():
    """A SteamID for the mock, on a UDP port the OS says is free."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return MOCK_ID_BASE + port


def mock_env(run, steam_id, name, **extra):
    return {"COOP_MOCK_STEAM_ID": str(steam_id), "COOP_MOCK_STEAM_LOG": str(run.folder / f"{name}.mock.json"),
            **extra}


def open_result(run, library, env=None):
    log = run.folder / "open.log"
    with open(log, "w", encoding="utf-8") as f:
        code = subprocess.run([str(run.exe), "open", "--library", str(library)], stdout=f, stderr=subprocess.STDOUT,
                              cwd=run.folder, env={**os.environ, **(env or {})}, timeout=30).returncode
    return code, log.read_text(encoding="utf-8", errors="replace").strip()


def sc_flat_no_library(run, args):
    code, text = open_result(run, run.folder / "not-there.so")
    expect(code != 0 and text.startswith("open: ERROR"), f"opened a library that does not exist: {text}")
    expect("not-there.so" in text, f"the error does not name the file: {text}")
    return text


def sc_flat_old_library(run, args):
    code, text = open_result(run, mock_library(run.exe, old=True))
    expect(code != 0 and text.startswith("open: ERROR"), f"took a library without SteamAPI_InitFlat: {text}")
    expect("SteamAPI_InitFlat" in text, f"the error does not name what is missing: {text}")
    return text


def sc_flat_steam_not_running(run, args):
    code, text = open_result(run, mock_library(run.exe), {"COOP_MOCK_STEAM_INIT": "2"})
    expect(code != 0 and text.startswith("open: ERROR"), f"started without Steam: {text}")
    expect("not running" in text, f"the error does not say why: {text}")
    return text


def sc_flat_open(run, args):
    steam_id = mock_id()
    env = mock_env(run, steam_id, "open")
    code, text = open_result(run, mock_library(run.exe), env)
    expect(code == 0 and text == f"open: OK {steam_id}", f"expected this user's id, got: {text}")
    expect((run.folder / "steam_appid.txt").read_text(encoding="utf-8").strip() == "480",
           "steam_appid.txt does not say 480")
    mock = json.loads((run.folder / "open.mock.json").read_text(encoding="utf-8"))
    expect(mock["errors"] == 0, f"used the library in a way the real one would not take: {mock}")
    expect(mock["relay_inits"] == 1, "the relay network was not started early")
    return "started as app 480, read this user's id, shut down cleanly"


def live_pair(run, reliable, unreliable, host_env=None, client_args=(), host_args=()):
    """A host and a client on the mock library, through the program a real run uses. Returns both results."""
    host_id, client_id = mock_id(), mock_id()
    common = ["--library", mock_library(run.exe), "--reliable", reliable, "--unreliable", unreliable,
              "--max-seconds", 60]
    host_out, client_out = run.folder / "host.json", run.folder / "client.json"
    host = run.start("host", [run.exe, "live", "host", *common, "--out", host_out, "--id-file", run.folder / "host.id",
                              *host_args], mock_env(run, host_id, "host", **(host_env or {})))
    deadline = time.monotonic() + 10
    while not (run.folder / "host.id").exists():
        expect(time.monotonic() < deadline and host.poll() is None, "the host did not start, see host.log")
        time.sleep(0.01)
    client = run.start("client", [run.exe, "live", "client", *common, "--out", client_out, "--connect", host_id,
                                  *client_args], mock_env(run, client_id, "client"))
    run.wait(client, 70, "client")
    run.wait(host, 70, "host")
    read = lambda path: json.loads(path.read_text(encoding="utf-8"))
    return read(host_out), read(client_out), read(run.folder / "host.mock.json"), read(run.folder / "client.mock.json")


def check_mock(name, mock):
    expect(mock["errors"] == 0, f"{name}: used the library in a way the real one would not take: {mock}")
    expect(mock["received"] == mock["released"],
           f"{name}: {mock['received']} messages read, {mock['released']} released")
    expect(mock["callbacks_read"] == mock["callbacks_freed"], f"{name}: a callback was not freed: {mock}")


def sc_flat_live(run, args):
    h, c, host_mock, client_mock = live_pair(run, 300, 300)
    for name, side in (("host", h), ("client", c)):
        peer = side["peers"][0]
        expect(side["finished"], f"{name}: ran out of time")
        expect(peer["connected"], f"{name}: never connected")
        expect(peer["reliable_received"] == 300 and peer["out_of_order"] == 0, f"{name}: reliable lane: {peer}")
        expect(peer["corrupt"] == 0 and peer["unreliable_duplicates"] == 0, f"{name}: {peer}")
        expect(peer["done_received"], f"{name}: the final message never arrived")
    check_mock("host", host_mock)
    check_mock("client", client_mock)
    expect(host_mock["accepted"] >= 1, "the host did not accept the session the library asked about")
    sent = host_mock["sent"] + client_mock["sent"]
    return f"two processes, {sent} messages, all released, rtt {c['peers'][0]['rtt_ms']} ms"


def sc_flat_clean_close(run, args):
    h, c, host_mock, client_mock = live_pair(run, 0, 0, client_args=["--hold", "--close-after-ms", 500],
                                             host_args=["--hold"])
    expect(h["peers"][0]["end"] == "closed", f"host end: {h['peers'][0]['end']}")
    check_mock("host", host_mock)
    check_mock("client", client_mock)
    return "the goodbye crossed the real backend"


def sc_flat_session_failed(run, args):
    h, c, host_mock, _ = live_pair(run, 0, 0, host_env={"COOP_MOCK_STEAM_FAIL_MS": "500"}, host_args=["--hold"],
                                   client_args=["--hold", "--close-after-ms", 1500])
    expect(h["peers"][0]["end"] == "timeout", f"host end: {h['peers'][0]['end']}")
    expect(host_mock["closed"] >= 1, "the broken session was not closed")
    check_mock("host", host_mock)
    return "callback 1252 ended the connection and closed the session"


SCENARIOS = {
    "sim": plain("sim", seeds="200"),
    "wrap": plain("wrap"),
    "lan": sc_lan,
    "good_wifi": lossy("good_wifi"),
    "bad_wifi": sc_bad_wifi,
    "mobile_tether": lossy("mobile_tether"),
    "transatlantic": sc_transatlantic,
    "hell": sc_hell,
    "outage_both": outage("both"),
    "outage_s2c": outage("s2c"),
    "outage_c2s": outage("c2s"),
    "blackout_timeout": plain("blackout_timeout"),
    "session_failed": plain("session_failed"),
    "peer_closed": plain("peer_closed"),
    "kill_client": plain("kill_client"),
    # Many seeds: a goodbye that lands behind a ping needs the right timing, and only seed 24 of 300 had it at first.
    "clean_close": plain("clean_close", profile="bad_wifi", seeds="1000"),
    "broken_after_data": plain("broken_after_data"),
    "failed_after_data": plain("failed_after_data"),
    "stall": plain("stall"),
    "bad_frames": plain("bad_frames"),
    "acks": plain("acks"),
    "accept_busy": plain("accept_busy"),
    "quiet": plain("quiet"),
    "lost_goodbye": plain("lost_goodbye"),
    "host_full": plain("host_full"),
    "version": plain("version"),
    "two_clients": plain("two_clients", profile="bad_wifi"),
    "three_clients": plain("three_clients", profile="bad_wifi"),
    "send_buffer": plain("send_buffer"),
    "gap": plain("gap"),
    "duplicates": plain("duplicates"),
    "junk": plain("junk"),
    "bad_peer": plain("bad_peer"),
    "strangers": plain("strangers"),
    "restart": plain("restart"),
    "early_traffic": plain("early_traffic", profile="bad_wifi"),
    "window": plain("window"),
    "pending": plain("pending"),
    "limits": plain("limits"),
    "disconnect": plain("disconnect"),
    "lifetime": plain("lifetime"),
    "connect_timeout": plain("connect_timeout"),
    "slow_setup": plain("slow_setup"),
    "session_match": plain("session_match", profile="bad_wifi"),
    "session_refused": plain("session_refused", profile="bad_wifi"),
}

# The mock library uses POSIX sockets.
MOCK_SCENARIOS = {
    "flat_no_library": sc_flat_no_library,
    "flat_old_library": sc_flat_old_library,
    "flat_steam_not_running": sc_flat_steam_not_running,
    "flat_open": sc_flat_open,
    "flat_live": sc_flat_live,
    "flat_clean_close": sc_flat_clean_close,
    "flat_session_failed": sc_flat_session_failed,
}
if not IS_WINDOWS:
    SCENARIOS.update(MOCK_SCENARIOS)


def target(asan):
    return "coop_steam_test_asan" if asan else "coop_steam_test"


def sanitizer_report(folder):
    """The line that says what a sanitizer found in a scenario's logs, or None."""
    for log in sorted(folder.glob("*.log")):
        for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
            if "ERROR: AddressSanitizer" in line or "ERROR: LeakSanitizer" in line or "runtime error: " in line:
                return f"{log.name}: {line}"
    return None


def find_exe(build_dir, config, asan=False):
    name = target(asan) + (".exe" if IS_WINDOWS else "")
    for path in (build_dir / "coop-steam-test" / name, build_dir / "coop-steam-test" / config / name):
        if path.exists():
            return path
    return None


def live_main(args, exe, out):
    """One side of a real run on Steam. Run the other side on the other PC, with the other account."""
    if args.live == "client" and not args.connect:
        print("RESULT: FAILED, --live client needs --connect <SteamID the host printed>")
        return 2
    result = out / f"{args.live}.json"
    cmd = [str(exe), "live", args.live, "--out", str(result), "--max-seconds", str(args.max_seconds)]
    if args.library:
        cmd += ["--library", str(Path(args.library).resolve())]
    if args.connect:
        cmd += ["--connect", args.connect]
    # steam_appid.txt goes into the working directory, so this run's folder keeps it out of everything else.
    code = subprocess.run(cmd, cwd=out).returncode
    if code != 0 or not result.exists():
        print(f"RESULT: FAILED, exit code {code}, no result (logs: {out})")
        return 1
    side = json.loads(result.read_text(encoding="utf-8"))
    peer = side["peers"][0]
    problems = []
    if not peer["connected"]:
        problems.append(f"never connected, end {peer['end']}")
    if peer["reliable_received"] != 600 or peer["out_of_order"] or peer["corrupt"] or peer["unreliable_duplicates"]:
        problems.append("lanes broke their promise")
    if not side["finished"]:
        problems.append("ran out of time")
    print(f"reliable {peer['reliable_received']}/600, unreliable {peer['unreliable_received']}/600, "
          f"rtt {peer['rtt_ms']} ms ({peer['rtt_min_ms']} to {peer['rtt_max_ms']}), end {peer['end']}")
    print(f"RESULT: {'FAILED, ' + '; '.join(problems) if problems else 'PASS'} (logs: {out}; they hold SteamIDs)")
    return 1 if problems else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--only", help="comma separated scenario names")
    ap.add_argument("--list", action="store_true", help="print the scenario names and stop")
    ap.add_argument("--build-dir", type=Path, help="default: build/x64 on Windows, build-cmake elsewhere")
    ap.add_argument("--config", default="Debug", choices=["Debug", "Release", "RelWithDebInfo"])
    ap.add_argument("--no-build", action="store_true", help="use the coop_steam_test already built")
    ap.add_argument("--asan", action="store_true", help="use coop_steam_test_asan: AddressSanitizer and UBSan")
    ap.add_argument("--seeds", default="20", help="seeds for the sim scenarios that take them")
    ap.add_argument("--timeout", type=int, default=300, help="seconds for one sim scenario")
    ap.add_argument("--out", type=Path, help="log folder, default local/test-steam/<stamp>")
    ap.add_argument("--live", choices=["host", "client"],
                    help="one side of a real run on Steam, see the top of this file")
    ap.add_argument("--library", help="--live: the Steam library, default the usual name next to the program")
    ap.add_argument("--connect", help="--live client: the SteamID the host printed")
    ap.add_argument("--max-seconds", type=int, default=300, help="--live: how long to wait for the other side")
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
    stamp = time.strftime("%Y%m%d-%H%M%S")
    out = args.out or ROOT / "local" / "test-steam" / stamp
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
    if args.live:
        return live_main(args, exe, out)

    lines = []
    failed = []
    for name in names:
        run = Run(exe, out / name, args)
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
        report = sanitizer_report(run.folder) if args.asan else None
        if report:
            line = f"FAILED  {name}: {report}"
            failed += [] if name in failed else [name]
        line += f" ({time.monotonic() - started:.1f} s)"
        print(line, flush=True)
        lines.append(line)

    passed = len(names) - len(failed)
    result = f"RESULT: {passed}/{len(names)} scenarios passed"
    print(result)
    print(f"logs: {out}")
    (out / "summary.txt").write_text("\n".join(lines + [result]) + "\n", encoding="utf-8")
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
