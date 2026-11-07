#pragma once

// Test switches for the determinism spike (WP-02). See docs/coop/QA.md.
//
// Each switch changes one thing that can differ between two machines running the same build, so its effect on the
// per-frame state hash can be measured on one PC. Off unless the environment variable is set when the game starts:
//   SF64_COOP_TEST_SPEED=max          no frame limit, each game frame is drawn once (GameEngine::ProcessGfxCommands,
//   SF64_COOP_TEST_SPEED=<n>          #137). With n > 1 the limit is n times the game's frame rate. Tests count frames,
//                                     not seconds; tools/coop/replay.py sets it for them.
//   SF64_COOP_TEST_NO_FMA3=1          Windows x64 only: the C runtime's math functions (sinf, cosf, ...) take
//                                     their code path for CPUs without FMA3 (_set_FMA3_enable(0)).
//   SF64_COOP_TEST_AUDIO_SAMPLES=low  every audio update renders the low sample count (as if the output device
//   SF64_COOP_TEST_AUDIO_SAMPLES=high always had enough, or never had enough, buffered; src/port/Engine.cpp:387-388).
//   SF64_COOP_TEST_LOCAL_INDEX=<i>    in co-op this client looks through player i (CoopPlayers_SetLocalIndex), which
//                                     two clients of one session would do. It changes what is drawn, not the input.
// One more, for a real session between two platforms, which the handshake otherwise refuses (decision 0002):
//   SF64_COOP_TEST_ANY_PLATFORM=1     on the host: a client that differs in platform and nothing else gets in, and
//                                     both players read what differs (CoopSession_AnyPlatform).
// And for tests of lockstep (src/port/coop/sync/lockstep_game.h, #20):
//   SF64_COOP_TEST_INPUT_DELAY=<n>    frames of input delay in place of COOP_LOCKSTEP_INPUT_DELAY
//   SF64_COOP_TEST_NO_LOCKSTEP=1      a level start starts no lockstep, so a run can show what it is there for
//   SF64_COOP_TEST_START_SEEDS=<a,b,c,d,e,f>  host: the level starts from these seeds, whatever frame Start came on
//   SF64_COOP_TEST_NO_DESYNC_CHECK=1  no state hashes go out or are compared in a session (desync_game.h, WP-23), so
//                                     a run can show what the check is there for
// With none of them set, nothing changes.

#ifdef __cplusplus
extern "C" {
#endif

int CoopTestSwitches_Speed(void); // factor for the frame limiter: 1 unless SF64_COOP_TEST_SPEED asks for more
void CoopTestSwitches_NoteDraws(int draws); // called at the draw loop with the draws of this game frame
int CoopTestSwitches_TakeMostDraws(void);   // the most draws a game frame got since the last call
void CoopTestSwitches_Init(void);
// Host: what a level start carries for the RNG, in the order of level_start.c, if SF64_COOP_TEST_START_SEEDS says so.
void CoopTestSwitches_StartSeeds(int* values);

#ifdef __cplusplus
}
#endif
