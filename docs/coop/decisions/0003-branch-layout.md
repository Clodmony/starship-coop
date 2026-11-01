# 0003: Branch layout: `coop` integrates, `main` mirrors upstream

- Status: Accepted
- Date: 2026-11-01
- Issue: #34
- Supersedes: the branch-layout sentence of 0001

## Context

0001 made the fork's `main` the integration branch, with upstream Starship reachable only through a second remote
(`starship`). That mixes two things in one branch name: what upstream ships and what we changed. Contributors and
agents had to add a second remote to see upstream at all, and generic fixes meant for upstream had no clean base in
this repository.

## Options

1. Keep `main` as the integration branch (0001).
2. `main` is an unchanged mirror of upstream `main`; a separate default branch holds the co-op work.

Option 2 does not make upstream merges smaller (conflicts come from how much co-op changes upstream files), but it:

- shows every co-op change with `git diff origin/main...origin/coop`;
- gives everyone upstream through `origin`, without a second remote;
- gives generic fixes a clean base (`origin/main`) for PRs to HarbourMasters;
- separates "update the mirror" (any time, fast-forward only, no risk) from "integrate upstream" (a reviewed sync PR
  when it suits).

## Decision

Option 2. `coop` is the default branch and the only integration branch, protected by the `coop` ruleset (PRs with a
code-owner review, `Co-op checks` required). `main` is a mirror of HarbourMasters/Starship `main`, updated only by
fast-forward by repository admins (ruleset `upstream-mirror`), and brought into `coop` with a
`sync/upstream-YYYY-MM-DD` PR merged with a merge commit.

Work branches are named `issue/<number>-<slug>`. The earlier `coop/<number>-<slug>` scheme is impossible next to a
branch called `coop` (git stores branches as paths, so `coop` cannot be both a branch and a folder).

## Consequences

- On 2026-11-01 `main` was reset once to upstream `3322643f` (a one-time force push before anyone else had cloned);
  the co-op commits live on `coop`.
- Docs, skills, `tools/coop/new_worktree.py`, workflow triggers and the stored rulesets use `coop` and `issue/...`.
- Never commit to `main`. If the mirror push is ever refused as not a fast-forward, someone did; stop and fix that
  first.
