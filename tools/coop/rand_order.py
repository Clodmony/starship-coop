#!/usr/bin/env python3
"""Two random draws whose order the compiler picks (#199): the statements of the game that take two or more draws
from Rand_ZeroOne where C does not say which comes first.

    python tools/coop/rand_order.py            # every such statement, with its class and the level that runs it
    python tools/coop/rand_order.py --check    # exit 1 when a file has more or fewer of them than the list below

Rand_ZeroOne is one sequence for the whole game, so when two draws sit in one expression, the order the compiler
evaluates them in decides which value lands where. C leaves that order open. In Macbeth a Windows game (MSVC) took the
right draw of `RAND_FLOAT(1.0f) - RAND_FLOAT(2.0f)` first and a Linux game (GCC) the left one, and the session parted
(#199). In a session that statement and Titania's 18 of the same class (#209) go through src/port/coop/math/rand_order.h,
which draws in the written order.

Classes, for two draws in one statement:
  operator  operands of one arithmetic expression: unsequenced, the compiler picks. The class of #199.
  args      in two arguments of one call: unspecified order. GCC on x86-64 takes them right to left; MSVC is not
            measured (docs/coop/NOTES.md, 2026-11-09, #199).
  init      in one brace initializer: indeterminately sequenced in C.
  (A draw separated from the other by &&, ||, ?:, a comma operator or a semicolon, or inside the other's argument, is
  ordered by the language and not listed.)
A statement that calls a CoopRand_ function is handled: its session path draws in order, and its other path is the
game's own expression, which co-op off keeps.

KNOWN is the list of open sites per file and class, as docs/coop/NOTES.md (2026-11-09, #199) lists them. --check
fails on a new one, so a change or an upstream sync that adds a statement of this kind shows it, and on a lost one, so
the list stays exact. Scans the tracked .c files in src/engine, src/overlays and src/sys: the simulation. Reads source
only.
"""
import argparse
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCAN = ("src/engine", "src/overlays", "src/sys")
DRAW = re.compile(r"\b(Rand_ZeroOne|Rand_ZeroOneSeeded|RAND_FLOAT|RAND_INT|RAND_FLOAT_CENTERED|RAND_DOUBLE_CENTERED|"
                  r"RAND_RANGE|RAND_FLOAT_SEEDED|RAND_INT_SEEDED|RAND_FLOAT_CENTERED_SEEDED)\s*\(")
HANDLED = re.compile(r"\bCoopRand_\w+\s*\(")
OPEN_CLASSES = ("operator", "args", "init")

# Open sites, file -> {class: count}. Listed with their levels in docs/coop/NOTES.md (2026-11-09, #199); the owner
# decides on them. No operator site is open: Titania's 18, `((Rand_ZeroOne() < 0.5f) ? -1 : 1) * (a + RAND_FLOAT(b))`,
# call CoopRand_SignedFloat in a session (#209), so a new one fails the check.
KNOWN = {
    "src/engine/fox_beam.c": {"args": 3},
    "src/engine/fox_demo.c": {"args": 5},
    "src/engine/fox_effect.c": {"args": 3},
    "src/engine/fox_enmy.c": {"args": 2},
    "src/engine/fox_enmy2.c": {"args": 4},
    "src/engine/fox_hud.c": {"args": 5},
    "src/engine/fox_play.c": {"args": 12},
    "src/engine/fox_tank.c": {"args": 3},
    "src/overlays/ovl_i1/fox_co.c": {"args": 9},
    "src/overlays/ovl_i2/fox_me.c": {"args": 1},
    "src/overlays/ovl_i2/fox_sx.c": {"args": 1},
    "src/overlays/ovl_i3/fox_a6.c": {"args": 1},
    "src/overlays/ovl_i3/fox_aq.c": {"args": 43},
    "src/overlays/ovl_i3/fox_so.c": {"args": 15},
    "src/overlays/ovl_i3/fox_zo.c": {"args": 15},
    "src/overlays/ovl_i4/fox_bo.c": {"args": 9},
    "src/overlays/ovl_i4/fox_fo.c": {"args": 4},
    "src/overlays/ovl_i4/fox_ka.c": {"args": 3},
    "src/overlays/ovl_i4/fox_sz.c": {"args": 3},
    "src/overlays/ovl_i5/fox_ma.c": {"args": 59},
    "src/overlays/ovl_i5/fox_ti.c": {"args": 1},
    "src/overlays/ovl_i5/fox_ti_cs.c": {"args": 3},
    "src/overlays/ovl_i6/fox_andross.c": {"args": 20},
    "src/overlays/ovl_i6/fox_sy.c": {"args": 8},
}

