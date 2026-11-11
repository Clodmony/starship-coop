#!/usr/bin/env python3
"""A level script's branch past its end, in a running game (#112). Linux only, as it needs gdb.

    python tools/coop/script_end_check.py                         # on the newest build
    python tools/coop/script_end_check.py --exe <Starship>
    python tools/coop/script_end_check.py --exe <build before the fix> --expect-stray   # the check turned round

Venom 1's script 40 (VE1 S40, `tools/coop/script_reader.py VE1 S40`) sets a trigger on Falco being active, waits a
frame, and loops to command 21 of its 0-20 when the trigger did not fire. On the N64 that is the first command of the
script after it in the ROM; the port joins a level's scripts in that order
(src/port/resource/importers/ScriptFactory.cpp). One game plays Venom 1 from past its intro, with gInvincible, under
gdb: when script 40 checks its trigger gdb sets Falco's shields to 0, so the loop is taken, and it logs each command
the actor then runs past command 20. They must be the commands of the script after it, as script_reader.py reads them
from the archive the game loaded, and the actor must reach that script's end. --expect-stray wants the first of them
to differ: run it on a build before the fix to show the check can fail (there it reads past its buffer, so what it
reads is whatever lies there). About a minute. Folders: local/test-script-end/ for the gdb script and log, the game's
own under local/test-replay-*. Prints one line per check and a RESULT line; exit 0 when all hold.
"""
import argparse
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import replay as replay_tool  # noqa: E402
import script_reader  # noqa: E402

ROOT = replay_tool.ROOT
INTERPRETER = ROOT / "src" / "engine" / "fox_enmy2.c"
LEVEL_VENOM_1 = 6  # include/sf64level.h
SCRIPT, LOOP_INDEX = 40, 6  # VE1 S40:6, the loop to command 21
VENOM_1 = replay_tool.Start("venom1", LEVEL_VENOM_1, True, 10, 14, replay_tool.START_SEEDS)
FRAMES = 3000
BUTTON_A = 0x8000

GDB = """set pagination off
set confirm off
set logging file {log}
set logging overwrite on
set logging redirect on
set logging enabled on
handle SIGPIPE pass nostop noprint
handle SIGTERM pass nostop noprint
break Play_Init
commands
silent
printf "START level=%d\\n", gCurrentLevel
continue
end
break fox_enmy2.c:{trigger_line} if this->aiType == {script}
commands
silent
set var gTeamShields[1] = 0
printf "TRIGGER index=%d\\n", this->aiIndex
continue
end
break fox_enmy2.c:{loop_line} if this->aiType == {script}
commands
silent
printf "LOOP index=%d count=%d done=%d\\n", this->aiIndex, actorScript[this->aiIndex + 1], this->iwork[0]
continue
end
break fox_enmy2.c:{switch_line} if this->aiType == {script} && this->aiIndex >= {end}
commands
silent
printf "PAST index=%d word1=%d word2=%d\\n", this->aiIndex, actorScript[this->aiIndex], actorScript[this->aiIndex + 1]
continue
end
run
"""


def line_after(path, text, start_text=None):
    """The number of the line after the first one holding text (after the line holding start_text, if given)."""
    lines = path.read_text(encoding="utf-8").splitlines()
    start = next(n for n, line in enumerate(lines) if start_text in line) if start_text else 0
    for number in range(start, len(lines)):
        if text in lines[number]:
            return number + 2
    sys.exit(f"script_end_check: {text!r} not found in {path}")


def expected_commands(o2r):
    """(commands of VE1 S40, commands of the entry after it in ROM order), from the archive."""
    archive = script_reader.Archive(o2r)
    level = script_reader.find_level("VE1")
    table_entry, slots = archive.table(level)
    entry, ours = archive.commands(table_entry, SCRIPT, slots[SCRIPT])
    number = int(entry.rpartition("_cmd_")[2])
    following = f"{table_entry}_cmd_{number + 1}"
    data, order, count = archive.resource(following, script_reader.RESOURCE_COMMANDS, "a script")
    words = struct.unpack_from(f"{order}{count * 2}H", data, script_reader.HEADER_SIZE + 4)
    return ours, list(zip(words[0::2], words[1::2]))


