#pragma once

// Makes Venom 1's Golemech raise its pillars from the last row of its tables with the most pillars, and logs where each
// pillar stands, for an unattended test (#276).
//
// Off unless the environment variable SF64_COOP_VE1_PILLAR_PROBE names a file. Before every update of the Golemech
// (OBJ_BOSS_VE1_GOLEMECH, Venom1_Ve1Golemech_Update in src/overlays/ovl_i1/fox_ve1.c) the probe sets its count of
// pillars, swork[21] and swork[22], to 5 and its row, swork[23], to 4. Its two pillar attacks (spB8 1 and 2) then take
// six pillars from row 4 of D_i1_80199CD0 and D_i1_80199B40, tables of five rows of five: the sixth is read past the
// table, as the game does once in a while in a fight (NOTES.md, 2026-11-12, the sanitizer sweep). Only the counts are
// forced; the boss's own update raises the pillars. One line per pillar, on its first update:
//
//   pillar frame=<gGameFrameCount> slot=<actor slot> x=<bits> y=<bits> z=<bits> bossz=<bits>
//
// with the float bits of its position in hex and of the Golemech's z at the boss's last update, and one line
// "golemech frame=<n>" when the boss is first seen. The probe changes the game, so a run with it says nothing about
// any other run; every game of a session must get it.

#ifdef __cplusplus
extern "C" {
#endif

void CoopVe1PillarProbe_Init(void);

#ifdef __cplusplus
}
#endif
