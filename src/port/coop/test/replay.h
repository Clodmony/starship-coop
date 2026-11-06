#pragma once

// Input recording and replay for tests (WP-03). See docs/coop/QA.md.
//
// Off unless one of these environment variables is set when the game starts:
//   SF64_COOP_REPLAY_RECORD=<file>  record controller input per game frame into <file>
//   SF64_COOP_REPLAY_PLAY=<file>    play <file> back instead of the live controllers
//   SF64_COOP_REPLAY_REPORT=<file>  where to write the result line (default: replay-report.txt)
// With none of them set, no listener is registered and the game runs unchanged.

#ifdef __cplusplus
extern "C" {
#endif

void CoopReplay_Init(void);

#ifdef __cplusplus
}
#endif
