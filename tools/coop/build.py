#!/usr/bin/env python3
"""Configure and build Starship Co-op, then run the repository checks.

Works the same on Linux, macOS and Windows (PowerShell or cmd). CI runs this script too, so a green local run
means the same steps as CI.

    python tools/coop/build.py                   # Debug build + checks
    python tools/coop/build.py --config Release
    python tools/coop/build.py --quiet           # full output to <build dir>/coop-build.log, summary only
    python tools/coop/build.py --assets          # also build sf64.o2r (needs baserom.z64) and starship.o2r

Generators: Ninja on Linux and macOS (build dir build-cmake), Visual Studio 2022 on Windows (build dir build/x64),
as in docs/BUILDING.md. Pass --ninja on Windows to use Ninja from a VS developer prompt instead.
"""
import argparse
import os
import platform
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
IS_WINDOWS = platform.system() == "Windows"


def parse_args():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--config", default="Debug", choices=["Debug", "Release", "RelWithDebInfo"])
    p.add_argument("--build-dir", type=Path, help="default: build/x64 on Windows, build-cmake elsewhere")
    p.add_argument("--ninja", action="store_true", help="use Ninja on Windows (needs a VS developer prompt)")
    p.add_argument("--clean", action="store_true", help="delete the build directory first")
    p.add_argument("--jobs", type=int, help="parallel build jobs (default: all cores)")
    p.add_argument("--assets", action="store_true", help="also build the ExtractAssets and GeneratePortO2R targets")
    p.add_argument("--no-check", action="store_true", help="skip tools/coop/check.py")
    p.add_argument("--quiet", action="store_true", help="write tool output to the log file, print a summary only")
    p.add_argument("-D", dest="defines", action="append", default=[], metavar="VAR=VALUE",
                   help="extra CMake cache entry, repeatable (for example -D CMAKE_C_COMPILER_LAUNCHER=ccache)")
    return p.parse_args()


class Runner:
    def __init__(self, log_path: Path, quiet: bool):
        self.log_path = log_path
        self.quiet = quiet
        log_path.parent.mkdir(parents=True, exist_ok=True)
        self.log = open(log_path, "w", encoding="utf-8", errors="replace")

    def run(self, cmd, step):
        line = "$ " + " ".join(str(c) for c in cmd)
        self.log.write(f"\n=== {step}\n{line}\n")
        self.log.flush()
        if not self.quiet:
            print(f"\n=== {step}\n{line}", flush=True)
        proc = subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, encoding="utf-8", errors="replace")
        tail = []
        for out in proc.stdout:
            self.log.write(out)
            tail.append(out)
            if len(tail) > 40:
                tail.pop(0)
            if not self.quiet:
                sys.stdout.write(out)
        proc.wait()
        self.log.flush()
        return proc.returncode, tail


def submodules_ready():
    return (ROOT / "libultraship" / "CMakeLists.txt").exists() and (ROOT / "tools" / "Torch" / "CMakeLists.txt").exists()


def main():
    args = parse_args()
    if shutil.which("cmake") is None:
        print("build: FAILED, cmake not found on PATH (see docs/BUILDING.md)")
        return 1
    use_vs = IS_WINDOWS and not args.ninja
    if not use_vs and shutil.which("ninja") is None:
        print("build: FAILED, ninja not found on PATH (see docs/BUILDING.md)")
        return 1

    build_dir = args.build_dir or (Path("build/x64") if IS_WINDOWS else Path("build-cmake"))
    build_dir = build_dir if build_dir.is_absolute() else ROOT / build_dir
    if args.clean and build_dir.exists():
        shutil.rmtree(build_dir)

    runner = Runner(build_dir / "coop-build.log", args.quiet)
    started = time.monotonic()

    def fail(step, tail):
        print(f"\nbuild: FAILED at {step} after {elapsed()} (full log: {runner.log_path})")
        if args.quiet:
            print("".join(tail[-30:]))
        return 1

    def elapsed():
        s = int(time.monotonic() - started)
        return f"{s // 60}m{s % 60:02d}s"

    steps = []
    if not submodules_ready():
        steps.append(("submodules", ["git", "submodule", "update", "--init", "--recursive"]))

    configure = ["cmake", "-S", ".", "-B", str(build_dir)]
    if use_vs:
        configure += ["-G", "Visual Studio 17 2022", "-T", "v143", "-A", "x64"]
    else:
        configure += ["-G", "Ninja", f"-DCMAKE_BUILD_TYPE={args.config}"]
    configure += [f"-D{d}" for d in args.defines]
    # Always configure: the source list is a GLOB_RECURSE, so new files only build after a configure.
    steps.append(("configure", configure))

    build = ["cmake", "--build", str(build_dir), "--config", args.config, "--parallel"]
    if args.jobs:
        build.append(str(args.jobs))
    if args.assets:
        if not (ROOT / "baserom.z64").exists():
            print("build: FAILED, --assets needs your ROM as baserom.z64 in the repo root (git ignores it)")
            return 1
        steps.append(("assets", build + ["--target", "ExtractAssets"]))
        steps.append(("port assets", build + ["--target", "GeneratePortO2R"]))
    steps.append(("build", build))

    for step, cmd in steps:
        code, tail = runner.run(cmd, step)
        if code != 0:
            return fail(step, tail)

    if not args.no_check:
        code, tail = runner.run([sys.executable, str(ROOT / "tools" / "coop" / "check.py")], "checks")
        if code != 0:
            return fail("checks", tail)

    system = {"Windows": "Windows", "Darwin": "macOS"}.get(platform.system(), platform.system())
    print(f"\nbuild: OK ({system}, {args.config}, {elapsed()}) -> {build_dir} (log: {runner.log_path})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
