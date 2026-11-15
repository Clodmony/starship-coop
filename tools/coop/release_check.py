#!/usr/bin/env python3
"""Checks a release tag before anything is built or published from it (#358).

    python tools/coop/release_check.py --tag v0.1.0-coop-alpha1 [--sha <commit>] [--branch origin/coop]

A release is a tag on a commit of `coop` whose code passed the full gate on the owner's PC (`tools/coop/test_all.py`,
recorded in docs/coop/QA.md). CI cannot run that gate: it needs a ROM and takes about 20 minutes with several game
clients. So this script checks the process that makes the tag mean "tested":

  - the tag is named vX.Y.Z-coop or vX.Y.Z-coop-<alpha|beta|rc><n>; a name with a suffix is a pre-release;
  - the commit is on the branch (default origin/coop), not on a side branch, and with --expect-sha it is the commit
    the workflow runs on;
  - docs/coop/releases/<tag>.md exists, with the lines `Gated commit: <hash>` and `Gate result: PASS ...` and the
    sections `## What you need`, `## How to play together`, `## QA`, `## Known issues` (docs/coop/releases/TEMPLATE.md);
  - the gated commit is the tagged commit or an ancestor of it, and everything that changed since is documentation
    (docs/ and the README), so the code of the tag is the code of the gate.

In GitHub Actions the results go to the step outputs: tag, prerelease (true or false), notes (the notes file).
Exit code 0 on OK, 1 otherwise.
"""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TAG = re.compile(r"^v\d+\.\d+\.\d+-coop(?P<suffix>-(?:alpha|beta|rc)\d+)?$")
SECTIONS = ("## What you need", "## How to play together", "## QA", "## Known issues")
DOC_ONLY = re.compile(r"^(docs/|README\.md$)")


def git(repo, *args):
    done = subprocess.run(["git", *args], cwd=repo, capture_output=True, text=True)
    return done.returncode, done.stdout.strip(), done.stderr.strip()


def check(repo, tag, sha=None, branch="origin/coop", expect=None):
    """Returns (problems, info). info holds prerelease and notes when the tag is named right. expect: the commit the
    workflow runs on, which has to be the tagged one (a run started from a branch would build another commit)."""
    problems = []
    info = {}
    named = TAG.match(tag)
    if not named:
        return [f"tag {tag!r} is not vX.Y.Z-coop or vX.Y.Z-coop-<alpha|beta|rc><n>"], info
    info["prerelease"] = bool(named.group("suffix"))

    code, tagged, error = git(repo, "rev-parse", "--verify", f"{sha or tag}^{{commit}}")
    if code != 0:
        return [f"no commit for {sha or tag}: {error}"], info
    if expect and expect != tagged:
        return [f"the workflow runs on {expect[:10]} but {tag} is {tagged[:10]}: run it from the tag"], info
    code, _, _ = git(repo, "merge-base", "--is-ancestor", tagged, branch)
    if code != 0:
        problems.append(f"{tagged[:10]} is not on {branch}: a release is cut from the tested branch only")

    notes = Path(repo) / "docs" / "coop" / "releases" / f"{tag}.md"
    info["notes"] = str(notes.relative_to(repo).as_posix())
    if not notes.exists():
        return problems + [f"{info['notes']} not found: copy docs/coop/releases/TEMPLATE.md and fill it in"], info
    text = notes.read_text(encoding="utf-8")
    gated = re.search(r"^Gated commit: ([0-9a-f]{10,40})\b", text, re.MULTILINE)
    result = re.search(r"^Gate result: PASS\b.*$", text, re.MULTILINE)
    if not gated:
        problems.append(f"{info['notes']}: no line `Gated commit: <hash>`")
    if not result:
        problems.append(f"{info['notes']}: no line `Gate result: PASS ...`")
    problems += [f"{info['notes']}: no section `{heading}`" for heading in SECTIONS if heading not in text]
    if "TODO" in text:
        problems.append(f"{info['notes']}: still holds a TODO from the template")
    if not gated:
        return problems, info

    code, full, error = git(repo, "rev-parse", "--verify", f"{gated.group(1)}^{{commit}}")
    if code != 0:
        return problems + [f"gated commit {gated.group(1)} not found: {error}"], info
    code, _, _ = git(repo, "merge-base", "--is-ancestor", full, tagged)
    if code != 0:
        return problems + [f"gated commit {full[:10]} is not the tagged commit or one of its ancestors"], info
    _, changed, _ = git(repo, "diff", "--name-only", full, tagged)
    code_changes = [name for name in changed.splitlines() if not DOC_ONLY.match(name)]
    if code_changes:
        shown = ", ".join(code_changes[:5]) + (" ..." if len(code_changes) > 5 else "")
        problems.append(f"{len(code_changes)} files other than documentation changed since the gated commit "
                        f"{full[:10]}: {shown}; run the gate again on the tagged commit")
    info["gated"] = full
    return problems, info


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--tag", required=True)
    p.add_argument("--sha", help="the commit to check instead of the tag's (a tag that is not made yet)")
    p.add_argument("--branch", default="origin/coop", help="the branch a release is cut from (default origin/coop)")
    p.add_argument("--expect-sha", help="the commit the workflow runs on (GITHUB_SHA); it has to be the tag's commit")
    p.add_argument("--repo", type=Path, default=ROOT, help=argparse.SUPPRESS)
    args = p.parse_args()

    problems, info = check(args.repo, args.tag, args.sha, args.branch, args.expect_sha)
    if problems:
        for problem in problems:
            print(f"release check: {problem}")
        print(f"release check: FAILED, {len(problems)} problems for {args.tag}")
        return 1
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as out:
            out.write(f"tag={args.tag}\nprerelease={'true' if info['prerelease'] else 'false'}\nnotes={info['notes']}\n")
    kind = "pre-release" if info["prerelease"] else "release"
    print(f"release check: OK {args.tag} ({kind}), gated commit {info['gated'][:10]}, notes {info['notes']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
