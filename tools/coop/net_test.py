#!/usr/bin/env python3
"""Tests for the co-op transport (WP-20): a virtual network, then two real processes through the chaos relay.

    python tools/coop/net_test.py                  # build coop_net_test, run every scenario
    python tools/coop/net_test.py --only lan,hell  # chosen scenarios
    python tools/coop/net_test.py --list
    python tools/coop/net_test.py --no-build --build-dir <dir>
    python tools/coop/net_test.py --asan           # the same under AddressSanitizer and UBSan; not on Windows

The game and its data are not involved, so no ROM is needed; the build directory only has to be configured
(python tools/coop/build.py). Prints one line per scenario and a RESULT line, keeps every log in
local/test-net/<stamp>/, and exits 0 only when everything passed. Ports are picked by the OS, so several runs can go
side by side.

Checks follow the hub's chaos testing page: end state and counts, never the timing of single packets, and each
scenario must also show that the relay really did damage, because a pass on an undamaged link proves nothing.
"""
import argparse
import json
import platform
import random
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
IS_WINDOWS = platform.system() == "Windows"
sys.path.insert(0, str(ROOT / "tools" / "coop"))
sys.path.insert(0, str(ROOT / "tools" / "coop" / "chaos"))
import client as chaos  # noqa: E402
from run_folder import make_run_folder  # noqa: E402

SEED = 4242
TIMEOUT_MS = 2000  # the keep-alive timeout in the scenarios that test it


class Fail(Exception):
    pass


def expect(ok, what):
    if not ok:
        raise Fail(what)


class Run:
    """One scenario: its folder, and every process it started, so they can all be stopped at the end."""

    def __init__(self, exe, folder):
        self.exe = exe
        self.folder = folder
        folder.mkdir(parents=True, exist_ok=True)
        self.procs = []
        self.count = 0

    def start(self, name, cmd):
        log = open(self.folder / f"{name}.log", "w", encoding="utf-8")
        proc = subprocess.Popen([str(c) for c in cmd], stdout=log, stderr=subprocess.STDOUT)
        log.close()
        self.procs.append(proc)
        return proc

    def host(self, *args, name="host"):
        port_file = self.folder / f"{name}.port"
        out = self.folder / f"{name}.json"
        proc = self.start(name, [self.exe, "host", "--port-file", port_file, "--out", out, *args])
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            text = port_file.read_text(encoding="utf-8").strip() if port_file.exists() else ""
            if text:
                return proc, int(text), out
            expect(proc.poll() is None, f"{name} exited at start, see {name}.log")
            time.sleep(0.01)
        raise Fail(f"{name} did not report its port")

    def client(self, port, *args, name="client"):
        out = self.folder / f"{name}.json"
        return self.start(name, [self.exe, "client", "--connect", f"127.0.0.1:{port}", "--out", out, *args]), out

    def relay(self, target_port, profile, seed=SEED):
        info = self.folder / "relay.json"
        cmd = [sys.executable, "-I", ROOT / "tools" / "coop" / "chaos" / "chaos.py", "relay",
               "--target", f"127.0.0.1:{target_port}", "--profile", profile, "--seed", seed,
               "--info-file", info, "--log", self.folder / "packets.jsonl"]
        self.start("relay", cmd)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if info.exists() and info.read_text(encoding="utf-8"):
                return json.loads(info.read_text(encoding="utf-8"))["listen"], str(info)
            time.sleep(0.01)
        raise Fail("the relay did not start")

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


def read(path):
    expect(path.exists(), f"no result file {path.name}")
    return json.loads(path.read_text(encoding="utf-8"))


def check_flow(peer, who, reliable, unreliable):
    """The promises of both lanes, for what one side received."""
    expect(peer["connected"], f"{who}: never connected")
    expect(peer["corrupt"] == 0, f"{who}: {peer['corrupt']} corrupt messages delivered")
    expect(peer["out_of_order"] == 0, f"{who}: {peer['out_of_order']} reliable messages out of order")
    expect(peer["reliable_received"] == reliable, f"{who}: {peer['reliable_received']}/{reliable} reliable messages")
    expect(peer["done_received"], f"{who}: the final message never arrived")
    expect(peer["unreliable_duplicates"] == 0, f"{who}: {peer['unreliable_duplicates']} unreliable duplicates")
    expect(peer["unreliable_received"] <= unreliable, f"{who}: more unreliable messages than were sent")


