#!/usr/bin/env python3
"""Tests for script_reader.py (#90): a reference resolves the way the game resolves it, and a bad one says why.

    python tools/coop/script_reader_test.py                  # unattended; prints PASS or FAIL
    python tools/coop/script_reader_test.py --only table-order,decode
    python tools/coop/script_reader_test.py --list

Each scenario writes a small archive with made-up scripts and runs the tool on it, so no ROM and no game data are
involved; past-end runs the tool's --past-end and --check (#112) on every level's table. The archive has its own writer and its own CRC here, not the tool's: a mistake on one side shows. Two
scenarios read the repo's sources instead: the tool's level table against the switch in `ActorEvent_ProcessScript`,
and its command list against the same function and the header. `check.py` runs this file. No build, a few seconds.
Exit code 0 when every scenario holds, 1 otherwise. Everything lands under local/test-scriptreader-*: deleted when
all scenarios hold, kept for reading when one fails.
"""
import argparse
import contextlib
import io
import re
import shutil
import struct
import subprocess
import sys
import time
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "coop" / "script_reader.py"
EVENT_HEADER = ROOT / "include" / "sf64event.h"
INTERPRETER = ROOT / "src" / "engine" / "fox_enmy2.c"
YAML = ROOT / "assets" / "yaml" / "us"

SCRP = 0x53435250  # src/port/resource/type/ResourceType.h
SCMD = 0x53434D44
TABLE = "D_CO_603D9E8"
TABLE_PATH = "ast_corneria/" + TABLE


def crc64(text):
    """CRC-64 as `CRC64()` in Torch's StrHash64.cpp computes it, one bit at a time."""
    crc = (1 << 64) - 1
    for byte in text.encode():
        crc ^= byte << 56
        for _ in range(8):
            crc = ((crc << 1) ^ 0x42F0E1EBA9EA3693 if crc >> 63 else crc << 1) & ((1 << 64) - 1)
    return crc


def resource(kind, body, order="<"):
    header = bytes([0 if order == "<" else 1, 0, 0, 0]) + struct.pack(order + "II", kind, 0)
    return header.ljust(0x40, b"\0") + body


def op(opcode, arg1, arg2):
    return ((opcode << 9) | arg1, arg2)


def commands(pairs, order="<"):
    words = [word for pair in pairs for word in pair]
    return resource(SCMD, struct.pack(f"{order}I{len(words)}H", len(pairs), *words), order)


def table(names, order="<"):
    return resource(SCRP, struct.pack(f"{order}I{len(names)}Q", len(names), *[crc64(n) for n in names]), order)


def write_archive(path, scripts, order="<", table_order=None, extra=None):
    """`scripts` are lists of pairs, stored as _cmd_0, _cmd_1, ...; `table_order` says which one each slot holds."""
    names = [f"{TABLE_PATH}_cmd_{i}" for i in range(len(scripts))]
    if table_order is not None:
        names = [names[i] for i in table_order]
    with zipfile.ZipFile(path, "w") as archive:
        archive.writestr("version", bytes([1]) + struct.pack(">I", 0x12345678))
        archive.writestr(TABLE_PATH, table(names, order))
        for i, pairs in enumerate(scripts):
            archive.writestr(f"{TABLE_PATH}_cmd_{i}", commands(pairs, order))
        for name, data in (extra or {}).items():
            archive.writestr(name, data)
    return path


def run(work, *args):
    proc = subprocess.run([sys.executable, "-I", str(TOOL), *map(str, args)], cwd=work, capture_output=True, text=True,
                          encoding="utf-8")
    return proc.returncode, proc.stdout, proc.stderr


class Failed(Exception):
    pass


def expect(condition, message):
    if not condition:
        raise Failed(message)


def resolves(work, archive, words, text):
    code, out, err = run(work, "--o2r", archive, *words)
    expect(code == 0, f"{words}: exit {code}: {err.strip()}")
    expect(text in out, f"{words}: no {text!r} in:\n{out}")
    return out


def marked(out):
    return [line for line in out.splitlines() if line.startswith(">")]


def wait_cmds(*times):
    return [op(48, 0, t) for t in times]


