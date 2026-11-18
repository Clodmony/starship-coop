#!/usr/bin/env python3
"""Tests tools/coop/release_check.py on throwaway git repositories (#358). No game, no network.

    python tools/coop/release_check_test.py

Each scenario builds a small repository with a `coop` branch, a gated commit, and a tag, then asks release_check
whether the release may go out. The good case passes; every way of cutting a release that is not the gated code is
refused: a side branch, a code change after the gate, a gated commit that is not behind the tag, notes missing or
unfinished, a badly named tag. Exit code 0 when all scenarios pass.
"""
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import release_check  # noqa: E402

WORK = Path(__file__).resolve().parents[2] / "local" / "test-release-check"
TAG = "v0.1.0-coop-alpha1"
NOTES = """# Starship Co-op {tag}

Gated commit: {gated}
Gate result: PASS, 59 of 59 steps

## What you need
Your own ROM.
## How to play together
Host and join.
## QA
Automated.
## Known issues
Some.
"""


def git(repo, *args):
    subprocess.run(["git", *args], cwd=repo, check=True, capture_output=True, text=True)


def head(repo):
    done = subprocess.run(["git", "rev-parse", "HEAD"], cwd=repo, check=True, capture_output=True, text=True)
    return done.stdout.strip()


def commit(repo, files, message):
    for name, text in files.items():
        path = repo / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "-m", message)
    return head(repo)


def new_repo(name):
    """A repository whose `coop` branch holds one code commit, the gated one. Returns (repo, gated)."""
    repo = WORK / name
    repo.mkdir(parents=True)
    git(repo, "init", "-q", "-b", "coop")
    git(repo, "config", "user.email", "test@example.invalid")
    git(repo, "config", "user.name", "test")
    git(repo, "config", "commit.gpgsign", "false")
    gated = commit(repo, {"src/game.c": "int main(void) { return 0; }\n"}, "game")
    return repo, gated


def notes(repo, gated, tag=TAG, text=None):
    return commit(repo, {f"docs/coop/releases/{tag}.md": text or NOTES.format(tag=tag, gated=gated)}, "notes")


def run(repo, tag=TAG, **kwargs):
    kwargs.setdefault("sha", head(repo))  # no tag object is made: the commit stands in for it
    return release_check.check(repo, tag, **kwargs)


def expect_fail(problems, word):
    if not problems:
        return "passed, but it must fail"
    if not any(word in problem for problem in problems):
        return f"failed, but not for {word!r}: {problems}"
    return None


def good():
    repo, gated = new_repo("good")
    notes(repo, gated)
    problems, info = run(repo, branch="coop")
    if problems:
        return f"a gated commit with finished notes was refused: {problems}"
    if info["prerelease"] is not True or info["gated"] != gated:
        return f"wrong info {info}"
    return None


def full_release_name():
    repo, gated = new_repo("full")
    notes(repo, gated, tag="v1.0.0-coop")
    problems, info = run(repo, tag="v1.0.0-coop", branch="coop")
    if problems or info["prerelease"] is not False:
        return f"v1.0.0-coop must be a release, not a pre-release: {problems} {info}"
    return None


def docs_after_the_gate():
    repo, gated = new_repo("docs")
    commit(repo, {"README.md": "now there is a download\n", "docs/coop/QA.md": "record\n"}, "docs")
    notes(repo, gated)
    problems, _ = run(repo, branch="coop")
    return f"documentation after the gate was refused: {problems}" if problems else None


def code_after_the_gate():
    repo, gated = new_repo("code")
    commit(repo, {"src/game.c": "int main(void) { return 1; }\n"}, "one more fix")
    notes(repo, gated)
    return expect_fail(run(repo, branch="coop")[0], "run the gate again")


def tool_after_the_gate():
    repo, gated = new_repo("tool")
    commit(repo, {"tools/coop/x.py": "print(1)\n"}, "tool")
    notes(repo, gated)
    return expect_fail(run(repo, branch="coop")[0], "tools/coop/x.py")


