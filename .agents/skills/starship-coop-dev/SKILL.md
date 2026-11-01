---
name: starship-coop-dev
description: Build, run and change Starship Co-op (the Star Fox 64 PC port with a co-op layer) - build commands, where co-op code goes, how to reach game code through hooks, how to run test instances without touching the owner's setup. Use when building, running or editing code in this repo.
---

# Developing Starship Co-op

## Build

Same as upstream; full steps in `docs/BUILDING.md`.

```sh
git submodule update --init --recursive
cmake -H. -Bbuild-cmake -GNinja -DCMAKE_BUILD_TYPE=Debug   # Linux/macOS; Windows: see docs/BUILDING.md
cmake --build build-cmake --target ExtractAssets            # needs the owner's US 1.0 or 1.1 ROM (path in local/)
cmake --build build-cmake --target GeneratePortO2R
cmake --build build-cmake -j
```

- Ask the owner before installing build dependencies. Linux packages are listed in `.github/workflows/linux.yml`.
- The ROM and the generated `sf64.o2r` / `starship.o2r` never go into git (`.gitignore` covers them; `tools/coop/check.sh`
  fails if one is tracked).
- Networking code needs `USE_NETWORKING` (SDL2_net), which `CMakeLists.txt` sets `OFF` today. Turning it on for co-op
  builds is part of WP-20.
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
