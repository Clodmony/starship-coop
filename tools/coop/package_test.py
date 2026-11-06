#!/usr/bin/env python3
"""Tests tools/coop/package.py: game data must not get into a package (#26).

    python tools/coop/package_test.py    # unattended; prints PASS or FAIL. No build, no ROM, a few seconds.

Works on a made-up build folder under local/test-package/: the decoys and the ROM header are invented bytes, nothing from
the game. Run it after changing package.py. Exit code 0 when every scenario holds, 1 otherwise. The folder is deleted when
all hold and kept for reading when one fails.
"""
import io
import os
import shutil
import subprocess
import sys
import warnings
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import package as package_tool  # noqa: E402

ROOT = package_tool.ROOT
WORK = ROOT / "local" / "test-package"
BUILD_ID = "0123456789ab"
SYSTEM = {"win32": "windows", "linux": "linux"}.get(sys.platform)
DECOYS = ["sf64.o2r", "starship.o2r", "baserom.z64", "mods/pack.otr"]  # a build folder's starship.o2r is a decoy too
ARCHIVE = "starship.o2r"


def fake_build(name, header=True):
    build = WORK / name
    exe = build / "Release" / "Starship.exe" if SYSTEM == "windows" else build / "Starship"
    exe.parent.mkdir(parents=True)
    exe.write_bytes(b"\x7fELF made up")
    (build / "gamecontrollerdb.txt").write_text("made up\n", encoding="utf-8")
    for decoy in DECOYS:
        (build / decoy).parent.mkdir(exist_ok=True)
        (build / decoy).write_bytes(b"decoy")
    if header:
        (build / "coop").mkdir()
        (build / "coop" / "coop_build_id.h").write_text(f'#define COOP_BUILD_ID "{BUILD_ID}"\n', encoding="utf-8")
    return build


def run_package(build, outputs=None):
    env = {key: value for key, value in os.environ.items() if key != "GITHUB_OUTPUT"}
    if outputs:
        env["GITHUB_OUTPUT"] = str(outputs)
    return subprocess.run([sys.executable, str(ROOT / "tools" / "coop" / "package.py"), "--config", "Release",
                           "--build-dir", str(build)], capture_output=True, text=True, encoding="utf-8", env=env)


def packaged():
    """A folder holding what a clean package holds."""
    build = fake_build(f"build-{len(list(WORK.iterdir()))}")
    assert run_package(build).returncode == 0
    return build / "package" / f"starship-coop-{BUILD_ID}-{SYSTEM}-x64"


def zip_bytes():
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w") as archive:
        archive.writestr("made-up", "made up")
    return out.getvalue()


def rom_header(width):
    """Written out here and not taken from package.py, so that a wrong table there fails. Made up but for the layout."""
    header = b"\x80\x37\x12\x40" + bytes(28) + b"STARFOX64   "
    return b"".join(bytes(reversed(header[i:i + width])) for i in range(0, len(header), width))


def clean():
    outputs = WORK / "outputs.txt"
    build = fake_build("clean")
    result = run_package(build, outputs)
    name = f"starship-coop-{BUILD_ID}-{SYSTEM}-x64"
    folder = build / "package" / name
    names = sorted(p.relative_to(folder).as_posix() for p in folder.rglob("*") if p.is_file())
    if result.returncode != 0:
        return f"package failed: {result.stdout}{result.stderr}"
    if f"package: OK {name}" not in result.stdout:
        return f"no OK line: {result.stdout}"
    if outputs.read_text(encoding="utf-8") != f"name={name}\npath={folder}\n":
        return f"step outputs wrong: {outputs.read_text(encoding='utf-8')}"
    if any(Path(n).name in {Path(d).name for d in DECOYS} - {ARCHIVE} for n in names):
        return f"a decoy went in: {names}"
    missing = {"config.yml", "README.txt", "gamecontrollerdb.txt", ARCHIVE} - set(names)
    if missing or not any(n.startswith("assets/") for n in names):
        return f"missing from the package: {sorted(missing)}, {len(names)} files"
    if (folder / ARCHIVE).read_bytes() == b"decoy":
        return "the build folder's starship.o2r went in"
    return None


def no_id():
    result = run_package(fake_build("no-id", header=False))
    return None if result.returncode != 0 and "coop_build_id.h" in result.stdout + result.stderr else "ran without an id"


def caught(path, data, expect):
    folder = packaged()
    target = folder / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
    problems = package_tool.scan(folder)
    return None if any(expect in p for p in problems) else f"{path}: wanted \"{expect}\", got {problems}"


def scan_clean():
    problems = package_tool.scan(packaged())
    return f"a clean package has problems: {problems}" if problems else None


def tracked_port():
    """The tracked files, read here and not through package.py, so that a wrong list there fails."""
    names = subprocess.run(["git", "ls-tree", "-r", "--name-only", "HEAD", "port"], cwd=ROOT, capture_output=True,
                           text=True, encoding="utf-8", check=True).stdout.split()
    return {n[len("port/"):]: subprocess.run(["git", "cat-file", "blob", f"HEAD:{n}"], cwd=ROOT, capture_output=True,
                                             check=True).stdout for n in names}


def archive_holds_port():
    with zipfile.ZipFile(packaged() / ARCHIVE) as archive:
        held = {name: archive.read(name) for name in archive.namelist()}
        count = len(archive.namelist())
    expected = tracked_port()
    if count != 13 or held != expected:
        return f"archive holds {count} files, {len(set(held) ^ set(expected))} names differ from the tracked ones"
    return None


