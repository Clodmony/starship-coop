#!/usr/bin/env python3
"""Puts a built game into a folder named for its co-op build, for CI to upload (#26).

    python tools/coop/package.py --config Release    # after build.py; prints package: OK <name>
    python tools/coop/package.py --scan <folder>     # only the scan, for example on a downloaded artifact

The name is starship-coop-<id>-<os>-x64 in <build dir>/package/. <id> comes from coop/coop_build_id.h in the build
folder, the id the game shows and the handshake compares (tools/coop/cmake/coop_build_id.cmake), so a name and a game
cannot disagree. In GitHub Actions the name and the folder also go to the step outputs name and path.

Game data must never leave in an artifact: the repository is public, and so are its artifacts. So only the files listed
here are copied, never a whole build folder (a developer's holds sf64.o2r and starship.o2r), and the result is scanned
before anything is uploaded: it fails on a file outside the list, a ROM or asset archive by name, a file that starts like
one whatever its name, or a ROM header anywhere inside a file. python tools/coop/package_test.py tests this.

Exit code 0 on OK, 1 otherwise.
"""
import argparse
import os
import platform
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from check import GAME_DATA  # noqa: E402

ALLOWED_NAMES = {"Starship", "Starship.exe", "config.yml", "gamecontrollerdb.txt", "README.txt"}
ALLOWED_IN_ASSETS = re.compile(r"\.ya?ml$", re.IGNORECASE)


def swapped(data, width):
    """data with every group of width bytes reversed: how .v64 (2) and .n64 (4) dumps differ from .z64 (1)."""
    return b"".join(data[i:i + width][::-1] for i in range(0, len(data), width))


# An N64 ROM starts with these four bytes; 0x20 in comes the game's name, padded with spaces.
ROM_START = b"\x80\x37\x12\x40"
ROM_NAME = b"STARFOX64   "
ROM_ORDERS = (1, 2, 4)
ROM_HEADERS = [re.compile(re.escape(swapped(ROM_START, order)) + b".{28}" + re.escape(swapped(ROM_NAME, order)), re.DOTALL)
               for order in ROM_ORDERS]
# .o2r is a zip, .otr an MPQ.
BAD_START = (b"PK\x03\x04", b"MPQ\x1a", *(swapped(ROM_START, order) for order in ROM_ORDERS))

README = """Starship Co-op, build {build_id}, {system} x64

A test build from the project's CI, not a release. The game shows this id at the right end of the menu bar (press F1).
To play together, everyone needs the same id.

There is nothing from the game in this folder: no ROM, no sf64.o2r, no starship.o2r. You need:

- Your own Star Fox 64 ROM, US 1.0 or 1.1. On the first start the game asks for it and makes sf64.o2r from it.
- starship.o2r next to the game. The game stops at start without it (it holds the shaders). See docs/BUILDING.md in
  the repository for how to make it.
{extra}"""
EXTRA = {
    "linux": """- chmod +x Starship: a download from Actions loses the executable flag.
- The libraries of Ubuntu 24.04: this build is linked against them. See .github/workflows/coop-build.yml for the list.
""",
    "windows": "",
}


def read_build_id(build_dir):
    header = Path(build_dir) / "coop" / "coop_build_id.h"
    if not header.exists():
        sys.exit(f"package: {header} not found; run python tools/coop/build.py first")
    found = re.search(r'#define COOP_BUILD_ID "([^"]+)"', header.read_text(encoding="utf-8"))
    if not found:
        sys.exit(f"package: no COOP_BUILD_ID in {header}")
    return found.group(1)


def scan(folder):
    """Returns one line for every file that must not leave in an artifact."""
    problems = []
    for path in sorted(p for p in Path(folder).rglob("*") if p.is_file() or p.is_symlink()):
        name = path.relative_to(folder).as_posix()
        if path.is_symlink():
            problems.append(f"{name}: symbolic link")
            continue
        listed = name in ALLOWED_NAMES or (name.startswith("assets/") and ALLOWED_IN_ASSETS.search(name))
        if not listed:
            problems.append(f"{name}: not on the list")
        if GAME_DATA.search(name):
            problems.append(f"{name}: ROM or asset archive by name")
        data = path.read_bytes()
        if data.startswith(BAD_START):
            problems.append(f"{name}: starts like a ROM or asset archive")
        if any(header.search(data) for header in ROM_HEADERS):
            problems.append(f"{name}: holds a ROM header")
    return problems


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--config", default="Debug", choices=["Debug", "Release", "RelWithDebInfo"])
    p.add_argument("--build-dir", type=Path, help="default: build/x64 on Windows, build-cmake elsewhere")
    p.add_argument("--scan", type=Path, metavar="FOLDER", help="scan this folder and stop")
    args = p.parse_args()

    if args.scan:
        problems = scan(args.scan)
        for problem in problems:
            print(f"package: {problem}")
        print(f"package: FAILED, {len(problems)} problems" if problems else f"package: scan OK ({args.scan.name})")
        return 1 if problems else 0

    system = platform.system().lower()
    if system not in EXTRA:
        sys.exit(f"package: no package for {platform.system()}; CI builds Linux and Windows")
    windows = system == "windows"
    build_dir = args.build_dir or Path("build/x64" if windows else "build-cmake")
    build_dir = build_dir if build_dir.is_absolute() else ROOT / build_dir
    exe = build_dir / args.config / "Starship.exe" if windows else build_dir / "Starship"
    if not exe.exists():
        sys.exit(f"package: {exe} not found; run python tools/coop/build.py first")

    build_id = read_build_id(build_dir)
    name = f"starship-coop-{build_id}-{system}-x64"
    folder = build_dir / "package" / name
    shutil.rmtree(build_dir / "package", ignore_errors=True)
    (folder / "assets").mkdir(parents=True)

    shutil.copy2(exe, folder / exe.name)
    shutil.copy2(ROOT / "config.yml", folder / "config.yml")
    for source in (ROOT / "assets").rglob("*"):
        if source.is_file() and ALLOWED_IN_ASSETS.search(source.name):
            target = folder / source.relative_to(ROOT)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
    # Fetched while configuring (CMakeLists.txt); controllers work without it, with fewer known mappings.
    if (build_dir / "gamecontrollerdb.txt").exists():
        shutil.copy2(build_dir / "gamecontrollerdb.txt", folder / "gamecontrollerdb.txt")
    text = README.format(build_id=build_id, system=platform.system(), extra=EXTRA[system])
    (folder / "README.txt").write_text(text, encoding="utf-8")

    problems = scan(folder)
    if problems:
        for problem in problems:
            print(f"package: {problem}")
        print(f"package: FAILED, {len(problems)} problems in {folder}")
        return 1

    files = [p for p in folder.rglob("*") if p.is_file()]
    size = sum(p.stat().st_size for p in files) / 1e6
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as out:
            out.write(f"name={name}\npath={folder}\n")
    print(f"package: OK {name} ({len(files)} files, {size:.1f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
