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
    // By session player; the same as the slot unless the lead swap spike moved one (#183).
    CoopInput_Route(CoopPlayers_Count(), CoopPlayers_SeatIndex(), gControllerHold, gControllerPress);
    if (CoopPlayers_LeadSwapCount() > 0) {
        OSContPad hold[COOP_PLAYERS_MAX];
        OSContPad press[COOP_PLAYERS_MAX];
        s32 i;

        for (i = 0; i < COOP_PLAYERS_MAX; i++) {
            hold[i] = gControllerHold[CoopPlayers_SeatOfSlot(i)];
            press[i] = gControllerPress[CoopPlayers_SeatOfSlot(i)];
        }
        for (i = 0; i < COOP_PLAYERS_MAX; i++) {
            gControllerHold[i] = hold[i];
            gControllerPress[i] = press[i];
        }
    }
}

void CoopInput_Init(void) {
    // NORMAL: after the replay (LOW), which has this frame's pads ready by then.
    REGISTER_LISTENER(GamePreUpdateEvent, CoopInput_OnGamePreUpdate, EVENT_PRIORITY_NORMAL);
}
