# 0002: Sync model: lockstep or host authority

- Status: Proposed
- Date: 2026-11-01, outcome proposed 2026-11-06
- Issue: #2 (WP-02, determinism spike)

## Context

Observed (see DESIGN.md section 2 for sources):

- The world is a set of fixed object arrays (`gActors[60]`, `gBosses[4]`, `gEffects[100]`, ...), one bump-allocated
  level buffer and globals.
- The game has 406 object ids (`OBJ_ID_MAX` in `include/sf64object.h`), with behaviour spread over `src/engine/`
  and the level overlays in `src/overlays/`. Syncing each type by hand, the way Ocarina of Time co-op forks sync
  enemies one type at a time, would be a very long job.
- Randomness comes from `Rand_ZeroOne`, seeded from the clock at boot, with a Linux/Switch-only re-seed from the clock
  when a seed hits 0.

WP-02 asks whether the simulation is deterministic given the same seeds and inputs. What follows is what is known so
far. Run details are in NOTES.md, under the issue each row names.

### Measured

Every run is one game process without transport, ROM US 1.0, drawing player 0. "2 players" means two `Player` structs
in that one process, both fed from one replay file. Each row compares runs of one build on one PC.

| Build | Input | Hash | Result | Source |
|---|---|---|---|---|
| Windows, MSVC, Debug | Recording: menus, all of Corneria, map. 1 player, 13545 frames | WP-04 | `determinism.py`: 6 of 6 variants equal to the first run on every frame | #2, on `e8c4e6a2` |
| Linux, GCC, Debug | By rule: Corneria, debug jump to all-range. 1 and 2 players, 5800 frames | With #53 and #67 | `hash_check.py run`: two runs equal on every frame | #67, `14a353d2` |
| Linux, GCC, Debug | By rule: rail section into all-range, with a player going down and a restart. 2 players, 10500 frames | With #53 | Two runs equal on every frame | #10, `e3aaaa4a` |
| Linux, GCC, Debug | By rule: L to level complete. 2 players, 12007 frames | With #53 | Two runs equal on every frame both logs hold | #53, `47a90af5` |

The six Windows variants ran at once: the defaults again, OpenGL, window size and frame pacing, two fixed audio sample
counts, and C runtime math without FMA3.

Observed, the hash:

- Two-player runs before #53 did not hash player 2. No run before #67 hashed the 13 per-player arrays, the co-op slots
  or the 6 team arrays, and a single-player log still leaves those out. So in rows 1, 3 and 4 a difference there shows
  only once it reaches something hashed. Only row 2, with 2 players, covers all of it.
- `hash_check.py probe` shows that the hash can see a value, not what the value was in these runs.

Observed, the handshake:

- A different build, ROM or simulation CVar refuses the connection. A different platform only warns
  (`CoopIdentity_Compare`, `src/port/coop/session/identity.c:78`, PROTOCOL.md rule 2). The owner's note on #55 keeps it
  that way for now: revisit when this record is accepted, before WP-22 (#20) relies on it.
- The build is the git commit (`tools/coop/cmake/coop_build_id.cmake`), the platform is OS, CPU and compiler with its
  version (`CoopIdentity_Platform`, `identity.c:12`). Neither names the build type, so a Debug and a Release build of
  one commit match. Read, not run.

Observed by reading, not run (#96, open and being worked on, so its result may change this record):

- Draw code writes state that is hashed, and some of it the simulation reads: `iwork[24]`
  (`src/engine/fox_edisplay.c:1283`, read by the all-range AI at `src/engine/fox_360.c:1233`),
  `lockOnTimers[gPlayerNum]` (`src/engine/fox_edisplay.c:1266`), `sfxSource`, and `gRadioState` (`Radio_Draw`, called at
  `src/engine/fox_game.c:569`, read by AI code at `src/engine/fox_360.c:1705`).
- Per-player arrays are written by `gPlayerNum` outside the player loops, where it is the local player
  (`src/engine/fox_hud.c:1089`, `:3456-3457`, `src/engine/fox_play.c:4932-4935`, set at `:7052` and `:7069`).
- Every client draws player 0 today, because `CoopPlayers_LocalIndex` returns 0 (`src/port/coop/game/players.c:59-61`).
  The design needs one camera per client (DESIGN.md section 3, WP-28).

Derived:

- On one PC with one build, the hashed state followed from inputs and seeds in every run above, all of them in
  Corneria. The Windows variants show that start time, CPU load, graphics backend, frame pacing and audio timing did
  not change it.
- **No run above covers the setup co-op needs.** All of them draw player 0. With one camera per client the hash will
  differ at the least, and where a value reaches the AI the simulation itself can diverge (#96). That goes against the
  Derived point in DESIGN.md section 2 that logic ticks apart from rendering. It is a risk to A and C, not to B, where
  only the host's simulation counts.
- The clock re-seed in `Rand_ZeroOne` did not get in the way: a non-zero seed never reaches 0, because the moduli are
  prime (`src/sys/sys_math.c:24-31`, #53). It is still a clock read inside the simulation on Linux, and it fires if a
  seed starts at 0 or something writes one. Upstream's comment says something does; nobody has looked into what.
- Clients with a different past differ from their first co-op frame: no level start clears `gTeamDamage` or
  `gTeamEventActorIndex` (#67).

Guess:

- A second PC with the same binary gives the same hashes: it changes the CPU and the drivers, and the no-FMA3 and
  backend variants changed nothing. No second PC was involved.
- Windows and Linux differ somewhere: each uses its own C runtime's `sinf` and `cosf` (`__sinf`, `__cosf`,
  `src/port/Engine.cpp:668-674`).
- A difference found in another level would be a bug to fix, not a reason to leave lockstep.

Unknown, because it was not run:

- **Two machines.** No run on two machines with the same build. That half of #2 stays open.
- **Two clients.** No two game clients have shared state, and no sync message exists yet (the range 0x0040-0x007F in
  PROTOCOL.md is empty). So the input delay at real latency is unknown too.
- **Other levels.** Only Corneria: no other all-range level, no Landmaster or Blue Marine, no space level, no Andross.
  No Linux row above is a whole level played to its end without a debug switch.
- **Other builds.** Debug only, on both OSes: no Release build has a recorded game run. One compiler per OS: MSVC on
  Windows, GCC on Linux. `determinism.py` has a recorded result on Windows only.
- **Windows against Linux.** Never compared: the runs above used different input. Whether two hash logs can be compared
  at all is open too, because the hash runs over raw bytes, padding included
  (`src/port/coop/sync/state_hash.c:3`, `:16`).
- **Whether #96 is a real divergence,** how soon it shows, and how many more draw-path writes exist. Its list comes from
  a sweep of per-player state, not of draw code.

## Options

### A. Deterministic lockstep (exchange inputs only)

Every client runs the full simulation of all players; only controller inputs travel, plus a state hash per frame to
detect desyncs. This is how emulator netplay of local multiplayer games works.

- Every enemy, boss, projectile and item is in sync for free, with no per-actor code.
- Bandwidth is tiny. A replay file and the network are interchangeable input sources, so the engine work is testable
  in one process before the netcode exists, and local split screen later needs no sync code at all.
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

Choose **A**, deterministic lockstep, for v1, provided #96 leaves it standing:

- Every client runs the whole simulation. Only inputs travel, plus a state hash per frame to detect desyncs.
- A session needs the same build, ROM and simulation CVars, which the handshake already enforces, and the same
  platform, which it does not enforce yet (see "Platform rule").
- C stays the fallback for rare desyncs, as the first draft said. It is not part of WP-22: it needs a snapshot format
  and fragmentation (decision 0004), and no desync has been seen that calls for it.

Why A: every run so far points that way, and nothing measured calls for B. B's cost is known and large: a snapshot
that holds across builds or code per actor for 406 object ids, plus client-side prediction. A's open risks are named
above, and each has a test that can show it.

This proposal rests on one-PC evidence. The first draft of this record set a stricter rule, kept here so the change
stays visible: "If two runs with the same seeds and inputs stay identical over a full level on one machine and between
two machines with the same build: choose **A** for v1, same build and same platform required, with C as the fallback
for rare desyncs. Otherwise choose **B**." The one-machine half is met on Windows, in Corneria, with the hash of that
time. The two-machine half is not met.

### Platform rule

A needs every client to compute the same game, and no run has compared two platforms. So the same platform is a
requirement of v1, and cross-platform play stays under "Later" in ROADMAP.md. While this record is Proposed nothing
changes: a platform difference keeps only warning, as the owner decided on #55.

Proposed for acceptance, and the owner's to decide: a platform difference refuses the connection, like a different
build, unless a comparison has shown by then that the two platforms compute the same game. Under A a warning lets two
players start a session that may desync with nothing they can do about it.

PROTOCOL.md gives one reason for the warning: the first two-PC tests run Windows against Linux. A refusal would not
block that comparison, which needs two hash logs and no session: input by rule is the same file on both
(`write_generated_replay`, `tools/coop/hash_check.py:127`; not tried across OSes). Only a real session across platforms
would need a test switch past the refusal.

### What would make this record acceptable

All of this runs on one PC, which fits the owner's preference at this stage:

1. **#96 has a result that leaves A standing.** Its test plays one replay with local index 0 and with index 1, and any
   difference in the hash logs has a written reason. Every write has its verdict, and DESIGN.md section 2 says what
   was found.
2. **A full level on the current hash.** Two runs of one build through all of Corneria with 2 players, no debug switch,
   equal on every frame. The Windows run is older than #53 and #67, and the Linux runs stop short of that.
3. **A Release build.** Two Release runs equal on every frame, and one comparison of Debug against Release on the same
   commit. CI builds Release (`.github/workflows/coop-build.yml:89`), and so does a package (`docs/BUILDING.md:33`). If
   the two differ, the build type goes into the identity, so that the handshake tells them apart.

The **two-machine run** is not on that list. That is a change from the first draft, so it is the owner's call:

- Recommended: accept on the three points above and keep the two-machine run as a named condition of A. #2 stays open
  for it, it has to pass before a first public release, and a difference that cannot be found and removed reopens the
  choice through a new record. WP-22 can then start once its other needs are met.
- Otherwise: the first draft's rule stands, this record stays Proposed until a two-machine run, and WP-22 may only
  prototype against it (`decisions/README.md`).

Not needed to accept, because they limit A rather than rule it out: other levels, which no test has reached yet, a
second compiler, Windows against Linux, and real latency. Each stays a stated gap until its work package runs it.

### What would change this proposal

- #96 shows that the simulation diverges by camera, and the writes cannot leave the draw path without a large change
  to game code: A with one camera per client is in doubt, and B has to be weighed again.
- Two runs of one build differ, on one PC or on two machines, and the cause cannot be found and removed: **B**, as the
  first draft said.
- Windows and Linux differ: nothing changes, that is what the same-platform term is for.
- Input delay is not playable at real latency: rollback, under "Later" in ROADMAP.md, comes before B.

## Consequences

If accepted as proposed.

Easier:

- No sync code per object type: the 406 object ids stay out of the sync layer.
- A replay file and the network stay interchangeable input sources, so engine work stays testable in one process.
- No message needs more than one datagram, so the limit in decision 0004 stands.

Harder:

- Every client has to compute the same game bit for bit. One missed write is a desync of the session, not a glitch on
  one actor. Nothing the simulation touches may read a clock, use unsynced randomness or depend on the local player.
- Each frame waits for every player's input, so one late message stalls everyone (decision 0004).
- All players need the same build on the same platform, so they update together and cross-platform play waits.
- Until C exists, a desync has no repair beyond its report. What a session does after one is open (WP-23).
- Late join and reconnect need a state transfer, which stays under "Later" in ROADMAP.md.

Next:

- #96 before WP-28 (#30) changes `CoopPlayers_LocalIndex`.
- WP-22 (#20): input exchange, frame-stamped, ids from 0x0040-0x007F.
- WP-23 (#21): compare hashes between clients. Until then every gap under "Unknown" stays silent in play.
- WP-24 (#22): a session starts after boot (#3), the host's seeds reach every client, none of them 0, and every client
  starts from the same values, including those no level start clears (#67).
- `Rand_ZeroOne`: the re-seed fixed or disabled in co-op, marked `// COOP:` (`src/sys/sys_math.c:24-28`).
- The CVar table from WP-21: nothing has tested it against state hashes yet (DESIGN.md section 2).

On acceptance these change with the record: `decisions/README.md`, DESIGN.md sections 2 and 5, and ROADMAP.md, where
WP-22 needs WP-02 (only its one-PC half, on the recommended route). If the platform rule changes: PROTOCOL.md rule 2,
`COOP_ID_DIFF_PLATFORM` and `COOP_ID_DIFF_REFUSE` with the comment at `src/port/coop/session/identity.h:4-5`, and the
`platform` scenario in `tools/coop/session_test.py:191`.
