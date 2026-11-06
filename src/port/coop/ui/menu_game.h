#pragma once

// What the co-op menu needs from the game itself (#126): menu.c runs with no game in tools/coop/menutest, and the
// event system's macros are C, so this is neither there nor in menu_window.cpp.
//
// The menu's session runs on GamePreUpdateEvent, as src/port/coop/test/join_driver.c does: before any game logic of the
// frame, so CoopLevelStart_Enter's change of level is taken up by Game_SetGameState on the same frame and nothing runs
// between the values and the level. The window's own update comes after Game_Update, with the audio and the timers of
// the frame still ahead of the next one.

#include "menu.h"

#ifdef __cplusplus
extern "C" {
#endif

// Ticks menu on every game frame from here on. Without a session that is a function that returns at once.
void CoopMenuGame_Listen(CoopMenu* menu);
// CoopMenuBackend.levelStart: the first level of the campaign, the only one run so far (docs/coop/NOTES.md, #126).
void CoopMenuGame_Level(CoopLevelStart* out);
// CoopMenuBackend.sessionEnded: the next level is the game's own again, with no roster to count players from.
void CoopMenuGame_SessionEnded(void);

#ifdef __cplusplus
}
#endif