# Where a file runs, for the listing. src/engine and src/sys serve every level.
LEVELS = {
    "ovl_i1/fox_co.c": "Corneria", "ovl_i1/fox_ve1.c": "Venom 1", "ovl_i1/fox_tr.c": "Training",
    "ovl_i2/fox_me.c": "Meteo", "ovl_i2/fox_sx.c": "Sector X", "ovl_i3/fox_a6.c": "Area 6", "ovl_i3/fox_aq.c": "Aquas",
    "ovl_i3/fox_so.c": "Solar", "ovl_i3/fox_zo.c": "Zoness", "ovl_i4/fox_bo.c": "Bolse", "ovl_i4/fox_fo.c": "Fortuna",
    "ovl_i4/fox_ka.c": "Katina", "ovl_i4/fox_sz.c": "Sector Z", "ovl_i5/fox_ma.c": "Macbeth",
    "ovl_i5/fox_ti.c": "Titania",
    "ovl_i5/fox_ti_cs.c": "Titania (its cutscenes)",
    "ovl_i6/fox_andross.c": "Venom 2 (Andross)", "ovl_i6/fox_sy.c": "Sector Y", "ovl_i6/fox_ve2.c": "Venom 2",
}


def strip(src):
    """Comments, string and character literals and preprocessor lines blanked, keeping every offset and line."""
    blank = lambda m: re.sub(r"[^\n]", " ", m.group(0))  # noqa: E731
    src = re.sub(r"/\*.*?\*/", blank, src, flags=re.S)
    src = re.sub(r"//[^\n]*", blank, src)
    src = re.sub(r'"(\\.|[^"\\\n])*"', lambda m: '"' + " " * (len(m.group(0)) - 2) + '"', src)
    src = re.sub(r"'(\\.|[^'\\\n])+'", lambda m: "'" + " " * (len(m.group(0)) - 2) + "'", src)
    return re.sub(r"^[ \t]*#[^\n]*", blank, src, flags=re.M)


def statements(src):
    """(start, end) of each statement: split at ; { } outside brackets, with a brace initializer kept whole."""
    depth = init = 0
    start = 0
    for i, c in enumerate(src):
        if c in "([":
            depth += 1
        elif c in ")]":
            depth -= 1
        elif c == "{":
            if init or depth > 0 or src[start:i].rstrip().endswith("="):
                init += 1
            else:
                yield start, i + 1
                start = i + 1
        elif c == "}":
            if init:
                init -= 1
            else:
                yield start, i + 1
                start = i + 1
        elif c == ";" and depth == 0 and not init:
            yield start, i + 1
            start = i + 1


def call_end(text, at):
    """Index of the bracket that closes the call whose name starts at `at`."""
    depth = 0
    for k in range(text.index("(", at), len(text)):
        if text[k] == "(":
            depth += 1
        elif text[k] == ")":
            depth -= 1
            if depth == 0:
                return k
    return len(text) - 1


