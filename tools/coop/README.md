# Co-op tools

| Script | What it does |
|---|---|
| `check.sh` | Repository checks, also run by CI: no ROMs or asset archives tracked, no file over 5 MB, skills mirrored, unique protocol ids. Prints `ALL OK`. |
| `new-worktree.sh <issue> <slug>` | Worktree plus branch `coop/<issue>-<slug>` from `origin/main`, with submodules. |
| `sync-skills.sh` | Copies `.agents/skills` to `.claude/skills`. Edit skills in `.agents/skills` only. |
| `setup-labels.sh <owner>/<repo>` | Creates the issue labels on GitHub (maintainers, once after publishing). |

Test tooling (replays, state hashes, two-client harness) will live here too; see `docs/coop/QA.md` and the roadmap.