def scenario_crc(work):
    """The writer's CRC is right on its own: the known check value of this CRC, without the final inversion."""
    expect(crc64("123456789") == 0x62EC59E3F1A4F00A ^ ((1 << 64) - 1), hex(crc64("123456789")))


def scenario_forms(work):
    """The ways NOTES writes a reference all reach the same command."""
    scripts = [wait_cmds(1), wait_cmds(2, 3, 4, 5)]
    archive = write_archive(work / "a.o2r", scripts)
    wanted = None
    for words in (["CO", "S1:2"], ["CO S1:2"], ["co", "s1:2"], ["Corneria", "1.2"], ["corneria", "S1.2"],
                  ["LEVEL_CORNERIA", "1:2"]):
        out = resolves(work, archive, words, "time=4")
        expect(len(marked(out)) == 1 and "time=4" in marked(out)[0], f"{words}: marked {marked(out)}")
        wanted = wanted or out
        expect(out == wanted, f"{words}: output differs from the first form")
    out = resolves(work, archive, ["CO", "S1"], "time=5")
    expect(marked(out) == [], f"no command named, yet marked {marked(out)}")


def scenario_level_names(work):
    """A level name of two words does not lose its second word to the script number."""
    code, out, err = run(work)
    expect(code == 0 and "VE1" in out and "Sector X" in out, f"level list: {code} {out} {err}")
    pairs = {"Venom 1": "aVe1EventScript", "VE1": "aVe1EventScript", "Sector X": "D_SX_60320D0", "SX": "D_SX_60320D0",
             "Area 6": "D_A6_6027F50", "A6": "D_A6_6027F50", "sector-y": "D_SY_6032E18"}
    for level, symbol in pairs.items():
        path = f"ast_x/{symbol}"
        archive = work / f"{symbol}.o2r"
        with zipfile.ZipFile(archive, "w") as out_zip:
            out_zip.writestr(path, table([path + "_cmd_0", path + "_cmd_1"]))
            out_zip.writestr(path + "_cmd_0", commands(wait_cmds(6)))
            out_zip.writestr(path + "_cmd_1", commands(wait_cmds(8)))
        resolves(work, archive, level.split() + ["1"], "time=8")
        listing = resolves(work, archive, level.split(), "2 scripts")
        expect("S1" in listing and "1 command" in listing, listing)


def scenario_table_order(work):
    """The script number is the table slot, as `levelScripts[this->aiType]`; the number in an entry name is not."""
    archive = write_archive(work / "a.o2r", [wait_cmds(10), wait_cmds(20), wait_cmds(30)], table_order=[2, 0, 1])
    resolves(work, archive, ["CO", "S0"], "time=30")
    resolves(work, archive, ["CO", "S1"], "time=10")
    resolves(work, archive, ["CO", "S2"], "time=20")


def scenario_command_index(work):
    """A command is two words: the index is `aiIndex / 2`, and a jump lands on the same numbering."""
    archive = write_archive(work / "a.o2r", [[op(48, 0, 7), op(121, 300, 2), op(127, 0, 0)]])
    out = resolves(work, archive, ["CO", "S0:1"], "EVOP_DAMAGE_TEAM")
    expect(len(marked(out)) == 1 and "EVOP_DAMAGE_TEAM" in marked(out)[0], f"marked {marked(out)}")


