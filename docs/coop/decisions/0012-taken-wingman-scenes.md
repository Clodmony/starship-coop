# 0012: A wingman a human flies is not shown twice: the scene goes to a free computer wingman

- Status: Proposed
- Date: 2026-11-09
- Issue: #202
- Changes: decision 0006, option C1 and rule 5 for on-rails scenes. 0006 stays Accepted for everything else.

## Context

Decision 0006 chose C1 for on-rails scenes: the scene actor of a human-flown wingman stays as a stand-in, flies its
script, takes no damage, and its lines play. The owner's two-PC run on 2026-11-09 (the second player flew as Falco) showed
the cost 0006 named: a computer Falco in every Falco scene, seen in trouble or turning back while the human Falco flies
on. The owner asks for another wingman to take the scene instead: Slippy for Falco, Peppy if Slippy is taken too.

The research is the #202 entry in `docs/coop/NOTES.md`. In short:

Observed (read at `0e5a5cf6` and with `tools/coop/script_reader.py` on a US 1.0 `sf64.o2r`, nothing run):

- **What says who a scene's Arwing is.** `EVOP_SET_TEAM_ID` writes the team id to `iwork[12]` and, for Falco, Slippy
  and Peppy, the actor's index to `gTeamEventActorIndex[id]` (`src/engine/fox_enmy2.c:1264-1272`). A teammate is drawn
  with one Arwing display list for all three (`src/engine/fox_enmy2.c:3980-3987`); its team arrow is placed by
  `iwork[12]` (`:4218-4224`). So the player tells the characters apart only by the arrow and the radio.
- **Where a scene names its wingman.** In the command arguments, not in the actor: the gate `EVC_*_ACTIVE` reads
  `gTeamShields[id]` (`:2408-2424`), `EVOP_DAMAGE_TEAM` hits its own actor if the id matches `iwork[12]` and else adds to
  `gTeamDamage[id]`, which drains `gTeamShields[id]` (`:1386-1392`, `src/engine/fox_play.c:7044-7052`), and
  `EVOP_SET_TARGET` reads `gTeamEventActorIndex[id]` (`src/engine/fox_enmy2.c:1453-1458`). The helper scripts of a scene
  (the leader and chasers of a rescue) gate on the same wingman: Corneria S59 is Falco, S60 its leader and S61 its
  chasers, all three gated on `EVC_FALCO_ACTIVE` at command 0.
- **What a line is.** `EVOP_PLAY_MSG` holds a message id and a speaker (`:1335-1341`); the voice is chosen by the
  message id (`src/engine/fox_radio.c:128-129`), the portrait and the speaker's gate by the speaker
  (`src/engine/fox_radio.c:80-94`). A line cannot be moved to another speaker: the recording is one character's.
- **Lines recorded for all three.** Some lines come in sets of three, one per wingman, used at the same step of scripts
  of the same shape. Macbeth S26, S27 and S28 (Falco, Slippy, Peppy) play 9100, 9110 and 9120 at command 8, 9130, 9140
  and 9150 at command 25, and 9160, 9170 and 9180 at command 35. Other sets: 20315, 20316 and 20317 (Meteo S82, Venom
  1 S91 to S93), 20319, 20320 and 20321 (Area 6 S22), 20306, 20305 and 20304 (Falco, Slippy, Peppy in Venom 1 S71),
  and the retreat lines 20220, 20222 and 20221 (`src/engine/fox_enmy2.c:2081-2090`). Of the 100 lines that the three
  wingmen speak in their own scene scripts, 21 belong to such a set; Corneria's rescue lines (S59 and S60) do not.
- **Retreat.** A scene actor whose `gTeamShields[iwork[12]]` falls to 0 or below turns back with that character's
  retreat line (`src/engine/fox_enmy2.c:2072-2097`), so a swapped actor turns back as the character it now is.
- **The Corneria route** reads `gTeamShields[TEAM_ID_FALCO] > 0` when the turn item loads
  (`src/engine/fox_enmy.c:1189`), not the scene. Under 0006 that value is the human's shields.
- Scenes run on every on-rails level with event scripts: Corneria, Meteo, Sector X, Area 6, Sector Y, Solar, Zoness,
  Macbeth, Titania and Venom 1 (#59 entry, table "Observed, level scripts"). Fortuna, Katina, Bolse, Sector Z and Venom
  2 place no event actors; Aquas and Andross read no wingman.

Derived:

- A swap is a remap of the scene's wingman id in four commands (`SET_TEAM_ID`, `*_ACTIVE`, `DAMAGE_TEAM`,
  `SET_TARGET`) for the scripts of that scene only. The flight, the timing, the enemies and their score do not change.
