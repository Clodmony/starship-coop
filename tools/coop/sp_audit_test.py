#!/usr/bin/env python3
"""Tests for sp_audit.py itself (#73): a site the list does not have yet stops the rerun and fails --check until
someone accepts it, and a change that adds no site does not.

    python tools/coop/sp_audit_test.py                       # unattended; prints PASS or FAIL
    python tools/coop/sp_audit_test.py --only star-row,no-row
    python tools/coop/sp_audit_test.py --list

Each scenario makes a small git repository with a copy of sp_audit.py, two game files and a verdict file, changes it,
and checks each exit code and output line. The repository has no commit, so a scenario that passes needs no history,
as in CI, which checks out one commit. No build, no ROM, a few seconds. Exit code 0 when every scenario holds, 1
otherwise. Everything lands under local/test-spaudit-*: deleted when all scenarios hold, kept for reading when one fails.
"""
import argparse
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "coop" / "sp_audit.py"
LIST = "docs/coop/audit/single-player.md"
VERDICTS = "docs/coop/audit/sp-verdicts.tsv"
ENGINE = "src/engine/fox_a.c"
OVERLAY = "src/overlays/ovl_x/fox_b.c"
FIRST_RUN = ("--accept",)  # there is no list yet, so every site is new

FILES = {
    ENGINE: """\
void Alpha_Update(void) {
    gPlayer[0].csState = 0;
    gPlayer[0].csTimer = 0;
}

void Beta_Update(void) {
    if (gCamCount == 1) {
        gControllerPress[gMainController].button = 0;
    }
}
""",
    OVERLAY: """\
void Gamma_Update(void) {
    gPlayer[0].csState = 0;
}

void Delta_Update(void) {
    gCamCount = 1;
}
""",
    VERDICTS: """\
# a comment
src/engine/fox_a.c\tAlpha_Update\tsim\tread\twhole function
src/engine/fox_a.c\tBeta_Update\tsetup\tsite\ta note with a | in it
src/engine/fox_a.c\tBeta_Update@gMainController\tinput\tsite\tone pattern of a function
src/overlays/ovl_x/*\t*\tflow\tname\tevery function of the folder
""",
}
ALPHA_ROW = "takes sim (read) from sp-verdicts.tsv:2"
BETA_ROW = "takes setup (site) from sp-verdicts.tsv:3"
PATTERN_ROW = "takes input (site) from sp-verdicts.tsv:4"
STAR_ROW = "takes flow (name) from sp-verdicts.tsv:5"
NEW_FUNCTION = "\nvoid Epsilon_Update(void) {\n    gPlayer[0].csState = 0;\n}\n"
UNTRACKED = "src/engine/copy of fox_a.c"  # a space, so the tool has to split git's output properly


class Repo:
    def __init__(self, folder):
        self.folder = folder
        self.failures = []
        for path, text in FILES.items():
            self.write(path, text)
        (folder / "tools" / "coop").mkdir(parents=True)
        shutil.copy(TOOL, folder / "tools" / "coop" / "sp_audit.py")
        self.git("init", "-q")
        self.git("add", ".")
        self.audit(*FIRST_RUN)

    def git(self, *args):
        subprocess.run(["git", *args], cwd=self.folder, check=True, capture_output=True)

    def read(self, path):
        p = self.folder / path
        return p.read_text(encoding="utf-8") if p.exists() else None

    def write(self, path, text):
        p = self.folder / path
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text, encoding="utf-8", newline="\n")

    def edit(self, path, old, new):
        text = self.read(path)
        assert old in text, old
        self.write(path, text.replace(old, new, 1))

    def audit(self, *args):
        r = subprocess.run([sys.executable, "-I", "tools/coop/sp_audit.py", *args], cwd=self.folder,
                           capture_output=True, text=True, encoding="utf-8")
        return r.returncode, r.stdout + r.stderr

    def expect(self, *args, code, says=(), lacks=()):
        """Run the tool; note every way the exit code or the output differs from what is wanted."""
        got, out = self.audit(*args)
        name = " ".join(args) or "rerun"
        if got != code:
            self.failures.append(f"{name}: exit {got}, wanted {code}")
        self.failures += [f"{name}: does not say {s!r}" for s in says if s not in out]
        self.failures += [f"{name}: says {s!r}" for s in lacks if s in out]

    def stopped(self, *says):
        """What a new site must do: --check names it, a rerun writes nothing, only --accept turns it green."""
        says += ("--accept",)  # every red step has to say the way out
        self.expect("--check", code=1, says=says)
        before = self.read(LIST)
        self.expect(code=1, says=says)
        if self.read(LIST) != before:
            self.failures.append("rerun: wrote the list past a new site")
        self.expect("--check", code=1, says=says)
        self.expect("--accept", code=0, says=says[:-1])
        self.expect("--check", code=0, says=["sp-audit: OK"], lacks=["new site"])

    def rerun_is_enough(self):
        """What any other change must do: --check is red without a new site, and a plain rerun turns it green."""
        self.expect("--check", code=1, says=["is stale"], lacks=["new site", "--accept"])
        self.expect(code=0, says=["sp-audit: OK"], lacks=["new site"])
        self.expect("--check", code=0, says=["sp-audit: OK"], lacks=["new site"])


