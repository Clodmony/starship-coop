#!/usr/bin/env python3
"""Prints a level event script from your own sf64.o2r (#90).

    python tools/coop/script_reader.py CO S59:34       # script 59 of Corneria, command 34 marked with >
    python tools/coop/script_reader.py Corneria 17.8   # the same kind of reference, as the #60 notes write it
    python tools/coop/script_reader.py CO S59          # the whole script, nothing marked
    python tools/coop/script_reader.py CO              # the scripts of a level and their sizes
    python tools/coop/script_reader.py                 # the levels that have scripts
    python tools/coop/script_reader.py CO S59 --o2r <file>

The scripts are ROM data, so docs/coop/NOTES.md cites them as level, script and command: the script is the slot in the
level's table (`levelScripts[this->aiType]`), the command is `aiIndex / 2`. This follows the same steps as the game:
the level picks a table (`ActorEvent_ProcessScript`, src/engine/fox_enmy2.c), a slot of the table holds the CRC64 of
an entry's path, and that entry holds the commands. The file format is what tools/Torch writes in
src/factories/sf64/ScriptFactory.cpp, the arguments are printed as the interpreter reads them, and the names come from
the headers in include/, so they follow the source. Message ids stay numbers.

It reads the one file you name and writes nothing. What it prints is game data: keep it out of git, issues and PRs.
Exit code 0, or 2 with one line on stderr saying what is wrong.
"""
import argparse
import re
import struct
import sys
import zipfile
from collections import namedtuple
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ARCHIVE_NAME = "sf64.o2r"
ARCHIVE_FOLDERS = ("build-cmake", "build/x64", ".")

Level = namedtuple("Level", "enum prefix name symbol")

# The switch in ActorEvent_ProcessScript, in its order. Prefixes as in include/sf64event.h; LEVEL_UNK_4 has none there.
LEVELS = [
    Level("LEVEL_VENOM_ANDROSS", "AND", "Andross", "D_ANDROSS_C037E3C"),
    Level("LEVEL_CORNERIA", "CO", "Corneria", "D_CO_603D9E8"),
    Level("LEVEL_METEO", "ME", "Meteo", "D_ME_602F3AC"),
    Level("LEVEL_AQUAS", "AQ", "Aquas", "D_AQ_60308B8"),
    Level("LEVEL_TITANIA", "TI", "Titania", "D_TI_600631C"),
    Level("LEVEL_SECTOR_X", "SX", "Sector X", "D_SX_60320D0"),
    Level("LEVEL_UNK_4", "UNK4", "Unk 4", "D_A6_60289FC"),
    Level("LEVEL_AREA_6", "A6", "Area 6", "D_A6_6027F50"),
    Level("LEVEL_SECTOR_Y", "SY", "Sector Y", "D_SY_6032E18"),
    Level("LEVEL_SOLAR", "SO", "Solar", "D_SO_6020DD0"),
    Level("LEVEL_ZONESS", "ZO", "Zoness", "D_ZO_602AAC0"),
    Level("LEVEL_VENOM_1", "VE1", "Venom 1", "aVe1EventScript"),
    Level("LEVEL_MACBETH", "MA", "Macbeth", "D_MA_60381D8"),
    Level("LEVEL_TRAINING", "TR", "Training", "D_TR_6009B34"),
]
# Levels the switch leaves to `default`, which is Corneria's table.
NO_TABLE = {"FO": "Fortuna", "KA": "Katina", "BO": "Bolse", "SZ": "Sector Z", "VE2": "Venom 2", "WZ": "Warp Zone"}

HEADER_SIZE = 0x40  # BaseExporter::WriteHeader in tools/Torch
RESOURCE_SCRIPT = 0x53435250  # src/port/resource/type/ResourceType.h
RESOURCE_COMMANDS = 0x53434D44
CHANGE_SCRIPT = 200  # EV_CHANGE_AI: a branch target from here on is a script, below it a command
CLOSE_Z = "EVC_CLOSE_Z"

ENUMS = {
    "EventOpcode": "sf64event.h",
    "EventCondition": "sf64event.h",
    "EventActorId": "sf64event.h",
    "EventAction": "sf64event.h",
    "EventSfx": "sf64event.h",
    "EventModeZ": "sf64event.h",
    "TexLineColor": "sf64event.h",
    "TeamId": "sf64player.h",
    "ItemDrop": "sf64object.h",
    "GroundSurface": "sf64level.h",
    "RadioCharacterId": "sf64mesg.h",
}


