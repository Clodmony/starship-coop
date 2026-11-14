#pragma once

// Test switches for the determinism spike (WP-02). See docs/coop/QA.md.
//
// Each switch changes one thing that can differ between two machines running the same build, so its effect on the
// per-frame state hash can be measured on one PC. Off unless the environment variable is set when the game starts:
//   SF64_COOP_TEST_SPEED=max          no frame limit, each game frame is drawn once (GameEngine::ProcessGfxCommands,
//   SF64_COOP_TEST_SPEED=<n>          #137). With n > 1 the limit is n times the game's frame rate. Tests count frames,
//                                     not seconds; tools/coop/replay.py sets it for them.
//   SF64_COOP_TEST_NO_DRAW=1          the game's draw code runs and builds its display lists, and the interpreter gets
//                                     an empty one instead (GameEngine::RunCommands, #147). The rest of a frame goes on
//                                     as before: ImGui, the window, the swap, the frame limit. 0 draws, as unset does;
//                                     tools/coop/replay.py sets it, and a tool that needs pixels asks for them.
//   SF64_COOP_TEST_NO_FMA3=1          Windows x64 only: the C runtime's math functions (sinf, cosf, ...) take
//                                     their code path for CPUs without FMA3 (_set_FMA3_enable(0)).
//   SF64_COOP_TEST_AUDIO_SAMPLES=low  every audio update renders the low sample count (as if the output device
//   SF64_COOP_TEST_AUDIO_SAMPLES=high always had enough, or never had enough, buffered; src/port/Engine.cpp:387-388).
//   SF64_COOP_TEST_LOCAL_INDEX=<i>    in co-op this client looks through player i (CoopPlayers_SetLocalIndex), which
//                                     two clients of one session would do. It changes what is drawn, not the input.
//   SF64_COOP_TEST_SYS_FRAME_OFFSET=<n>  gSysFrameCount, the frames since boot, jumps ahead by n on the first game
//                                     frame (rounded down to an even number): a client that booted earlier (#129).
// And for tests of lockstep (src/port/coop/sync/lockstep_game.h, #20):
//   SF64_COOP_TEST_INPUT_DELAY=<n>    frames of input delay in place of COOP_LOCKSTEP_INPUT_DELAY
//   SF64_COOP_TEST_NO_LOCKSTEP=1      a level start starts no lockstep, so a run can show what it is there for
//   SF64_COOP_TEST_START_SEEDS=<a,b,c,d,e,f>  host: the level starts from these seeds, whatever frame Start came on
//   SF64_COOP_TEST_START_NO_TITLE_RADIO=1  host: the level starts with gRadioMsgId 0, whatever the title screen left
//                                     in it. The title's radio sets it to 10, 20, ... as its lines go by
//                                     (Title_Radio_PlayMessage, src/overlays/ovl_menu/fox_title.c), on a clock that
//                                     starts after the logo's 1000 ms of real time, so two sessions that start on
//                                     the same frame can carry different ones (#289). For a test that compares one
//                                     session with another; both games of one session always get the host's.
//   SF64_COOP_TEST_NO_DESYNC_CHECK=1  no state hashes go out or are compared in a session (desync_game.h, WP-23), so
//                                     a run can show what the check is there for
// And for the lives rules (decision 0007):
//   SF64_COOP_TEST_FOX_REFILL_OFF=1   no level is open for the refill of player 0 (rule 3, CoopPlayers_RefillFox), so
//                                     "player 0 goes down in a level that is not open" can be run on a level that is.
//                                     It can only close levels. Set it on every game of a test.
//   SF64_COOP_TEST_LEAD_SWAP=1        the spike of #183 (0007 rule 13): at pool 0, where player 0 would go down while
//                                     another player flies, that player takes slot 0 (CoopPlayers_LeadSwap). An
//                                     experiment, not a rule; the seat map it keeps is not in the state hash.
// And for the audio reset of a level start (#289):
//   SF64_COOP_TEST_AUDIO_RESET_RUNS_ON=1  a level start does not play the sound ahead until no audio reset is running
//                                     (CoopLevelStart_Enter), so a run can show what that is there for
// With none of them set, nothing changes.

#ifdef __cplusplus
extern "C" {
#endif

int CoopTestSwitches_Speed(void); // factor for the frame limiter: 1 unless SF64_COOP_TEST_SPEED asks for more
union Gfx* CoopTestSwitches_ListToDraw(union Gfx* commands); // commands, or an empty list with SF64_COOP_TEST_NO_DRAW
void CoopTestSwitches_NoteDraws(int draws); // called at the draw loop with the draws of this game frame
int CoopTestSwitches_TakeMostDraws(void);   // the most draws a game frame got since the last call
void CoopTestSwitches_Init(void);
// Host: what a level start carries for the RNG, in the order of level_start.c, if SF64_COOP_TEST_START_SEEDS says so,
// and for the radio, if SF64_COOP_TEST_START_NO_TITLE_RADIO does.
void CoopTestSwitches_StartSeeds(int* values);

#ifdef __cplusplus
}
#endif
