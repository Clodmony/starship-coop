// What the co-op menu needs from the game itself, see menu_game.h.

#ifdef USE_NETWORKING

#include "menu_game.h"

#include <SDL2/SDL.h>

#include "global.h"
#include "port/coop/game/players.h"
#include "port/coop/sync/lockstep_game.h"
#include "port/coop/test/switches.h"
#include "port/hooks/Events.h"

static CoopMenu* sMenu = NULL;

static void CoopMenuGame_OnGamePreUpdate(IEvent* event) {
    (void) event;
    // The logo adds 1 to gGameState after a second of real time (Timer_CreateTask in Game_Update). A level start in that
    // second would turn GSTATE_PLAY into GSTATE_ENDING, so nothing reaches the game before that is over. A window opens
    // that fast only for a script, and the session waits for less than the transport's timeout.
    if ((gGameState >= GSTATE_BOOT) && (gGameState <= GSTATE_LOGO_WAIT)) {
        return;
    }
    CoopMenu_Tick(sMenu, SDL_GetTicks());
}

void CoopMenuGame_Listen(CoopMenu* menu) {
    sMenu = menu;
    // After the state hash (EVENT_PRIORITY_NORMAL): a frame's hash shows what came before this frame's session.
    REGISTER_LISTENER(GamePreUpdateEvent, CoopMenuGame_OnGamePreUpdate, EVENT_PRIORITY_HIGH);
}

void CoopMenuGame_Level(CoopLevelStart* out) {
    CoopLevelStart_FromGame(out, LEVEL_CORNERIA, 0);
    CoopTestSwitches_StartSeeds(out->values);
}

static bool Link_Send(int peer, uint16_t msgId, const void* data, uint16_t size, CoopNetLane lane) {
    return (sMenu->session != NULL) && CoopSession_Send(sMenu->session, peer, msgId, data, size, lane);
}

static void Link_Pump(void) {
    CoopMenu_Tick(sMenu, SDL_GetTicks());
}

static void Link_Fail(const char* text) {
    CoopMenu_Fail(sMenu, text);
}

static const CoopSyncLink sLink = { Link_Send, Link_Pump, Link_Fail };

void CoopMenuGame_EnterLevel(const CoopLevelStart* start) {
    CoopLevelStart_Enter(start);
    CoopSyncGame_Begin(&sLink, start->roster, sMenu->self, sMenu->phase == COOP_MENU_HOSTING);
}

void CoopMenuGame_SessionEnded(void) {
    CoopSyncGame_End();
    CoopPlayers_SetRoster(NULL);
}

void CoopMenuGame_Message(int peer, uint16_t msgId, const uint8_t* data, uint16_t size) {
    CoopSyncGame_Receive(peer, msgId, data, size);
}

uint32_t CoopMenuGame_Waiting(int32_t* player) {
    return CoopSyncGame_Waiting(player);
}

#endif
