# 0002: Sync model: lockstep or host authority

- Status: Proposed
- Date: 2026-11-01
- Issue: WP-02 (determinism spike)

## Context

Observed (see DESIGN.md section 2 for sources):

- The world is a set of fixed object arrays (`gActors[60]`, `gBosses[4]`, `gEffects[100]`, ...), one bump-allocated
  level buffer and globals.
- The game has 406 object ids (`OBJ_ID_MAX` in `include/sf64object.h`), with behaviour spread over `src/engine/`
  and the level overlays in `src/overlays/`. Syncing each type by hand, the way Ocarina of Time co-op forks sync
  enemies one type at a time, would be a very long job.
- Randomness comes from `Rand_ZeroOne`, seeded from the clock at boot, with a Linux/Switch-only re-seed from the clock
  when a seed hits 0.

Unknown: whether the simulation is deterministic given the same seeds and inputs. That is what WP-02 measures.

## Options

### A. Deterministic lockstep (exchange inputs only)

Every client runs the full simulation of all players; only controller inputs travel, plus a state hash per frame to
detect desyncs. This is how emulator netplay of local multiplayer games works.

- Every enemy, boss, projectile and item is in sync for free, with no per-actor code.
- Bandwidth is tiny. Local co-op (M1) and online co-op run the same code path.
- Needs determinism: synced RNG seeds, no clock reads in the simulation, the Linux re-seed workaround fixed or
  disabled in co-op, the same build on every client, simulation-affecting CVars identical. Floating point may differ
  between compilers and CPUs, so cross-platform play may not hold.
- Input delay: each frame waits for every player's input. With high latency this needs rollback (save and restore
  state each frame), which the fixed arrays make plausible but not free.

### B. Host authority with state snapshots

The host simulates the world; clients send their inputs or their own player state, receive snapshots, and draw the
rest as puppets.

- Tolerates non-determinism and platform differences.
- Needs a snapshot of the object arrays that is portable across builds (the structs hold pointers, which differ), or
  per-actor sync code. Client-side prediction for the local player is needed to feel responsive.
- Much more code per game system; each new system is a new desync source.

### C. Lockstep with host correction

Lockstep as in A, plus the host sending a full snapshot when hashes diverge. Same-build only.

## Proposed decision

Run WP-02 first. If two runs with the same seeds and inputs stay identical over a full level on one machine and
between two machines with the same build: choose **A** for v1, same build and same platform required, with C as the
fallback for rare desyncs. Otherwise choose **B**.

Accept this record (and fill in the decision) when WP-02 has a result.

## Consequences

To be written when accepted.