def decode_cases():
    """(opcode, arg1, arg2, what the interpreter reads from them), written from fox_enmy2.c, not from the macros."""
    speed = (2 << 7) | 33  # EMZ_PLAYER in bits 7-8, speed in the low 7
    return [
        (0, speed, 44, ["speed=33", "EMZ_PLAYER", "time=44"]),
        (1, speed, 44, ["speed=33", "EMZ_PLAYER", "time=44"]),
        (2, 0, 0x1FF, ["zvel=255"]),
        (3, 0, 0, []),
        (4, 0, 45, ["time=45"]),
        (8, 0, 0, []),
        (9, 90, 25, ["angle=90", "step=2.5"]),
        (10, 90, 25, ["angle=90", "step=2.5"]),
        (11, 90, 25, ["angle=90", "step=2.5"]),
        (12, 90, 25, ["angle=90", "step=2.5"]),
        (16, 90, 25, ["angle=90", "step=2.5"]),
        (17, 90, 25, ["angle=90", "step=2.5"]),
        (18, 90, 25, ["angle=90", "step=2.5"]),
        (19, 90, 25, ["angle=90", "step=2.5"]),
        (20, 90, 25, ["angle=90", "step=2.5"]),
        (21, 90, 25, ["angle=90", "step=2.5"]),
        (24, 0, 0, []),
        (25, 0, 0, []),
        (40, 15, 46, ["time=46", "turn=15"]),
        (41, 15, 46, ["time=46", "turn=15"]),
        (42, 15, 46, ["time=46", "turn=15"]),
        (43, 15, 46, ["time=46", "turn=15"]),
        (44, 15, 46, ["time=46", "turn=15"]),
        (45, 1, 47, ["TEAM_ID_FALCO", "spread=47"]),
        (46, 15, 46, ["time=46", "turn=15"]),
        (47, 15, 46, ["time=46", "turn=15"]),
        (48, 0, 48, ["time=48"]),
        (56, 9, 49, ["time=49", "voice=9"]),
        (57, 0, 2, ["TEAM_ID_SLIPPY"]),
        (58, 0, 16, ["EVSFX_SEARCHLIGHT_MISS"]),
        (59, 0, 16, ["EVSFX_SEARCHLIGHT_MISS"]),
        (96, 199, 1, ["EVC_NO_TARGET", "command 199"]),
        (96, 200, 45, ["EVC_PASSED_ALL_RINGS", "script 0"]),
        (96, 511, 67, ["EVC_EXPERT_MODE", "script 311"]),
        (96, 5, 0, ["EVC_NONE", "command 5"]),
        (96, 3, 100, ["EVC_CLOSE_Z", "z=0", "command 3"]),
        (96, 3, 125, ["EVC_CLOSE_Z", "z=2500", "command 3"]),
        (104, 500, 43, ["EVID_TEAMMATE", "health=500"]),
        (104, 7, 201, ["201", "health=7"]),
        (105, 0, 3, ["TEAM_ID_PEPPY"]),
        (112, 0, 18, ["EVACT_GFOX_COVER_FIRE"]),
        (113, 8, 50, ["group=50", "flags=8"]),
        (116, 0, 13, ["DROP_1UP"]),
        (118, 0, 51, ["reverb=51"]),
        (119, 0, 2, ["SURFACE_WATER"]),
        (120, 10, 52, ["msg=52", "RCID_FALCO"]),
        (121, 255, 1, ["TEAM_ID_FALCO", "damage=255"]),
        (122, 0, 0, []),
        (124, 0, 4, ["TXLC_RED"]),
        (125, 0, 0, []),
        (126, 199, 53, ["count=53", "command 199"]),
        (126, 200, 53, ["count=53", "script 0"]),
        (127, 0, 0, []),
    ]


def scenario_decode(work):
    """Every command prints its name and what the interpreter takes from each half."""
    cases = decode_cases()
    archive = write_archive(work / "a.o2r", [[op(*case[:3]) for case in cases]])
    code, out, err = run(work, "--o2r", archive, "CO", "S0")
    expect(code == 0, f"exit {code}: {err}")
    lines = [line for line in out.splitlines() if re.match(r"\s+\d+\s", line)]
    expect(len(lines) == len(cases), f"{len(lines)} commands printed, {len(cases)} written:\n{out}")
    header = EVENT_HEADER.read_text(encoding="utf-8")
    for line, (opcode, arg1, arg2, parts) in zip(lines, cases):
        name = re.search(rf"/\*\s*{opcode} \*/ (EVOP_\w+)", header).group(1)
        expect(re.search(rf"\b{name}\b", line), f"{opcode}: no {name} in {line!r}")
        words = f"{(opcode << 9) | arg1:04X} {arg2:04X}"
        expect(words in line, f"{opcode}: raw words {words} missing from {line!r}")
        for part in parts:
            expect(part in line, f"{opcode} {arg1} {arg2}: no {part!r} in {line!r}")


def scenario_unknown_values(work):
    """A value no enum has still prints, as a number: the reader sees it instead of a crash or a blank."""
    archive = write_archive(work / "a.o2r", [[op(5, 3, 4), op(45, 9, 1), op(116, 0, 99), op(112, 0, 300)]])
    code, out, err = run(work, "--o2r", archive, "CO", "S0")
    expect(code == 0, f"exit {code}: {err}")
    for part in ("opcode 5", "team 9", "item 99", "action 300"):
        expect(part in out, f"no {part!r} in:\n{out}")


