#pragma once

// An input source per player (WP-14). No game code in here, so tools/coop/inputtest can run it without a game;
// input_game.c connects it to the game.
//
// In co-op, once per frame and before any game logic, CoopInput_Route fills the pad slots by player number: slot i of
// gControllerHold and gControllerPress holds what player i does this frame. That is true on every client, whichever
// player is local, so game code that reads a slot by player number, or the slot of gMainController (player 0's), sees
// the same input on all of them.
//
//   - The local player answers to the local controller, which is port 0 on every client (NOTES.md, 2026-11-06, #68).
//   - Every other player follows its source. With no source, or nothing from it this frame, the player idles.
//   - Nothing else reaches a slot: ports 1 to 3 of this PC are dropped, and a slot with no player in it stays empty.
//
// A source is a replay file (src/port/coop/test/replay.c) or the network: in a session lockstep (src/port/coop/sync/
// lockstep_game.c) is the source of every player, the local one too, and sends what CoopInput_Controller returns. Replay
// frames and network frames carry the same thing, the pair the sender's player used, so a receiver has nothing to work
// out again and cannot work it out differently.

#include <libultra/controller.h>
#include <stdint.h>

#include "players.h"

#ifdef __cplusplus
extern "C" {
#endif

// What one player does on one frame.
typedef struct {
    OSContPad hold;  // as in gControllerHold: raw sticks
    OSContPad press; // as in gControllerPress: the buttons that went down this frame, sticks after the dead zone
} CoopInputFrame;

typedef struct {
    // Fills out with what `player` does this frame and returns nonzero, or returns 0 when it has nothing for this
    // frame. It cannot wait: this runs inside the frame, so a sync model that has to wait for a player waits before the
    // frame starts.
    int32_t (*read)(void* user, int32_t player, CoopInputFrame* out);
    void* user;
} CoopInputSource;

// Sets the source of `player`, NULL for none. The source must outlive its use. It is ignored for as long as `player`
// is the local player, so a source can be set before anyone knows who is local.
void CoopInput_SetSource(int32_t player, const CoopInputSource* source);

// Puts `source` in the place of the local controller, NULL to go back to it: a replay that flies every player, or a
// sync layer that delays the local player's own input.
void CoopInput_SetLocalSource(const CoopInputSource* source);

// Back to the start: no sources, the local player on the local controller.
void CoopInput_ClearSources(void);

// Takes `source` out wherever it is set, a player's or the local one, and leaves every other source where it is: for a
// source that ends while another layer may have put its own in since (#324: a replay that runs out in a session).
void CoopInput_ClearSource(const CoopInputSource* source);

// Fills the slots for this frame. `hold` and `press` are the four slots as Controller_UpdateInput left them, `count` is
// the number of players and `local` the local player's index.
void CoopInput_Route(int32_t count, int32_t local, OSContPad* hold, OSContPad* press);

// Call on frames without CoopInput_Route. A button already down when routing starts again is not a press.
void CoopInput_Rest(void);

// What the local controller did on the last routed frame, whoever it steers. This is what a client sends.
const CoopInputFrame* CoopInput_Controller(void);

// The entry of gControllerRumbleFlags for `player`. Controller_Rumble only serves entry 0, which is the local pad
// (src/sys/sys_joybus.c, the status bit), so the local player gets that one and player 0 takes its place.
int32_t CoopInput_RumbleSlot(int32_t player, int32_t local);

#ifdef __cplusplus
}
#endif
