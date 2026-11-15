# 0016: The session resumes after a short drop and repairs a desync in place

- Status: Proposed (written 2026-11-15 after the owner asked for both; nothing is built; the owner's answers are at the end and he reads
  the text in the pull request that brings it)
- Date: 2026-11-15
- Issues: #267 (options 2 and 3 of decision 0014), #189 (the report a repair must still write)
- Changes: the rejoin window of 0014 item 3 (15 s to 30 s); nothing in an accepted record. Adds to decision 0014, which says "options 2 and 3 are not part of this
  record; they stay for later", and builds the fallback that decision 0002 names as option C ("lockstep with host
  correction"). The level start of 0014 stays as the last resort for both.

## Context

Decision 0014 made a lost player or a desync cost the level, not the session: the level starts again for everyone from
the host's state. The owner's answer, 2026-11-15: that is still a lot to lose, the game is not big, and a short stop
that syncs everything again is better than a restart. Two cases, two mechanisms; they share one rule: whatever cannot be
fixed on the spot falls back to 0014's level start, so nothing here makes a session end that does not end today.

### Observed (read in the code at `07d9207b`; nothing here was run, the measurement reads reports kept from earlier runs)

Drops:

- A link ends after 5 s without a datagram (`DEFAULT_TIMEOUT_MS`, `src/port/coop/net/net_core.c:20`). A client whose
  link ends in a level reconnects by itself with the session id and its character (`Menu_Rejoin`,
  `src/port/coop/ui/menu.c:74`); the host keeps its slot for `COOP_SESSION_REJOIN_MS`, 15 s
  (`src/port/coop/session/session.h:30`, `Host_Left` `session.c:718`).
- Today the host sends `SESSION_RECOVER` to everyone at the first loss and, once nobody is waited for, a level start with
  the next run (`Host_Restart`, `session.c:746`). That restart is what this record removes for a client whose game
  went on.
- A frame runs only when every present player's input for it is in (`CoopLockstep_Waiting`,
  `src/port/coop/sync/lockstep.c:65`). While one player is away every game stops at the same frame; none runs on. The
  64-frame input ring (`COOP_LOCKSTEP_RING`, `lockstep.h:27`) is therefore not the limit of a resume, as 0014 and #267
  say: it holds what a peer can still lack, which is at most the input delay (up to 16 frames) and a block.
- The host's lockstep asks the session to drop a player whose input has been missing for 10 s
  (`COOP_LOCKSTEP_STALL_MS`, `Sync_Stalled`, `lockstep_game.c:167`), a client waits 40 s (`SYNC_CLIENT_STALL_MS`,
  `:44`). The first is shorter than the 15 s of the rejoin window.
- A host's per-link record of what a peer already has only ever grows (`if (confirmed > from->next[player])`,
  `lockstep.c:234`), so a new connection to the same player must reset it.
- The desync check sends its hashes on the reliable lane (`desync_game.c:77`) and is built to lose some: "a late or lost
  hash costs a frame unchecked, never a frame held" (`desync.h:10-11`).

Desync:

- The hash and the dump run over one set of bytes: 397 to 417 chunks and about 100,000 to 104,000 bytes at the commits
  of the kept reports, in 11 regions (`players`, `actors`, `bosses`, `effects`, `items`, `scenery`, `sprites`,
  `scenery360`, `shots`, `misc`, `rng`, `globals`; NOTES.md 2026-11 desync report entries, `state_hash.h`). The dump
  is the structs themselves, copied and then cleared of what differs between games with nothing wrong
  (`state_hash.c:243` `ClearInfoPointers`; `:306` `sfx.srcPos`/`srcVel`; `:406` `Effect.unk_74`; the sound and view
  state of `ClearViewState`), with each `Actor.iwork` word normalised (`:368`; a pointer into a hashed array becomes
  array and offset, any other pointer `FOREIGN_POINTER`, `:66`).
- Live pointers in the hashed structs: `ObjectInfo` `draw`, `action` and `hitbox` (`include/sf64object.h:152`), `Effect.unk_74`
  (`:229`), `Player.sfx.srcPos` and `srcVel` (`include/sf64player.h:159-160`). All of them are cleared in the dump. The
  `ObjectInfo` of an object is set from its id (`Object_SetInfo(&obj->info, obj->id)`, `src/engine/fox_boss.c:86`, `fox_beam.c:28`).