def scenario_big_endian(work):
    """The first byte of a resource says its byte order; a file built on another machine still reads."""
    archive = write_archive(work / "a.o2r", [wait_cmds(0x0102)], order=">")
    resolves(work, archive, ["CO", "S0"], "time=258")


def scenario_version(work):
    """A reference is only good for one ROM, so the archive's version shows with the script; none is not an error."""
    archive = write_archive(work / "a.o2r", [wait_cmds(1)])
    resolves(work, archive, ["CO", "S0"], "12345678")
    bare = work / "bare.o2r"
    with zipfile.ZipFile(archive) as src, zipfile.ZipFile(bare, "w") as dst:
        for name in src.namelist():
            if name != "version":
                dst.writestr(name, src.read(name))
    resolves(work, bare, ["CO", "S0"], "version unknown")


def refused(work, args, text):
    code, out, err = run(work, *args)
    expect(code == 2, f"{args}: exit {code}, wanted 2: {out} {err}")
    expect(text in err, f"{args}: no {text!r} in {err!r}")
    expect("Traceback" not in err, f"{args}: traceback:\n{err}")
    expect(out == "", f"{args}: printed {out!r}")


def scenario_bad_references(work):
    """A reference that does not exist says what is wrong and prints no script."""
    archive = write_archive(work / "a.o2r", [wait_cmds(1, 2), wait_cmds(3)])
    o2r = ["--o2r", archive]
    refused(work, [*o2r, "XX", "S1"], "XX S1")
    refused(work, [*o2r, "CO", "banana"], "CO banana")
    refused(work, [*o2r, "CO", "S2"], "2 scripts")
    refused(work, [*o2r, "CO", "S0:2"], "2 commands")
    refused(work, [*o2r, "FO", "S1"], "no script table")
    refused(work, [*o2r, "Sector Z"], "no script table")


def scenario_bad_files(work):
    """A file that is not what it should be stops with a sentence, not a traceback."""
    missing = work / "missing.o2r"
    refused(work, ["--o2r", missing, "CO", "S0"], "missing.o2r")
    text = work / "text.o2r"
    text.write_text("not a zip", encoding="utf-8")
    refused(work, ["--o2r", text, "CO", "S0"], "text.o2r")

    good = [wait_cmds(1)]
    no_table = work / "no-table.o2r"
    with zipfile.ZipFile(no_table, "w") as archive:
        archive.writestr("other", b"x")
    refused(work, ["--o2r", no_table, "CO", "S0"], TABLE)

    wrong_kind = write_archive(work / "wrong-kind.o2r", good)
    with zipfile.ZipFile(work / "wrong-kind2.o2r", "w") as dst, zipfile.ZipFile(wrong_kind) as src:
        for name in src.namelist():
            dst.writestr(name, src.read(name) if name != TABLE_PATH else resource(SCMD, struct.pack("<I", 0)))
    refused(work, ["--o2r", work / "wrong-kind2.o2r", "CO", "S0"], "not a script table")

    short = work / "short.o2r"
    with zipfile.ZipFile(short, "w") as archive:
        archive.writestr(TABLE_PATH, resource(SCRP, struct.pack("<I", 5)))
    refused(work, ["--o2r", short, "CO", "S0"], "cut short")

    dangling = work / "dangling.o2r"
    with zipfile.ZipFile(dangling, "w") as archive:
        archive.writestr(TABLE_PATH, table([TABLE_PATH + "_cmd_0"]))
    refused(work, ["--o2r", dangling, "CO", "S0"], "no entry")

    cut = work / "cut.o2r"
    with zipfile.ZipFile(cut, "w") as archive:
        archive.writestr(TABLE_PATH, table([TABLE_PATH + "_cmd_0"]))
        archive.writestr(TABLE_PATH + "_cmd_0", commands(wait_cmds(1, 2))[:-2])
    refused(work, ["--o2r", cut, "CO", "S0"], "cut short")

    damaged = work / "damaged.o2r"
    data = write_archive(damaged, [wait_cmds(0x4142)]).read_bytes()
    damaged.write_bytes(data.replace(struct.pack("<H", 0x4142), struct.pack("<H", 0x4143)))
    refused(work, ["--o2r", damaged, "CO", "S0"], "Bad CRC")


