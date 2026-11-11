#pragma once

// Starts a test game straight in a level, with no menus (#148). See docs/coop/QA.md.
//
// Off unless this variable is set when the game starts; with it unset no listener is registered:
//   SF64_COOP_TEST_START_LEVEL=<level>   the game goes to this LevelId (include/sf64level.h) on the first frame the
//                                        title screen would run, through CoopLevelStart_Enter, as a new game
//   SF64_COOP_TEST_START_SKIP_INTRO=1    with it: and past the level's intro, the way Start does on a planet that has
//                                        been cleared. Player 0 flies from the first frames.
//   SF64_COOP_TEST_START_TITLE_FRAMES=<n> with it: the title screen runs n frames before the level, so the game comes
//                                        the whole way from boot as a player's does
//   SF64_COOP_TEST_START_RESET=<n>       with it: n frames into the level player 0's right wing takes a hit (40 left,
//                                        as #187 saw) and the game resets the way F4 does, back to boot and the title
//                                        screen, where it stays. A session can start from there in the same process.
//
// Frames still count from GSTATE_START, as in a replay and the hash log, so a level starts a few frames in.

#ifdef __cplusplus
extern "C" {
#endif

void CoopTestStart_Init(void);

// What this game was told, for the replay report: menus, level:<level> or level:<level>:skip-intro.
const char* CoopTestStart_Name(void);

#ifdef __cplusplus
}
#endif
