#include "names.h"

#include <string.h>

#include "players.h"
#include "port/coop/session/session.h" // CoopCharacter

static char sNames[COOP_PLAYERS_MAX][COOP_NAME_MAX + 1];

void CoopNames_Set(int32_t index, const char* name) {
    if ((index < 0) || (index >= COOP_PLAYERS_MAX)) {
        return;
    }
    if (name == NULL) {
        name = "";
    }
    strncpy(sNames[index], name, COOP_NAME_MAX);
    sNames[index][COOP_NAME_MAX] = '\0';
}

const char* CoopNames_Typed(int32_t index) {
    if ((index < 0) || (index >= COOP_PLAYERS_MAX)) {
        return "";
    }
    return sNames[index];
}

// Player `index`'s character. Without a roster (the debug player count) players 1 to 3 are Falco, Slippy and Peppy, as
// CoopScore_Face has them.
static int32_t CoopNames_Character(int32_t index) {
    int32_t character = CoopPlayers_Character(index);

    if (character >= COOP_CHARACTER_FOX) {
        return character;
    }
    switch (index) {
        case 0:
            return COOP_CHARACTER_FOX;
        case 1:
            return COOP_CHARACTER_FALCO;
        case 2:
            return COOP_CHARACTER_SLIPPY;
        default:
            return COOP_CHARACTER_PEPPY;
    }
}

const char* CoopNames_Label(int32_t index) {
    if (CoopNames_Typed(index)[0] != '\0') {
        return CoopNames_Typed(index);
    }
    switch (CoopNames_Character(index)) {
        case COOP_CHARACTER_FALCO:
            return "FALCO";
        case COOP_CHARACTER_SLIPPY:
            return "SLIPPY";
        case COOP_CHARACTER_PEPPY:
            return "PEPPY";
        default:
            return "FOX";
    }
}
