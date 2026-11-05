# Co-op tools

Python 3 scripts, so they work the same on Windows (PowerShell), Linux and macOS. Python is already needed to build
Starship. Run them from the repository root.

| Script | What it does |
|---|---|
| `python tools/coop/build.py` | Configure, build and run the checks; prints `build: OK` or `build: FAILED at <step>`. `--quiet` for a summary only, `--config Release`, `--assets` (needs `baserom.z64`), `--clean`. CI runs the same script. |
| `python tools/coop/check.py` | Repository checks, also run by CI: no ROMs or asset archives tracked, no file over 5 MB, skills mirrored, unique protocol ids. Prints `ALL OK`. |
| `python tools/coop/new_worktree.py <issue> <slug>` | Worktree plus branch `issue/<issue>-<slug>` from `origin/coop`, with submodules. |
| `python tools/coop/sp_audit.py` | Single-player assumptions audit (WP-05): every `gPlayer[0]`, `gPlayer->`, `gMainController` and `gCamCount` site in the game code with its function and the verdict from `docs/coop/audit/sp-verdicts.tsv`; writes `docs/coop/audit/single-player.md`. `--check` fails when the list is stale or a site has no verdict, `--todo` lists functions without one. Rerun after an upstream sync. |
| `python tools/coop/sync_skills.py` | Copies `.agents/skills` to `.claude/skills`. Edit skills in `.agents/skills` only. |
| `python tools/coop/setup_labels.py <owner>/<repo>` | Creates the issue labels on GitHub (maintainers). |

Test tooling (replays, state hashes, two-client harness) will live here too; see `docs/coop/QA.md` and the roadmap.
