#pragma once

// The handover in the game (#189), see handover.h: connects it to the session, the game's own folder, the co-op log
// and the desync report. menu_game.c and the test join driver run it before they close a session, and pass on what
// they hear of another game's handover while the session runs.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "port/coop/session/session.h"

#ifdef __cplusplus
extern "C" {
#endif

// Before a session closes: this game's desync report of the session and the rest of its co-op log go to every player
// still connected, and theirs come into this game's own folder. Blocks until everybody has everything, or for
// COOP_HANDOVER_MS at most (SF64_COOP_TEST_HANDOVER_MS=<ms> for tests, 0 to 60000), and pumps SDL's events meanwhile
// so the window stays alive. Returns at once with nobody connected, and inside a handover. leaving: the player chose
// to leave, which the others are told.
void CoopHandoverGame_Run(CoopSession* s, bool leaving);

// A COOP_MSG_HANDOVER message while the session runs: another game has begun to end the session. True, with the text
// for the player, if this session has to end now too and hand over: in a level every player is needed, and a client's
// only peer is its host. A host before the level answers that client and goes on with the others: false. Anything but
// a BEGIN is dropped here (false); what follows a BEGIN belongs to the handover it starts.
bool CoopHandoverGame_Heard(CoopSession* s, int peer, const uint8_t* data, uint16_t size, char* text, size_t cap);

#ifdef __cplusplus
}
#endif
