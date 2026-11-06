#pragma once

// Input recording and replay for tests (WP-03). See docs/coop/QA.md.
//
// Off unless one of these environment variables is set when the game starts:
//   SF64_COOP_REPLAY_RECORD=<file>  record controller input per game frame into <file>
//   SF64_COOP_REPLAY_PLAY=<file>    play <file> back instead of the live controllers
//   SF64_COOP_REPLAY_REPORT=<file>  where to write the result line (default: replay-report.txt)
// With none of them set, no listener is registered and the game runs unchanged.
//
// The report ends with result=<value>. Recording: RECORDED. Playback: PASS, FAIL_MISMATCH (a checkpoint differs),
// FAIL_INCOMPLETE (the game closed first), FAIL_NO_FRAMES (the file holds a header and nothing else), FAIL_TRUNCATED
// (the file ends inside a frame). Either mode: ERROR_OPEN_FILE, ERROR_BAD_FILE (no valid header).

#ifdef __cplusplus
extern "C" {
#endif

void CoopReplay_Init(void);

#ifdef __cplusplus
}
#endif
