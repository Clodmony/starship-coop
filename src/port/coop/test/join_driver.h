#pragma once

// A scripted join flow for tests (WP-24): the game hosts or joins a session, picks and starts a level with nobody at
// the keyboard, as the co-op menu does for a player (WP-25). tools/coop/join_test.py drives it. See docs/coop/QA.md.
//
// Off unless SF64_COOP_JOIN_PORT_FILE, SF64_COOP_JOIN_CONNECT or SF64_COOP_JOIN_WATCH is set when the game starts:
//   SF64_COOP_JOIN_PORT_FILE=<file>  host on a free port and write it into <file>
//   SF64_COOP_JOIN_CONNECT=<host:port>  join that host
//   SF64_COOP_JOIN_REPORT=<file>     one line for each thing that happened, see below
//   SF64_COOP_JOIN_PICKS=<a,b>       client: characters to ask for, the next one after each refusal
//   SF64_COOP_JOIN_CLIENTS=<n>       host: try to start once this many clients have joined (default 1)
//   SF64_COOP_JOIN_LEVEL=<n>         host: the LevelId to start (default 0, Corneria)
//   SF64_COOP_JOIN_START_FRAME=<n>   host: not before this frame, counted as in the state hash log
//   SF64_COOP_JOIN_POKE_TEAM=1       host: test only, gives the values no level start clears something to carry
//   SF64_COOP_JOIN_KEEP_OWN=1        client: test only, goes to the level but keeps its own values, as a control
//   SF64_COOP_JOIN_MAIN_PORT=<n>     test only: gMainController before the level, as the title screen of a PC with its
//                                    first controller on another port leaves it
//   SF64_COOP_JOIN_WATCH=1           no session of its own: reports `level_running` for a level the co-op menu started,
//                                    for tools/coop/menu_test.py
// With none set, no listener is registered and the game runs unchanged.
//
// Report lines, each a word and key=value pairs: `joined player=N`, `pick character=Falco`, `roster a b c d`,
// `pick_refused character=Falco`, `refused text=...`, `left`, `ended text=...`, `start_refused`, then
// `level_start frame=N level=N phase=N roster=a,b,c,d values=...` when the session says so and `level_running frame=N
// level=N phase=N players=N characters=a,b,c,d seeds=... team_damage=... team_event=... main=N` on the first frame after
// Play_Init. Until then gGameFrameCount still counts the screen the game came from, so that is where games can agree.

#ifdef __cplusplus
extern "C" {
#endif

void CoopJoinDriver_Init(void);

#ifdef __cplusplus
}
#endif
