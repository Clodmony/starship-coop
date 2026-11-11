#pragma once

// State pokes for the tests of #210: who scores a kill, where a life goes, the wing-broken level event and the
// Saucerer, for tools/coop/lockstep_test.py (`kill`, `lives`, `wing`, `saucerer`). Each poke is made on the same frame
// on every game of a session, from simulated state only, so the games stay one game; the test then compares their
// hashes.
//
// Off unless both are set when the game starts:
//   SF64_COOP_SCORE_PROBE=<kind>:<frame>[:<player>]   on lockstep frame <frame> (a game outside lockstep counts
//                                                     gGameFrameCount) the poke of <kind> is made for <player>
//                                                     (default 1)
//   SF64_COOP_SCORE_REPORT=<file>                     one line per thing seen, below
//
// Kinds and their report lines (`players frame=N states=s0,s1 parked=p0,p1` comes with every poke):
//   kill      four all-range enemies, each put into its last dying frame (timer_0BE 2, as func_enmy2_800763A4 counts
//             it), so the next update ends it through Actor_Despawn: on <frame> one killed by <player>
//             (CoopPlayers_DmgSource, as a shot of that player writes it) with gDropHitCountItem 14 in all-range, the
//             count at which the next kill drops lasers; on +20 one with DMG_SRC_2; on +40 one shot down by Falco
//             (NPC_SHOT_ID + AI360_FALCO); on +60 one killed by player 0. One frame after each:
//             `kill frame=N source=S hits=a,b drop=c,d lasers=e,f gone=0|1` (before, after; lasers counts the laser
//             items in play)
//   lives     on <frame> gGoldRingCount[0] is set to 5 and a gold ring appears where <player> will be, on +60 a 1-up.
//             `lives frame=N rings=R pool=L0 slot=L` (L: gLifeCount[<player>]) whenever one of them changes
//   wing      on <frame> <player>'s two wings are broken and an event actor runs Corneria's script 39 (CO S39: on
//             EVC_WING_BROKEN it becomes a wing repair item, else it despawns a frame later), 2000 ahead of player 0
//             and 600 above it; on +60 every wing is whole again and a second one runs the same script.
//             `event frame=N n=<1 or 2> type=T status=S` whenever one changes (type 71 is EVID_ITEM_WING_REPAIR)
//   saucerer  Katina: on the first frame from <frame> on that player 0 and <player> fly, the Saucerer goes into
//             its fight (state 6 at x 0, y 2000, z 0, the core timer 5760, gAllRangeEventTimer 20000), as
//             Katina_UpdateEvents puts it after a restart from the checkpoint; 2 frames later a single laser of
//             <player> appears inside the first hatch's hitbox, 22 frames later one of player 0 inside the second's
//             (not on one frame: the boss keeps one dmgPart). `shot frame=N player=P box=B slot=S at=x,y,z` for
//             each, `hatch frame=N state=S hp=a,b,c,d core=C` whenever one changes
// With neither set the game runs unchanged.

#ifdef __cplusplus
extern "C" {
#endif

void CoopScoreProbe_Init(void);

#ifdef __cplusplus
}
#endif
