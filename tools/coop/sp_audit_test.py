#!/usr/bin/env python3
"""Tests for sp_audit.py itself (#73, #114): a site the list does not have yet stops the rerun and fails --check until
someone accepts it, any other difference fails --check until a rerun, and a change of line numbers alone passes --check
and waits for the next rerun.

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
RERUN = "run python tools/coop/sp_audit.py"
NOTE = "only line numbers differ"

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
        lacks = (*lacks, "Traceback")
        if got != code:
            self.failures.append(f"{name}: exit {got}, wanted {code}")
        self.failures += [f"{name}: does not say {s!r}" for s in says if s not in out]
        self.failures += [f"{name}: says {s!r}" for s in lacks if s in out]

    def stopped(self, *says):
        """What a new site must do: --check names it, a rerun writes nothing, only --accept turns it green."""
        says += ("--accept",)  # every red step has to say the way out
        lacks = ["changed"] + ([] if any("lost site" in s for s in says) else ["lost site"])
        self.expect("--check", code=1, says=says, lacks=lacks)
        before = self.read(LIST)
        self.expect(code=1, says=says, lacks=lacks)
        if self.read(LIST) != before:
            self.failures.append("rerun: wrote the list past a new site")
        self.expect("--check", code=1, says=says, lacks=lacks)
        self.expect("--accept", code=0, says=says[:-1])
        self.expect("--check", code=0, says=["sp-audit: OK"], lacks=["new site", "lost site", NOTE])

    def rerun_is_enough(self, *says):
        """What anything but a new site must do: --check names it and what to run, and a plain rerun turns it green."""
        self.expect("--check", code=1, says=says + ("is stale", RERUN), lacks=["new site", "--accept"])
        self.expect(code=0, says=["sp-audit: OK"], lacks=["new site"])
        self.expect("--check", code=0, says=["sp-audit: OK"], lacks=["new site", "lost site", "changed", NOTE])

    def drifts(self, *rows):
        """What line numbers alone must do: --check passes and writes nothing, and a plain rerun writes these rows."""
        before = self.read(LIST)
        self.expect("--check", code=0, says=["sp-audit: OK", NOTE],
                    lacks=["new site", "lost site", "changed", "stale", "--accept"])
        if self.read(LIST) != before:
            self.failures.append("--check: wrote the list")
        self.expect(code=0, says=["sp-audit: OK"], lacks=["new site", "lost site", NOTE])
        self.failures += [f"rerun: the list has no row {s!r}" for s in rows if s not in self.read(LIST)]
        self.expect("--check", code=0, says=["sp-audit: OK"], lacks=["new site", "lost site", NOTE])


def clean(r):
    r.expect("--check", code=0, says=["sp-audit: OK"], lacks=["new site", "warning"])
    r.expect(code=0, says=["sp-audit: OK"], lacks=["new site", "warning"])
    r.expect("--accept", code=0, says=["sp-audit: OK"], lacks=["new site", "warning"])


def lines_move(r):
    r.write(ENGINE, "\n\n" + r.read(ENGINE))
    r.drifts("| `Alpha_Update` | 4:P0, 5:P0 |", "| `Beta_Update` | 9:CC |", "| `Beta_Update` | 10:MC |")


def site_moves_in_function(r):
    # One site past another of a different pattern: the same file, function and pattern on another line.
    r.edit(ENGINE, "    if (gCamCount == 1) {\n        gControllerPress[gMainController].button = 0;\n",
           "    gControllerPress[gMainController].button = 0;\n    if (gCamCount == 1) {\n")
    r.drifts("| `Beta_Update` | 8:CC |", "| `Beta_Update` | 7:MC |")


def function_moves(r):
    # The rows change order, the sites do not.
    alpha, beta = r.read(ENGINE).split("\n\n")
    r.write(ENGINE, beta + "\n" + alpha + "\n")
    r.drifts("| `Beta_Update` | 2:CC |", "| `Alpha_Update` | 8:P0, 9:P0 |")


def site_removed(r):
    r.edit(ENGINE, "    gPlayer[0].csTimer = 0;\n", "")
    r.rerun_is_enough(f"lost site: {ENGINE} Alpha_Update gPlayer[0]: 2 listed, 1 now")


def site_removed_last(r):
    # The last site of a pattern in its function; the star row still has other sites, so only the lost site is red.
    r.edit(OVERLAY, "    gCamCount = 1;\n", "")
    r.rerun_is_enough(f"lost site: {OVERLAY} Delta_Update gCamCount: 1 listed, 0 now")


def row_changed(r):
    r.edit(VERDICTS, "sim\tread\twhole function", "view\tsite\tchanged after reading")
    r.rerun_is_enough(f"changed site: {ENGINE} Alpha_Update gPlayer[0]: listed sim | read | whole function, "
                      "now view | site | changed after reading")


def verdict_changed(r):
    r.edit(VERDICTS, "sim\tread", "view\tread")
    r.rerun_is_enough(f"changed site: {ENGINE} Alpha_Update gPlayer[0]: listed sim | read | whole function, "
                      "now view | read | whole function")


def basis_changed(r):
    r.edit(VERDICTS, "sim\tread", "sim\tsite")
    r.rerun_is_enough(f"changed site: {ENGINE} Alpha_Update gPlayer[0]: listed sim | read | whole function, "
                      "now sim | site | whole function")


def note_changed(r):
    # A | in the note, so the columns have to be read past it. The other row of the function has not changed.
    r.edit(VERDICTS, "a note with a | in it", "a note with two | | in it")
    r.expect("--check", code=1, lacks=["Beta_Update gMainController"])
    r.rerun_is_enough(f"changed site: {ENGINE} Beta_Update gCamCount: listed setup | site | a note with a \\| in it, "
                      "now setup | site | a note with two \\| \\| in it")


def star_row_changed(r):
    # One line of the verdict file, and every site under it is named.
    r.edit(VERDICTS, "flow\tname", "view\tname")
    was, now = "flow | name | every function of the folder", "view | name | every function of the folder"
    r.rerun_is_enough(f"changed site: {OVERLAY} Gamma_Update gPlayer[0]: listed {was}, now {now}",
                      f"changed site: {OVERLAY} Delta_Update gCamCount: listed {was}, now {now}")


def moved_and_changed(r):
    # Line numbers are ignored, not what comes with them.
    r.write(ENGINE, "\n" + r.read(ENGINE))
    r.edit(VERDICTS, "sim\tread", "view\tread")
    r.rerun_is_enough(f"changed site: {ENGINE} Alpha_Update gPlayer[0]: listed sim | read | whole function, "
                      "now view | read | whole function")


def function_row_new_pattern(r):
    r.edit(ENGINE, "    if (gCamCount == 1) {", "    gPlayer[0].csState = 0;\n    if (gCamCount == 1) {")
    r.stopped(f"new site: {ENGINE} Beta_Update gPlayer[0]: 0 listed, 1 now, lines 7", BETA_ROW)


def function_row_one_more(r):
    r.edit(ENGINE, "    gPlayer[0].csTimer = 0;\n", "    gPlayer[0].csTimer = 0;\n    gPlayer[0].csState = 1;\n")
    r.stopped(f"new site: {ENGINE} Alpha_Update gPlayer[0]: 2 listed, 3 now, lines 2, 3, 4", ALPHA_ROW)


def pattern_row(r):
    r.edit(ENGINE, "    if (gCamCount == 1) {", "    gMainController = 1;\n    if (gCamCount == 1) {")
    r.stopped(f"new site: {ENGINE} Beta_Update gMainController: 1 listed, 2 now, lines 7, 9", PATTERN_ROW)


def star_row(r):
    r.write(OVERLAY, r.read(OVERLAY) + NEW_FUNCTION)
    r.stopped(f"new site: {OVERLAY} Epsilon_Update gPlayer[0]: 0 listed, 1 now, lines 10", STAR_ROW)


def star_row_new_file(r):
    path = "src/overlays/ovl_x/fox_c.c"
    r.write(path, NEW_FUNCTION)
    r.git("add", path)
    r.stopped(f"new site: {path} Epsilon_Update gPlayer[0]: 0 listed, 1 now, lines 3", STAR_ROW)


def star_row_site_moved(r):
    # The row's site count stays the same here, but the key has the function in it: this is no moved line. It is lost
    # in one function and new in the other, even under one row, because nobody has read it where it is now.
    r.edit(OVERLAY, "    gPlayer[0].csState = 0;\n", "")
    r.edit(OVERLAY, "    gCamCount = 1;\n", "    gCamCount = 1;\n    gPlayer[0].csState = 0;\n")
    r.stopped(f"new site: {OVERLAY} Delta_Update gPlayer[0]: 0 listed, 1 now, lines 6", STAR_ROW,
              f"lost site: {OVERLAY} Gamma_Update gPlayer[0]: 1 listed, 0 now")


def star_row_function_renamed(r):
    # The same key at work: a new name means new sites, and the row cannot tell a rename from a new function.
    r.edit(OVERLAY, "Gamma_Update", "Gamma_Tick")
    r.stopped(f"new site: {OVERLAY} Gamma_Tick gPlayer[0]: 0 listed, 1 now, lines 2", STAR_ROW,
              f"lost site: {OVERLAY} Gamma_Update gPlayer[0]: 1 listed, 0 now")


def no_row(r):
    r.write(ENGINE, r.read(ENGINE) + NEW_FUNCTION)
    says = [f"new site: {ENGINE} Epsilon_Update gPlayer[0]: 0 listed, 1 now, lines 13", "no verdict row"]
    r.expect("--check", code=1, says=says + ["1 functions have no verdict"])
    r.expect(code=1, says=says + ["1 functions have no verdict"])
    r.expect("--accept", code=1, says=says + ["1 functions have no verdict"])  # accepted, but it still needs a row
    r.expect("--check", code=1, says=["1 functions have no verdict"], lacks=["new site"])
    r.write(VERDICTS, r.read(VERDICTS) + f"{ENGINE}\tEpsilon_Update\tsim\tread\tnew row\n")
    r.rerun_is_enough(f"changed site: {ENGINE} Epsilon_Update gPlayer[0]: listed unknown | none | , "
                      "now sim | read | new row")


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
    r.drifts("| `Alpha_Update` | 2:P0, 4:P0 |")


def file_deleted(r):
    # Tracked, deleted, not committed. The scan works on what is on disk, so its sites are lost, and the file is named.
    path = "src/overlays/ovl_x/fox_c.c"
    r.write(path, NEW_FUNCTION)
    r.git("add", path)
    r.expect("--accept", code=0, says=["sp-audit: OK"])
    (r.folder / path).unlink()
    r.expect("--todo", code=0)
    r.rerun_is_enough(f"warning: {path} is tracked by git but not on disk",
                      f"lost site: {path} Epsilon_Update gPlayer[0]: 1 listed, 0 now")
    r.write(path, NEW_FUNCTION)  # restored after that rerun: the list no longer has its site, so nothing slips in
    r.stopped(f"new site: {path} Epsilon_Update gPlayer[0]: 0 listed, 1 now, lines 3", STAR_ROW)


def file_deleted_no_site(r):
    path = "src/engine/fox_c.c"
    r.write(path, "int x;\n")
    r.git("add", path)
    (r.folder / path).unlink()
    r.expect("--check", code=0, says=["sp-audit: OK"], lacks=["warning", "lost site", NOTE])
    r.expect(code=0, says=["sp-audit: OK"], lacks=["warning", "lost site"])


def file_removed(r):
    # The deletion is in the index, as in a commit: lost sites, and nothing to warn about.
    path = "src/overlays/ovl_x/fox_c.c"
    r.write(path, NEW_FUNCTION)
    r.git("add", path)
    r.expect("--accept", code=0, says=["sp-audit: OK"])
    r.git("rm", "-q", "-f", path)
    lost = f"lost site: {path} Epsilon_Update gPlayer[0]: 1 listed, 0 now"
    r.expect("--check", code=1, says=[lost], lacks=["warning"])
    r.rerun_is_enough(lost)


def hand_edit_row(r):
    r.edit(LIST, "| sim | read | whole function |", "| view | read | whole function |")
    r.rerun_is_enough(f"changed site: {ENGINE} Alpha_Update gPlayer[0]: listed view | read | whole function, "
                      "now sim | read | whole function")


def hand_edit_split_row(r):
    # Two rows for the sites of one key, the second one right: every row counts, not the last one read.
    r.edit(LIST, "| `Alpha_Update` | 2:P0, 3:P0 | sim | read | whole function |\n",
           "| `Alpha_Update` | 2:P0 | view | read | whole function |\n"
           "| `Alpha_Update` | 3:P0 | sim | read | whole function |\n")
    r.rerun_is_enough(f"changed site: {ENGINE} Alpha_Update gPlayer[0]: "
                      "listed sim | read | whole function / view | read | whole function, "
                      "now sim | read | whole function")


def hand_edit_text(r):
    r.edit(LIST, "Do not edit by hand", "Edit by hand")
    r.rerun_is_enough("changed text: listed ", "Edit by hand")


def hand_edit_summary(r):
    r.edit(LIST, "6 sites in 4 functions", "7 sites in 4 functions")
    r.rerun_is_enough("changed text: listed '7 sites in 4 functions")


def conflict_markers_text(r):
    # Outside the rows: the sites are all there, so only the text can show this.
    line = "6 sites in 4 functions in 2 files.\n"
    r.edit(LIST, line, f"<<<<<<< HEAD\n{line}=======\n7 sites in 4 functions in 2 files.\n>>>>>>> other\n")
    r.rerun_is_enough("changed text: listed '<<<<<<< HEAD'")


def conflict_markers_one_side(r):
    # Around a row, with the other side empty, so no site is counted twice.
    row = "| `Gamma_Update` | 2:P0 | flow | name | every function of the folder |\n"
    r.edit(LIST, row, f"<<<<<<< HEAD\n{row}=======\n>>>>>>> other\n")
    r.rerun_is_enough("changed text: listed '<<<<<<< HEAD'")


def hand_edit_line_numbers(r):
    # What stays open, on purpose: line numbers are the one thing --check ignores, so a wrong one passes with the note.
    r.edit(LIST, "2:P0, 3:P0", "20:P0, 30:P0")
    r.drifts("| `Alpha_Update` | 2:P0, 3:P0 |")


SCENARIOS = {
    "clean": clean,
    "lines-move": lines_move,
    "site-moves-in-function": site_moves_in_function,
    "function-moves": function_moves,
    "site-removed": site_removed,
    "site-removed-last": site_removed_last,
    "row-changed": row_changed,
    "verdict-changed": verdict_changed,
    "basis-changed": basis_changed,
    "note-changed": note_changed,
    "star-row-changed": star_row_changed,
    "moved-and-changed": moved_and_changed,
    "function-row-new-pattern": function_row_new_pattern,
    "function-row-one-more": function_row_one_more,
    "pattern-row": pattern_row,
    "star-row": star_row,
    "star-row-new-file": star_row_new_file,
    "star-row-site-moved": star_row_site_moved,
    "star-row-function-renamed": star_row_function_renamed,
    "no-row": no_row,
    "list-missing": list_missing,
    "untracked-site": untracked_site,
    "untracked-no-site": untracked_no_site,
    "untracked-ignored": untracked_ignored,
    "known-gap-swap": known_gap_swap,
    "file-deleted": file_deleted,
    "file-deleted-no-site": file_deleted_no_site,
    "file-removed": file_removed,
    "hand-edit-row": hand_edit_row,
    "hand-edit-split-row": hand_edit_split_row,
    "hand-edit-text": hand_edit_text,
    "hand-edit-summary": hand_edit_summary,
    "conflict-markers-text": conflict_markers_text,
    "conflict-markers-one-side": conflict_markers_one_side,
    "hand-edit-line-numbers": hand_edit_line_numbers,
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
