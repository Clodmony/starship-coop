// What the co-op menu needs from the game itself, see menu_game.h.

#ifdef USE_NETWORKING

#include "menu_game.h"

#include <SDL2/SDL.h>

#include "global.h"
#include "port/coop/game/players.h"
#include "port/coop/test/menu_driver.h"
#include "port/hooks/Events.h"

static CoopMenu* sMenu = NULL;

static void CoopMenuGame_OnGamePreUpdate(IEvent* event) {
    uint32_t now = SDL_GetTicks();

    (void) event;
    // Times the gaps the other games see, for menu_test.py; nothing without SF64_COOP_TEST_MENU.
    CoopMenuDriver_Tick(now, sMenu->session != NULL);
    CoopMenu_Tick(sMenu, now);
}

void CoopMenuGame_Listen(CoopMenu* menu) {
    sMenu = menu;
    // After the state hash (EVENT_PRIORITY_NORMAL): a frame's hash shows what came before this frame's session.
    REGISTER_LISTENER(GamePreUpdateEvent, CoopMenuGame_OnGamePreUpdate, EVENT_PRIORITY_HIGH);
}

void CoopMenuGame_Level(CoopLevelStart* out) {
    CoopLevelStart_FromGame(out, LEVEL_CORNERIA, 0);
}

void CoopMenuGame_SessionEnded(void) {
    CoopPlayers_SetRoster(NULL);
}

#endif