- **Measurement.** 37 desync reports of earlier runs (Corneria rails and all-range, Meteo, Macbeth, Aquas, the map after
  Corneria; Windows MSVC against Linux GCC in one of them; one to four players), every dump scanned in 8-byte words:
  **0 words equal to `FOREIGN_POINTER`, 0 words tagged as a pointer into a hashed array.** So in those dumps no value is
  a pointer the receiver could not rebuild from its own memory or from an id.
- A desync is found within `COOP_DESYNC_PERIOD` (10) frames plus the round trip (`desync.h`), and a game is at most
  `COOP_LOCKSTEP_MAX_DELAY` (16) frames ahead of another.

### Derived

- A resume needs the client's game to be alive and in the same run; it is no help for a client that restarted, a
  desync, or a host that went. The window for it is 5 s (the link ends) to 15 s today (the slot is given up); 30 s with the owner's answer 1.
- A dump that restores is the dump we already have, written back into the variables it came from, with the cleared fields
  left as the receiver has them and `ObjectInfo` rebuilt from the object id. That is one format, one code path to keep
  in step with the hash (a new hashed variable is a new chunk on both sides).
- At about 100 KB a snapshot is some 90 datagrams (`COOP_NET_MAX_MESSAGE` is one datagram, decision 0004), a few tens of
  milliseconds on a LAN and roughly a second at 100 ms round trip, once, on the reliable lane; no fragmentation layer is
  needed, only a chunk counter.

### Guesses (each is a test below)

- The hashed set is enough for most desyncs seen so far: every one of them was *found* by it, so what diverged first
  was hashed, but whether the divergence had already spread to state the hash does not hold is what NOTES.md 2026-11-06
  calls unknown ("State outside the hashed set that still affects the game: overlay statics, `gLevelObjects` progress
  beyond `gObjectLoadIndex`, audio").
- A desync that comes from a platform difference (Windows and Linux floats, the Aquas eel read) comes again on the
  same code path after a repair, so a repair is a patch for the player, not for the bug.

### Unknown

- Whether a repaired game plays on equal to the host's for the rest of a level, for each kind of desync. Only a run
  that pokes state, repairs it and compares every later frame says so (Test plan).
- What a person sees and hears at a repair or a resume on two PCs (the stop, the music).
- Whether the cleared `Effect.unk_74` and the `ObjectInfo` pointers can all be rebuilt on the receiver without changing
  a later frame.

## Options

1. **Level start only** (0014 as built). Costs the level since its last checkpoint at every parting.
2. **Resume after a drop** (0014 option 2). Saves the level for a connection that comes back; does nothing for a
   desync.
3. **Repair in place from the host's state** (0014 option 3, 0002 option C). Saves the level for a desync (and, in the
   same way, for a client that rejoins with a game that fell behind); needs a restore path that matches the hash.
4. **Roll back to a common frame and play the inputs again.** Needs each game to keep its own snapshots (pointers are valid
   inside one process, so no format), but a desync that comes from the code and the platform comes again on the same
   inputs: it helps only a divergence that has a random cause (uninitialised memory, timing). Not decided here; the
   measurement of part B says how many of the reports had such a cause.

## Decision (proposed)

