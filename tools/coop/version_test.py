#!/usr/bin/env python3
"""Tests that the game shows which co-op build it is (#26). Linux only.

    python tools/coop/version_test.py                # unattended; prints PASS or FAIL
    python tools/coop/version_test.py --exe <file>   # that build; its id comes from coop/coop_build_id.h next to it
    python tools/coop/version_test.py --list

Each scenario starts one game with the menu bar open, in its own folder, without a window and off the GPU, and captures
one shown frame with versiontest/frame_shot.c. The check reads the pixels of that frame, so it sees what a player sees:

  label   The right end of the menu bar shows "Co-op <id>", with <id> the id the handshake compares. The menu font has
          fixed-width cells, and a character looks the same in every cell, so the cells of the label must repeat exactly
          as the characters of the text do. The same frame must not pass for an id with another pattern. This is not
          OCR: it tells a right id from a wrong one, it does not spell one out.
  narrow  In a window with no room for the label, the menu bar shows only the menus: up to where label starts it equals
          the bar of label, pixel for pixel, and after that it is empty.

Needs a build with assets (python tools/coop/build.py --assets) or --exe, and a C compiler. Exit code 0 when every
scenario holds, 1 otherwise. Everything lands under local/test-*: deleted when all hold, kept for reading when one fails.
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import package as package_tool  # noqa: E402
import replay as replay_tool  # noqa: E402

ROOT = replay_tool.ROOT
WORK = ROOT / "local" / "test-version"
SHOT_SWAP = 240     # the boot logo is over and the menu bar is drawn
RECORD_FRAMES = 300  # the game stops after the capture: recorded frames start later than swaps
TIMEOUT = 180
BAR_ROWS = 24       # the menu bar is opaque for these rows at the default font
CELL = 7            # width of a character of the menu font
LABEL_PREFIX = "Co-op "  # src/port/coop/ui/version_label.cpp
WIDE = 640
NARROW = 500        # the menus end before this and the label does not fit after them


def game_frame(exe, name, width):
    """Runs one game and returns (width, height, rgb bytes) of the captured frame, or None."""
    shot = WORK / f"{name}.ppm"
    shot.unlink(missing_ok=True)
    env = {
        "SDL_VIDEODRIVER": "offscreen", "SDL_AUDIODRIVER": "dummy", "LIBGL_ALWAYS_SOFTWARE": "1",
        "GALLIUM_DRIVER": "llvmpipe", "LD_PRELOAD": str(WORK / "frame_shot.so"),
        "SF64_COOP_SHOT_FRAME": str(SHOT_SWAP), "SF64_COOP_SHOT_FILE": str(shot),
    }
    config = {"CVars": {"gOpenMenuBar": 1}, "Window": {"Width": width, "Height": 480}}
    replay = WORK / f"{name}.sfcr"
    replay.unlink(missing_ok=True)
    _, run, _ = replay_tool.run_game("record", replay, exe, TIMEOUT, tag=f"version-{name}", env_extra=env, config=config,
                                     frames=RECORD_FRAMES)
    runs.append(run)
    if not shot.exists():
        return None
    data = shot.read_bytes()
    magic, size, depth, pixels = data.split(b"\n", 3)
    w, h = (int(v) for v in size.split())
    return (w, h, pixels) if magic == b"P6" and depth == b"255" else None


def bar_rows(frame):
    """The menu bar as one bytes object per row of pixels."""
    w, _, pixels = frame
    return [pixels[y * w * 3:(y + 1) * w * 3] for y in range(BAR_ROWS)]


def text_columns(rows, width):
    """Columns of the bar with any pixel that is not the bar's own colour."""
    background = rows[0][:3]
    return [x for x in range(width) if any(row[x * 3:x * 3 + 3] != background for row in rows)]


def cell(rows, x):
    return tuple(row[x * 3:(x + CELL) * 3] for row in rows)


def cells_match(rows, start, text):
    """True when the cells from x = start repeat as the characters of text do, and a space is empty."""
    seen = {}
    background = rows[0][:3] * CELL
    for i, char in enumerate(text):
        pixels = cell(rows, start + i * CELL)
        if (char == " ") != all(row == background for row in pixels):
            return False
        seen.setdefault(char, pixels)
        if seen[char] != pixels:
            return False
    return len(set(seen.values())) == len(seen)


