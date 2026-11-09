// What the co-op menu needs from the game itself, see menu_game.h.

#ifdef USE_NETWORKING

#include "menu_game.h"

#include <SDL2/SDL.h>

#include "global.h"
#include "port/coop/game/players.h"
#include "port/coop/net/protocol.h"
#include "port/coop/sync/handover_game.h"
#include "port/coop/sync/lockstep_game.h"
#include "port/coop/test/menu_driver.h"
#include "port/coop/test/switches.h"
#include "port/hooks/Events.h"

static CoopMenu* sMenu = NULL;

static void CoopMenuGame_OnGamePreUpdate(IEvent* event) {
    uint32_t now;

    (void) event;
    // The logo adds 1 to gGameState after a second of real time (Timer_CreateTask in Game_Update). A level start in
    // that second would turn GSTATE_PLAY into GSTATE_ENDING, so nothing reaches the game before that is over. A window
    // opens that fast only for a script, and the session waits for less than the transport's timeout.
    if ((gGameState >= GSTATE_BOOT) && (gGameState <= GSTATE_LOGO_WAIT)) {
        return;
    }
    now = SDL_GetTicks();
    // Times the gaps the other games see, for menu_test.py; nothing without SF64_COOP_TEST_MENU.
    CoopMenuDriver_Tick(now, sMenu->session != NULL);
    CoopMenu_Tick(sMenu, now);
}

void CoopMenuGame_Listen(CoopMenu* menu) {
    sMenu = menu;
    // After the state hash (EVENT_PRIORITY_NORMAL): a frame's hash shows what came before this frame's session.
    REGISTER_LISTENER(GamePreUpdateEvent, CoopMenuGame_OnGamePreUpdate, EVENT_PRIORITY_HIGH);
}

// The levels the lobby offers (#211). Left out: Training and Versus, which are no campaign levels, and the two ids the
// game leaves unnamed (LEVEL_UNK_4, LEVEL_UNK_15). Phase 0 for all; the route state a campaign builds on the way is not
// there, and the map after a level started this way is untested (NOTES.md, #211).
static const struct {
    uint8_t level;
    const char* name;
} sStartLevels[] = {
    { LEVEL_CORNERIA, "Corneria" }, { LEVEL_METEO, "Meteo" },
    { LEVEL_SECTOR_Y, "Sector Y" }, { LEVEL_FORTUNA, "Fortuna" },
    { LEVEL_KATINA, "Katina" },     { LEVEL_AQUAS, "Aquas" },
    { LEVEL_SOLAR, "Solar" },       { LEVEL_ZONESS, "Zoness" },
    { LEVEL_SECTOR_X, "Sector X" }, { LEVEL_TITANIA, "Titania" },
    { LEVEL_MACBETH, "Macbeth" },   { LEVEL_SECTOR_Z, "Sector Z" },
    { LEVEL_BOLSE, "Bolse" },       { LEVEL_AREA_6, "Area 6" },
    { LEVEL_VENOM_1, "Venom" },     { LEVEL_VENOM_ANDROSS, "Venom (Andross)" },
    { LEVEL_VENOM_2, "Venom 2" },
};
static int32_t sStartIndex = 0; // Corneria

int32_t CoopMenuGame_StartLevelCount(void) {
    return (int32_t) ARRAY_COUNT(sStartLevels);
}

const char* CoopMenuGame_StartLevelName(int32_t index) {
    return ((index >= 0) && (index < CoopMenuGame_StartLevelCount())) ? sStartLevels[index].name : "";
}

int32_t CoopMenuGame_StartLevelIndex(void) {
    return sStartIndex;
}

void CoopMenuGame_SetStartLevel(int32_t index) {
    if ((index >= 0) && (index < CoopMenuGame_StartLevelCount())) {
        sStartIndex = index;
    }
}

void CoopMenuGame_Level(CoopLevelStart* out) {
    CoopLevelStart_FromGame(out, sStartLevels[sStartIndex].level, 0);
    CoopTestSwitches_StartSeeds(out->values);
}

static bool Link_Send(int peer, uint16_t msgId, const void* data, uint16_t size, CoopNetLane lane) {
    return (sMenu->session != NULL) && CoopSession_Send(sMenu->session, peer, msgId, data, size, lane);
}

// A frame that waits for input pumps the session from here, so this is a session tick too: without the driver's
// tick a lockstep wait would read as a stall, though the other games hear from this one the whole time.
static void Link_Pump(void) {
    uint32_t now = SDL_GetTicks();

    CoopMenuDriver_Tick(now, sMenu->session != NULL);
    CoopMenu_Tick(sMenu, now);
}

static void Link_Fail(const char* text) {
    CoopMenu_Fail(sMenu, text);
}

// For a desync report: this game's identity from the menu's backend, a peer's as the session holds it.
static void Link_Identity(int peer, char* out, size_t cap) {
    const CoopIdentity* id = NULL;
    CoopIdentity own;

    out[0] = '\0';
    if (peer < 0) {
        sMenu->backend->identity(&own);
        id = &own;
    } else if (sMenu->session != NULL) {
        id = CoopSession_PeerIdentity(sMenu->session, peer);
    }
    if (id != NULL) {
        CoopIdentity_Line(id, out, cap);
    }
}

static const CoopSyncLink sLink = { Link_Send, Link_Pump, Link_Fail, Link_Identity };

void CoopMenuGame_EnterLevel(const CoopLevelStart* start) {
    CoopLevelStart_Enter(start);
    CoopSyncGame_Begin(&sLink, start->roster, sMenu->self, sMenu->phase == COOP_MENU_HOSTING);
}

void CoopMenuGame_SessionEnded(void) {
    CoopSyncGame_End();
    CoopPlayers_SetRoster(NULL);
    sStartIndex = 0; // the next session starts at Corneria again unless its host picks another level (#211)
}

void CoopMenuGame_Message(int peer, uint16_t msgId, const uint8_t* data, uint16_t size) {
    char text[COOP_SESSION_TEXT];

    if (msgId == COOP_MSG_HANDOVER) {
        if ((sMenu->session != NULL) && CoopHandoverGame_Heard(sMenu->session, peer, data, size, text, sizeof(text))) {
            CoopMenu_Fail(sMenu, text);
        }
        return;
    }
    CoopSyncGame_Receive(peer, msgId, data, size);
}

void CoopMenuGame_Handover(CoopSession* s, bool leaving) {
    CoopSyncGame_End();
    CoopHandoverGame_Run(s, leaving);
}

uint32_t CoopMenuGame_Waiting(int32_t* player) {
    return CoopSyncGame_Waiting(player);
}

#endif