def classify(text):
    """The classes of the pairs of draws in one statement (see the top of this file), as a set."""
    stack, enclosing = [], []
    for c in text:
        if c in "({[":
            stack.append(len(enclosing))
        enclosing.append(tuple(stack))
        if c in ")}]" and stack:
            stack.pop()
    calls = [(m.start(), call_end(text, m.start())) for m in DRAW.finditer(text)]
    found = set()
    for i, (a0, a1) in enumerate(calls):
        for b0, _ in calls[i + 1:]:
            if b0 < a1:
                continue  # inside the first draw's argument: evaluated before that call
            ea, eb = enclosing[a0], enclosing[b0]
            common = 0
            while common < min(len(ea), len(eb)) and ea[common] == eb[common]:
                common += 1
            level = ea[:common]
            between = "".join(text[k] for k in range(a1 + 1, b0) if len(enclosing[k]) == len(level))
            between = re.sub(r"[()\[\]{}]", "", between)
            if re.search(r"&&|\|\||[?:;]", between):
                continue
            if "," not in between:
                found.add("operator")
                continue
            opener = level[-1] if level else None
            if opener is not None and text[opener] == "{":
                found.add("init")
            elif opener is not None and text[opener] == "(":
                name = re.search(r"(\w+)\s*$", text[:opener])
                if name and name.group(1) not in ("if", "while", "for", "switch", "return", "sizeof"):
                    found.add("args")
            # else: a comma operator, which orders its operands
    return found


def tracked_files():
    out = subprocess.run(["git", "ls-files", "-z", "--", *SCAN], cwd=ROOT, capture_output=True, text=True,
                         encoding="utf-8").stdout
    return sorted(p for p in out.split("\0") if p.endswith(".c"))


def sites(files=None, read=None):
    """[(path, line, class, text)] of every open pair class in the given files (default: the tracked simulation)."""
    read = read or (lambda p: (ROOT / p).read_text(encoding="utf-8", errors="replace"))
    found = []
    for path in files if files is not None else tracked_files():
        src = strip(read(path))
        for a, b in statements(src):
            text = src[a:b]
            first = DRAW.search(text)
            if not first or len(DRAW.findall(text)) < 2 or HANDLED.search(text):
                continue
            line = src.count("\n", 0, a + first.start()) + 1
            for cls in sorted(classify(text) & set(OPEN_CLASSES)):
                found.append((path, line, cls, " ".join(text.split())))
    return found


def level_of(path):
    for key, name in LEVELS.items():
        if path.endswith(key):
            return name
    return "every level" if path.startswith(("src/engine/", "src/sys/")) else "?"


def counts(found):
    table = {}
    for path, _, cls, _ in found:
        table.setdefault(path, Counter())[cls] += 1
    return table


def check(found):
    """Failures: per file and class, how the counts differ from KNOWN, with the sites of a file that has more."""
    have = counts(found)
    failures = []
    for path in sorted(set(have) | set(KNOWN)):
        for cls in OPEN_CLASSES:
            n, want = have.get(path, Counter())[cls], KNOWN.get(path, {}).get(cls, 0)
            if n > want:
                lines = [f"    {p}:{ln}: {t[:150]}" for p, ln, c, t in found if p == path and c == cls]
                failures.append(f"{path}: {n} {cls} sites, the list has {want}. Order the draws in a session (see "
                                f"src/port/coop/math/rand_order.h), or list the site in KNOWN and docs/coop/NOTES.md:\n"
                                + "\n".join(lines))
            elif n < want:
                failures.append(f"{path}: {n} {cls} sites, the list has {want}: lower the count in KNOWN")
    return failures


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--check", action="store_true", help="exit 1 when the sites differ from the list")
    args = p.parse_args()
    found = sites()
    total = Counter(cls for _, _, cls, _ in found)
    summary = ", ".join(f"{total[c]} {c}" for c in OPEN_CLASSES)
    if args.check:
        failures = check(found)
        for f in failures:
            print(f"FAIL: rand order: {f}")
        print(f"rand order: {'FAILED' if failures else 'ok'}, open sites as listed: {summary}")
        return 1 if failures else 0
    for path, line, cls, text in found:
        print(f"{path}:{line}: {cls} ({level_of(path)}): {text[:150]}")
    print(f"rand order: {summary}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