def clean(r):
    r.expect("--check", code=0, says=["sp-audit: OK"], lacks=["new site", "warning"])
    r.expect(code=0, says=["sp-audit: OK"], lacks=["new site", "warning"])
    r.expect("--accept", code=0, says=["sp-audit: OK"], lacks=["new site", "warning"])


def lines_move(r):
    r.write(ENGINE, "\n\n" + r.read(ENGINE))
    r.rerun_is_enough()


def site_removed(r):
    r.edit(ENGINE, "    gPlayer[0].csTimer = 0;\n", "")
    r.rerun_is_enough()


def row_changed(r):
    r.edit(VERDICTS, "sim\tread\twhole function", "view\tsite\tchanged after reading")
    r.rerun_is_enough()


def function_row_new_pattern(r):
    r.edit(ENGINE, "    if (gCamCount == 1) {", "    gPlayer[0].csState = 0;\n    if (gCamCount == 1) {")
    r.stopped(f"new site: {ENGINE} Beta_Update gPlayer[0]: 0 listed, 1 now, lines 7", BETA_ROW)


def function_row_one_more(r):
    r.edit(ENGINE, "    gPlayer[0].csTimer = 0;\n", "    gPlayer[0].csTimer = 0;\n    gPlayer[0].csState = 1;\n")
    r.stopped(f"new site: {ENGINE} Alpha_Update gPlayer[0]: 2 listed, 3 now, lines 2, 3, 4", ALPHA_ROW)


def pattern_row(r):
    r.edit(ENGINE, "    if (gCamCount == 1) {", "    gMainController = 1;\n    if (gCamCount == 1) {")
    r.stopped(f"new site: {ENGINE} Beta_Update gMainController: 1 listed, 2 now, lines 7, 9", PATTERN_ROW)


def bare_argument(r):
    r.edit(ENGINE, "    gPlayer[0].csTimer = 0;\n", "    gPlayer[0].csTimer = 0;\n    Foo(gPlayer, 1);\n    Bar(1, gPlayer);\n")
    r.stopped(f"new site: {ENGINE} Alpha_Update gPlayer arg: 0 listed, 2 now, lines 4, 5", ALPHA_ROW)


def bare_not_argument(r):
    # A fake match in the decomp, an alias and the other names that start the same are not arguments.
    r.edit(ENGINE, "    gPlayer[0].csTimer = 0;\n", "    gPlayer[0].csTimer = 0;\n    if (gPlayer) {}\n    p = gPlayer;\n"
           "    Foo(gPlayerNum, gPlayerShots);\n")
    r.rerun_is_enough()


def star_row(r):
    r.write(OVERLAY, r.read(OVERLAY) + NEW_FUNCTION)
    r.stopped(f"new site: {OVERLAY} Epsilon_Update gPlayer[0]: 0 listed, 1 now, lines 10", STAR_ROW)


def star_row_new_file(r):
    path = "src/overlays/ovl_x/fox_c.c"
    r.write(path, NEW_FUNCTION)
    r.git("add", path)
    r.stopped(f"new site: {path} Epsilon_Update gPlayer[0]: 0 listed, 1 now, lines 3", STAR_ROW)


def star_row_site_moved(r):
    # The row's site count stays the same here; the list still sees it, because it counts per function.
    r.edit(OVERLAY, "    gPlayer[0].csState = 0;\n", "")
    r.edit(OVERLAY, "    gCamCount = 1;\n", "    gCamCount = 1;\n    gPlayer[0].csState = 0;\n")
    r.stopped(f"new site: {OVERLAY} Delta_Update gPlayer[0]: 0 listed, 1 now, lines 6", STAR_ROW)


