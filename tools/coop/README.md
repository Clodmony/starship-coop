# Co-op tools

Python 3 scripts, so they work the same on Windows (PowerShell), Linux and macOS. Python is already needed to build
Starship. Run them from the repository root.

| Script | What it does |
|---|---|
| `python tools/coop/build.py` | Configure, build and run the checks; prints `build: OK` or `build: FAILED at <step>`. `--quiet` for a summary only, `--config Release`, `--assets` (needs `baserom.z64`), `--clean`. CI runs the same script. |
| `python tools/coop/net_test.py` | Transport tests: a virtual network, then two real processes through the chaos relay. `--only a,b`, `--list`, `--no-build`. Needs a configured build directory, no ROM. Logs in `local/test-net/`. |
| `python tools/coop/net_mutants.py` | Mutation check for the transport: breaks one behaviour of `net_core.c` at a time, rebuilds, and requires the named `net_test.py` scenarios to fail. Prints caught or SURVIVED per break; exit 0 only when all are caught. `--only a,b`, `--list`, `--check` (patterns only, no build). About 10 minutes. Run it after changing `net_core.c`, and add a break for every bug you fix there. |
| `python tools/coop/check.py` | Repository checks, also run by CI: no ROMs or asset archives tracked, no file over 5 MB, skills mirrored, unique protocol ids, every break in `net_mutants.py` still matches. Prints `ALL OK`. |
| `python tools/coop/new_worktree.py <issue> <slug>` | Worktree plus branch `issue/<issue>-<slug>` from `origin/coop`, with submodules. |
| `python tools/coop/sync_skills.py` | Copies `.agents/skills` to `.claude/skills`. Edit skills in `.agents/skills` only. |
| `python tools/coop/setup_labels.py <owner>/<repo>` | Creates the issue labels on GitHub (maintainers). |

`chaos/` is the hub's chaos relay, see its README. `nettest/` is the test program `net_test.py` drives.

More test tooling (replays, state hashes, two-client harness) will live here too; see `docs/coop/QA.md` and the
roadmap.
