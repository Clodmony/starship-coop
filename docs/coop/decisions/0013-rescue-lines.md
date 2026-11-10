# 0013: A borrowed rescue keeps its radio: the substitute's own help and thanks lines

- Status: Proposed
- Date: 2026-11-10
- Issue: #15
- Changes: decision 0012, rule 5 (radio), for the scenes listed in a table. 0012 stays Accepted for everything else.

## Context

Decision 0012 chose C2 for the lines of a borrowed scene: a taken wingman's line plays in the substitute's voice if the
game recorded it for all three wingmen, and is dropped otherwise. It left C3 (a hand-made table of the substitute's own
recordings) "for after a listening pass".

The owner's two-PC run on 2026-11-10 (build `ed61d6fd`, the second player flying Falco on Corneria) showed the cost
0012 named: Slippy took Falco's rescue as 0012 says, but the scene ran silent, because none of Falco's rescue lines
belongs to a set of three. The owner asks for Slippy to call for help, for Peppy to name Slippy, and for Slippy to say
thanks after the rescue.

Observed (read with `tools/coop/script_reader.py` on a US 1.0 `sf64.o2r`, and the message texts decoded from the same
archive with the character table of `tools/Torch/src/factories/sf64/MessageFactory.cpp`; texts are not copied here):

- Falco's rescue on Corneria is S59 (Falco), S60 (its leader) and S61 (chasers) (`docs/coop/NOTES.md`, #59 table).
  Falco speaks three lines in it: 2167 at `CO S60:7` (something on his ship is failing), 2166 at `CO S60:25` (asks Fox
  for help) and 2337 at `CO S59:44` (a grudging thanks). Peppy's 20235 at `CO S60:17` is a flying tip that names no
  wingman.
- Slippy's own rescue on Corneria is S17 with S16. Slippy speaks 2062 at `CO S17:8` (a call for help) and 2336 at
  `CO S17:33` (thanks to Fox).
- Slippy's fly-in on Corneria, S51, has two lines of Peppy's that name Slippy: 20267 at `CO S51:3` (calls him back)
  and 2061 at `CO S51:13` (warns him of an enemy behind him).
- A line is a message id and a speaker in `EVOP_PLAY_MSG` (`src/engine/fox_enmy2.c:1335-1341`); the voice follows the
  message id. So a line can be replaced by another recorded line with its own speaker, but not moved to another voice.

Derived:

- For Corneria's Falco rescue flown by Slippy, Slippy's own rescue lines and Peppy's warning fit the three places
  Falco's lines sit: trouble at the start, a call for help, thanks at the end.

Unknown:

- How the replaced lines sound in place (timing against the chasers, the length of each line). Nobody has listened.
- Fitting lines for the other rescues of the #59 table and for Peppy as the substitute. On Corneria Peppy has no
  recorded rescue lines of his own (his S1 fly-in speaks a flying tip only).

## Options

1. **Keep 0012 C2.** Costs: a borrowed rescue stays silent where its wingman's lines are not in a set of three.
2. **A table per scene, by message id and substitute** (0012 C3): a taken wingman's line becomes a named line of the
   substitute, or of another speaker that names the substitute, with that line's own speaker. Lines not in the table
   keep C2. Costs: the table has to be filled scene by scene from the scripts and the decoded texts; a near fit can
   sound wrong until someone listens.
3. **Generic lines per character** (one help call and one thanks per wingman, used in every rescue). Costs: the same
   two lines in every level, and lines from another level's story can name the wrong place or enemy.

## Decision

**Option 2, starting with Corneria's Falco rescue flown by Slippy.** The owner asked for it on 2026-11-10. When Slippy
flies Falco's rescue on Corneria, Falco's 2167 plays as Slippy's 2062 (Slippy's call for help), Falco's 2166 as
Peppy's 2061 (Peppy warns Slippy), and Falco's 2337 as Slippy's 2336 (Slippy thanks Fox). Every other line of a
borrowed scene follows 0012 rule 5. The table lives with the scene table in `src/port/coop/game/`; more rows are added
the same way, each with its script places and the line it replaces.

**Lines of other speakers that name the taken wingman** go in the same table (0012 left them open, "Unknown"). The
owner's second run on 2026-11-10 (build `b98c3870`) heard one: in Falco's lead to Corneria's waterfall (S14), flown by
Slippy, Fox's 2185 at `CO S14:19` still asks Falco where he is going. It plays as Peppy's 20267 (Peppy calls Slippy
back, his line from Slippy's fly-in at `CO S51:3`). Falco's own lines in S14 (2181, 2188) stay dropped: Slippy
recorded no lead lines (searched in the decoded texts; the "follow me" lines are Falco's and James's).

## Consequences

- The table is fixed data keyed by the shared choice of substitute (0012 rule 2), so every client plays the same line
  on the same frame; the radio state the hash already holds stays equal.
- A test checks that, with the client flying Falco on Corneria, the rescue plays 2062, 2061 and 2336 and none of
  Falco's 2166, 2167, 2337, and that Fox's 2185 never plays while a human flies Falco.
- A listening pass on a real run is still owed; if a line sounds wrong, its row changes.
- Next rows to fill: the other rescues of the #59 table (Area 6, Sector Y, Solar, Zoness, Macbeth, Titania) and
  Peppy as the substitute.