def exchange(run, profile, reliable=600, unreliable=600, seed=SEED, during=None, seconds=120):
    """Host and client send to each other through the relay until both have everything. Returns both results."""
    args = ["--reliable", reliable, "--unreliable", unreliable, "--max-seconds", seconds]
    host, port, host_out = run.host(*args)
    relay_port, info = run.relay(port, profile, seed)
    client, client_out = run.client(relay_port, *args)
    if during:
        during(info)
    run.wait(client, seconds + 10, "client")
    run.wait(host, seconds + 10, "host")
    h, c = read(host_out), read(client_out)
    expect(h["finished"] and c["finished"], f"ran out of time: host {h['finished']}, client {c['finished']}")
    check_flow(h["peers"][0], "host", reliable, unreliable)
    check_flow(c["peers"][0], "client", reliable, unreliable)
    return h["peers"][0], c["peers"][0], chaos.stats(info)


def dropped(stats):
    return stats["c2s"]["dropped"] + stats["s2c"]["dropped"]


def duplicated(stats):
    return stats["c2s"]["duplicated"] + stats["s2c"]["duplicated"]


def summary(h, c, stats):
    return (f"relay dropped {dropped(stats)} of {stats['c2s']['seen'] + stats['s2c']['seen']}, "
            f"duplicated {duplicated(stats)}; resent {h['resent'] + c['resent']}, "
            f"unreliable through {h['unreliable_received']}+{c['unreliable_received']}, rtt {c['rtt_ms']} ms")


# ---- Scenarios. Each returns the text for its summary line. ----

def sc_sim(run, args):
    out = run.folder / "sim.log"
    with open(out, "w", encoding="utf-8") as log:
        code = subprocess.run([str(run.exe), "sim", "--seeds", str(args.seeds)], stdout=log, stderr=subprocess.STDOUT,
                              timeout=600).returncode
    text = out.read_text(encoding="utf-8").strip()
    expect(code == 0, text.splitlines()[-1] if text else f"exit code {code}")
    return text


def sc_lan(run, args):
    h, c, stats = exchange(run, "lan")
    expect(dropped(stats) == 0, "the lan profile dropped packets")
    for name, peer in (("host", h), ("client", c)):
        expect(peer["unreliable_received"] == 600, f"{name}: lost unreliable messages on a clean link")
        # A timing limit: a resend on a clean link means an ack came later than the resend timeout. It failed once, at a
        # load above 90, and not since (#121); the rtt in the message tells a slow machine from a transport that resends.
        expect(peer["resent"] <= 6, f"{name}: {peer['resent']} resends on a clean link (rtt {peer['rtt_ms']} ms)")
    return summary(h, c, stats)


def lossy(profile):
    def scenario(run, args):
        h, c, stats = exchange(run, profile)
        expect(dropped(stats) > 0, "the relay dropped nothing, so this pass would prove nothing")
        expect(h["resent"] + c["resent"] > 0, "nothing was resent, although the relay dropped packets")
        return summary(h, c, stats)
    return scenario


def sc_bad_wifi(run, args):
    h, c, stats = exchange(run, "bad_wifi")
    expect(dropped(stats) > 0, "the relay dropped nothing")
    expect(duplicated(stats) > 0, "the relay duplicated nothing")
    lost = 1200 - h["unreliable_received"] - c["unreliable_received"]
    expect(lost > 0, "no unreliable message was lost, so the lane was not tested")
    return summary(h, c, stats)


def sc_transatlantic(run, args):
    h, c, stats = exchange(run, "transatlantic", reliable=300, unreliable=300)
    # 90 ms each way, plus or minus 8.
    expect(170 <= c["rtt_ms"] <= 260, f"client rtt {c['rtt_ms']} ms, expected about 180")
    expect(170 <= h["rtt_ms"] <= 260, f"host rtt {h['rtt_ms']} ms, expected about 180")
    return summary(h, c, stats)


def sc_hell(run, args):
    h, c, stats = exchange(run, "hell", reliable=300, unreliable=300, seconds=180)
    expect(dropped(stats) > 0 and duplicated(stats) > 0, "the relay dropped or duplicated nothing")
    return summary(h, c, stats)


