#!/usr/bin/env python3
"""Create a git worktree and branch for one issue, so several branches (and agents) can build side by side.

    python tools/coop/new_worktree.py <issue-number> <short-slug>

Creates branch issue/<issue>-<slug> from origin/coop (or local coop if there is no origin) in
$COOP_WORKTREE_ROOT/<issue>-<slug>, default a "<repo>-worktrees" folder next to the repository, and initialises the
submodules there. Remove the worktree after the PR is merged: git worktree remove <path>
"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def git(*args, cwd=ROOT, check=True):
    return subprocess.run(["git", *args], cwd=cwd, check=check)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    issue, slug = sys.argv[1], sys.argv[2]
    root = Path(os.environ.get("COOP_WORKTREE_ROOT") or ROOT.parent / f"{ROOT.name}-worktrees")
    branch = f"issue/{issue}-{slug}"
    path = root / f"{issue}-{slug}"

    base = "coop"
    if git("rev-parse", "--verify", "--quiet", "origin/coop", check=False).returncode == 0:
        git("fetch", "origin", "coop")
        base = "origin/coop"

    root.mkdir(parents=True, exist_ok=True)
    git("worktree", "add", "-b", branch, str(path), base)
    git("submodule", "update", "--init", "--recursive", cwd=path)
    print(f"worktree ready: {path} (branch {branch} from {base})")
    print(f'remove it after the PR is merged: git worktree remove "{path}"')
    return 0


if __name__ == "__main__":
    sys.exit(main())