def side_branch():
    repo, gated = new_repo("side")
    git(repo, "checkout", "-q", "-b", "other")
    notes(repo, gated)
    return expect_fail(run(repo, branch="coop")[0], "not on coop")


def gated_not_behind():
    repo, gated = new_repo("behind")
    git(repo, "checkout", "-q", "-b", "other", "HEAD~0")
    other = commit(repo, {"src/other.c": "int x;\n"}, "other")
    git(repo, "checkout", "-q", "coop")
    notes(repo, other)
    return expect_fail(run(repo, branch="coop")[0], "not the tagged commit or one of its ancestors")


def run_from_a_branch():
    repo, gated = new_repo("branchrun")
    notes(repo, gated)
    tagged = head(repo)
    problems, _ = run(repo, branch="coop", expect=tagged)
    if problems:
        return f"a run on the tagged commit was refused: {problems}"
    return expect_fail(run(repo, branch="coop", expect=gated)[0], "run it from the tag")


def no_notes():
    repo, _ = new_repo("nonotes")
    return expect_fail(run(repo, branch="coop")[0], "not found")


def unfinished_notes():
    repo, gated = new_repo("todo")
    notes(repo, gated, text=NOTES.format(tag=TAG, gated=gated) + "\nTODO: write the known issues\n")
    return expect_fail(run(repo, branch="coop")[0], "TODO")


def missing_section():
    repo, gated = new_repo("section")
    text = NOTES.format(tag=TAG, gated=gated).replace("## Known issues", "## Other")
    notes(repo, gated, text=text)
    return expect_fail(run(repo, branch="coop")[0], "## Known issues")


def failed_gate():
    repo, gated = new_repo("fail")
    text = NOTES.format(tag=TAG, gated=gated).replace("Gate result: PASS", "Gate result: FAIL")
    notes(repo, gated, text=text)
    return expect_fail(run(repo, branch="coop")[0], "Gate result: PASS")


def bad_names():
    repo, gated = new_repo("names")
    notes(repo, gated)
    for tag in ("0.1.0-coop", "v0.1-coop", "v0.1.0", "v0.1.0-coop-alpha", "v0.1.0-coop-final", "v0.1.0-coop-alpha1x"):
        problems, _ = run(repo, tag=tag, branch="coop")
        if not any("is not vX.Y.Z-coop" in problem for problem in problems):
            return f"{tag} must be refused for its name: {problems}"
    return None


def short_hash():
    repo, gated = new_repo("short")
    notes(repo, gated[:12])
    problems, info = run(repo, branch="coop")
    return f"a 12-digit gated hash was refused: {problems}" if problems or info["gated"] != gated else None


SCENARIOS = {
    "good": good,
    "full_release_name": full_release_name,
    "docs_after_the_gate": docs_after_the_gate,
    "code_after_the_gate": code_after_the_gate,
    "tool_after_the_gate": tool_after_the_gate,
    "side_branch": side_branch,
    "gated_not_behind": gated_not_behind,
    "run_from_a_branch": run_from_a_branch,
    "no_notes": no_notes,
    "unfinished_notes": unfinished_notes,
    "missing_section": missing_section,
    "failed_gate": failed_gate,
    "bad_names": bad_names,
    "short_hash": short_hash,
}


def main():
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    failed = 0
    for name, scenario in SCENARIOS.items():
        try:
            problem = scenario()
        except Exception as error:  # one broken scenario must not hide the others
            problem = f"{type(error).__name__}: {error}"
        failed += problem is not None
        print(f"release check test: {name}: {'ok' if problem is None else 'FAIL: ' + problem}")
    if failed:
        print(f"release check test: FAIL ({failed} of {len(SCENARIOS)}), kept {WORK}")
        return 1
    shutil.rmtree(WORK, ignore_errors=True)
    print(f"release check test: PASS ({len(SCENARIOS)} of {len(SCENARIOS)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
