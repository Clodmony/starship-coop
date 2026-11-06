#include "input_game.h"

#include "input.h"
#include "players.h"

#include "global.h"
#include "port/hooks/Events.h"

// Co-op off: the pads stay as Controller_UpdateInput and a replay left them, as upstream.
static void CoopInput_OnGamePreUpdate(IEvent* event) {
    (void) event;
    if (!CoopPlayers_IsCoop()) {
        CoopInput_Rest();
        return;
    }
    CoopInput_Route(CoopPlayers_Count(), CoopPlayers_LocalIndex(), gControllerHold, gControllerPress);
}

void CoopInput_Init(void) {
    // NORMAL: after the replay (LOW), which has this frame's pads ready by then.
    REGISTER_LISTENER(GamePreUpdateEvent, CoopInput_OnGamePreUpdate, EVENT_PRIORITY_NORMAL);
}