- Scripted hits then land on the substitute. Corneria S59 ends with a 255 hit (`CO S59:34`), so a failed rescue of the
  borrowed scene downs Slippy, where under 0006 it hurt nobody. The route does not move: it still reads the human
  Falco.
- The remap has to be per scene, not per level: content gated on "Falco up" outside a scene (Corneria S8, S36) and
  lines that only pick a speaker by who is up still mean the human, as 0006 says.

Unknown:

- Whether a substitute's own scene can start while it flies a borrowed one. The scripts start from the level object
  lists by `zPos1`, which no tool reads yet. The rule below does not depend on the answer.
- Which lines of the other speakers in a scene name the taken wingman (for example Peppy's line in Corneria S60).
  Only the text in the ROM says so; it is not read here, and it stays out of git. Listed for WP-17's listening test.
- Which scenes a real run reaches (the same unknown as in 0006).

## Options

### A. Who takes the scene

1. **Keep the stand-in (0006 C1).** No change. Costs: the double the owner saw.
2. **The next free computer wingman, in a fixed order.** The order is Falco, Slippy, Peppy, round: a taken Falco goes to
   Slippy, then Peppy; a taken Slippy to Peppy, then Falco; a taken Peppy to Falco, then Slippy. "Free" means a
   computer wingman, up, and not on screen in another scene. Costs: a table of scenes per level, a remap in the
   interpreter, and scripted hits on a character who was not in that scene before.
3. **Hide the stand-in.** Same simulation as A1 with a draw change. Costs: chasers weave around nothing, and the lines
   still call for the human.
4. **Drop the scene** (0006 C3). Costs: the enemies and lines go, and the level offers less.

### B. When nobody is free (three humans, or every computer wingman down or busy)

1. **The stand-in, as 0006 C1.** Nothing is lost, the double comes back in that case only.
2. **Drop the scene.** No double; the scene's enemies, score and lines are lost.
3. **Hide the stand-in**, as A3.
4. **A guest flies it: Katt, or Bill.** Both are event actors the game already has, with their own ships (`EVID_KATT`,
   `EVID_BILL`, `include/sf64event.h:421-422`), their own team arrows (`src/engine/fox_enmy2.c:4221-4222`) and their own
   voices, and a scene actor with their id takes no damage (`src/engine/fox_enmy2.c:2119-2121`). The scene keeps its
   enemies and score, nobody is shown twice, and the rescue cannot fail. Katt is the guest; Bill when Katt is on screen
   in her own scene (Zoness S6, S23, S49; Macbeth S10, S38) or already a guest, and the stand-in (B1) if both are busy.
   Costs: the scene's `EVOP_INIT_ACTOR` must also be remapped from `EVID_TEAMMATE` to the guest's id, and whether
   `EVID_KATT` and `EVID_BILL` fly a teammate's script unchanged is unknown; the taken wingman's lines go (C1 or C2
   have no sets for Katt and Bill).

   Checked against the routes (#59 entry, decision 0006): no route or warp reads a scene's actor. Corneria reads the
   arches and `gTeamShields[TEAM_ID_FALCO]` (`src/engine/fox_enmy.c:1189`), the human's under 0006; Sector X reads
   Slippy in the Spyborg fight, not in a scene (`src/overlays/ovl_i2/fox_sx.c:971`, `:1031`); the warps read no wingman;
   Sector Y's object 444 and the medals read the values, the humans'. A guest who cannot be hurt keeps every route at
   least as open as a computer wingman would.

### C. The lines of a borrowed scene

1. **Drop the taken wingman's own lines** in that scene, by speaker. The other speakers' lines play. Costs: a scene can
   lose its call for help or its thanks; a line from another speaker may still name the taken wingman.
2. **C1, but a line from a set of three is swapped** for the substitute's line of the same set. The sets above come
   from the game's own scripts, so 21 of 100 lines keep a voice with no listening. Costs: a small table of sets; that
   the lines of a set say the same thing is derived from where they sit, not checked by listening.
3. **C2, plus a hand-made table** of further replacements from the substitute's own recordings, where one fits, for
   example Slippy's own rescue lines for Corneria's Falco rescue. Costs: someone has to listen and match lines per
   scene; a near fit can sound wrong.
4. **Keep every line.** Costs: the human hears their character speak for the substitute's Arwing.

Dialogue scenes lose more under C1 and C2: Titania S2 is a Falco scene in which Fox answers Falco's lines, and Fox's
answers would play to nobody.

## Decision

**Proposed: A2, B4, C2, with C3 left for after a listening pass.** The owner chose B4 on 2026-11-09. A scene that would
put a human-flown wingman on screen is flown by the next free computer wingman in the order Falco, Slippy, Peppy. If
none is free, a guest flies it: Katt, or Bill; if both are busy, the stand-in, as 0006 C1. In a borrowed scene the taken wingman's own lines are swapped for the substitute's line where
the game recorded one for all three, and dropped otherwise; everything else in the radio follows 0006.

The rules:

1. **Which scenes.** A scene is a fly-in or rescue script and the helper scripts tied to it, as the #59 table lists
   them, held in a per-level table in `src/port/coop/game/`. Only on-rails event scripts; boss fights and all-range
   stay as 0006 B3.
2. **The choice.** Made once per scene, when the first script of that scene spawns, from the roster and
   `gTeamShields`, the same on every client. "Free" means a computer wingman whose `gTeamShields` is above 0 and whose
   `gTeamEventActorIndex` does not point at a live event actor with its id. The choice is stored for the level and
   reset at each level start; it is simulation state and joins the hash. With no free computer wingman the scene goes
   to a guest as B4 says: Katt, else Bill, else the stand-in. A guest is free when no live event actor carries its id.
3. **The remap.** In the scripts of a borrowed scene, the taken wingman's id becomes the substitute's in
   `EVOP_SET_TEAM_ID`, `EVC_*_ACTIVE`, `EVOP_DAMAGE_TEAM` and `EVOP_SET_TARGET`. Nothing else in the script changes.
   For a guest, `EVOP_INIT_ACTOR`'s `EVID_TEAMMATE` also becomes `EVID_KATT` or `EVID_BILL`, and its gates
   (`EVC_*_ACTIVE` of the taken wingman) count as true while the guest flies.
   Content and lines outside scenes keep reading the taken wingman, which under 0006 means the human.
4. **Damage.** The substitute takes the scene's scripted hits as the original wingman did, the 255 at `CO S59:34`
   included, and turns back with its own retreat line if they down it. A guest takes none, by the game's own rule. The
   human takes none, as 0006 rule 4.
5. **Radio.** In a borrowed scene, a line whose speaker is the taken wingman plays as the substitute's line of the same
   set, with the substitute as speaker, if the line belongs to a set of three; otherwise it does not play, and the
   script moves on, as `EVOP_PLAY_MSG` always does. Lines of other speakers play. The sets are a table by message id;
   option C3 only adds rows to it.
6. **Corneria's route** stays as 0006 says: the arches and the human Falco being up when the turn item loads. Saving
   the borrowed scene keeps Slippy, not the route.
7. **Levels.** Corneria, Meteo, Sector X, Area 6, Sector Y, Solar, Zoness, Macbeth, Titania and Venom 1. The other
   levels have no such scenes.
8. **Co-op off** changes nothing; the remap runs only in an active session and only for a human-flown id.

## Consequences

Easier:

- The double the owner saw goes away, with four players too unless Katt and Bill are both busy.
- The scene's enemies, timing and score stay, so a borrowed scene offers what the original did.
- The rule is one remap at four commands plus one line table, all in the interpreter; the scripts in the ROM stay as
  they are.
- 21 of the scene lines keep a voice, in the substitute's own recording.

Harder:

- One upstream edit in `ActorEvent_ProcessScript` and `ActorEvent_ProcessTriggers` (`src/engine/fox_enmy2.c`), marked
  `// COOP:`, or a hook there.
- A per-level table of scenes to write and keep right.
- A computer wingman can now lose shields in a scene that was not its own, so it is down more often than in single
  player.
- Lines in other speakers' voices may still name the taken wingman until someone listens through the scenes.
- Most borrowed scenes go quiet where the taken wingman spoke (79 of 100 lines have no set), and in dialogue scenes
  such as Titania S2 the other speakers answer a line that did not play.

Next:

- WP-17 (#15) builds rules 1 to 5 after 0006 rules 1 to 4.
- A listening pass over the borrowed scenes, per level, to find lines that name the taken wingman (and, for C2,
  replacement lines).
- A tool that reads the level object lists, to answer whether a substitute's own scene can overlap a borrowed one.
- A run of a teammate scene flown as `EVID_KATT` and `EVID_BILL`, before B4 is built, to see that their actors follow
  the script.
