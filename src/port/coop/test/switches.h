#pragma once

// Test switches for the determinism spike (WP-02). See docs/coop/QA.md.
//
// Each switch changes one thing that can differ between two machines running the same build, so its effect on the
// per-frame state hash can be measured on one PC. Off unless the environment variable is set when the game starts:
//   SF64_COOP_TEST_NO_FMA3=1          Windows x64 only: the C runtime's math functions (sinf, cosf, ...) take
//                                     their code path for CPUs without FMA3 (_set_FMA3_enable(0)).
//   SF64_COOP_TEST_AUDIO_SAMPLES=low  every audio update renders the low sample count (as if the output device
//   SF64_COOP_TEST_AUDIO_SAMPLES=high always had enough, or never had enough, buffered; src/port/Engine.cpp:387-388).
// With none of them set, nothing changes.

#ifdef __cplusplus
extern "C" {
#endif

void CoopTestSwitches_Init(void);

#ifdef __cplusplus
}
#endif