def no_row(r):
    r.write(ENGINE, r.read(ENGINE) + NEW_FUNCTION)
    says = [f"new site: {ENGINE} Epsilon_Update gPlayer[0]: 0 listed, 1 now, lines 13", "no verdict row"]
    r.expect("--check", code=1, says=says + ["1 functions have no verdict"])
    r.expect(code=1, says=says + ["1 functions have no verdict"])
    r.expect("--accept", code=1, says=says + ["1 functions have no verdict"])  # accepted, but it still needs a row
    r.expect("--check", code=1, says=["1 functions have no verdict"], lacks=["new site"])
    r.write(VERDICTS, r.read(VERDICTS) + f"{ENGINE}\tEpsilon_Update\tsim\tread\tnew row\n")
    r.rerun_is_enough()


def list_missing(r):
    (r.folder / LIST).unlink()
    r.expect("--check", code=1, says=["new site", "is stale"])
    r.expect(code=1, says=["new site"])
    if r.read(LIST) is not None:
        r.failures.append("rerun: wrote a list when there was none to compare with")
    r.expect("--accept", code=0, says=["sp-audit: OK"])
    r.expect("--check", code=0, says=["sp-audit: OK"], lacks=["new site"])


def untracked_site(r):
    r.write(UNTRACKED, NEW_FUNCTION)
    says = [f"warning: {UNTRACKED} is not tracked by git, so its 1 sites are not in the list"]
    r.expect("--check", code=0, says=says + ["sp-audit: OK"])
    r.expect(code=0, says=says + ["sp-audit: OK"])
    r.git("add", UNTRACKED)  # a tracked file is scanned, and this one has no row
    r.expect("--check", code=1, says=["new site", "no verdict row"], lacks=["warning"])


def untracked_no_site(r):
    r.write(UNTRACKED, "int x;\n")
    r.expect("--check", code=0, says=["sp-audit: OK"], lacks=["warning"])


def untracked_ignored(r):
    r.write(".gitignore", "*.gen.c\n")
    r.write("src/engine/fox_a.gen.c", NEW_FUNCTION)
    r.expect("--check", code=0, says=["sp-audit: OK"], lacks=["warning"])


def known_gap_swap(r):
    # A limit, stated in the list's header: the list holds counts and line numbers, not the code on the line.
    r.edit(ENGINE, "    gPlayer[0].csTimer = 0;\n", "    gPlayer[0].csFrameCount = 0;\n")
    r.expect("--check", code=0, says=["sp-audit: OK"], lacks=["new site"])
    r.edit(ENGINE, "    gPlayer[0].csFrameCount = 0;\n", "\n    gPlayer[0].csFrameCount = 0;\n")
    r.rerun_is_enough()


SCENARIOS = {
    "clean": clean,
    "lines-move": lines_move,
    "site-removed": site_removed,
    "row-changed": row_changed,
    "function-row-new-pattern": function_row_new_pattern,
    "function-row-one-more": function_row_one_more,
    "pattern-row": pattern_row,
    "bare-argument": bare_argument,
    "bare-not-argument": bare_not_argument,
    "star-row": star_row,
    "star-row-new-file": star_row_new_file,
    "star-row-site-moved": star_row_site_moved,
    "no-row": no_row,
    "list-missing": list_missing,
    "untracked-site": untracked_site,
    "untracked-no-site": untracked_no_site,
    "untracked-ignored": untracked_ignored,
    "known-gap-swap": known_gap_swap,
}


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--only", help="comma-separated scenarios (default: all)")
    p.add_argument("--list", action="store_true", help="print the scenarios and stop")
    args = p.parse_args()

    if args.list:
        print("\n".join(SCENARIOS))
        return 0
    chosen = list(SCENARIOS) if not args.only else [s.strip() for s in args.only.split(",") if s.strip()]
    unknown = [s for s in chosen if s not in SCENARIOS]
    if unknown:
        sys.exit(f"sp_audit_test: unknown scenario(s) {', '.join(unknown)}; known: {', '.join(SCENARIOS)}")

    bench = ROOT / "local" / f"test-spaudit-{time.strftime('%Y%m%d-%H%M%S')}"
    failed = []
    for name in chosen:
        repo = Repo(bench / name)
        SCENARIOS[name](repo)
        print(f"sp_audit_test: {name}: " + (f"FAIL: {'; '.join(repo.failures)}" if repo.failures else "ok"))
        if repo.failures:
            failed.append(name)
    if failed:
        print(f"sp_audit_test: kept {bench.relative_to(ROOT).as_posix()}")
    else:
        shutil.rmtree(bench, ignore_errors=True)
    print("sp_audit_test: FAIL" if failed else "sp_audit_test: PASS")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
