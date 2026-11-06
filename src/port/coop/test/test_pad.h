#pragma once

// A gamepad that presses what a script says, for unattended tests of live input (#12). See docs/coop/QA.md.
//
// Off unless SF64_COOP_TEST_PAD=<file> is set when the game starts. Each line of the file is one step:
//
//   <frame> <port> <buttons> <stick x> <stick y>
//
// sorted by frame. A step holds until the next step of its port. Frames count like replay frames: 0 is the first frame
// in GSTATE_START. Buttons are OSContPad.button in hex, sticks are N64 stick units.
//
// Port 0 is an SDL virtual gamepad, so a step takes the road a real pad takes: SDL, the default mappings
// (src/port/Engine.cpp), libultraship's port 0, sNextController, Controller_UpdateInput. It reaches gControllerHold[0]
// two frames later (docs/coop/NOTES.md, 2026-11-06, #68). No real device can reach ports 1 to 3 on a fresh config, so a
// step on one of those is written to sNextController and shows up one frame later.

#ifdef __cplusplus
extern "C" {
#endif

void CoopTestPad_Init(void);

// The frame the script is on, -1 before the first one. Listeners that run after this one get this frame's number.
int CoopTestPad_Frame(void);

#ifdef __cplusplus
}
#endif