def write_replay(path, frames):
    """Pad 0 fires every 30 frames, nothing else."""
    data = bytearray(b"SFCR" + struct.pack("<III6i", 2, 20, 204, *VENOM_1.seeds))
    idle = bytes(20)
    for frame in range(frames):
        pads = struct.pack("<H18x", BUTTON_A if frame % 30 == 25 else 0) + idle * 3
        data += struct.pack("<I", frame) + pads + pads + bytes(40)
    path.write_bytes(data)


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--exe", help="Starship executable (default: newest build found)")
    p.add_argument("--expect-stray", action="store_true", help="for a build before the fix: what it reads must differ")
    args = p.parse_args()
    if sys.platform != "linux":
        sys.exit("script_end_check: Linux only, it runs the game under gdb")
    exe = replay_tool.find_exe(args.exe)
    o2r = replay_tool.find_data(exe, "sf64.o2r")
    ours, following = expected_commands(o2r)
    folder = ROOT / "local" / "test-script-end"
    folder.mkdir(parents=True, exist_ok=True)
    log = folder / "gdb.log"
    (folder / "end.gdb").write_text(GDB.format(
        log=log, script=SCRIPT, end=len(ours) * 2,
        trigger_line=line_after(INTERPRETER, "case EVC_FALCO_ACTIVE:"),
        loop_line=line_after(INTERPRETER, "case EV_OPC(EVOP_LOOP):"),
        switch_line=line_after(INTERPRETER, "actorScript = LOAD_ASSET(levelScripts[this->aiType]);") + 1),
        encoding="utf-8")
    wrapper = folder / "end-gdb.sh"
    wrapper.write_text(f'#!/bin/sh\nexec gdb -q -batch -x "{folder / "end.gdb"}" --args "{exe}"\n', encoding="utf-8")
    wrapper.chmod(0o755)
    replay = folder / "input.sfcr"
    write_replay(replay, FRAMES)
    _, game, seconds = replay_tool.run_game("play", replay, wrapper, 900, tag="script-end", start=VENOM_1,
                                            config={"CVars": {"gInvincible": 1}})
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    stops = [(m[1], {k: int(v) for k, v in (f.split("=") for f in m[2].split())})
             for m in re.finditer(r"^(START|TRIGGER|LOOP|PAST) (.*)$", text, re.MULTILINE)]
    checks = [(any(k == "START" and v["level"] == LEVEL_VENOM_1 for k, v in stops),
               f"gdb saw Venom 1 start ({seconds:.0f} s, folder {game})")]
    taken = [v for k, v in stops if k == "LOOP" and v["index"] == LOOP_INDEX * 2 and v["done"] <= v["count"]]
    checks.append((bool(taken), f"script {SCRIPT} took its loop at command {LOOP_INDEX} on {len(taken)} frames"))
    past = []
    for kind, v in stops:
        if kind == "PAST":
            pair = (v["word1"] & 0xFFFF, v["word2"] & 0xFFFF)
            if not past or past[-1] != (v["index"], pair):
                past.append((v["index"], pair))
    first = past[0] if past else None
    want = (len(ours) * 2, following[0])
    if args.expect_stray:
        checks.append((first is not None and first != want,
                       f"the first command past the end is {first}, not the next script's {want}: this build reads "
                       f"past its buffer"))
    else:
        checks.append((first == want, f"the first command past the end is {first}, the next script's {want}"))
        ran = [pair for index, pair in past if index < (len(ours) + len(following)) * 2]
        inside = all(pair == following[(index - len(ours) * 2) // 2] for index, pair in past
                     if index < (len(ours) + len(following)) * 2)
        checks.append((bool(ran) and inside, f"{len(ran)} commands past the end, each the next script's own"))
    failed = 0
    for ok, text_ in checks:
        print(f"script_end_check: {'ok  ' if ok else 'FAIL'} {text_}", flush=True)
        failed += 0 if ok else 1
    print(f"RESULT: {'PASS' if not failed else f'FAIL, {failed} checks'} on {exe}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
