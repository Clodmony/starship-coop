#!/usr/bin/env python3
"""Repository checks for Starship Co-op. CI runs this on every pull request; run it before you push.

    python tools/coop/check.py

Prints "co-op checks: ALL OK" and exits 0, or lists every failure and exits 1.
"""
import filecmp
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GAME_DATA = re.compile(r"\.(z64|n64|v64|u64|o2r|otr)$", re.IGNORECASE)
# The Steamworks SDK sits behind an agreement: its headers and libraries stay out, the transport loads the player's own.
STEAM_SDK = re.compile(r"(^|/)((lib)?steam_api(64)?\.(dll|lib|so|dylib)|sdkencryptedappticket\w*\.\w+|"
                       r"steam_api\w*\.(h|json)|isteam\w+\.h|steamnetworkingtypes\.h|steam_appid\.txt)$",
                       re.IGNORECASE)
MAX_BYTES = 5 * 1024 * 1024


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, encoding="utf-8")


def run_tool(name, *args):
    return subprocess.run([sys.executable, "-I", str(ROOT / "tools" / "coop" / name), *args],
                          cwd=ROOT, capture_output=True, text=True, encoding="utf-8")


def dirs_differ(a: Path, b: Path):
    if not a.is_dir() or not b.is_dir():
        return True
    cmp = filecmp.dircmp(a, b)
    if cmp.left_only or cmp.right_only or cmp.funny_files:
        return True
    _, mismatch, errors = filecmp.cmpfiles(a, b, cmp.common_files, shallow=False)
    if mismatch or errors:
        return True
    return any(dirs_differ(a / d, b / d) for d in cmp.common_dirs)


def main():
    failures = []
    tracked = git("ls-files", "-z").stdout.split("\0")
    tracked = [t for t in tracked if t]

    # 1. No game data: ROM images and asset archives must never be tracked.
    bad = [t for t in tracked if GAME_DATA.search(t)]
    if bad:
        failures.append("game data tracked (ROMs or asset archives):\n  " + "\n  ".join(bad))

    bad = [t for t in tracked if STEAM_SDK.search(t)]
    if bad:
        failures.append("Steamworks SDK files tracked:\n  " + "\n  ".join(bad))

    # 2. No large files (asset dumps, builds, recordings).
    for t in tracked:
        p = ROOT / t
        if p.is_file() and not p.is_symlink() and p.stat().st_size > MAX_BYTES:
            failures.append(f"file over 5 MB: {t} ({p.stat().st_size} bytes)")

    # 3. Skills: .claude/skills must mirror .agents/skills.
    if dirs_differ(ROOT / ".agents" / "skills", ROOT / ".claude" / "skills"):
        failures.append(".claude/skills differs from .agents/skills; run python tools/coop/sync_skills.py")

    # 4. Protocol registry: message ids must be unique.
    protocol = (ROOT / "docs" / "coop" / "PROTOCOL.md").read_text(encoding="utf-8")
    ids = [m[:2].lower() + m[2:].upper() for m in re.findall(r"^\|\s*(0x[0-9A-Fa-f]{4})\s*\|", protocol, re.MULTILINE)]
    dups = sorted({i for i in ids if ids.count(i) > 1})
    if dups:
        failures.append("duplicate message ids in docs/coop/PROTOCOL.md: " + ", ".join(dups))

    # 5. Mutation checks: every break must still match the source it guards (no build, no write).
    for tool in ("net_mutants.py", "session_mutants.py", "steam_mutants.py"):
        mut = run_tool(tool, "--check")
        if mut.returncode != 0:
            failures.append(f"tools/coop/{tool} --check failed; update the break that guards the code you changed:\n  "
                            + "\n  ".join((mut.stdout + mut.stderr).strip().splitlines()))

    # 6. Script reader: its level and command tables must still match the game source (no build, no ROM).
    reader = subprocess.run([sys.executable, "-I", str(ROOT / "tools" / "coop" / "script_reader_test.py")],
                            cwd=ROOT, capture_output=True, text=True, encoding="utf-8")
    if reader.returncode != 0:
        failures.append("tools/coop/script_reader_test.py failed:\n  "
                        + "\n  ".join(reader.stdout.strip().splitlines()))

    # 7. Run folders: each test tool gets a folder of its own, however close together runs start.
    folders = subprocess.run([sys.executable, "-I", str(ROOT / "tools" / "coop" / "run_folder_test.py")],
                             cwd=ROOT, capture_output=True, text=True, encoding="utf-8")
    if folders.returncode != 0:
        failures.append("tools/coop/run_folder_test.py failed:\n  "
                        + "\n  ".join((folders.stdout + folders.stderr).strip().splitlines()))

    # 8. Single-player audit: the list must match the code, and the tool must still pass its own tests.
    for args in (("sp_audit.py", "--check"), ("sp_audit_test.py",)):
        audit = run_tool(*args)
        lines = (audit.stdout + audit.stderr).strip().splitlines()
        if audit.returncode != 0:
            failures.append(f"tools/coop/{' '.join(args)} failed:\n  " + "\n  ".join(lines))
        else:  # the result, and any warning; a passed scenario says nothing new
            print("\n".join(f"info: {line}" for line in lines if not line.endswith(": ok")))

    # 9. Crashes: a crashed game fails the run in every test tool (#116; a stand-in for the game, no build, no ROM).
    crash = run_tool("crash_test.py")
    if crash.returncode != 0:
        failures.append("tools/coop/crash_test.py failed:\n  "
                        + "\n  ".join((crash.stdout + crash.stderr).strip().splitlines()))

    # Info: where the fork diverges from upstream game code.
    grep = git("grep", "-n", "COOP:", "--", "src", "include", ":!src/port/coop")
    markers = len(grep.stdout.splitlines()) if grep.returncode == 0 else 0
    print(f'info: {markers} COOP: markers in upstream code (git grep -n "COOP:" lists them)')

    for f in failures:
        print(f"FAIL: {f}")
    if failures:
        print("co-op checks: FAILED")
        return 1
    print("co-op checks: ALL OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
