---
name: issue-loop
description: One Starship Co-op issue end to end in one session, starting with a plan the owner approves - orient from the tracker, plan, claim, branch, reproduce, test first, build, verify by script, draft PR, hand-off comment, then /clear. Use when the owner types /issue-loop or asks to start or continue an issue ("continue", "next issue", "work on #N").
---

# Issue loop

One issue per session. A long session re-sends the whole conversation on every step, so the loop ends at a
hand-off and a fresh context, not at the next issue. The shared rules are in `AGENTS.md` and `CONTRIBUTING.md`;
this skill is the order of work. Speak to the owner in the owner's language; issues, PRs, commits and repo files
stay in English.

## Steps

1. **Orient.** `git fetch origin`. Read `gh issue list --state open`, `gh pr list` (with reviews), the candidate
   issues and all their comments: the last hand-off is the newest comment, not a file. Read `docs/coop/DESIGN.md`,
   the accepted records in `docs/coop/decisions/`, recent `docs/coop/NOTES.md` entries and the owner's private
   `local/` notes. Done when you know: open PRs and what they wait for, who holds which claim, and which issues are
   free (nobody claimed, every "Needs" done or not blocking). Check that what you remember from an earlier session
   still matches the code.

2. **Plan, before any change.** If the owner did not name an issue, propose one with the free candidates as
   options, recommended first. Then show the owner a plan: the issue, the steps in order, the files likely touched,
   what will be checked and how, and what exists at the end (branch, draft PR, hand-off comment). Name any point
   where the plan departs from the issue's "Done when". Wait for the owner's go; change nothing before it.

3. **Claim.** Ask before posting: the claim comment publishes under the owner's account. Comment on the issue
   (branch name and a one-line plan). Done when the comment is posted.

4. **Branch** from the up-to-date `origin/coop`: `python tools/coop/new_worktree.py <issue> <slug>` or a plain
   branch `issue/<issue>-<slug>` (`docs/<slug>` for docs-only work). Stacked on an open PR only when the issue needs
   it; say so in the claim. Done when `git status` shows the new branch, clean.

5. **Understand and reproduce.** Read the decomp code involved and write findings to `docs/coop/NOTES.md` as you
   go: date, issue, Observed (file:line), Derived, Guess, Unknown. For a bug or missing behaviour, trigger it in the
   running game with the test tools (`tools/coop/replay.py`, `tools/coop/hash_check.py`, see `docs/coop/QA.md`),
   not only in the code. Done when every part of the issue is either red (a failure you can trigger on demand) or
   confirmed working by a run. Research issues: done when every question has a sourced answer or is marked
   `unknown`.

6. **Test first.** Turn each red into a scripted check that fails on the current build (replay plus state check,
   or a repo check in `tools/coop/`). Keep the failing output: the PR quotes it.

7. **Build.** Smallest change that turns the checks green, following the code rules in `AGENTS.md`: co-op code in
   `src/port/coop/`, hooks before edits, `// COOP:` on every upstream edit, everything gated on an active co-op
   session, message ids reserved in `docs/coop/PROTOCOL.md`. An architecture choice no accepted record covers stops
   the work: write a decision record instead. A claim in a comment, commit or doc states only what a run showed.

8. **Verify by script.** Run `python tools/coop/build.py --quiet` (docs-only: `python tools/coop/check.py`), the
   issue's checks, and the `test_all.py --only` steps for the area touched (table "Which steps for which change" in
   `tools/coop/README.md`) unattended in the background; wait for the notification and read only the summary. It
   must end in `build: OK`. The full `test_all.py` is not a step here: it runs once per batch of PRs before the
   merge (CONTRIBUTING.md). A failure in code you did not touch: rerun that check alone before calling it flaky. A failure
   from your change: fix, commit, rerun. Record the QA level and the test identity (commit, ROM version, OS,
   compiler, graphics backend). Never use the owner's own config, saves, `mods/` or running game.

9. **PR.** Show the owner the diff and wait for the go. Then commit (`<area>: <what changed>`), push, and open a
   draft against `coop` with the template filled in: what was reproduced, the failing output before, the change,
   the results after, the verify summary, the QA level, AI use disclosed. `Closes #<issue>` only when the whole
   issue is done. Never merge: merging is a maintainer's step after review.

10. **Hand off and stop.** Ask before posting, then comment on the issue: done, verified (how), left, next concrete
    step, surprises; facts with sources, guesses labelled. Stable findings go to `docs/coop/NOTES.md` in the same
    PR. Delete this issue's scratch data. Then ask the owner about every task still theirs (reviewing the diff,
    PRs waiting for review, decisions to propose, cleanup to approve) as choices, recommended option first. Act on
    the answers that are yours. Then tell the owner: "Issue done, please /clear before the next issue." Start
    nothing new.

Stop at the first contradiction between code, tests and docs, and tell the owner.

## Scratch storage

Working data (probe scripts, PR and comment bodies, captures, test instances) lives in `local/` (git ignores it),
for example `local/scratch/<issue>/` and `local/test-<name>/`. Nothing from there goes into commits, issues or
PRs: no usernames, local paths, tokens or game data.

A parallel run of several worktrees shares one base build, ROM copy and lock files, which do not fit in one
checkout's `local/`. Keep those in a scratch folder outside the repo, as your owner names it. The same rule holds:
nothing from there goes into commits, issues or PRs.

## Probing

Interactive probing is for debugging only. Put a probe in a script file and run it in one call; print only the
lines you need.
