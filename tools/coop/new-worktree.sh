#!/usr/bin/env bash
# Create a git worktree and branch for one issue, so several branches (and agents) can build side by side.
# Usage: tools/coop/new-worktree.sh <issue-number> <short-slug>
# Worktrees go to $COOP_WORKTREE_ROOT (default: a "<repo>-worktrees" folder next to the repository).
set -euo pipefail
if [ $# -ne 2 ]; then
  echo "usage: $0 <issue-number> <short-slug>" >&2
  exit 2
fi
issue=$1
slug=$2
top=$(git rev-parse --show-toplevel)
root=${COOP_WORKTREE_ROOT:-"$(dirname "$top")/$(basename "$top")-worktrees"}
branch="coop/${issue}-${slug}"
dir="$root/${issue}-${slug}"

base=main
if git -C "$top" rev-parse --verify --quiet origin/main > /dev/null; then
  git -C "$top" fetch origin main
  base=origin/main
fi

mkdir -p "$root"
git -C "$top" worktree add -b "$branch" "$dir" "$base"
git -C "$dir" submodule update --init --recursive
echo "worktree ready: $dir (branch $branch from $base)"
echo "remove it after the PR is merged: git worktree remove \"$dir\""