Options 3 and 2, in that order (the owner's answer of 2026-11-15: the repair first); 1 stays as the fallback of both.
Option 4 stays for later.

### A. Resume after a drop (built second; what was built, PR of 2026-11-15)

1. **When.** A client's connection times out in a level, or it sends no input for `COOP_LOCKSTEP_STALL_MS` (10 s, the
   host drops it as lost), the host's and the client's game are both alive, and the client comes back with the session id
   inside `COOP_SESSION_RESUME_MS` (30 s, the owner's answer 1; 15 s without resume, as in 0014). A session that resumes
   is a flag of the host's session (`CoopSession_SetResume`), on by default in the game, off with
   `SF64_COOP_TEST_NO_RESUME`, so every test of the old behaviour stays as it was.
2. **Telling the others.** Changed from the first draft, which wanted a new notice: nothing starts again at the first
   loss, so no `SESSION_RECOVER` goes out. Every game stops where it is, as lockstep does by itself, and the window the
   game already has ("waiting for player N", `CoopSyncGame_Waiting`) says whom the others wait for; the host's window and
   log say "Player N lost the connection. Waiting for it to come back (30 s)." When the wait ends, `SESSION_RECOVER`
   goes out with a new reason 4, "Player N did not come back. Starting the level again together." (protocol 12).
3. **Coming back.** The client comes back as it did (`SESSION_REJOIN`) and its game keeps its lockstep standing while it
   does, where it stopped lockstep before. If it lands in the slot it had and no recovery is under way, the host asks its
   game, `SYNC_RESUME`: its run and frame. The client answers `SYNC_RESUMED`: yes if its game is in that run, within half
   the input ring of that frame and no repair is under way. Both then reopen their lockstep link
   (`CoopLockstep_Reopen`: what the peer confirmed is forgotten, but not the peer's own stream, which a client refuses
   back) and the frames flow. In another slot, a no, no answer in 5 s, or a loss with nobody left to start for, the
   level starts again as in 0014. A player's slot is its player index in the running level, which is why another slot
   cannot resume: the first loss of two clients who then come back the other way round takes the level start.
4. **The check goes on.** Hashes lost in the gap are frames unchecked (`desync.h:10-11`); nothing is resent. A repair
   under way when a player is lost ends by its time limit (10 s) and the level starts again.
5. **Stall.** The host's check of a missing input (`Sync_Stalled`) leaves a player who is away alone, and one it has
   just asked to go on (its stall clock starts over when it asks): a first version of this asked a player to go on and
   dropped it again at once, from a clock that still held the wait.
6. **Strictness.** The state hash and the desync check are not loosened (0014 rule 7). A resumed session has equal logs
   on every frame, or the scenario fails.

### B. Repair a desync in place (built first)

1. **When.** The check names a differing frame (#189) in a session of two to four, no game is in the middle of a level
   start, the level is past frame 60 (`COOP_REPAIR_MIN_FRAME`) and the last repair is 300 frames back
   (`COOP_REPAIR_SPACING`). A desync is still
   written to the report and the log, as now; the repair does not hide it.
2. **Who is right.** The host's state. A client that differs gets the host's.
3. **Barrier.** The host picks a frame `B` that every game is still to reach (the frame it has now plus the input delay, the
   check period and a margin), tells every client, and holds at the start of `B`, before the frame runs, which is where
   the hash is taken. A client runs on to `B`, holds, and takes the snapshot. The inputs in the ring carry on from `B`
   for everyone.
4. **Snapshot.** The bytes of the dump of frame `B`, in chunks of one datagram on the reliable lane (`SYNC_REPAIR` messages,
   ids reserved in `PROTOCOL.md` in the pull request that builds it). The receiver writes each chunk into the
   variable it names, leaves the fields the hash clears as they are, sets `ObjectInfo` from `obj.id` and turns tagged
   `iwork` words into local addresses (the inverse of `NormalizeWord`). A `FOREIGN_POINTER` in a snapshot, a chunk of an
   unknown name or a size that does not fit refuses the repair.
5. **Check.** The receiver hashes frame `B` again and sends the total; the host compares it with its own. Equal: both go on.
   Different (the hash, or a fingerprint of what the object pointers stand for, which the hash clears), or no answer in
   `COOP_REPAIR_MS` (10 s): 0014's level start.
6. **Limits.** A second parting within 300 frames of a repair is not repaired: it takes the level start, and two partings
   in a row end the session as 0014 item 8 says. A repair per game is counted in the log, and a test run that is not
   about repairs fails on the first one (see Consequences).
7. **Saying so.** One line in the co-op window and the log: "The games parted. Syncing them again." (and, if it falls
   back, the line of 0014 item 4).
8. **Scope.** Same build and same ROM (the handshake already enforces both, 0002), and the platform rule of 0010. A repair
   across Windows and Linux is attempted like any other and is tested by the owner on two PCs; it is not claimed here.

### Build order

1. This record, accepted or changed by the owner.
2. B, a restore function next to `state_hash.c` and a repair step in `desync_game.c` (the owner's order: a desync has
   cost more real sessions than a drop).
3. A, in `session.c`, `menu.c`, `lockstep.c`, `lockstep_game.c`: smaller, and each part is testable on one PC.

## Test plan

On one Linux PC with real games, as the `survive_*` scenarios of `tools/coop/lockstep_test.py` do (QA.md).

- **A.** A client stopped with SIGSTOP (as `frozen` does) for 7 s and for 12 s and thawed: no `run=2`, no level start, the
  logs of every game equal on every frame, no desync report. For 17 s: today's restart. Through the chaos relay with all
  datagrams dropped for the same times, with two, three and four games. A rejoin in a different run: refused, level
  start.
- **B.** `SF64_COOP_HASH_POKE` flips one bit on the client (the existing `desync` scenario): the log has one repair, no level
  start, the client's hash log equals the host's on every frame from `B` on, and the host's log equals a run without the
  poke on every frame. The same with a poke in each region (`players`, `actors`, `bosses`, `effects`, `items`,
  `scenery`, `sprites`, `shots`, `misc`, `rng`, `globals`), with three and four games, on rails and in all-range, at a
  checkpoint, and 200 frames into the level after the map. A second poke within 300 frames: level start. A
  state the hash does not hold changed on the client (a test-only switch): the repair succeeds, a later frame parts
  again, and the session ends as 0014 says; this is the line between what the repair can and cannot do, and it is
  written into QA.md.
- **Mutants.** `desync_mutants.py` and `lockstep_mutants.py` get mutants for the restore (one region not written, one
  `ObjectInfo` not set, one chunk dropped) and for the resume (the reset of the link record left out); each must make a
  test fail.
- **By hand, not by an agent:** a real drop between two PCs, a real desync across Windows and Linux.

## Consequences

- A short drop and a desync cost a short stop and not the level; the level start of 0014 stays under both.
- A repair hides a desync from the player, so tests that are not about repairs must fail on one. They do: a repair
  writes the report of its desync and the hash logs of the two games differ from the flipped value to the barrier, so
  `lockstep_test.py` (outside its `resync` scenarios, which set the repair on; the others switch it off with
  `SF64_COOP_TEST_NO_REPAIR`), `campaign.py` and `hash_check.py`, which compare every frame, fail on a repaired
  session. No tool needed a change.
- The host can now write any hashed value into a client, where before it could only send pad input. The restore checks
  sizes, array offsets and object ids, not the meaning of every field; a session is between people who chose to play
  together. The values of a player who is down are not in the hash, so they can differ unseen and show as a new
  desync when the player returns (read in the code, not tried); the second desync then takes the level start.
- A new hashed variable is now also a variable the restore has to write. The hash and the restore live in one place
  (`state_hash.c`) and the mutants above check that they stay in step.
- Not tried: Windows, a real connection between two PCs, a Release build, a person playing through a repair. State outside
  the hashed set stays a limit of B, and the test line above says where it ends.
- Costs (a guess, not measured): A is a few hundred lines of C with its tests; B is somewhat larger because of the restore
  and its mutants.

## The owner's answers (2026-11-15)

1. **The window: 30 s** for a player who is away (`COOP_SESSION_REJOIN_MS` goes from 15 s to 30 s; 0014 item 3 and its
   Q2 answer of 15 s are changed by this). The host's stall check (10 s) and a client's wait for the host (40 s,
   `COOP_SESSION_RECOVER_MS`) then have to be read against 30 s: the stall check must not drop an away player, and the
   client's wait must be longer than the window. The build changes both and a test holds them.
2. **A repair counts as a test failure by default**; a test about repairs says it expects one.
3. **Order: repair first**, because a desync has cost more sessions than a drop so far (the sessions of 2026-11-10 ended
   in desyncs at the Aquas boss and on the map, not in drops).
4. **A repair across platforms** is tried like any other; the owner tests it on two PCs. It is not claimed until then.
5. **On screen:** one line in the co-op window and in the log, no overlay. The game is held at the barrier frame for the
   time the state takes to arrive.
