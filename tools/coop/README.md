# Co-op tools

Python 3 scripts, so they work the same on Windows (PowerShell), Linux and macOS. Python is already needed to build
Starship. Run them from the repository root.

| Script | What it does |
|---|---|
| `python tools/coop/build.py` | Configure, build and run the checks; prints `build: OK` or `build: FAILED at <step>`. `--quiet` for a summary only, `--config Release`, `--assets` (needs `baserom.z64`), `--clean`. CI runs the same script. |
| `python tools/coop/net_test.py` | Transport tests: a virtual network, then two real processes through the chaos relay. `--only a,b`, `--list`, `--no-build`. Needs a configured build directory, no ROM. Logs in `local/test-net/`. |
| `python tools/coop/session_test.py` | Session handshake tests (WP-21): build, ROM, CVar and platform mismatches in a virtual network, then real processes over UDP and the chaos relay. `--only a,b`, `--list`, `--no-build`, `--asan` (the same under AddressSanitizer and UBSan, not on Windows; CI runs it on Linux). No ROM needed. Logs in `local/test-session/`. |
| `python tools/coop/player_trace.py run <replay> --players 2` | Plays a replay with several co-op players in a fresh `local/test-trace-*` folder and checks the per-frame player trace (parked on rails, joins in all-range, moves). `check <trace.txt>` checks an existing trace. Needs the replay code from WP-03 in the build. |
| `python tools/coop/hash_check.py run\|probe\|diff` | State hash checks (WP-04, #53). `run [<replay>]`: two runs must match and a one-bit poke must show at exactly its frame; `--players 2 --poke-player 1` for co-op. `probe`: one game checks what the hash can and cannot see, player by player. Without a replay they play input by rule. See `docs/coop/QA.md`. |
| `python tools/coop/replay_test.py` | Tests the replay tools themselves (#54): records a short replay nobody plays, then plays it whole and damaged through `replay.py`, `hash_check.py` and `determinism.py`. Only the whole file may pass, and a decoy `SHIP_HOME` must stay untouched. `--only a,b`, `--list`. About 5 minutes, needs a build with `--assets`. Logs in `local/test-replaytools-*`, kept when a scenario fails. |
| `python tools/coop/exit_test.py` | Tests that a test run stops soon after SIGTERM, SIGINT or SIGQUIT (#71): without a replay, recording and playing. Each game must end within 10 s with exit code 0, and the report, the replay and the hash log must keep their end. `plain` shows that a game with no test variable keeps upstream's handler. `--only a,b`, `--repeat n` (default 3), `--exe`, `--list`. Linux only, about 7 s per game, needs a build with `--assets`. Logs in `local/test-*`, kept when a run fails. |
| `python tools/coop/net_mutants.py` | Mutation check for the transport: breaks one behaviour of `net_core.c` at a time, rebuilds, and requires the named `net_test.py` scenarios to fail. Prints caught or SURVIVED per break; exit 0 only when all are caught. `--only a,b`, `--list`, `--check` (patterns only, no build). About 10 minutes. Run it after changing `net_core.c`, and add a break for every bug you fix there. |
| `python tools/coop/session_mutants.py` | The same for the session: breaks one behaviour of `session.c` at a time and requires the named `session_test.py` scenarios to fail. Same options as `net_mutants.py`, plus `--asan` for the breaks only the sanitizers can see. The top of the file lists the changes no test can see, with the reason. Under a minute. Run it after changing `session.c`, and add a break for every bug you fix there. Both tools run on `mutants.py`, which also holds the rules for how a run guards the source. |
| `python tools/coop/check.py` | Repository checks, also run by CI: no ROMs or asset archives tracked, no file over 5 MB, skills mirrored, unique protocol ids, every break in `net_mutants.py` and `session_mutants.py` still matches. Prints `ALL OK`. |
| `python tools/coop/new_worktree.py <issue> <slug>` | Worktree plus branch `issue/<issue>-<slug>` from `origin/coop`, with submodules. |
| `python tools/coop/sp_audit.py` | Single-player assumptions audit (WP-05): every `gPlayer[0]`, `gPlayer->`, `gMainController` and `gCamCount` site in the game code with its function and the verdict from `docs/coop/audit/sp-verdicts.tsv`; writes `docs/coop/audit/single-player.md`. `--check` fails when the list is stale or a site has no verdict, `--todo` lists functions without one. Rerun after an upstream sync and read the diff: a new site under an existing row takes that verdict without a warning. |
| `python tools/coop/sync_skills.py` | Copies `.agents/skills` to `.claude/skills`. Edit skills in `.agents/skills` only. |
| `python tools/coop/setup_labels.py <owner>/<repo>` | Creates the issue labels on GitHub (maintainers). |

`chaos/` is the hub's chaos relay, see its README. `nettest/` is the test program `net_test.py` drives, `sessiontest/` the one `session_test.py` drives.
`cmake/coop_build_id.cmake` writes the git commit into the game for the handshake.

More test tooling (replays, state hashes, two-client harness) will live here too; see `docs/coop/QA.md` and the
roadmap.