def scenario_read_only(work):
    """The tool reads the archive and leaves it, and the folder it runs in, as they were."""
    archive = write_archive(work / "a.o2r", [wait_cmds(1)])
    before = archive.read_bytes(), sorted(p.name for p in work.iterdir())
    resolves(work, archive, ["CO", "S0"], "time=1")
    expect((archive.read_bytes(), sorted(p.name for p in work.iterdir())) == before, "something changed on disk")


def tool_namespace():
    """The tool's tables, read by running the file in this process."""
    namespace = {"__name__": "script_reader", "__file__": str(TOOL)}
    exec(compile(TOOL.read_text(encoding="utf-8"), str(TOOL), "exec"), namespace)
    return namespace


def scenario_levels_match_source(work):
    """The tool's table is the switch the game runs, and every symbol is a script in both US ROM lists."""
    source = INTERPRETER.read_text(encoding="utf-8")
    body = source[source.index("void ActorEvent_ProcessScript("):source.index("actorScript = LOAD_ASSET(levelScripts")]
    game = dict(re.findall(r"case (LEVEL_\w+):\s*levelScripts = \(uint16_t\*\*\) LOAD_ASSET\((\w+)\);", body))
    default = re.search(r"default:\s*levelScripts = \(uint16_t\*\*\) LOAD_ASSET\((\w+)\);", body).group(1)
    expect(len(game) == 14, f"{len(game)} levels in the switch, was 14: read the change, then fix this test")
    levels = tool_namespace()["LEVELS"]
    ours = {level.enum: level.symbol for level in levels}
    expect(ours == game, f"differs from the switch: {set(ours.items()) ^ set(game.items())}")
    expect(default == ours["LEVEL_CORNERIA"], f"default table is {default}")
    for rev in ("rev0", "rev1"):
        scripts = set()
        for path in (YAML / rev).glob("*.yaml"):
            scripts.update(re.findall(r"type:\s*SF64:SCRIPT\b.*?symbol:\s*(\w+)", path.read_text(encoding="utf-8")))
        expect(scripts == set(ours.values()), f"{rev}: differs from SF64:SCRIPT rows: {scripts ^ set(ours.values())}")


def scenario_commands_match_source(work):
    """A command added to the header or to the interpreter fails here until the tool prints it."""
    header = re.findall(r"\b(EVOP_\w+)\b", EVENT_HEADER.read_text(encoding="utf-8").split("typedef enum EventOpcode")[1]
                        .split("} EventOpcode")[0])
    source = INTERPRETER.read_text(encoding="utf-8")
    body = source[source.index("void ActorEvent_ProcessScript("):source.index("void ActorEvent_UpdateTexLines(")]
    cases = re.findall(r"case EV_OPC\((EVOP_\w+)\):", body)
    formats = tool_namespace()["FORMATS"]
    expect(set(header) == set(cases), f"header and interpreter differ: {set(header) ^ set(cases)}")
    expect(set(formats) == set(header), f"tool and header differ: {set(formats) ^ set(header)}")


def write_every_table(path, extra_scripts, gap=None):
    """Every level's table with two plain scripts, plus `extra_scripts` {prefix: [pairs, ...]} after them. A table
    named in `gap` numbers its entries 0 and 2, as no Torch export does."""
    with zipfile.ZipFile(path, "w") as archive:
        archive.writestr("version", bytes([1]) + struct.pack(">I", 0x12345678))
        for level in tool_namespace()["LEVELS"]:
            base = f"ast_x/{level.symbol}"
            scripts = [wait_cmds(1), wait_cmds(2, 3)] + extra_scripts.get(level.prefix, [])
            numbers = [0, 2] + list(range(3, len(scripts) + 1)) if level.prefix == gap else range(len(scripts))
            names = [f"{base}_cmd_{n}" for n in numbers]
            archive.writestr(base, table(names))
            for name, pairs in zip(names, scripts):
                archive.writestr(name, commands(pairs))
    return path


