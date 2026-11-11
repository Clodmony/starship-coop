#pragma once

// Log of the wingmen's shields through every store, and a one-shot change to them, for unattended tests (#89).
//
// Both are off unless their environment variable is set when the game starts; with neither set nothing is registered.
//
//   SF64_COOP_TEAM_TRACE=<file>   appends a line whenever one of the fields below changes, and every 300 game frames:
//
//     frame=<frame of a replay, counted from GSTATE_START> gs=<gGameState> level=<gCurrentLevel> phase=<gLevelPhase>
//     ps=<gPlayState>
//     p0=<player 0 state, -1 outside a level> mission=<gMissionStatus> path=<gPathProgress>
//     team=<gTeamShields 1,2,3> saved=<gSavedTeamShields 1,2,3> prev=<gPrevPlanetTeamShields 1,2,3>
//     map=<D_ctx_80177C58 1,2,3> ships=<event actors flying as Falco, Slippy, Peppy>
//     p1=<player 1's shields, -1 while it does not fly in co-op> scenes=<CoopTeam_Choices, 16 numbers: 0 not
//     chosen, else the team id that flies that scene of a human-flown wingman, src/port/coop/game/team.h>
//     ai=<all-range and boss-fight wingmen flying on their own AI, as Falco, Slippy, Peppy>
//     body=<CoopTeam_Body of the all-range body of Falco, Slippy, Peppy: 0 none, 1 on its human, 2 away, 3 elsewhere>
//     shown=<CoopTeam_DisplayShields of Falco, Slippy, Peppy: what their bars draw in the status rows and the radio>
//     radio=<gRadioMsgId of the last radio line>,<gRadioMsgRadioId, its speaker>
//     guests=<event actors flying as Katt, as Bill, in their own ships>
//
//     (all on one line). path shows in every line but does not cause one. gGameFrameCount restarts with each level and
//     screen, so it would not say where in a replay a line belongs.
//
//   SF64_COOP_TEAM_POKE=<level>,<frames>,<id>,<value>   writes value to gTeamShields[id] once, after player 0 has been
//     active for that many frames of that level, and logs a "# poke" line. An optional second <id>,<value> pair is
//     written on the same frame. Stands in for the hits a wingman takes.
//     tools/coop/wingman_check.py uses both.
//
//   SF64_COOP_TEAM_DOWN=<level>,<frames>,<player>   sets co-op player <player>'s (1..3) shields to 0 and starts its hit
//     timer (radioDamageTimer) once, after player 0
//     has been active for that many frames of that level, so the game takes that player down its own way, and logs a
//     "# down" line. Set it on every client (#15, the all-range body's away state).
//
//   SF64_COOP_TEAM_RINGS=<level>,<frames>,<player>,<shields>   once, after player 0 has been active for that many frames
//     of that level: gGoldRingCount[0] = 3 (the team's shield upgrade, up to 383 shields) and co-op player <player>'s
//     (1..3) shields to <shields>, and logs a "# rings" line. Set it on every client (the wingman bar of a human with
//     the upgrade, CoopTeam_DisplayShields).

#ifdef __cplusplus
extern "C" {
#endif

void CoopTeamTrace_Init(void);

#ifdef __cplusplus
}
#endif