def sc_corrupt(run, args):
    def flip(info):
        chaos._call(info, "set_params", params={"corrupt": 0.05})

    h, c, stats = exchange(run, "good_wifi", during=flip)
    bad = h["bad_datagrams"] + c["bad_datagrams"]
    expect(bad > 0, "the relay flipped bits, but no datagram failed the checksum")
    return f"{bad} damaged datagrams rejected, 0 delivered; " + summary(h, c, stats)


def outage(direction):
    def scenario(run, args):
        def cut(info):
            time.sleep(3)
            chaos.partition(info, direction, seconds=2)

        h, c, stats = exchange(run, "good_wifi", during=cut)
        reasons = sum(stats[d]["reasons"].get("partition", 0) for d in ("c2s", "s2c"))
        expect(reasons > 0, "the outage dropped nothing")
        return f"2 s {direction} outage dropped {reasons}, all reliable messages still arrived; " + summary(h, c, stats)
    return scenario


def sc_blackout_timeout(run, args):
    timing = ["--timeout-ms", TIMEOUT_MS, "--hold", "--max-seconds", 30]
    host, port, host_out = run.host(*timing)
    relay_port, info = run.relay(port, "lan")
    client, client_out = run.client(relay_port, *timing)
    time.sleep(1)
    expect(host.poll() is None and client.poll() is None, "a side left before the outage")
    started = time.monotonic()
    chaos.partition(info, "both")
    run.wait(host, 20, "host")
    run.wait(client, 20, "client")
    took = time.monotonic() - started
    h, c = read(host_out)["peers"][0], read(client_out)["peers"][0]
    expect(h["connected"] and c["connected"], "never connected")
    expect(h["end"] == "timeout" and c["end"] == "timeout", f"ends: host {h['end']}, client {c['end']}")
    expect(took < TIMEOUT_MS / 1000 + 3, f"took {took:.1f} s to notice")
    return f"both sides reported a timeout {took:.1f} s after the link died (limit {TIMEOUT_MS} ms)"


def sc_kill_client(run, args):
    timing = ["--timeout-ms", TIMEOUT_MS, "--hold", "--max-seconds", 30]
    host, port, host_out = run.host(*timing)
    client, _ = run.client(port, *timing)
    time.sleep(1)
    expect(client.poll() is None, "the client left early")
    started = time.monotonic()
    client.kill()
    client.wait()
    run.wait(host, 20, "host")
    took = time.monotonic() - started
    h = read(host_out)["peers"][0]
    expect(h["connected"] and h["end"] == "timeout", f"host: connected {h['connected']}, end {h['end']}")
    return f"host dropped a killed client after {took:.1f} s (limit {TIMEOUT_MS} ms)"


def sc_clean_close(run, args):
    host, port, host_out = run.host("--hold", "--max-seconds", 30)
    client, _ = run.client(port, "--hold", "--close-after-ms", 500, "--max-seconds", 30)
    run.wait(client, 20, "client")
    started = time.monotonic()
    run.wait(host, 3, "host")
    took = time.monotonic() - started
    h = read(host_out)["peers"][0]
    expect(h["end"] == "closed", f"host end: {h['end']}")
    return f"host saw the goodbye {took:.2f} s after the client exited"


def sc_host_full(run, args):
    host, port, _ = run.host("--hold", "--peers", 1, "--max-seconds", 30)
    first, _ = run.client(port, "--hold", "--max-seconds", 30, name="first")
    time.sleep(0.5)
    second, second_out = run.client(port, "--hold", "--max-seconds", 15, name="second")
    run.wait(second, 20, "second client")
    s = read(second_out)["peers"][0]
    expect(not s["connected"] and s["end"] == "refused_full", f"second client: {s['connected']}, {s['end']}")
    expect(first.poll() is None, "the first client was dropped")
    return "a second client on a full host was refused, the first stayed"


