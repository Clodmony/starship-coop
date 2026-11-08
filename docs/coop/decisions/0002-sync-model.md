# 0002: Sync model: lockstep or host authority

- Status: Accepted
- Date: 2026-11-01, outcome proposed 2026-11-06, accepted by the owner 2026-11-07
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

WP-02 asks whether the simulation is deterministic given the same seeds and inputs. What follows is what was known
when the owner decided. Run details are in NOTES.md, under the issue each row names.

### Measured

Every run is one game process without transport, ROM US 1.0. "2 players" means two `Player` structs in that one
process, both fed from one replay file. Each row compares runs on one PC, of one build unless it says otherwise, drawing
player 0 unless it says otherwise.

| Build | Input | Hash | Result | Source |
|---|---|---|---|---|
| Windows, MSVC, Debug | Recording: menus, all of Corneria, map. 1 player, 13545 frames | WP-04 | `determinism.py`: 6 of 6 variants equal to the first run on every frame | #2, on `e8c4e6a2` |
| Linux, GCC, Debug | By rule: Corneria, debug jump to all-range. 1 and 2 players, 5800 frames | With #53 and #67 | `hash_check.py run`: two runs equal on every frame | #67, `14a353d2` |
| Linux, GCC, Debug | By rule: rail section into all-range, with a player going down and a restart. 2 players, 10500 frames | With #53 | Two runs equal on every frame | #10, `e3aaaa4a` |
| Linux, GCC, Debug | By rule: L to level complete. 2 players, 12007 frames | With #53 | Two runs equal on every frame both logs hold | #53, `47a90af5` |
| Linux, GCC, Debug | By rule: rails, and all-range. 2 players, 10500 and 8000 frames, drawing player 0 and player 1 | With #53 and #67 | `hash_check.py views`: the two equal on every frame, total and all 12 regions, the cameras apart | #96, `66df1cac` |
| Linux, GCC, Debug | Pilot's recording: all of Corneria from level start to the map, no debug switch. 2 players, 14384 frames | With #53 and #67 | `full_level.py`: two runs equal on every frame, replay `PASS` | #142, `dab18fc5` |
| Linux, GCC, Release and Debug | By rule: rails, and all-range. 2 players, 10500 and 8000 frames | With #53 and #67 | `hash_check.py builds`: two Release runs, and Debug against Release, equal on every frame | #141, `57db0c76` |
| Linux, GCC, Release | By rule: the same two. 30, 60, 120, 144 and 360 fps | With #53 and #67 | `hash_check.py rates`: all five equal on every frame, each with the draws its rate asks for | #141, `57db0c76` |

The six Windows variants ran at once: the defaults again, OpenGL, window size and frame pacing, two fixed audio sample
counts, and C runtime math without FMA3.

Observed, the hash:

- Two-player runs before #53 did not hash player 2. No run before #67 hashed the 13 per-player arrays, the co-op slots
  or the 6 team arrays, and a single-player log still leaves those out. So in rows 1, 3 and 4 a difference there shows
  only once it reaches something hashed. Row 2 and the rows after row 4, with 2 players, cover all of it.
- `hash_check.py probe` shows that the hash can see a value, not what the value was in these runs.
- A hash on one PC only sees a stray write that lands on hashed memory. #141's write was found because GCC at `-O2` put
  the RNG seeds under `sInterpolationMatrixStack`. In a Debug build the same write hit unused memory and no hash showed
  it, and a different compiler lays memory out differently.

Observed, the handshake:

- A different build, ROM or simulation CVar refused the connection. A different platform only warned, as the owner
  decided on #55, to be revisited when this record was accepted, before WP-22 (#20) relies on it. It refuses now, see
  "Platform rule" under Decision.
- The build is the git commit (`tools/coop/cmake/coop_build_id.cmake`), the platform is OS, CPU and compiler with its
  version (`CoopIdentity_Platform`, `src/port/coop/session/identity.c:12`). Neither names the build type, so a Debug
  and a Release build of one commit match. Read, not run. #141's runs give no reason to tell them apart: the two
  computed the same game.