def archive_repeats():
    first = (packaged() / ARCHIVE).read_bytes()
    second = (packaged() / ARCHIVE).read_bytes()
    return None if first == second else "two packages of one commit hold different archives"


def repacked(extra=None, drop=None, change=None, compression=zipfile.ZIP_STORED):
    """A zip of the tracked files, changed as asked."""
    files = tracked_port()
    files.update(extra or {})
    files.pop(drop, None)
    if change:
        files[change] = files[change] + b"\n"
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w", compression) as archive:
        for name, data in sorted(files.items()):
            archive.writestr(zipfile.ZipInfo(name), data, compress_type=compression)
    return out.getvalue()


def good_archive():
    return (packaged() / ARCHIVE).read_bytes()


def repeated_entry():
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w") as archive, warnings.catch_warnings():
        warnings.simplefilter("ignore")
        for name, data in sorted(tracked_port().items()):
            archive.writestr(zipfile.ZipInfo(name), data)
        archive.writestr(zipfile.ZipInfo(name), data)
    return out.getvalue()


def other_port_files():
    """A file under port/ that nobody accepted must stop the package, not join it."""
    names = package_tool.PORT_FILES
    package_tool.PORT_FILES = names[:-1]
    try:
        package_tool.port_archive()
    except SystemExit as stopped:
        return None if "port/" in str(stopped) else f"stopped for another reason: {stopped}"
    finally:
        package_tool.PORT_FILES = names
    return "packed although port/ does not hold the accepted files"


SCENARIOS = {
    "clean": clean,
    "no-id": no_id,
    "scan-clean": scan_clean,
    "archive-holds-port": archive_holds_port,
    "archive-repeats": archive_repeats,
    "port-not-accepted": other_port_files,
    # A different .o2r, under any name or place but the one.
    "other-o2r-with-good-bytes": lambda: caught("other.o2r", good_archive(), "by name"),
    "good-bytes-wrong-folder": lambda: caught("assets/starship.o2r", good_archive(), "by name"),
    "good-bytes-wrong-case": lambda: caught("STARSHIP.O2R", good_archive(), "by name"),
    "different-archive": lambda: caught(ARCHIVE, zip_bytes(), "not the archive"),
    "archive-not-a-zip": lambda: caught(ARCHIVE, b"made up", "not the archive"),
    "archive-empty": lambda: caught(ARCHIVE, b"", "not the archive"),
    # The right archive with something in it.
    "archive-extra-file": lambda: caught(ARCHIVE, repacked(extra={"extra": b"made up"}), "not the archive"),
    "archive-extra-rom": lambda: caught(ARCHIVE, repacked(extra={"baserom.z64": rom_header(1)}), "not the archive"),
    "archive-file-missing": lambda: caught(ARCHIVE, repacked(drop="shaders/opengl/default.shader.fs"), "not the archive"),
    "archive-file-changed": lambda: caught(ARCHIVE, repacked(change="shaders/opengl/default.shader.fs"), "not the archive"),
    "archive-file-twice": lambda: caught(ARCHIVE, repeated_entry(), "not the archive"),
    "archive-compressed": lambda: caught(ARCHIVE, repacked(compression=zipfile.ZIP_DEFLATED), "not the archive"),
    # A zip reader does not see these, so a list of its entries would pass them.
    "archive-data-before": lambda: caught(ARCHIVE, rom_header(1) + good_archive(), "not the archive"),
    "archive-data-after": lambda: caught(ARCHIVE, good_archive() + rom_header(1), "not the archive"),
    "name-o2r": lambda: caught("sf64.o2r", b"made up", "by name"),
    "name-otr-in-assets": lambda: caught("assets/pack.OTR", b"made up", "by name"),
    "name-z64": lambda: caught("baserom.z64", b"made up", "by name"),
    "not-listed": lambda: caught("notes.bin", b"made up", "not on the list"),
    "not-listed-in-assets": lambda: caught("assets/texture.png", b"made up", "not on the list"),
    "zip-renamed": lambda: caught("README.txt", zip_bytes(), "starts like"),
    "zip-as-yaml": lambda: caught("assets/yaml/x.yaml", zip_bytes(), "starts like"),
    "mpq-renamed": lambda: caught("config.yml", b"MPQ\x1amade up", "starts like"),
}
for width, order in ((1, "z64"), (2, "v64"), (4, "n64")):
    # Four bytes in, so only the search inside a file can find it; a file that starts with it is the next one.
    SCENARIOS[f"rom-inside-{order}"] = lambda w=width: caught("config.yml", b"code" + rom_header(w), "ROM header")
    SCENARIOS[f"rom-renamed-{order}"] = lambda w=width: caught("config.yml", rom_header(w)[:4] + b"made up", "starts like")


def main():
    if SYSTEM is None:
        sys.exit("package test: Linux and Windows only, as package.py")
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    failed = 0
    for name, scenario in SCENARIOS.items():
        try:
            problem = scenario()
        except Exception as error:  # one broken scenario must not hide the others
            problem = f"{type(error).__name__}: {error}"
        failed += problem is not None
        print(f"package test: {name}: {'ok' if problem is None else 'FAIL: ' + problem}")
    if failed:
        print(f"package test: FAIL ({failed} of {len(SCENARIOS)}), kept {WORK}")
        return 1
    shutil.rmtree(WORK, ignore_errors=True)
    print(f"package test: PASS ({len(SCENARIOS)} of {len(SCENARIOS)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