def past_end_check(archive, known):
    """print_past_end --check in this process, with `known` as its list: (exit code, output)."""
    tool = tool_namespace()
    tool["KNOWN_PAST_END"] = known
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        code = tool["print_past_end"](tool["Archive"](archive), tool["Names"](tool["load_enums"]()), True)
    return code, out.getvalue()


def scenario_past_end(work):
    """#112: a branch at or past its script's end is listed, one inside is not, and --check holds the list."""
    loop, trigger = 126, 96  # EVOP_LOOP, EVOP_SET_TRIGGER
    archive = write_every_table(work / "a.o2r", {
        "CO": [[op(loop, 0, 1), op(trigger, 2, 7), op(loop, 3, 1)]],  # S2: targets 0 and 2 inside, 3 one past the end
        "SX": [[op(trigger, 200 + 3, 7), op(loop, 200 + 2, 1)]],  # S2: script 3 past the table of 3, script 2 inside
    })
    found = {"CO S2:2": "command 3 of 0-2", "SX S2:0": "script 3 of 0-2"}
    tool = tool_namespace()
    have, unordered = tool["past_end"](tool["Archive"](archive), tool["Names"](tool["load_enums"]()))
    expect(have == found and unordered == [], f"found {have}, unordered {unordered}")
    code, out, err = run(work, "--o2r", archive, "--past-end")
    expect(code == 0 and "CO S2:2 -> command 3 of 0-2" in out and "2 branches in 14 tables" in out, f"{code} {out}{err}")
    code, out, err = run(work, "--o2r", archive, "--past-end", "--check")
    expect(code == 1 and "CO S2:2 is new" in out and "A6 S26:53 is listed" in out, f"the real list passed: {out}")
    code, out = past_end_check(archive, dict.fromkeys(found, "why"))
    expect(code == 0 and "past the end: ok" in out, f"the matching list failed: {out}")
    code, out = past_end_check(archive, {"CO S2:2": "why"})
    expect(code == 1 and "SX S2:0 is new" in out, f"a new branch passed: {out}")
    code, out = past_end_check(archive, {**dict.fromkeys(found, "why"), "ME S0:0": "why"})
    expect(code == 1 and "ME S0:0 is listed" in out, f"a lost branch passed: {out}")
    gap = write_every_table(work / "gap.o2r", {}, gap="VE1")
    code, out = past_end_check(gap, {})
    expect(code == 1 and "VE1 (ast_x/aVe1EventScript)" in out, f"a table out of ROM order passed: {out}")


SCENARIOS = {
    "crc": scenario_crc,
    "forms": scenario_forms,
    "level-names": scenario_level_names,
    "table-order": scenario_table_order,
    "command-index": scenario_command_index,
    "decode": scenario_decode,
    "unknown-values": scenario_unknown_values,
    "big-endian": scenario_big_endian,
    "version": scenario_version,
    "bad-references": scenario_bad_references,
    "bad-files": scenario_bad_files,
    "read-only": scenario_read_only,
    "levels-match-source": scenario_levels_match_source,
    "commands-match-source": scenario_commands_match_source,
    "past-end": scenario_past_end,
}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--only", help="comma-separated scenario names")
    parser.add_argument("--list", action="store_true", help="print the scenario names")
    args = parser.parse_args()
    if args.list:
        print("\n".join(SCENARIOS))
        return 0
    names = args.only.split(",") if args.only else list(SCENARIOS)
    unknown = [n for n in names if n not in SCENARIOS]
    if unknown:
        print(f"unknown scenario: {', '.join(unknown)}")
        return 1

    base = ROOT / "local" / f"test-scriptreader-{int(time.time() * 1000)}"
    failed = 0
    for name in names:
        work = base / name
        work.mkdir(parents=True)
        try:
            SCENARIOS[name](work)
            print(f"ok    {name}")
        except Exception as failure:  # a crash in one scenario must not hide the others
            failed += 1
            print(f"FAIL  {name}: {failure!r}" if not isinstance(failure, Failed) else f"FAIL  {name}: {failure}")
    if failed:
        print(f"script_reader_test: FAIL ({failed} of {len(names)}), folders in {base.relative_to(ROOT)}")
        return 1
    shutil.rmtree(base)
    print(f"script_reader_test: PASS ({len(names)} scenarios)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