def sc_two_clients(run, args):
    common = ["--reliable", 300, "--unreliable", 300, "--max-seconds", 90]
    host, port, host_out = run.host("--peers", 2, *common)
    relay_port, info = run.relay(port, "bad_wifi")
    a, a_out = run.client(relay_port, *common, name="client-a")
    b, b_out = run.client(relay_port, *common, name="client-b")
    run.wait(a, 100, "client a")
    run.wait(b, 100, "client b")
    run.wait(host, 100, "host")
    peers = read(host_out)["peers"]
    expect(len(peers) == 2, "the host did not report two peers")
    for i, peer in enumerate(peers):
        check_flow(peer, f"host peer {i}", 300, 300)
    check_flow(read(a_out)["peers"][0], "client a", 300, 300)
    check_flow(read(b_out)["peers"][0], "client b", 300, 300)
    stats = chaos.stats(info)
    expect(dropped(stats) > 0, "the relay dropped nothing")
    return f"one host, two clients; relay dropped {dropped(stats)}"


def sc_stray_datagrams(run, args):
    host, port, host_out = run.host("--reliable", 200, "--unreliable", 200, "--max-seconds", 60)
    client, client_out = run.client(port, "--reliable", 200, "--unreliable", 200, "--max-seconds", 60)
    rng = random.Random(SEED)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sent = 0
    while client.poll() is None and sent < 3000:
        sock.sendto(rng.randbytes(rng.randrange(0, 1400)), ("127.0.0.1", port))
        sent += 1
        time.sleep(0.001)
    sock.close()
    run.wait(client, 70, "client")
    run.wait(host, 70, "host")
    h, c = read(host_out)["peers"][0], read(client_out)["peers"][0]
    check_flow(h, "host", 200, 200)
    check_flow(c, "client", 200, 200)
    expect(h["bad_datagrams"] > 0, "the host did not count the junk it was sent")
    return f"{sent} junk datagrams sent at the host, {h['bad_datagrams']} rejected, traffic unharmed"


SCENARIOS = {
    "sim": sc_sim,
    "lan": sc_lan,
    "good_wifi": lossy("good_wifi"),
    "bad_wifi": sc_bad_wifi,
    "mobile_tether": lossy("mobile_tether"),
    "transatlantic": sc_transatlantic,
    "hell": sc_hell,
    "corrupt": sc_corrupt,
    "outage_both": outage("both"),
    "outage_s2c": outage("s2c"),
    "outage_c2s": outage("c2s"),
    "blackout_timeout": sc_blackout_timeout,
    "kill_client": sc_kill_client,
    "clean_close": sc_clean_close,
    "host_full": sc_host_full,
    "two_clients": sc_two_clients,
    "stray_datagrams": sc_stray_datagrams,
}


def target(asan):
    return "coop_net_test_asan" if asan else "coop_net_test"


def sanitizer_report(folder):
    """The line that says what a sanitizer found in a scenario's logs, or None; the report itself ends in stack frames.
    A finding stops the program that made it, and a scenario does not watch every program to its end, so this also
    covers those. CI keeps no logs, so the summary line must say it."""
    for log in sorted(folder.glob("*.log")):
        for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
            if "ERROR: AddressSanitizer" in line or "ERROR: LeakSanitizer" in line or "runtime error: " in line:
                return f"{log.name}: {line}"
    return None


def find_exe(build_dir, config, asan=False):
    name = target(asan) + (".exe" if IS_WINDOWS else "")
    for path in (build_dir / "coop-net-test" / name, build_dir / "coop-net-test" / config / name):
        if path.exists():
            return path
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--only", help="comma separated scenario names")
    ap.add_argument("--list", action="store_true", help="print the scenario names and stop")
    ap.add_argument("--build-dir", type=Path, help="default: build/x64 on Windows, build-cmake elsewhere")
    ap.add_argument("--config", default="Debug", choices=["Debug", "Release", "RelWithDebInfo"])
    ap.add_argument("--no-build", action="store_true", help="use the coop_net_test already built")
    ap.add_argument("--asan", action="store_true", help="use coop_net_test_asan: AddressSanitizer and UBSan")
    ap.add_argument("--seeds", type=int, default=200, help="seeds for the sim scenario")
    ap.add_argument("--out", type=Path, help="log folder, default local/test-net/<stamp>")
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
    out = args.out or make_run_folder(ROOT / "local" / "test-net")
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
            detail = SCENARIOS[name](run, args)
            line = f"ok      {name}: {detail}"
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
