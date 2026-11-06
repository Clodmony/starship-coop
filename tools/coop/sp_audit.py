#!/usr/bin/env python3
"""Single-player assumptions audit (WP-05): every use of gPlayer[0], gPlayer->, gMainController and gCamCount in the
game code, with the function it sits in and a verdict for co-op.

    python tools/coop/sp_audit.py            # rewrite docs/coop/audit/single-player.md; stops at a new site
    python tools/coop/sp_audit.py --accept   # the same, and takes new sites into the list
    python tools/coop/sp_audit.py --check    # exit 1 if a site is new, lost or has no verdict, or a verdict row is bad
    python tools/coop/sp_audit.py --todo     # print functions that have sites but no verdict

The committed list is also the record of the known sites. A site it does not have would take the verdict of its row
without anyone reading it, so it stops the rerun and fails --check, named with that row, until --accept takes it in.
After an upstream sync that is the one command to run. --accept records that the site was shown, not that the verdict
fits. A site the list has and the code does not is lost: it fails --check until a plain rerun.

--check compares sites by file, function and pattern, and nothing else in the list (#114). A moved line passes, so two
changes that each pass cannot leave the integration branch red by moving lines between them. The line numbers, verdicts
and notes in the list may lag until the next rerun, and --check says so. New means more sites of a pattern in a function
than the list has, lost means fewer. So a site that moves to another function is lost there and new here, even under one
row of the verdict file: nobody has read it where it is now. A site that replaces another of the same pattern in the
same function does not show. The list is part of the commit, so this needs no history: CI checks out one commit.

A file git does not track is not scanned and gets a warning. So does a tracked file that is deleted but not committed,
and its sites count as lost.

Verdicts live in docs/coop/audit/sp-verdicts.tsv, one line per function (or per file with function "*"):

    path<TAB>function<TAB>verdict<TAB>basis<TAB>note

path may be a glob (fnmatch). function may end in "@<pattern>" (for example "Play_Main@gMainController") to give the
sites of one pattern in that function their own verdict; "*@<pattern>" does that for every function. The most
specific entry wins: function with pattern, then "*@<pattern>", then function, then "*"; at each step an exact path
ranks above a glob path. basis says how the verdict was reached: "read" (the whole function was read), "site" (the matched
lines and their surroundings were read) or "name" (inferred from the function name, the file and the pattern without
reading the code; a guess).

Verdicts:
    sim     gameplay logic that means "the player": aiming, targeting, collisions, triggers, spawns relative to the
            player. Needs a choice of player (nearest, owner, all) once there are two.
    view    what one screen shows or plays: camera, HUD, screen effects, sound listener, rumble. Should follow the
            local player of this client (WP-28).
    input   reads a controller through gMainController or gPlayerNum. Needs the input source per player (WP-14).
    flow    level flow that one player drives: intro, level complete, warps, deaths, game over, route, cutscenes.
            Needs a game rule (WP-07) before code (WP-16, WP-19).
    setup   gCamCount used as player count, camera count or "versus mode" flag, or players allocated and initialized.
            WP-10 split the player count from the camera count; NOTES.md lists what stayed on gCamCount and why.
    ok      not affected in co-op: menus, title, map, ending, versus-only code, debug, declarations, or code that
            already loops over all players.
    unknown not decided; read it before relying on it.
"""
import argparse
import fnmatch
import re
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs" / "coop" / "audit" / "single-player.md"
VERDICTS = ROOT / "docs" / "coop" / "audit" / "sp-verdicts.tsv"
SCOPE = ["src/engine", "src/overlays", "include"]
SUFFIXES = (".c", ".h", ".cpp", ".inc.c")
PATTERNS = {
    "gPlayer[0]": re.compile(r"\bgPlayer\[0\]"),
    "gPlayer->": re.compile(r"\bgPlayer->"),
    "gMainController": re.compile(r"\bgMainController\b"),
    "gCamCount": re.compile(r"\bgCamCount\b"),
}
SHORT = {"gPlayer[0]": "P0", "gPlayer->": "P->", "gMainController": "MC", "gCamCount": "CC"}
VERDICT_NAMES = ["sim", "view", "input", "flow", "setup", "ok", "unknown"]
BASES = ["read", "site", "name"]
# A function definition in decomp style starts in column 0 and its body closes with "}" in column 0.
HEADER = re.compile(r"^(?!typedef|struct|union|enum|extern|return|if|for|while|switch|#)[A-Za-z_][\w \t\*]*?\b(\w+)\s*\(")
# One function in the generated list: name, then its sites.
LIST_ROW = re.compile(r"\| `([^`]+)` \| ([^|]+) \|")


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, encoding="utf-8", check=True)