class ReaderError(Exception):
    pass


def crc64_table():
    table = []
    for byte in range(256):
        crc = byte << 56
        for _ in range(8):
            crc = ((crc << 1) ^ 0x42F0E1EBA9EA3693 if crc >> 63 else crc << 1) & ((1 << 64) - 1)
        table.append(crc)
    return table


CRC64_TABLE = crc64_table()


def crc64(text):
    """`CRC64()` in tools/Torch/lib/strhash64/StrHash64.cpp: the hash a script table stores for an entry's path."""
    crc = (1 << 64) - 1
    for byte in text.encode("utf-8"):
        crc = CRC64_TABLE[((crc >> 56) ^ byte) & 0xFF] ^ ((crc << 8) & ((1 << 64) - 1))
    return crc


def parse_enum(source, name):
    match = re.search(rf"typedef enum {name}\s*\{{(.*?)\}}\s*{name}\s*;", source, re.DOTALL)
    if not match:
        raise ReaderError(f"no enum {name} in the headers")
    values = {}
    value = 0
    for item in re.sub(r"/\*.*?\*/|//[^\n]*", "", match.group(1), flags=re.DOTALL).split(","):
        ident, _, assigned = item.strip().partition("=")
        if not ident:
            continue
        if assigned:
            value = int(assigned.strip(), 0)
        values.setdefault(value, ident.strip())
        value += 1
    return values


def load_enums():
    sources = {}
    enums = {}
    for name, header in ENUMS.items():
        if header not in sources:
            sources[header] = (ROOT / "include" / header).read_text(encoding="utf-8")
        enums[name] = parse_enum(sources[header], name)
    return enums


class Names:
    def __init__(self, enums):
        self.enums = enums

    def name(self, enum, value, label):
        return self.enums[enum].get(value, f"{label} {value}")

    def team(self, value):
        return self.name("TeamId", value, "team")

    def target(self, value):
        return f"script {value - CHANGE_SCRIPT}" if value >= CHANGE_SCRIPT else f"command {value}"

    def condition(self, value):
        close_z = next(key for key, name in self.enums["EventCondition"].items() if name == CLOSE_Z)
        if value >= close_z:
            return f"{CLOSE_Z} z={(value - close_z) * 100}"
        return self.name("EventCondition", value, "condition")


def speed_format(names, arg1, arg2):
    return f"speed={arg1 & 0x7F} {names.name('EventModeZ', (arg1 >> 7) & 3, 'zmode')} time={arg2}"


def turn_format(names, arg1, arg2):
    return f"angle={arg1} step={arg2 / 10:.1f}"


def chase_format(names, arg1, arg2):
    return f"time={arg2} turn={arg1}"


def plain_format(names, arg1, arg2):
    return ""


def sfx_format(names, arg1, arg2):
    return names.name("EventSfx", arg2, "sfx")


