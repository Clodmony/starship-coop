#!/usr/bin/env python3
"""Tests what a session earns (decision 0017, K2, #373): the merge in src/port/coop/game/save.c, with no game.

    python tools/coop/save_merge_test.py              # unattended; prints PASS or FAIL. No build, no ROM, seconds.
    python tools/coop/save_merge_test.py --no-mutants # the cases alone

When a session ends, each game reads its own save back from its file and ORs in the planet flags the session earned:
the flags the session's copy holds at its end and did not begin with. Then the game's own Save_Write writes the file,
once. This test compiles save.c with the game's own src/engine/fox_save.c (its checksum, Save_Write and Save_Read) and
tools/coop/savetest/coop_save_test.c, which stands in for the save file, with AddressSanitizer and UBSan where the
compiler has them. Each case makes its saves with make_save.py, plays a session as steps (the host's save comes in, a
level's end sets flags, the session leaves) and compares the save file afterwards, byte for byte, with what this
script works out by itself:

  client_medal    host blank, client all cleared with its own volumes: the session's medal on Corneria is kept
  host_own        the host's own game: Corneria and Meteo cleared, both kept, named in the map's order
  not_the_hosts   the host had other planets and other flags on Corneria: the client gets the session's flags only,
                  and keeps the flags of its own that the session's copy does not hold
  nothing         a session that ends before a clear: no write, the file byte for byte
  had_it          the session earned what this save had already: no write
  host_had_it     the host had Corneria cleared, this save not, the session clears it again: nothing is earned
  ranking         the ranking table and the ending-seen byte of the session's copy are not kept
  recovery        the level starts again in the session with the host's progress as it then stood: what was earned
                  before stays earned
  two_sessions    the second session begins with what the first one kept, and earns nothing again
  unused_bits     the three unused bits of a planet byte are not kept
  played          a planet flown and not cleared (a game over) keeps `played` and says so
  expert          an expert clear and its medal are kept and named
  broken_file     an own save file that cannot be read at the session's end is left as it is

In every case nothing reaches the file between the session's start and its end.

Then the mutants: save.c with one behaviour broken, each built from a copy of the source (the source itself is never
changed), and each has to fail at least one case. `whole_byte` is the one the decision names: the session's whole
planet byte copied in place of the OR of what it earned.

Needs a C compiler (CC, cc, gcc, clang; with cl on Windows no sanitizers). Exit code 0 when every case holds and every
mutant is caught. The folder local/test-save-merge/ is deleted when all hold and kept for reading when one fails.
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools" / "coop"))
import make_save  # noqa: E402

SOURCE = ROOT / "src" / "port" / "coop" / "game" / "save.c"
GAME_SAVE = ROOT / "src" / "engine" / "fox_save.c"
DRIVER = ROOT / "tools" / "coop" / "savetest" / "coop_save_test.c"
WORK = ROOT / "local" / "test-save-merge"
INCLUDES = [ROOT / "tools" / "coop" / "savetest", ROOT / "src", ROOT / "src" / "port" / "coop" / "game",
            ROOT / "include", ROOT / "libultraship" / "include" / "libultraship"]

FLAGS = 0xF8  # the five flags of a planet byte, include/sf64save.h
CLEAR, MEDAL, PLAYED, EXPERT, EXPERT_MEDAL = 0x80, 0x40, 0x20, 0x10, 0x08
SLOT = {name: n for n, name in enumerate(make_save.SLOTS)}
OWN_VOLUMES = (40, 50, 60)

# (name, text in save.c that must occur exactly once, replacement)
MUTANTS = [
    ("whole_byte", "gSaveFile.save.raw[i] |= kept[i];", "gSaveFile.save.raw[i] = session.raw[i];"),
    ("assigned", "gSaveFile.save.raw[i] |= kept[i];", "gSaveFile.save.raw[i] = kept[i];"),
    ("hosts_progress_kept", "(session.raw[i] & ~sBegan[i] & ~gSaveFile.save.raw[i] & flags)",
     "(session.raw[i] & ~gSaveFile.save.raw[i] & flags)"),
    ("began_taken_again", "    if (!sEntered) {\n        memcpy(sBegan, merged.raw, sizeof(sBegan));",
     "    if (true) {\n        memcpy(sBegan, merged.raw, sizeof(sBegan));"),
    ("unused_bits_kept", "(session.raw[i] & ~sBegan[i] & ~gSaveFile.save.raw[i] & flags)",
     "(session.raw[i] & ~sBegan[i] & ~gSaveFile.save.raw[i])"),
    ("always_writes", "    if (!any) {\n", "    if (false) {\n"),
    ("own_file_not_read", "    if (Save_Read() != 0) {\n", "    if (false) {\n"),
    ("written_in_session", "    return sEntered;\n", "    return false;\n"),
    ("broken_file_written",
     "\"written, and what the session earned is not kept\");\n        return;\n",
     "\"written, and what the session earned is not kept\");\n"),
    ("kept_line_stays", "        memcpy(sBegan, merged.raw, sizeof(sBegan));\n        sKept[0] = '\\0';\n",
     "        memcpy(sBegan, merged.raw, sizeof(sBegan));\n"),
    ("no_kept_line", "    snprintf(sKept, sizeof(sKept), \"Kept from this session: %s.\", names);\n", ""),
    ("medal_not_named", "        if (planet.flags.normalMedal) {\n", "        if (false) {\n"),
]


def find_compiler():
    for name in (os.environ.get("CC"), "cc", "gcc", "clang", "cl"):
        if name and shutil.which(name):
            return name
    sys.exit("save_merge_test: no C compiler found (set CC)")


def build(cc, source, folder):
    """The driver with `source` (save.c or a mutant of it) and the game's fox_save.c. Returns (exe, how) or
    (None, why)."""
    folder.mkdir(parents=True, exist_ok=True)
    exe = folder / ("coop_save_test.exe" if sys.platform == "win32" else "coop_save_test")
    sources = [str(DRIVER), str(source), str(GAME_SAVE)]
    if Path(cc).stem.lower() == "cl":
        tries = [([cc, "/nologo", "/W3", "/Od", "/D_CRT_SECURE_NO_WARNINGS", "/DAVOID_UB=1",
                   *[f"/I{i}" for i in INCLUDES], *sources, f"/Fe:{exe}"], "cl")]
    else:
        # fox_save.c is the game's: unused locals, gDefaultSave filled byte by byte. AVOID_UB as the game's build has
        # it (CMakeLists.txt).
        base = [cc, "-std=gnu11", "-g", "-O1", "-Wall", "-Wno-unused-variable", "-Wno-missing-braces", "-Werror",
                "-DAVOID_UB=1"]
        for include in INCLUDES:
            base += ["-I", str(include)]
        base += [*sources, "-o", str(exe)]
        tries = [(base + ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"], "with ASan and UBSan"),
                 (base, "without sanitizers")]
    errors = ""
    for cmd, how in tries:
        done = subprocess.run(cmd, cwd=folder, capture_output=True, text=True)
        if done.returncode == 0:
            return exe, how
        errors = (done.stdout + done.stderr)[-1500:]
    return None, errors


class Session:
    """What one run of the test program gave."""

    def __init__(self, exe, folder, name, own, steps):
        folder.mkdir(parents=True, exist_ok=True)
        own_path = folder / f"{name}-own.sav"
        out_path = folder / f"{name}-after.sav"
        own_path.write_bytes(own)
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:abort_on_error=0", UBSAN_OPTIONS="print_stacktrace=1")
        done = subprocess.run([str(exe), str(own_path), str(out_path), *steps], capture_output=True, text=True,
                              cwd=folder, env=env, timeout=120)
        self.code = done.returncode
        self.text = done.stdout + done.stderr
        lines = done.stdout.splitlines()
        self.logs = [line[4:] for line in lines if line.startswith("log ")]
        values = dict(line.split("=", 1) for line in lines if "=" in line and not line.startswith("log "))
        self.blocked = int(values.get("blocked", -1))
        self.written = int(values.get("written", -1))
        self.in_session = int(values.get("written_in_session", -1))
        self.kept = values.get("kept")
        self.kept_after_enter = values.get("kept_after_enter", "")  # of the last enter
        self.memory = bytes.fromhex(values.get("memory", ""))
        self.after = out_path.read_bytes() if out_path.exists() else b""


def with_planets(blob, planets):
    """A save file (512 bytes) as `blob` with these planet bytes ORed in ({slot name: bits}), checksum and backup as
    Save_Write leaves them."""
    data = bytearray(blob[:make_save.DATA_SIZE])
    for name, bits in planets.items():
        data[SLOT[name]] |= bits
    save = bytes(data) + make_save.checksum(data).to_bytes(2, "little")
    return save + save


def host_file(folder, name, blob):
    path = folder / f"{name}-host.sav"
    folder.mkdir(parents=True, exist_ok=True)
    path.write_bytes(blob)
    return f"enter:{path}"


def sets(planets):
    return [f"set:{SLOT[name]}:{bits:x}" for name, bits in planets.items()]


def problems_of(run, own, want, kept, writes, sets_count):
    """What is wrong with a run: the file afterwards against `want`, what is in memory, the kept line, the writes."""
    found = []
    if run.code != 0:
        return [f"the test program ended with {run.code}: {run.text[-600:]}"]
    if run.in_session != 0:
        found.append(f"{run.in_session} writes reached the save file during the session")
    if run.blocked < sets_count:
        found.append(f"{run.blocked} Save_Write calls were held back in the session, want {sets_count}")
    if run.after != want:
        found.append(f"the save file afterwards: planets {make_save.planets(run.after)}, want "
                     f"{make_save.planets(want)}"
                     + ("" if run.after[:16] != want[:16] else "; the planets are right, another byte differs: "
                        + ", ".join(f"0x{n:03x}" for n in range(len(want)) if run.after[n:n + 1] != want[n:n + 1])[:200]))
    if make_save.check(run.after, "the save file afterwards"):
        found += make_save.check(run.after, "the save file afterwards")
    if run.memory != run.after[:make_save.SAVE_SIZE] and want != b"":
        found.append(f"the save in memory after the session is not the file's: planets {make_save.planets(run.memory)}")
    if run.kept != kept:
        found.append(f"the line for the player is `{run.kept}`, want `{kept}`")
    if run.kept_after_enter != "":
        found.append(f"the line for the player still stands after the next session began: `{run.kept_after_enter}`")
    if run.written != writes:
        found.append(f"{run.written} writes of the save file after the session, want {writes}")
    if want == own and run.written == 0 and any("written to the save file" in line for line in run.logs):
        found.append("the co-op log says the file was written")
    if kept and not any(kept[len("Kept from this session: "):-1] in line for line in run.logs):
        found.append(f"the co-op log does not name what was kept: {run.logs[-1:]}")
    return found


def cases(exe, folder):
    """Every case: (name, problems, a line about it)."""
    blank = make_save.save_file()
    full = make_save.save_file(clear=make_save.slots("all"), volumes=OWN_VOLUMES, sound_mode=1)
    done = []

    def case(name, own, steps, want, kept, writes, sets_count, line):
        run = Session(exe, folder, name, own, steps)
        done.append((name, problems_of(run, own, want, kept, writes, sets_count), line))
        return run

    # The session's medal on Corneria, into a save that had the clear: one bit and the checksum.
    earned = {"corneria": CLEAR | MEDAL | PLAYED}
    want = with_planets(full, earned)
    case("client_medal", full, [host_file(folder, "client_medal", blank), *sets(earned), "leave"], want,
         "Kept from this session: Corneria (medal).", 1, 1,
         f"Corneria {full[SLOT['corneria']]:02x} -> {want[SLOT['corneria']]:02x}, volumes {make_save.settings(want)}")

    earned = {"meteo": CLEAR | PLAYED, "corneria": CLEAR | PLAYED}
    want = with_planets(blank, earned)
    case("host_own", blank, ["enter_own", *sets(earned), "leave"], want, "Kept from this session: Corneria, Meteo.", 1,
         2, f"planets {make_save.planets(want)}")

    # The host: Katina and Meteo cleared, Corneria played with an expert medal. This save: Zoness cleared, Corneria
    # played and cleared on expert. The session clears Corneria: the clear is kept, the host's medal and planets not.
    host = with_planets(make_save.save_file(clear=make_save.slots("katina,meteo")), {"corneria": PLAYED | EXPERT_MEDAL})
    own = with_planets(make_save.save_file(clear=make_save.slots("zoness"), volumes=OWN_VOLUMES),
                       {"corneria": PLAYED | EXPERT})
    want = with_planets(own, {"corneria": CLEAR})
    case("not_the_hosts", own, [host_file(folder, "not_the_hosts", host), *sets({"corneria": CLEAR | PLAYED}), "leave"],
         want, "Kept from this session: Corneria.", 1, 1,
         f"Corneria {own[SLOT['corneria']]:02x} -> {want[SLOT['corneria']]:02x} with the host's at "
         f"{host[SLOT['corneria']]:02x}; Katina and Meteo stay {want[SLOT['katina']]:02x} {want[SLOT['meteo']]:02x}")

    case("nothing", full, [host_file(folder, "nothing", blank), "leave"], full, "", 0, 0, "no write")

    earned = {"corneria": CLEAR | PLAYED}
    case("had_it", full, [host_file(folder, "had_it", blank), *sets(earned), "leave"], full, "", 0, 1, "no write")

    host = make_save.save_file(clear=make_save.slots("corneria"))
    case("host_had_it", blank, [host_file(folder, "host_had_it", host), *sets(earned), "leave"], blank, "", 0, 1,
         "no write: the flags were in the copy the session began with")

    earned = {"fortuna": CLEAR | PLAYED}
    want = with_planets(full, {})
    case("ranking", full, [host_file(folder, "ranking", blank), "rank", "leave"], want, "", 0, 1,
         "ranking and ending-seen byte as before, no write")
    want = with_planets(blank, earned)
    case("ranking_and_clear", blank, [host_file(folder, "ranking_and_clear", full), "rank", *sets(earned), "leave"],
         blank, "", 0, 2, "the host had Fortuna: no write, ranking as before")
    case("ranking_and_new", blank, [host_file(folder, "ranking_and_new", blank), "rank", *sets(earned), "leave"],
         want, "Kept from this session: Fortuna.", 1, 2, "Fortuna kept, ranking and ending-seen byte as before")

    first, second = {"corneria": CLEAR | PLAYED}, {"meteo": CLEAR | MEDAL | PLAYED}
    want = with_planets(blank, {**first, **second})
    case("recovery", blank,
         [host_file(folder, "recovery", blank), *sets(first), host_file(folder, "recovery-again", with_planets(blank, first)),
          *sets(second), "leave"], want, "Kept from this session: Corneria, Meteo (medal).", 1, 2,
         f"planets {make_save.planets(want)}")

    want = with_planets(blank, first)
    case("two_sessions", blank,
         ["enter_own", *sets(first), "leave", "enter_own", "leave"], want, "", 1, 1,
         "one write, by the first; the second session's line is empty")

    want = with_planets(blank, first)
    case("unused_bits", blank, [host_file(folder, "unused_bits", blank), *sets({"corneria": CLEAR | PLAYED | 0x07}),
                                "leave"], want, "Kept from this session: Corneria.", 1, 1,
         f"Corneria {want[SLOT['corneria']]:02x}")

    want = with_planets(blank, {"katina": PLAYED})
    case("played", blank, [host_file(folder, "played", blank), *sets({"katina": PLAYED}), "leave"], want,
         "Kept from this session: Katina (played).", 1, 1, f"Katina {want[SLOT['katina']]:02x}")

    earned = {"corneria": PLAYED | EXPERT | EXPERT_MEDAL, "venom2": PLAYED | CLEAR | MEDAL | EXPERT | EXPERT_MEDAL}
    want = with_planets(with_planets(blank, {"corneria": PLAYED | CLEAR}), earned)
    case("expert", with_planets(blank, {"corneria": PLAYED | CLEAR}),
         [host_file(folder, "expert", blank), *sets(earned), "leave"], want,
         "Kept from this session: Corneria (expert, expert medal), Venom 2 (expert, medal, expert medal).", 1, 2,
         f"planets {make_save.planets(want)}")

    # The own file is damaged while the session runs: nothing is written over it.
    broken = bytearray(full)
    broken[make_save.DATA_SIZE] ^= 0x55
    broken[make_save.SAVE_SIZE + make_save.DATA_SIZE] ^= 0x55
    run = Session(exe, folder, "broken_file", full,
                  [host_file(folder, "broken_file", blank), *sets({"corneria": CLEAR | PLAYED}), "break", "leave"])
    found = [] if run.code == 0 else [f"the test program ended with {run.code}: {run.text[-600:]}"]
    if run.after != bytes(broken):
        found.append("the damaged save file was written over")
    if run.written != 0 or run.in_session != 0:
        found.append(f"{run.written} writes of the save file")
    if run.kept != "":
        found.append(f"the line for the player is `{run.kept}`")
    if not any("could not be read" in line for line in run.logs):
        found.append(f"the co-op log does not say that the file could not be read: {run.logs[-1:]}")
    done.append(("broken_file", found, "left as it was, said in the co-op log"))
    return done


def mutant_source(name, text, replacement, folder):
    """save.c with one break, in `folder`. The text has to occur exactly once."""
    source = SOURCE.read_text(encoding="utf-8").replace("\r\n", "\n")
    count = source.count(text)
    if count != 1:
        return None, f"mutant {name}: its text occurs {count} times in {SOURCE.relative_to(ROOT)}, want once"
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / "save.c"
    path.write_text(source.replace(text, replacement), encoding="utf-8")
    return path, ""


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--no-mutants", action="store_true", help="the cases alone")
    args = ap.parse_args()

    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    for path in (SOURCE, GAME_SAVE, DRIVER):
        if not path.exists():
            print(f"save_merge_test: FAIL, {path.relative_to(ROOT)} does not exist")
            return 1
    cc = find_compiler()
    exe, how = build(cc, SOURCE, WORK / "build")
    if exe is None:
        print("save_merge_test: FAIL, the test does not build:\n" + how)
        return 1
    failures = []
    results = cases(exe, WORK / "cases")
    for name, found, line in results:
        if found:
            failures += [f"{name}: {problem}" for problem in found]
        else:
            print(f"save_merge_test: ok {name}: {line}")
    caught = 0
    if not args.no_mutants and not failures:
        for name, text, replacement in MUTANTS:
            source, why = mutant_source(name, text, replacement, WORK / "mutants" / name)
            if source is None:
                failures.append(why)
                continue
            mutant, why = build(cc, source, WORK / "mutants" / name)
            if mutant is None:
                failures.append(f"mutant {name} does not build: {why[-400:]}")
                continue
            failed = [case for case, found, _ in cases(mutant, WORK / "mutants" / name / "cases") if found]
            if failed:
                caught += 1
                print(f"save_merge_test: mutant {name} caught by {', '.join(failed)}")
            else:
                failures.append(f"mutant {name} passed every case")
    print(f"save_merge_test: built {how}; {len(results)} cases"
          + ("" if args.no_mutants else f", {caught} of {len(MUTANTS)} mutants caught"))
    if failures:
        print(f"save_merge_test: FAIL, {len(failures)} problems; first {min(12, len(failures))}:")
        for failure in failures[:12]:
            print("  " + failure)
        print(f"save_merge_test: kept {WORK.relative_to(ROOT)}")
        return 1
    shutil.rmtree(WORK, ignore_errors=True)
    print("save_merge_test: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