def label_start(rows, width, text):
    """Where text sits at the right end of the bar: a character's pixels can stop short of the end of its cell."""
    columns = text_columns(rows, width)
    if not columns:
        return None
    for slack in range(CELL):
        start = columns[-1] + 1 + slack - len(text) * CELL
        if start >= 0 and cells_match(rows, start, text):
            return start
    return None


def other_pattern(build_id):
    """An id of the same length whose first two characters repeat where build_id's do not, or the other way round."""
    second = next(c for c in "0123456789abcdef" if c != build_id[0]) if build_id[0] == build_id[1] else build_id[0]
    return build_id[0] + second + build_id[2:]


def scenario_label(exe, build_id):
    """Returns (passed, detail, frame, where the label starts)."""
    frame = game_frame(exe, "label", WIDE)
    if frame is None:
        return False, "no frame captured", None, None
    if frame[0] != WIDE:
        return False, f"frame is {frame[0]} wide, asked for {WIDE}", None, None
    rows = bar_rows(frame)
    text = LABEL_PREFIX + build_id
    columns = text_columns(rows, WIDE)
    start = label_start(rows, WIDE, text)
    if start is None:
        last = columns[-1] if columns else "none"
        return False, f"no \"{text}\" in the menu bar (last text column {last} of {WIDE})", frame, WIDE
    wrong = LABEL_PREFIX + other_pattern(build_id)
    if label_start(rows, WIDE, wrong) is not None:
        return False, f"the check cannot tell \"{text}\" from \"{wrong}\"", None, None
    detail = f"\"{text}\" at x={start}, ends {WIDE - start - len(text) * CELL} px from the edge; not \"{wrong}\""
    return True, detail, frame, start


def scenario_narrow(exe, wide, label_x):
    frame = game_frame(exe, "narrow", NARROW)
    if frame is None:
        return False, "no frame captured"
    if frame[0] != NARROW:
        return False, f"frame is {frame[0]} wide, asked for {NARROW}"
    rows = bar_rows(frame)
    menus = min(label_x, NARROW)
    differ = [y for y, (a, b) in enumerate(zip(bar_rows(wide), rows)) if a[:menus * 3] != b[:menus * 3]]
    if differ:
        return False, f"the menus differ from the wide bar on {len(differ)} of {BAR_ROWS} rows"
    extra = [x for x in text_columns(rows, NARROW) if x >= menus]
    if extra:
        return False, f"{len(extra)} columns of text after the menus, from x={extra[0]}"
    return True, f"{BAR_ROWS} rows equal to the wide bar up to x={menus}, empty after that"


runs = []


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--exe", help="Starship executable (default: newest build found)")
    p.add_argument("--list", action="store_true", help="print the scenarios and stop")
    args = p.parse_args()
    if args.list:
        print("label\nnarrow")
        return 0
    if sys.platform != "linux":
        sys.exit("version: Linux only")

    exe = replay_tool.find_exe(args.exe)
    build_id = package_tool.read_build_id(exe.parent)
    WORK.mkdir(parents=True, exist_ok=True)
    source = Path(__file__).resolve().parent / "versiontest" / "frame_shot.c"
    subprocess.run([os.environ.get("CC", "cc"), "-shared", "-fPIC", "-O1", "-o", str(WORK / "frame_shot.so"), str(source),
                    "-ldl"], check=True)
    print(f"version: build id {build_id}, exe {exe}")

    results = []
    passed, detail, wide, label_x = scenario_label(exe, build_id)
    results.append(("label", passed, detail))
    if wide is None:
        results.append(("narrow", False, "needs the frame of label"))
    else:
        results.append(("narrow", *scenario_narrow(exe, wide, label_x)))

    for name, passed, detail in results:
        print(f"version: {name}: {'ok' if passed else 'FAIL'}: {detail}")
    failed = [name for name, passed, _ in results if not passed]
    if failed:
        print(f"version: FAIL ({len(failed)} of {len(results)}), kept {WORK}")
        return 1
    for run in runs:
        shutil.rmtree(run, ignore_errors=True)
    shutil.rmtree(WORK, ignore_errors=True)
    print(f"version: PASS ({len(results)} of {len(results)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