# What each case of ActorEvent_ProcessScript takes from the 9-bit arg1 and the 16-bit arg2. Some macros in
# include/sf64event.h name these the other way round (EVENT_F4_PLUS_X); the interpreter is what runs.
FORMATS = {
    "EVOP_SET_SPEED": speed_format,
    "EVOP_SET_ACCEL": speed_format,
    "EVOP_SET_BASE_ZVEL": lambda n, a, b: f"zvel={b & 0xFF}",
    "EVOP_SET_AS_LEADER": plain_format,
    "EVOP_START_FORMATION": lambda n, a, b: f"time={b}",
    "EVOP_STOP_FORMATION": plain_format,
    "EVOP_F4_PLUS_X": turn_format,
    "EVOP_F4_MINUS_X": turn_format,
    "EVOP_F4_PLUS_Y": turn_format,
    "EVOP_F4_MINUS_Y": turn_format,
    "EVOP_ROT_PLUS_X": turn_format,
    "EVOP_ROT_MINUS_X": turn_format,
    "EVOP_ROT_PLUS_Y": turn_format,
    "EVOP_ROT_MINUS_Y": turn_format,
    "EVOP_ROT_PLUS_Z": turn_format,
    "EVOP_ROT_MINUS_Z": turn_format,
    "EVOP_SET_ROTATE": plain_format,
    "EVOP_STOP_ROTATE": plain_format,
    "EVOP_PURSUE_PLAYER": chase_format,
    "EVOP_FLEE_PLAYER": chase_format,
    "EVOP_PURSUE_TARGET": chase_format,
    "EVOP_FLEE_TARGET": chase_format,
    "EVOP_CHASE_TARGET": chase_format,
    "EVOP_SET_TARGET": lambda n, a, b: f"{n.team(a)} spread={b}",
    "EVOP_PURSUE_CAMERA": chase_format,
    "EVOP_FLEE_CAMERA": chase_format,
    "EVOP_SET_WAIT": lambda n, a, b: f"time={b}",
    "EVOP_SET_CALL": lambda n, a, b: f"time={b} voice={a}",
    "EVOP_RESTORE_TEAM": lambda n, a, b: n.team(b),
    "EVOP_PLAY_SFX": sfx_format,
    "EVOP_STOP_SFX": sfx_format,
    "EVOP_SET_TRIGGER": lambda n, a, b: f"{n.condition(b)} -> {n.target(a)}",
    "EVOP_INIT_ACTOR": lambda n, a, b: f"{n.name('EventActorId', b, 'actor')} health={a}",
    "EVOP_SET_TEAM_ID": lambda n, a, b: n.team(b),
    "EVOP_SET_ACTION": lambda n, a, b: n.name("EventAction", b, "action"),
    "EVOP_ADD_TO_GROUP": lambda n, a, b: f"group={b} flags={a}",
    "EVOP_DROP_ITEM": lambda n, a, b: n.name("ItemDrop", b, "item"),
    "EVOP_SET_REVERB": lambda n, a, b: f"reverb={b}",
    "EVOP_SET_SURFACE": lambda n, a, b: n.name("GroundSurface", b, "surface"),
    "EVOP_PLAY_MSG": lambda n, a, b: f"msg={b} {n.name('RadioCharacterId', a, 'speaker')}",
    "EVOP_DAMAGE_TEAM": lambda n, a, b: f"{n.team(b)} damage={a}",
    "EVOP_STOP_BGM": plain_format,
    "EVOP_MAKE_TEXLINE": lambda n, a, b: n.name("TexLineColor", b, "color"),
    "EVOP_STOP_TEXLINE": plain_format,
    "EVOP_LOOP": lambda n, a, b: f"count={b} -> {n.target(a)}",
    "EVOP_STOP_SCRIPT": plain_format,
}


def format_command(names, word1, word2):
    """The opcode is the top 7 bits of the first word, arg1 the low 9 (`EVENT_CMD`, include/sf64event.h)."""
    opcode, arg1 = word1 >> 9, word1 & 0x1FF
    name = names.enums["EventOpcode"].get(opcode)
    if name is None:
        return f"opcode {opcode}", f"arg1={arg1} arg2={word2}"
    return name, FORMATS[name](names, arg1, word2)


class Archive:
    def __init__(self, path):
        self.path = path
        try:
            self.zip = zipfile.ZipFile(path)
        except (OSError, zipfile.BadZipFile) as error:
            raise ReaderError(f"cannot read {path} as a zip archive: {error}") from error
        self.names = self.zip.namelist()
        self.by_hash = {crc64(name): name for name in self.names}

    def version(self):
        if "version" not in self.names:
            return "version unknown"
        data = self.zip.read("version")
        return f"version {data[1:5].hex().upper()}" if len(data) >= 5 else "version unknown"

    def resource(self, entry, kind, label):
        """Returns the byte order and the count that starts every script resource, after checking the header."""
        data = self.zip.read(entry)
        if len(data) < HEADER_SIZE + 4:
            raise ReaderError(f"{entry}: cut short, {len(data)} bytes")
        if data[0] not in (0, 1):
            raise ReaderError(f"{entry}: not {label}, byte order {data[0]}")
        order = ">" if data[0] else "<"
        if struct.unpack_from(order + "I", data, 4)[0] != kind:
            raise ReaderError(f"{entry}: not {label}")
        return data, order, struct.unpack_from(order + "I", data, HEADER_SIZE)[0]

    def table(self, level):
        entries = [name for name in self.names if name.rsplit("/", 1)[-1] == level.symbol]
        if not entries:
            raise ReaderError(f"{self.path}: no entry named {level.symbol}, the table of {level.name}")
        data, order, count = self.resource(entries[0], RESOURCE_SCRIPT, "a script table")
        if len(data) < HEADER_SIZE + 4 + count * 8:
            raise ReaderError(f"{entries[0]}: cut short, {count} scripts in {len(data)} bytes")
        return entries[0], struct.unpack_from(f"{order}{count}Q", data, HEADER_SIZE + 4)

    def commands(self, table_entry, script, slot):
        entry = self.by_hash.get(slot)
        if entry is None:
            raise ReaderError(f"{table_entry}: script {script} has no entry, hash {slot:016X}")
        data, order, count = self.resource(entry, RESOURCE_COMMANDS, "a script")
        if len(data) < HEADER_SIZE + 4 + count * 4:
            raise ReaderError(f"{entry}: cut short, {count} commands in {len(data)} bytes")
        words = struct.unpack_from(f"{order}{count * 2}H", data, HEADER_SIZE + 4)
        return entry, list(zip(words[0::2], words[1::2]))


