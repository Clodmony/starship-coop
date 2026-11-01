---
name: starship-coop-issue
description: Work one Starship Co-op issue end to end in one session - read the tracker, claim, branch or worktree, research or build, test, draft PR, hand-off comment. Use when starting or continuing work on an issue or work package in this repo ("continue", "next issue", "take WP-xx").
---

# One issue, one session

1. **Orient.** `git fetch origin`. Read `gh issue list --state open`, `gh pr list`, then the chosen issue and
   all its comments: the last hand-off is there. Read `docs/coop/DESIGN.md`, accepted records in
   `docs/coop/decisions/`, the issue's pointers, and `local/` for your owner's private setup.
2. **Pick and claim.** If the owner did not name an issue, propose one whose "Needs" are done and that nobody holds.
   Ask the owner before posting the claim comment (it publishes under their account).
3. **Branch.** `tools/coop/new-worktree.sh <issue> <slug>` (branch `coop/<issue>-<slug>` from `origin/main`), or a
   plain branch with the same name. One issue per branch.
4. **Understand before changing.** Read the decomp code involved. Write findings to `docs/coop/NOTES.md` as you go:
   date, issue, Observed (file:line), Derived, Guess, Unknown.
5. **Build.** Follow the code rules in `AGENTS.md`: code in `src/port/coop/`, hooks before edits, `// COOP:` on every
   upstream edit, everything gated on an active co-op session, message ids reserved in `docs/coop/PROTOCOL.md`. If
   the work needs an architecture choice that no accepted record covers, stop and write a decision record instead.
6. **Verify.** Run the check from the issue's "Done when" as a script, unattended, and read only its summary. Run
   `tools/coop/check.sh`. Record the QA level and the test identity (commit, ROM version, OS, compiler, backend).
   Never use the owner's own config, saves or running game for tests.
7. **PR.** Show the owner the diff. Then push and open a draft: `gh pr create --draft`, template filled in,
   `Closes #<issue>`, AI use disclosed.
8. **Hand off.** Comment on the issue: done, verified (how), left, next concrete step, surprises. Then tell the owner
   the session is finished and suggest starting a fresh one for the next issue.

Stop at the first contradiction between code, tests and docs, and tell the owner.
