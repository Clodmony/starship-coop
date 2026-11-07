#pragma once

// Connects lockstep.c to the game (WP-22). Whoever holds the session, the co-op menu or a test driver, starts it when a
// level starts and feeds it what the session hears; sync sits below the session, so it reaches it only through
// CoopSyncLink. See docs/coop/PROTOCOL.md, Lockstep.
//
// A level start puts every game on its way into the level. Each one gets there in its own time, so frame 0 is the first
// frame after Play_Init, the one join_test.py compares from. From there on every game frame belongs to lockstep, in
// menus and on the map too: a frame runs when every player's input for it is here, or it does not run at all. Then
// push_frame() does nothing but keep the window alive, and no audio frame runs either, because radio lines follow the
// sound thread (Audio_GetCurrentVoiceStatus) and that has to stay in step with the game frames.
//
// Outside a session of two or more players nothing here does anything, and CoopSyncGame_HoldFrame returns 0 at once.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "port/coop/net/transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool (*send)(int peer, uint16_t msgId, const void* data, uint16_t size, CoopNetLane lane);
    // Polls the session, whose messages come back through CoopSyncGame_Receive. A frame that waits needs this, as the
    // listener that usually does it runs inside the frame.
    void (*pump)(void);
    // Lockstep cannot go on: end the session and show this to the player. CoopSyncGame_End follows from there.
    void (*fail)(const char* text);
    // One line on a game's identity for a desync report (WP-23): peer -1 is this game, as the session knows them. May
    // be NULL, or leave "" when it does not know.
    void (*identity)(int peer, char* out, size_t cap);
} CoopSyncLink;

void CoopSyncGame_Init(void);

// The session started a level, with these CoopCharacters by player index, and this game is player `self`. The roster
// tells who plays, and the local player's camera, HUD and controller follow `self` from here on.
void CoopSyncGame_Begin(const CoopSyncLink* link, const uint8_t* roster, int32_t self, bool host);

// A message above the session range. Ids that are not lockstep's are left alone.
void CoopSyncGame_Receive(int peer, uint16_t msgId, const uint8_t* data, uint16_t size);

// The session is over, for whatever reason. Input goes back to the local controller, the others idle, and the level
// goes on in this game.
void CoopSyncGame_End(void);

// First thing of a frame. Nonzero means this frame does not run.
int32_t CoopSyncGame_HoldFrame(void);

// Nonzero from the level start to the end of the session: something only this game does would part it from the others.
int32_t CoopSyncGame_IsOn(void);

// The frame that runs next, -1 while no frame belongs to lockstep. Frame 0 is the first one in the level.
int32_t CoopSyncGame_Frame(void);

// How long, in ms, this game has been waiting for player `*player`; 0 if it is not waiting.
uint32_t CoopSyncGame_Waiting(int32_t* player);

// Test only. What `player` does on the current frame; the input delay in use; a send that goes nowhere from now on,
// with the connection left up; the frames that had to wait and the ms they took, since the game started; no lockstep at
// all, as a control.
void CoopSyncGame_Read(int32_t player, void* frame);
uint32_t CoopSyncGame_Delay(void);
void CoopSyncGame_Mute(void);
uint32_t CoopSyncGame_HeldFrames(void);
uint32_t CoopSyncGame_HeldMs(void);
void CoopSyncGame_SetOff(bool off);

#ifdef __cplusplus
}
#endif
