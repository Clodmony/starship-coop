# 0010: Cross-platform sessions: a platform difference warns, it does not refuse

- Status: Accepted
- Date: 2026-11-08, accepted by the owner 2026-11-08
- Issue: #171
- Supersedes: the "Platform rule" section of [0002](0002-sync-model.md), nothing else in it.

## Context

Observed:

- Decision 0002 chose deterministic lockstep: every client computes the whole game, only inputs travel, and a state
  hash per frame finds a desync. That needs every client to compute the same game, bit for bit.
- Its Platform rule made a platform difference refuse the connection, like a different build: `COOP_ID_DIFF_PLATFORM`
  in `COOP_ID_DIFF_REFUSE` (`src/port/coop/session/identity.h`), the gate in `Host_Hello`
  (`src/port/coop/session/session.c`), PROTOCOL.md rule 2. The platform is the OS, the CPU architecture and the
  compiler with its version, compared as text (`CoopIdentity_Platform`, `src/port/coop/session/identity.c`). So two
  players who built one commit with two versions of one compiler were refused too.
- The rule had a test switch for a real cross-platform test, `SF64_COOP_TEST_ANY_PLATFORM` on the host. It has only
  ever let in a bot that claimed another platform (NOTES.md, 2026-11-07, #2).
- Nobody has compared two platforms. Every determinism result so far is from one PC: Linux, GCC, Debug and Release,
  frame rates from 30 to 360, two real games through all of Corneria (QA.md, "Results"). Windows Release on the same
  commit is a named condition of 0002 (#145), still open.
- The desync check (WP-23, #21) exists: each client hashes its state every frame, the hashes travel, and the first
  frame whose hashes differ ends in a report that names the frame, the fields that differ and each game's build and
  platform (`src/port/coop/sync/desync_game.h`, `src/port/coop/sync/desync_report.c`,
  `coop-desync-<frame>-player<n>.txt`).
- The owner wants to test a session between a Windows PC and a Linux PC (#171), as the first two-PC test
  (TWO_PC_TEST.md).

Unknown:

- Whether a Windows build and a Linux build of one commit compute the same game. Floating point may differ between
  compilers and CPUs (0002, option A). The desync check is what will show it, on the first frame that parts.

## Options

1. **Keep the refusal, test through the switch.** Costs: a host-side environment variable that no player would set,
   so the test setup is not what players get, and a second code path in the handshake. Risks: a rule that is meant to
   be lifted "once a comparison shows" has no comparison that could run outside a test.
2. **Warn instead of refusing.** Both players read which platforms meet, the session runs, and the desync check
   reports if the games part. Costs: two players can start a session that may desync, as 0002 feared, with nothing to
   do about it but quit. Risks: a player who skipped the warning takes a desync for a bug of the game.
3. **Refuse until a comparison has run, then flip.** Costs: the comparison needs two hash logs of one replay on two
   OSes (`tools/coop/hash_check.py`), which nobody has made, and the refusal blocks the session that would give the
   broadest evidence. Risks: the rule stays where #149 left it for as long as nobody does the comparison.

## Decision

A platform difference warns and does not refuse, on both sides. `COOP_ID_DIFF_PLATFORM` leaves `COOP_ID_DIFF_REFUSE`:
the host lets in a client whose build, ROM and simulation CVars match, whatever its platform, and the JOINED event on
both sides carries the platform bit and a text that names both platforms (`CoopIdentity_Describe`). The menu shows that
text to both players. A difference in build, ROM or CVars still refuses, and the refusal still names the platform when
it differs too. The test switch `SF64_COOP_TEST_ANY_PLATFORM` and `CoopSession_AnyPlatform` go: the rule they bypassed
is gone. This record supersedes only the Platform rule of 0002; the sync model, its conditions and the rest of 0002
stand.

Plainly: nobody has compared two platforms. Lockstep needs both to compute the same game, so a session between a
Windows game and a Linux game may desync. The desync check (WP-23) is what shows that, with the frame and the fields
that differ. A refusal would show nothing.

## Consequences

Easier:

- A Windows game and a Linux game can join one session, so the test the owner asked for runs with what players get,
  not with a test switch.
- Two self-built copies of one commit from two compiler versions can play, with the same warning.
- One code path in the handshake: no switch, no test-only mask.

Harder:

- A cross-platform session may desync. Until a recovery exists (option C in 0002), a desync ends in a report and the
  session is over. TWO_PC_TEST.md says what to send back.
- A warning is weaker than a refusal. The text has to say plainly that the two may compute different games.

Next:

- The first session of a Windows game against a Linux game, with the desync check on, and its result on #171: either
  the two stayed in step through Corneria, or the report of the first frame that parted. That result decides whether
  cross-platform play leaves "Later" in ROADMAP.md, or whether a difference has to be traced to a compiler.
- `COOP_PROTOCOL_VERSION` stays 6: no message layout changes, and bit 3 of the diff keeps its meaning. A host with the
  old rule meets a client of this build with a different build id anyway, so its refusal carries bit 0 too.

These changed with this record: `decisions/README.md`, the Platform rule heading in 0002, PROTOCOL.md rule 2,
TWO_PC_TEST.md, DESIGN.md section 5, ROADMAP.md, QA.md, `src/port/coop/session/identity.h`, `session.c`, `session.h`,
`src/port/coop/test/switches.h`, the `platform` scenarios of `tools/coop/session_test.py`, `menu_test.py` and
`coop_menu_test.c`, and the breaks in `tools/coop/session_mutants.py`.
