---
name: starship-coop-dev
description: Build, run and change Starship Co-op (the Star Fox 64 PC port with a co-op layer) - build commands, where co-op code goes, how to reach game code through hooks, how to run test instances without touching the owner's setup. Use when building, running or editing code in this repo.
---

# Developing Starship Co-op

## Build

One script, the same on Linux, macOS and Windows, and the same one CI runs:

```sh
python tools/coop/build.py --quiet           # submodules if missing, configure, build, repo checks; prints a summary
python tools/coop/build.py --assets --quiet  # also sf64.o2r from baserom.z64 (the owner's ROM) and starship.o2r
```

Run it unattended and read only the summary line (`build: OK ...` or `build: FAILED at <step>` with the log tail).
The full log is `<build dir>/coop-build.log`. Build dirs: `build-cmake` (Ninja) on Linux/macOS, `build/x64` (Visual
Studio 2022) on Windows. Upstream's manual steps are in `docs/BUILDING.md`.

- Build locally before every push; CI repeats the build on Linux and Windows for PRs that touch code.
- Ask the owner before installing build dependencies. Linux packages are listed in `.github/workflows/coop-build.yml`.
- The ROM (`baserom.z64`) and the generated `sf64.o2r` / `starship.o2r` never go into git (`.gitignore` covers them;
  `python tools/coop/check.py` fails if one is tracked).
- Networking code needs `USE_NETWORKING` (SDL2_net). `CMakeLists.txt` hard-sets it with `set(USE_NETWORKING OFF)`, so a
  `-D` on the command line does not turn it on; changing that is part of WP-20.
- Several worktrees can build at once; each has its own `build-cmake/`. Builds are heavy; avoid running more than the
  machine has memory for.

## Where code goes

- `src/port/coop/{session,net,sync,game,ui,test}/` per `docs/coop/DESIGN.md` section 3. `CMakeLists.txt` collects
  `src/port/*.c`, `*.cpp` and `*.h` with `GLOB_RECURSE ALL_FILES`, so new files there are built after you re-run the
  cmake configure step.
- Game code access: prefer events (`src/port/hooks/list/*.h`, `DEFINE_EVENT`; fire with `CALL_EVENT`; listen with
  `REGISTER_LISTENER(Event, callback, EVENT_PRIORITY_NORMAL)` as `src/port/mods/PortEnhancements.c` does). Add a new
  event when a hook is missing.
- Upstream file edits: minimal, marked `// COOP:`, gated on an active co-op session.
- Useful globals (see DESIGN.md section 2): `gPlayer`, `gCamCount`, `gPlayerNum`, `gMainController`,
  `gControllerPress[]`, `gTeamShields[]`, the object arrays in `include/sf64context.h`, `Rand_ZeroOne`.

## Running test instances

- Each instance gets its own folder under `local/test-<name>/` with its own `starship.cfg.json`, saves and logs.
  Never run tests against the owner's own config, saves or `mods/` folder, and never kill a Starship process you did
  not start.
- Starship reads config and saves relative to where it runs; confirm where a given build writes them before the first
  test run (observe, then note it in `docs/coop/NOTES.md`).
- Port debug mods live in `src/mods/` (level select, spawner, boss killer); they help reach a test state quickly.