def scope_files(*flags):
    files = git("ls-files", "-z", *flags, "--", *SCOPE).stdout.split("\0")
    return [f for f in files if f.endswith(SUFFIXES)]


def sites(files):
    """Return a list of (path, line, function, pattern) for every match, sorted."""
    out = []
    for path in files:
        if not (ROOT / path).is_file():
            continue  # tracked, but deleted and not committed; main() says so
        text = (ROOT / path).read_text(encoding="utf-8", errors="replace")
        if not any(p.search(text) for p in PATTERNS.values()):
            continue
        func = "<file scope>"
        pending = None
        in_comment = False
        for n, line in enumerate(text.splitlines(), 1):
            stripped = line
            if in_comment:
                if "*/" in stripped:
                    in_comment = False
                continue
            if stripped.startswith("/*") and "*/" not in stripped:
                in_comment = True
                continue
            m = HEADER.match(line)
            if m and not line.rstrip().endswith(";") and "=" not in line.split("(")[0]:
                pending = m.group(1)
                if "{" in line:
                    func, pending = pending, None
            elif pending and line.startswith("{"):
                func, pending = pending, None
            elif pending and line.rstrip().endswith(";"):
                pending = None  # a prototype spread over several lines
            for name, pat in PATTERNS.items():
                if pat.search(line):
                    # Skip matches inside line comments.
                    code = line.split("//", 1)[0]
                    if pat.search(code):
                        out.append((path, n, func, name))
            if line.startswith("}"):
                func = "<file scope>"
    return sorted(out, key=lambda s: (s[0], s[1], s[3]))


def load_verdicts():
    entries = []
    errors = []
    if not VERDICTS.exists():
        return entries, errors
    for n, raw in enumerate(VERDICTS.read_text(encoding="utf-8").splitlines(), 1):
        if not raw.strip() or raw.startswith("#"):
            continue
        cols = raw.split("\t")
        if len(cols) < 4:
            errors.append(f"sp-verdicts.tsv:{n}: needs path, function, verdict, basis[, note]")
            continue
        path, func, verdict, basis = cols[:4]
        note = cols[4] if len(cols) > 4 else ""
        if verdict not in VERDICT_NAMES:
            errors.append(f"sp-verdicts.tsv:{n}: unknown verdict {verdict!r}")
        if basis not in BASES:
            errors.append(f"sp-verdicts.tsv:{n}: unknown basis {basis!r}")
        entries.append({"line": n, "path": path, "func": func, "verdict": verdict, "basis": basis, "note": note})
    return entries, errors


def match(entries, path, func, pattern):
    best, rank = None, 99
    for e in entries:
        exact_path = e["path"] == path
        if not exact_path and not fnmatch.fnmatch(path, e["path"]):
            continue
        if e["func"] == f"{func}@{pattern}":
            r = 0
        elif e["func"] == f"*@{pattern}":
            r = 2
        elif e["func"] == func:
            r = 4
        elif e["func"] == "*":
            r = 6
        else:
            continue
        r += 0 if exact_path else 1
        if r < rank:
            best, rank = e, r
    return best


def listed_sites(text):
    """Count the sites of a generated list per (path, function, short pattern)."""
    counts = Counter()
    path = None
    for line in text.splitlines():
        if line.startswith("### `"):
            path = line[5:-1]
        m = LIST_ROW.match(line)
        if m:
            for site in m.group(2).split(", "):
                counts[path, m.group(1), site.split(":")[-1]] += 1
    return counts


def new_sites(all_sites, listed):
    """Return (path, function, pattern, count in the list, lines now) wherever the list has fewer sites than the code."""
    now = defaultdict(list)
    for path, line, func, pattern in all_sites:
        now[path, func, pattern].append(line)
    out = []
    for (path, func, pattern), lines in now.items():
        was = listed[path, func, SHORT[pattern]]
        if len(lines) > was:
            out.append((path, func, pattern, was, lines))
    return out