Observed by running (#96, NOTES.md 2026-11-06), the camera drawn:

- Before its fix, one replay drawn with player 0 and with player 1 split one frame after Corneria starts, and the RNG
  region differed later, so the two clients no longer played the same game. Draw code frees what it does not see, holds
  game logic and feeds boss AI.
- After it, the two are equal on every frame in the rows above, and every client runs the draw code of every object
  (`src/port/coop/game/view.h`). Other levels are #129. #142's whole level with player 1 drawn, once and by hand: hash
  log equal to player 0's on all 14384 frames, the cameras apart on 79 of 112 sampled frames.

Observed by running (#141, NOTES.md 2026-11-07), a Release build:

- Two Release runs of one build differed. Frame interpolation wrote below its own matrix stack, onto the RNG seeds, and
  the clock re-seed in `Rand_ZeroOne` then made each run its own. Upstream has the bug. With the fix in
  `src/port/interpolation/FrameInterpolation.cpp`, the last two rows above hold.
- Before that fix, 30 against 60 fps differed too, so a client's refresh rate changed the game.

Derived:

- On one PC with one build, the hashed state followed from inputs and seeds in every run above, all of them in
  Corneria. The Windows variants show that start time, CPU load, graphics backend, frame pacing and audio timing did
  not change it.
- Rendering is not apart from the simulation: draw code runs game logic (#96), and a stray write in the draw path did
  reach the RNG seeds (#141). Neither parts two clients in the runs above, but the risk stays. It is a risk to A and C,
  not to B, where only the host's simulation counts.
- `gInterpolationFPS` is local and may differ between clients. It does not go into the handshake, and `hash_check.py
  rates` is what shows that it may: all five rates computed the same game on Linux Release, in Corneria.
- The clock re-seed in `Rand_ZeroOne` no longer fires in these runs: a non-zero seed never reaches 0, because the moduli
  are prime (`src/sys/sys_math.c:24-31`, #53). It is still a clock read inside the simulation on Linux, and it fires if a
  seed starts at 0 or something writes one. #141 found what wrote one on GCC builds, the write above, which is what
  upstream's comment means by something corrupting the seeds, at least there. Whether anything else does is unknown.
- Clients with a different past would differ from their first co-op frame: no level start clears `gTeamDamage` or
  `gTeamEventActorIndex` (#67). The host's level start now carries them, and the seeds
  (`src/port/coop/game/level_start.c`, #22).

Guess:

- A second PC with the same binary gives the same hashes: it changes the CPU and the drivers, and the no-FMA3 and
  backend variants changed nothing. No second PC was involved.
- Windows and Linux differ somewhere: each uses its own C runtime's `sinf` and `cosf` (`__sinf`, `__cosf`,
  `src/port/Engine.cpp:676-683`).
- A difference found in another level would be a bug to fix, not a reason to leave lockstep.

Unknown, because it was not run:

- **Two machines.** No run on two machines with the same build. That half of #2 stays open, and it is a named condition
  below.
- **Two clients.** Two and three game clients have started one level together and stayed equal with no input after it
  (`join_test.py`, #22). None has played together: no sync message exists yet (the range 0x0040-0x007F in PROTOCOL.md is
  empty). So the input delay at real latency is unknown too.
- **Other levels.** Only Corneria: no other all-range level, no Landmaster or Blue Marine, no space level, no Andross.
- **Other builds.** No Windows Release run: `hash_check.py builds` and `rates` have run on Linux only (#145), and MSVC
  lays memory out its own way, so a stray write there can land somewhere else. A named condition below. One compiler
  per OS: MSVC on Windows, GCC on Linux. `determinism.py` has a recorded result on Windows only.
- **Windows against Linux.** Never compared: the runs above used different input. Whether two hash logs can be compared
  at all is open too, because the hash runs over raw bytes, padding included
  (`src/port/coop/sync/state_hash.c:3`, `:16`).
- **Stray writes nothing hashes.** Whether another one exists: a hash cannot see it, on one PC or on two. #145 lists the
  stray reads and writes #141 came across.

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

## Decision

**A, deterministic lockstep, for v1.** The owner accepted this on 2026-11-07, on #2.

- Every client runs the whole simulation. Only inputs travel, plus a state hash per frame to detect desyncs.
- A session needs the same build, ROM, simulation CVars and platform, and the handshake enforces all four.
- C stays the fallback for rare desyncs, as the first draft said. It is not part of WP-22: it needs a snapshot format
  and fragmentation (decision 0004), and no desync has been seen that calls for it.

Why A: every run so far points that way, and nothing measured calls for B. B's cost is known and large: a snapshot
that holds across builds or code per actor for 406 object ids, plus client-side prediction. A's open risks are named
above, and each has a test that can show it.

### What it rests on

This decision rests on one-PC evidence. All three points the proposal asked for are met on Linux with GCC, in Corneria:

1. **#96 left A standing.** One replay played with local index 0 and with index 1 is equal on every frame, the writes
   have their verdicts, and DESIGN.md section 2 says what was found. Other levels are #129.
2. **A full level on the current hash.** Two runs of one build through all of Corneria with 2 players, no debug switch,
   equal on every frame: `tools/coop/full_level.py` (#142, PR #143).
3. **A Release build.** Two Release runs, and Debug against Release on the same commit, equal on every frame:
   `hash_check.py builds`, and `rates` for the frame rate (#141, PR #144). They differed at first, and the cause was
   found and removed. The two computed the same game, so the build type stays out of the identity.

### Conditions before a first public release

1. **The two-machine run:** the same build on two machines, equal on every frame. #2 stays open for it.
2. **Windows Release:** `hash_check.py builds` and `rates` run there (#145). The fix of #141 removes that mechanism on
   Windows too, but nobody has run it, and a different compiler lays memory out differently.

A difference that cannot be found and removed reopens the choice through a new record. WP-22 does not wait for these:
it needs the one-PC half of WP-02.

Not conditions, because they limit A rather than rule it out: other levels, which no test has reached yet, a second
compiler on one OS, Windows against Linux, and real latency. Each stays a stated gap until its work package runs it.

### Platform rule

Platform rule superseded by [0010](0010-cross-platform-sessions.md).

A needs every client to compute the same game, and no run has compared two platforms. So the same platform is a
requirement of v1, and cross-platform play stays under "Later" in ROADMAP.md.

A platform difference refuses the connection, like a different build, unless a comparison has shown by then that the
two platforms compute the same game. This replaces the warning the owner decided on #55: under A a warning lets two
players start a session that may desync with nothing they can do about it. The platform is OS, CPU and compiler with its
version, compared as text.

A test switch may pass the refusal, for a real cross-platform test: `SF64_COOP_TEST_ANY_PLATFORM` on the host
(`src/port/coop/test/switches.h`). A difference in anything else still refuses.

PROTOCOL.md gave one reason for the old warning: the first two-PC tests run Windows against Linux. The refusal does not
block that comparison, which needs two hash logs and no session: input by rule is the same file on both
(`write_generated_replay`, `tools/coop/hash_check.py`; not tried across OSes). Only a real session across platforms needs
the switch.

### What would change this decision

- A level beyond Corneria diverges by camera, and the writes cannot leave the draw path without a large change to game
  code (#129): A with one camera per client is in doubt, and B has to be weighed again.
- Two runs of one build differ, on one PC or on two machines, and the cause cannot be found and removed: **B**, as the
  first draft said.
- Windows and Linux differ: nothing changes, that is what the same-platform term is for.
- Input delay is not playable at real latency: rollback, under "Later" in ROADMAP.md, comes before B.

### How the rule changed

The first draft of this record set a stricter rule, kept here so the change stays visible: "If two runs with the same
seeds and inputs stay identical over a full level on one machine and between two machines with the same build: choose
**A** for v1, same build and same platform required, with C as the fallback for rare desyncs. Otherwise choose **B**."

The proposal of 2026-11-06 chose A provided #96 left it standing. It named the three one-PC points above as what would
make the record acceptable, and took the two-machine run off that list, as a change from the first draft for the owner
to call: accept on the three points and keep the run as a named condition, or keep the first draft's rule, with the
record Proposed until a two-machine run and WP-22 only prototyping against it.

The owner took the first route on 2026-11-07, added Windows Release as a second named condition after #141, and decided
the platform rule, which the proposal had left open.

## Consequences

Easier:

- No sync code per object type: the 406 object ids stay out of the sync layer.
- A replay file and the network stay interchangeable input sources, so engine work stays testable in one process.
- No message needs more than one datagram, so the limit in decision 0004 stands.

Harder:

- Every client has to compute the same game bit for bit. One missed write is a desync of the session, not a glitch on
  one actor. Nothing the simulation touches may read a clock, use unsynced randomness or depend on the local player.
- Each frame waits for every player's input, so one late message stalls everyone (decision 0004).
- All players need the same build on the same platform, so they update together and cross-platform play waits. Two
  players who build one commit themselves need the same compiler version too.
- Until C exists, a desync has no repair beyond its report. What a session does after one is open (WP-23).
- Late join and reconnect need a state transfer, which stays under "Later" in ROADMAP.md.

Next:

- WP-22 (#20): input exchange, frame-stamped, ids from 0x0040-0x007F.
- WP-23 (#21): compare hashes between clients. Until then every gap under "Unknown" stays silent in play.
- The two conditions above, before a first public release: #2 and #145.
- `Rand_ZeroOne`: the re-seed fixed or disabled in co-op, marked `// COOP:` (`src/sys/sys_math.c:24-28`). It tests
  `sRandSeed2` twice and never `sRandSeed3`, so a seed 3 of 0 is not caught (#145).
- The CVar table from WP-21: nothing has tested it against state hashes yet (DESIGN.md section 2).

These changed with this record: `decisions/README.md`, DESIGN.md sections 2 and 5, ROADMAP.md, where WP-22 needs only
the one-PC half of WP-02, PROTOCOL.md rule 2, `COOP_ID_DIFF_PLATFORM` and `COOP_ID_DIFF_REFUSE` with the comment in
`src/port/coop/session/identity.h`, and the `platform` scenario in `tools/coop/session_test.py`.
