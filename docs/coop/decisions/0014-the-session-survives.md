# 0014: The session survives a lost player or a desync: the level starts again from the host's state

- Status: Proposed (the rules are the owner's answers of 2026-11-11, all three the recommended option; built and scripted-tested; the owner reads and accepts the text in the pull request that merges it)
- Date: 2026-11-11
- Issues: #189 (the line that names the first differing field), the owner's "reconnect silently if necessary" (no issue of its own yet)
- Changes: decision 0002 (sync model), consequence "a desync ends the session"; the end of a session in WP-21/WP-23 (a lost player or a desync). Decision 0009 item 8 (the host's save) stays as it is.

## Context

Until now a session ended for everyone when one player went away, sent no input for 10 seconds, or when the games parted
(WP-23 chose that as the smallest honest behaviour). In the two real sessions of 2026-11-10 that cost a long run at the
Aquas boss. The owner asked whether the host's save could be synced after each level and the session reconnect silently
if necessary. The host's save is built (decision 0009 item 8). This record is the other half: not ending the session.

One message cannot repair games that have parted: their worlds differ in actors, random numbers and the audio clock.
So the recovery point has to be a point where every game can start from the same state, and that is a level start.

## Options

1. **The level starts again for everyone, from the host's state** (a new `SESSION_LEVEL_START` with the host's save).
   The level that parted is lost, the rest is silent.
2. **Reconnect inside the input ring:** no level is lost, but only a drop is helped, not a desync, and only one that is
   shorter than the input ring holds.
3. **A full state snapshot** (decision 0002, option C): no level is lost and a desync is helped too, but it needs a
   snapshot of the whole game state, which decision 0002 weighs and which is not built.

## Decision

Option 1. Options 2 and 3 are not part of this record; they stay for later.

1. **When.** A desync, a lost connection, a player who sends no input for 10 seconds, and a player who leaves a session of
   three or four. The host says so (`SESSION_RECOVER`, reason and player), sends its save again and a level start with
   the next run number. Every message of lockstep and the desync check carries the run number, so a late message of the
   level that parted is dropped.
2. **Which level.** The level the team last entered, from its start (the owner's Q1). If the host reached a checkpoint in
   it, the level starts there instead, as after a death but with no life taken (the owner's addition of 2026-11-11,
   second step). The host's restart point is the level its game entered last, by the map or by a jump.
3. **Waiting.** For a player whose connection was lost the host waits 15 seconds (the owner's Q2). That game goes back
   by itself with the session id and gets its slot and character back; after 15 seconds the level starts again with
   whoever is there. A player who leaves on purpose is not waited for.
4. **Saying so.** One line in the co-op window, which comes up by itself, and the same line in the co-op log (the owner's
   Q3): "The games parted. Starting the level again together." or "Player 2 lost the connection. Waiting for it to come
   back (15 s), then the level starts again."
5. **Session id.** A random 64-bit number made by the host outside the simulation (never from the game's seeds), in
   `SESSION_WELCOME` and echoed in `SESSION_REJOIN`. A returning player is recognised by it, a client refuses a host that
   restarted. It is no authentication; the protocol has none.
6. **The save rule of 0009 item 8 stays.** The host's progress is sent again at the new level start, no real save is
   written by any machine in a session.
7. **The check stays as strict as it was.** The state hash and the desync check are not loosened. A desync still writes its
   report; #189 adds one line in both games' co-op logs (`first difference at frame N, players A and B: ...`) which names
   the field, the same one `desync_report.py diff` names.
8. **When it does not help.** With nobody else left, with a host that is gone, with a different build or ROM, and when the
   games part twice in a row within 300 frames of the level start, the session ends cleanly as before.
9. **Protocol.** `SESSION_REJOIN` 0x0019, `SESSION_RECOVER` 0x001A, `SYNC_DUMP` 0x0043; `COOP_PROTOCOL_VERSION` 10
   together with the names message of #258 (`docs/coop/PROTOCOL.md`).

## Consequences

- A parted level costs the team the part of the level since its last checkpoint (or since its start), not the session.
- Proven by scripted runs of real games on one Linux PC with two and three clients (`survive_*` in
  `tools/coop/lockstep_test.py`, `docs/coop/QA.md`). **Not tried:** a real connection drop between two PCs, Windows,
  four players, the map after a restarted level.
- What a lost player's game does when it cannot come back (its window, its music) is the game's own handling of a session
  that ended; nobody has watched it on a real PC.
- Option 2 would save the level for a short drop; option 3 would also save it after a desync. Neither is built.