def squeeze(text):
    return re.sub(r"[\s_-]", "", text).upper()


def find_level(text):
    key = squeeze(text)
    key = key[len("LEVEL"):] if key.startswith("LEVEL") else key
    for prefix, name in NO_TABLE.items():
        if key in (prefix, squeeze(name)):
            raise ReaderError(f"{name} has no script table: the game's switch falls back to Corneria's")
    for level in LEVELS:
        if key in (level.prefix, squeeze(level.name), squeeze(level.enum[len("LEVEL_"):])):
            return level
    return None


def parse_reference(words):
    """`CO S59:34`, `Corneria 59.34`, `Venom 1 39` or just a level. Returns level, script, command, or None for each."""
    text = " ".join(words)
    level = find_level(text)
    if level:
        return level, None, None
    head, _, tail = text.rpartition(" ")
    match = re.fullmatch(r"[sS]?(\d+)(?:[:.](\d+))?", tail)
    level = find_level(head) if match and head else None
    if not level:
        raise ReaderError(f"not a reference: {text}. Write a level and a script, like CO S59:34 or Corneria 59.34")
    return level, int(match.group(1)), int(match.group(2)) if match.group(2) else None


def find_archive():
    for folder in ARCHIVE_FOLDERS:
        path = ROOT / folder / ARCHIVE_NAME
        if path.is_file():
            return path
    raise ReaderError(f"no {ARCHIVE_NAME} in {', '.join(ARCHIVE_FOLDERS)}; name yours with --o2r <file>")


def print_levels():
    width = max(len(level.name) for level in LEVELS)
    for level in LEVELS:
        print(f"{level.prefix:<5} {level.name:<{width}}  {level.enum}, table {level.symbol}")


def print_level(archive, level):
    entry, slots = archive.table(level)
    print(f"{level.prefix} {level.name}: {entry}, {len(slots)} scripts, {archive.version()}")
    for script, slot in enumerate(slots):
        count = len(archive.commands(entry, script, slot)[1])
        print(f"  S{script:<4} {count} command{'' if count == 1 else 's'}")


def print_script(archive, names, level, script, command):
    table_entry, slots = archive.table(level)
    if script >= len(slots):
        raise ReaderError(f"{level.prefix} S{script}: {level.name} has {len(slots)} scripts")
    entry, commands = archive.commands(table_entry, script, slots[script])
    if command is not None and command >= len(commands):
        raise ReaderError(f"{level.prefix} S{script}:{command}: script {script} has {len(commands)} commands")
    print(f"{level.prefix} S{script} ({level.name}): {entry}, {len(commands)} commands, {archive.version()}")
    for index, (word1, word2) in enumerate(commands):
        name, text = format_command(names, word1, word2)
        mark = ">" if index == command else " "
        print(f"{mark} {index:4}  {word1:04X} {word2:04X}  {name:<22} {text}".rstrip())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("reference", nargs="*", help="a level, or a level and a script: CO S59:34")
    parser.add_argument("--o2r", type=Path, help=f"your {ARCHIVE_NAME}; default: the first in " +
                        ", ".join(ARCHIVE_FOLDERS))
    args = parser.parse_args()
    try:
        if not args.reference:
            print_levels()
            return 0
        level, script, command = parse_reference(args.reference)
        archive = Archive(args.o2r or find_archive())
        if script is None:
            print_level(archive, level)
        else:
            print_script(archive, Names(load_enums()), level, script, command)
    except (ReaderError, zipfile.BadZipFile) as error:
        print(f"script_reader: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
