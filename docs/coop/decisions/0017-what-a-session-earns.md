# 0017: What a session earns: keep nothing, or merge the planets a session cleared into each player's own save

- Status: Proposed (written 2026-11-16 for #262's item "what a session earns is not kept"; **nothing is built**; it is
  a rule question for the owner, who has not been asked yet. Questions at the end)
- Date: 2026-11-16
- Issue: #262 (block "Sessions"); option S3 of [0009](0009-vehicles-cutscenes-save.md), item 8
- Changes, if accepted: one sentence of 0009 item 8 ("What a session earns is not kept, on any machine"). The rest of
  item 8 stays: the host's progress is the session's save, and no machine writes a real save **during** a session.

## Context

Decision 0009 item 8 (Accepted 2026-11-10, S1 as the owner changed it) made the host's progress the save of a session
and stopped every real write while the session runs. It says of its own cost: "clients gain no progress", and of S3,
"a merge can still come later". #262 lists that merge as open, "if wanted". This record puts the choice in front of
the owner with what the code allows.

### Observed (read in the code at `80a8e8ee`; nothing here was run)

- A session's save lives in memory only. `CoopSave_Enter` (`src/port/coop/game/save.c:66`) copies the host's progress
  over this game's `gSaveFile` and keeps this game's own sound settings; `Save_WriteData` writes nothing while
  `CoopSave_WriteBlocked()` is true (`src/sys/sys_joybus.c:105`); `CoopSave_Leave` (`save.c:90`) reads the game's own
  file back with `Save_Read()` and logs "what the session earned is not kept". It is called from one place, the end of
  lockstep (`src/port/coop/sync/lockstep_game.c:612`), however the session ended.
- So at `CoopSave_Leave`, just before the read, `gSaveFile.save` holds the host's progress as it was at the session's
  level start **plus everything the session's own `Save_Write` calls set since**, and the file on disk holds this
  player's progress from before the session. Both are 256 bytes (`COOP_SAVE_SIZE`, `save.h`).
- What a level sets: `gSaveFile.save.data.planet[planetId]` gets `played`, `normalClear` or `expertClear`, and
  `normalMedal` or `expertMedal` (`src/engine/fox_hud.c:1311-1325`, written at `:1332`; the map sets the same five bits
  for the planet just flown, `src/overlays/ovl_menu/fox_map.c:1803-1830`, written at `:1479`). One byte per planet, five
  bits used (`PlanetData`, `include/sf64save.h:15-22`), 16 planets.
- What else a run writes: the ranking table (names, route, lives, medals and per-planet stats of the ten best runs,
  `SaveData` 0x18 to 0xE9, `include/sf64save.h:38-53`), filled after Venom and at a game over
  (`src/overlays/ovl_menu/fox_option.c:448`, `:459`, `:4404`, `fox_map.c:1538`), and the ending-seen byte `padEE[0]`
  (`src/overlays/ovl_ending/fox_end2.c:843`).
- The save's progress is in the state hash (region `globals`, 0009 item 8), so two games of a session that hold
  different progress are a desync, and a repair (decision 0016) writes the host's over a client's.
- The session copy is the same on every game of the session (0009, Derived: every `Save_Write` runs on every game on
  the same frame).
- The scripted sessions already read every game's save before, during and after a session: the join driver's
  `save_after` report line (`src/port/coop/test/join_driver.c:268`) and `lockstep_test.py`'s `map_saves` scenarios
  check that each `default.sav` is byte for byte as before.

### Derived

- "What the session earned" can be computed on each PC without a message: the bits that are set in the session's copy
  at `CoopSave_Leave` and were not set in the copy `CoopSave_Enter` put there. That difference is the same on every
  game, and it leaves out what the host already had: a player does not get the host's whole progress by joining once.
- Planet flags only ever go from 0 to 1 in play, so a merge is a bitwise OR into the player's own planet bytes. An OR
  cannot take a medal away, whatever the session copy holds.
- The ranking table cannot be merged that way: a row is one run, and whose run a co-op run is has no answer in the
  save's format (three letters per row).
- A merge needs one real write after the session ended, which is the one kind of damage 0009's S1 cannot do. It can be
  held small: read the own file (as `CoopSave_Leave` does today), OR at most 16 bytes, write through the game's own
  `Save_Write` (checksum and backup copy as upstream writes them).

