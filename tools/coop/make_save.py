#!/usr/bin/env python3
"""Star Fox 64 save files for tests, made from the game's own source, so that two test games can hold two different
saves as two real PCs do (decision 0009 item 8). No save file goes into git; a test makes the ones it needs.

    python tools/coop/make_save.py --out <file>                      # blank: what a game with no save writes at boot
    python tools/coop/make_save.py --out <file> --clear all          # every planet cleared and played
    python tools/coop/make_save.py --out <file> --clear corneria,meteo --volumes 40,50,60 --sound-mode 1
    python tools/coop/make_save.py --show <file>                     # planets, settings and checksums of a save file

The layout is include/sf64save.h: a file is 512 bytes, a save and its backup of 256 bytes each. A save is 254 bytes of
SaveData, then Save_Checksum (src/engine/fox_save.c), low byte first as the PC port writes it. The blank save is
gDefaultSave, read from src/engine/fox_save.c, which a game without a save writes at boot (src/engine/fox_game.c,
GSTATE_CHECK_SAVE). A planet byte: 0x80 normal clear, 0x40 normal medal, 0x20 played, 0x10 expert clear, 0x08 expert
medal; the game writes 0xA0 (cleared and played) when a level is finished, which --clear does. --played sets 0x20 alone.

It never writes over a file unless --force is given. Other tools import it: save_file(), planets(), settings().
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SAVE_SOURCE = ROOT / "src" / "engine" / "fox_save.c"
LEVEL_HEADER = ROOT / "include" / "sf64level.h"
AUDIO_HEADER = ROOT / "include" / "sf64audio_external.h"

DATA_SIZE = 0xFE  # sizeof(SaveData)
SAVE_SIZE = 0x100  # sizeof(Save)
FILE_SIZE = 0x200  # sizeof(SaveFile): the save and its backup
SETTINGS = slice(0x14, 0x18)  # soundMode, musicVolume, voiceVolume, sfxVolume
LANGUAGES = slice(0xEB, 0xED)  # textLanguage, voiceLanguage: EU only, no code of the port reads them
CLEARED_AND_PLAYED = 0xA0
PLAYED = 0x20
# Save slot -> name: PlanetId 0 to 12, then SAVE_SLOT_SOLAR, SAVE_SLOT_VENOM_1, SAVE_SLOT_VENOM_2 (include/sf64level.h).
SLOTS = ("meteo", "area6", "bolse", "sectorz", "sectorx", "sectory", "katina", "macbeth", "zoness", "corneria",
         "titania", "aquas", "fortuna", "solar", "venom1", "venom2")


def checksum(raw):
    """Save_Checksum, src/engine/fox_save.c, over the 254 data bytes."""
    v = 0
    for b in raw[:DATA_SIZE]:
        v ^= b
        v <<= 1
        v = (v & 0xFE) | ((v >> 8) & 1)
    return (v & 0xFF) | 0x9500


def _no_comments(text):
    return re.sub(r"//[^\n]*", "", re.sub(r"/\*.*?\*/", "", text, flags=re.S))


def _enum(text, first):
    """{name: value} of the enum whose body holds `first`, counting on from explicit values as C does."""
    body = next((b for b in re.findall(r"enum\s*\w*\s*\{(.*?)\}", _no_comments(text), re.S) if first in b), None)
    if body is None:
        raise SystemExit(f"no enum with {first} in the headers")
    values, value = {}, -1
    for item in (i.strip() for i in body.split(",")):
        if not item:
            continue
        name, _, given = item.partition("=")
        value = int(given.strip(), 0) if given.strip() else value + 1
        values[name.strip()] = value
    return values


def _names():
    level = LEVEL_HEADER.read_text(encoding="utf-8")
    names = {"true": 1, "false": 0}
    names.update(_enum(level, "PLANET_METEO"))
    names.update(_enum(AUDIO_HEADER.read_text(encoding="utf-8"), "SOUNDMODE_STEREO"))
    for name, value in re.findall(r"#define\s+(SAVE_SLOT_\w+)\s+(\d+)", level):
        names[name] = int(value)
    return names


def _value(token, names):
    token = token.strip()
    if re.fullmatch(r"'.'", token):
        return ord(token[1])
    if token in names:
        return names[token]
    return int(token, 0)


def blank():
    """gDefaultSave's 254 data bytes, read from src/engine/fox_save.c. The initializer fills Save.raw byte by byte
    (raw is the union's first member), and PLANET_STATS (include/sf64save.h) gives two bytes."""
    text = _no_comments(SAVE_SOURCE.read_text(encoding="utf-8"))
    match = re.search(r"Save\s+gDefaultSave\s*=\s*\{(.*?)\};", text, re.S)
    if match is None:
        raise SystemExit(f"no gDefaultSave in {SAVE_SOURCE}")
    names = _names()
    out = bytearray()
    body = match.group(1)
    for stats, plain in re.findall(r"PLANET_STATS\(([^)]*)\)|([^,\s][^,]*)", body):
        if stats:
            hits, planet, peppy, falco, slippy = (_value(t, names) for t in stats.split(","))
            out.append(hits - 256 if hits > 255 else hits)
            out.append(planet | ((1 if hits > 255 else 0) << 4) | (peppy << 5) | (falco << 6) | (slippy << 7))
        elif plain.strip():
            out.append(_value(plain, names) & 0xFF)
    if len(out) != DATA_SIZE:
        raise SystemExit(f"gDefaultSave in {SAVE_SOURCE} gave {len(out)} bytes, a SaveData is {DATA_SIZE}")
    return out


def slots(names):
    """Save slots for 'all' or comma-separated names."""
    if names in (None, ""):
        return []
    if names == "all":
        return list(range(len(SLOTS)))
    found = []
    for name in (n.strip() for n in names.split(",")):
        if name not in SLOTS:
            raise SystemExit(f"unknown planet {name!r}; known: {', '.join(SLOTS)}")
        found.append(SLOTS.index(name))
    return found


def save_file(clear=(), played=(), volumes=None, sound_mode=None):
    """A whole save file (512 bytes): gDefaultSave with the planets of `clear` cleared and played, those of `played`
    played, the volumes (music, voice, sfx) and the sound mode as given, the checksum, and the backup the same."""
    data = blank()
    for slot in played:
        data[slot] |= PLAYED
    for slot in clear:
        data[slot] |= CLEARED_AND_PLAYED
    if sound_mode is not None:
        data[SETTINGS.start] = sound_mode
    if volumes is not None:
        data[SETTINGS.start + 1:SETTINGS.stop] = bytes(volumes)
    save = bytes(data) + checksum(data).to_bytes(2, "little")
    return save + save


def planets(blob):
    return " ".join(f"{b:02x}" for b in blob[:16])


def settings(blob):
    """(sound mode, music, voice, sfx) of the first save in a file or of a save."""
    return tuple(blob[SETTINGS])


def check(blob, label):
    """Problems with a save file: its size, each half's checksum."""
    if len(blob) != FILE_SIZE:
        return [f"{label}: {len(blob)} bytes, a save file is {FILE_SIZE}"]
    problems = []
    for name, off in (("save", 0), ("backup", SAVE_SIZE)):
        part = blob[off:off + SAVE_SIZE]
        stored = int.from_bytes(part[DATA_SIZE:], "little")
        if stored != checksum(part):
            problems.append(f"{label}: the {name} has checksum {stored:04x}, Save_Checksum gives {checksum(part):04x}")
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", help="the save file to write")
    ap.add_argument("--clear", default="", help="all, or comma-separated planets: " + ",".join(SLOTS))
    ap.add_argument("--played", default="", help="planets played, not cleared")
    ap.add_argument("--volumes", help="music,voice,sfx, each 0 to 99 (default as gDefaultSave: 99)")
    ap.add_argument("--sound-mode", type=int, help="0 stereo, 1 mono, 2 headset (default 0)")
    ap.add_argument("--force", action="store_true", help="write over --out")
    ap.add_argument("--show", help="print what a save file holds and stop")
    args = ap.parse_args()

    if args.show:
        blob = Path(args.show).read_bytes()
        for problem in check(blob, args.show):
            print(problem)
        print(f"planets {planets(blob)}; sound mode and volumes {settings(blob)}")
        return 0
    if not args.out:
        ap.error("--out or --show")
    volumes = [int(v) for v in args.volumes.split(",")] if args.volumes else None
    if volumes is not None and (len(volumes) != 3 or not all(0 <= v <= 99 for v in volumes)):
        ap.error("--volumes takes three numbers from 0 to 99")
    out = Path(args.out)
    if out.exists() and not args.force:
        sys.exit(f"{out} exists; --force to write over it")
    blob = save_file(slots(args.clear), slots(args.played), volumes, args.sound_mode)
    out.write_bytes(blob)
    print(f"wrote {out}: planets {planets(blob)}, sound mode and volumes {settings(blob)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