def lost_sites(all_sites, listed):
    """Return (path, function, pattern, count in the list, count now) wherever the list has too many sites."""
    now = Counter((path, func, SHORT[pattern]) for path, _, func, pattern in all_sites)
    names = {short: pattern for pattern, short in SHORT.items()}
    return [(path, func, names.get(short, short), was, now[path, func, short])
            for (path, func, short), was in listed.items() if was > now[path, func, short]]


def render(all_sites, entries):
    groups = defaultdict(list)
    hits = {}
    for s in all_sites:
        e = match(entries, s[0], s[2], s[3])
        key = (s[0], s[2], e["line"] if e else 0)
        groups[key].append(s)
        hits[key] = e
    rows = []
    for key, ss in groups.items():
        rows.append((key[0], key[1], ss, hits[key]))
    rows.sort(key=lambda r: (r[0], r[2][0][1]))

    by_verdict = Counter()
    by_pattern_verdict = defaultdict(Counter)
    funcs_by_verdict = Counter()
    by_basis = Counter()
    for path, func, ss, e in rows:
        v = e["verdict"] if e else "unknown"
        funcs_by_verdict[v] += 1
        by_basis[e["basis"] if e else "none"] += len(ss)
        for s in ss:
            by_verdict[v] += 1
            by_pattern_verdict[s[3]][v] += 1

    L = []
    L.append("# Single-player assumptions audit (WP-05)")
    L.append("")
    L.append("Generated by `python tools/coop/sp_audit.py` from the code and `sp-verdicts.tsv`. Do not edit by hand: change")
    L.append("the verdicts file and rerun. `--check` fails when a site is new, lost or has no verdict. It compares sites by")
    L.append("file, function and pattern, so the line numbers, verdicts and notes here may lag until the next rerun.")
    L.append("")
    L.append("**The verdicts are a starting list, not checked facts.** A sample of 45 found 5 wrong (#56). The others have not")
    L.append("been rechecked, a rerun does not recheck them, and the `flow` group is not usable as a checklist yet.")
    L.append("")
    L.append("This file is also the record of the known sites. A site it does not have would take the verdict of its function's row")
    L.append("or of a `*` row for its file without anyone reading it, so it stops the rerun and fails `--check`, named with that row,")
    L.append("until `--accept` takes it in. That records that the site was shown, not that the verdict fits. A site this file has and")
    L.append("the code does not is lost: it fails `--check` until a plain rerun. New means more sites of a pattern in a function than")
    L.append("this file has, lost means fewer. So a site that moves to another function is lost there and new here, even under one")
    L.append("row, and a site that replaces another of the same pattern in the same function does not show.")
    L.append("")
    L.append("Scope: `" + "`, `".join(SCOPE) + "`.")
    L.append("Patterns: `" + "`, `".join(PATTERNS) + "`. Matches inside `//` comments are skipped.")
    L.append("One site is one pattern on one line. Verdicts are given per function (or per pattern within a function);")
    L.append("their meaning is in the docstring of `tools/coop/sp_audit.py`. Basis `read`: the whole function was read;")
    L.append("`site`: the matched lines and their surroundings were read; `name`: inferred from the function name, file and")
    L.append("pattern without reading the code, so treat it as a guess.")
    L.append("")
    L.append("## Summary")
    L.append("")
    nfuncs = len({(r[0], r[1]) for r in rows})
    L.append(f"{len(all_sites)} sites in {nfuncs} functions in {len({r[0] for r in rows})} files.")
    L.append("A function whose sites have more than one verdict counts once per verdict in the functions column.")
    L.append("")
    L.append("| verdict | sites | functions | " + " | ".join(f"`{p}`" for p in PATTERNS) + " |")
    L.append("|---|---:|---:|" + "---:|" * len(PATTERNS))
    for v in VERDICT_NAMES:
        if by_verdict[v] or funcs_by_verdict[v]:
            L.append(f"| {v} | {by_verdict[v]} | {funcs_by_verdict[v]} | "
                     + " | ".join(str(by_pattern_verdict[p][v]) for p in PATTERNS) + " |")
    L.append("")
    L.append("Basis per site: " + ", ".join(f"{b} {by_basis[b]}" for b in BASES + ["none"] if by_basis[b]) + ".")
    L.append("")
    L.append("## Sites")
    current = None
    for path, func, ss, e in rows:
        if path != current:
            current = path
            L.append("")
            L.append(f"### `{path}`")
            L.append("")
            L.append("| function | sites (line: pattern) | verdict | basis | note |")
            L.append("|---|---|---|---|---|")
        lines = ", ".join(f"{s[1]}:{SHORT[s[3]]}" for s in ss)
        v = e["verdict"] if e else "unknown"
        b = e["basis"] if e else "none"
        note = (e["note"] if e else "").replace("|", "\\|")
        L.append(f"| `{func}` | {lines} | {v} | {b} | {note} |")
    L.append("")
    L.append("Pattern keys: P0 `gPlayer[0]`, P-> `gPlayer->`, MC `gMainController`, CC `gCamCount`.")
    L.append("")
    return "\n".join(L), rows


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="fail if a site is new, lost or has no verdict")
    mode.add_argument("--accept", action="store_true", help="rewrite the list and take new sites into it")
    mode.add_argument("--todo", action="store_true", help="list functions without a verdict")
    args = ap.parse_args()

    tracked = scope_files()
    all_sites = sites(tracked)
    entries, errors = load_verdicts()
    text, rows = render(all_sites, entries)

    used = set()
    missing = []
    for path, func, ss, e in rows:
        if e:
            used.add(e["line"])
        else:
            missing.append((path, func, len(ss), sorted({s[3] for s in ss}), [s[1] for s in ss]))
    unused = [e for e in entries if e["line"] not in used]
    for e in unused:
        errors.append(f"sp-verdicts.tsv:{e['line']}: matches no site ({e['path']} {e['func']})")

    if args.todo:
        for path, func, n, pats, lines in missing:
            print(f"{path}\t{func}\t{n}\t{','.join(pats)}\t{lines[0]}")
        print(f"{len(missing)} functions without verdict", file=sys.stderr)
        return 0

    for path in scope_files("--others", "--exclude-standard"):
        found = sites([path])
        if found:
            print(f"sp-audit: warning: {path} is not tracked by git, so its {len(found)} sites are not in the list")

    if missing:
        errors.append(f"{len(missing)} functions have no verdict (run with --todo)")
    current = OUT.read_text(encoding="utf-8").replace("\r\n", "\n") if OUT.exists() else ""
    listed = listed_sites(current)
    for path in sorted({key[0] for key in listed}.intersection(tracked)):
        if not (ROOT / path).is_file():
            print(f"sp-audit: warning: {path} is tracked by git but not on disk, so its sites count as lost; "
                  "restore it, or commit the deletion")
    new = new_sites(all_sites, listed)
    lost = lost_sites(all_sites, listed)
    for path, func, pattern, was, lines in new:
        e = match(entries, path, func, pattern)
        row = f"takes {e['verdict']} ({e['basis']}) from sp-verdicts.tsv:{e['line']}" if e else "no verdict row"
        print(f"sp-audit: new site: {path} {func} {pattern}: {was} listed, {len(lines)} now, "
              f"lines {', '.join(map(str, lines))}; {row}")
    for path, func, pattern, was, count in lost:
        print(f"sp-audit: lost site: {path} {func} {pattern}: {was} listed, {count} now")
    accept = "read the new sites, fix their rows where the verdict is wrong, then run python tools/coop/sp_audit.py --accept"
    name = OUT.relative_to(ROOT).as_posix()
    if args.check:
        if new or lost:
            errors.append(f"{name} is stale; " + (accept if new else "run python tools/coop/sp_audit.py"))
        elif current != text:
            print("sp-audit: note: same sites, but a rerun would change the list; run python tools/coop/sp_audit.py")
    elif new and not args.accept:
        errors.append(f"{name} not written; {accept}")
    else:
        OUT.parent.mkdir(parents=True, exist_ok=True)
        OUT.write_text(text, encoding="utf-8", newline="\n")
        print(f"wrote {name}: {len(all_sites)} sites, {len(rows)} functions")

    for e in errors:
        print("sp-audit: " + e)
    if errors:
        return 1
    print("sp-audit: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