### Unknown

- Whether the owner wants it at all. On 2026-11-10 the owner chose "no machine writes a real save during a session"
  over keeping the host's file as the session's save; that was about a session parting on the map, not about medals.
- What a session ended by a desync should keep. A level's flags are set at its clear, on every game on the same frame,
  so a level cleared before the games parted was cleared by everyone; a parted level sets nothing. Not run.
- Whether a medal earned with cheats on should count. Single player on this port keeps it (the cheats are CVars and
  `Save_Write` does not look at them: read, not run); a session needs the same cheats on every game (the handshake).
- Expert mode in a session (`gExpertMode` is a level start value): its clears and medals have their own bits; not run
  in any session test.

## Options

- **K0. Keep nothing** (0009 item 8 as accepted, today). Costs nothing. A team that clears the campaign together has
  no medal and no cleared planet to show for it on any PC, the host's included, and Expert mode stays locked for a
  player who earned every medal in sessions.
- **K1. The host keeps what the session earned, clients nothing.** At the session's end the host ORs the session's new
  planet flags into its own file. Smallest change to "the host's save holds the progress" (`DESIGN.md` section 1).
  Clients still earn nothing.
- **K2. Every player keeps the planet flags the session earned** (S3 of 0009, for the host too). Each game ORs the
  bits the session set into its own save when the session ends. No message, no host authority over a client's file
  beyond those bits. Rankings and the ending-seen byte are not kept.
- **K3. K2 plus the host's ranking rows.** The host's ranking table takes the session's rows, as single player would.
  Puts co-op runs into the host's single-player rankings (the risk 0009 names under S1); clients' rankings stay
  theirs.
- **K4. A separate co-op save** (S4 of 0009). Keeps co-op apart from single player, needs its own file code, and the
  unlocks a player has do not carry into it. Rejected in 0009 for that reason; listed for completeness.

## Decision (proposed)

**K2**, if the owner wants progress kept at all; otherwise K0 stays and this record is closed as "not wanted".

1. **What.** The five flags of each planet (`played`, `normalClear`, `normalMedal`, `expertClear`, `expertMedal`) that
   are set in the session's copy when the session ends and were not set in the copy the session began with.
2. **Who.** Every game of the session, the host's too, into its own save.
3. **When.** Once, in `CoopSave_Leave`, after the game's own file is back in memory: OR the bits, then one
   `Save_Write`. During the session nothing is written, as now. A session that earned nothing writes nothing.
4. **Not kept.** The ranking table, the ending-seen byte, anything else in the save. A player's sound settings are
   their own already.
5. **Saying so.** The co-op log's line at the end names the planets kept, in place of "what the session earned is not
   kept"; the co-op window gets one line ("Kept from this session: Corneria (medal), Meteo.").
6. **Co-op off:** nothing changes; no code outside `src/port/coop/game/save.c` and its caller.

### Test plan (when built)

On one Linux PC with real games, as `map_saves` does: a session of two with two different saves flies Corneria to its
clear (pilots, as `survive_gaps_route`), then ends. Each `default.sav` afterwards equals the one from before the
session but for Corneria's byte and the checksum, and Corneria's byte is the old one OR the session's new bits; a
session that ends before the clear leaves both files byte for byte as they were; a host that had Corneria cleared and
a client that had not: the client gets the clear, not the host's other planets. A unit test of the OR on made-up saves
(`make_save.py`), and a mutant that copies the whole planet byte instead of the OR must fail it.

## Consequences

- Easier: playing the campaign together counts for every player.
- Harder: a real save is written by co-op code, once, at the session's end. A bug there can set flags a player did not
  earn; it cannot clear one. The byte-for-byte check of the save tests has to allow exactly the earned bits.
- A crash or a killed game in a session keeps nothing (the write is at the end), as today.
- Rankings stay single player's on every PC.
- `README.md` ("Save" row), `TWO_PC_TEST.md` and `save.h` say "not kept" today and change with the build.

## Questions for the owner

1. Is keeping progress from a session wanted at all? (K0 keeps things as they are.)
2. If yes: every player (K2, recommended) or the host only (K1)?
3. Should a medal count when the session ran with cheats on? (Recommended: as single player does, yes; the record
   does not add a rule.)
4. Rankings: stay out (recommended), or the host's table takes co-op runs (K3)?
