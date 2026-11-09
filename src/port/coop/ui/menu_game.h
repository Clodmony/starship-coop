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
// CoopMenuBackend.levelStart: the level the host chose in the lobby, Corneria unless it picked another (#211).
void CoopMenuGame_Level(CoopLevelStart* out);
// The levels a host can start a session at (#211): the campaign's, in the order of its first routes. An index is a
// place in this list, not a LevelId. The choice is the host's alone and goes back to Corneria when the session ends.
int32_t CoopMenuGame_StartLevelCount(void);
const char* CoopMenuGame_StartLevelName(int32_t index);
int32_t CoopMenuGame_StartLevelIndex(void);
void CoopMenuGame_SetStartLevel(int32_t index);
// CoopMenuBackend.enterLevel: goes there, and from there on the frames of this game are in lockstep with the others.
void CoopMenuGame_EnterLevel(const CoopLevelStart* start);
// CoopMenuBackend.sessionEnded: the next level is the game's own again, with no roster to count players from.
void CoopMenuGame_SessionEnded(void);
// CoopMenuBackend.message and .waiting: what sync needs from the session, and what the window tells the player. A
// HANDOVER from another game that ends the session (#189) ends it here too, through CoopMenu_Fail.
void CoopMenuGame_Message(int peer, uint16_t msgId, const uint8_t* data, uint16_t size);
uint32_t CoopMenuGame_Waiting(int32_t* player);
// CoopMenuBackend.handover: lockstep ends first, so its last lines go along, then the handover (handover_game.h).
void CoopMenuGame_Handover(CoopSession* s, bool leaving);

#ifdef __cplusplus
}
#endif
