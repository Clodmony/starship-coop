#ifndef COOP_GAME_NAMES_H
#define COOP_GAME_NAMES_H

// Who flies which ship, for the eyes only (#257, #258): each player's name, drawn over its Arwing on the other players'
// screens (view.c) and in its row of the hits (score.c). The owner chose the name as the only mark, with no letter.
//
// Display only. Nothing here is in the game state or the state hash, and nothing the simulation reads comes from here:
// two games with other names, or none, play the same game. The session carries the names (PROTOCOL.md, SESSION_NAME);
// the menu and the join driver hand them in here. Outside co-op nothing reads them.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A name holds at most this many characters, from the game's small font, which draws upper case letters, digits, the
// space and four signs (sSmallChars, src/engine/fox_std_lib.c). Nothing else: no lower case, no accents.
#define COOP_NAME_MAX 8
#define COOP_NAME_CHARS " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.!:"

// Player `index`'s name, "" for none. Keeps at most COOP_NAME_MAX characters; the menu and the session hand in only
// names that hold to COOP_NAME_CHARS.
void CoopNames_Set(int32_t index, const char* name);
// What player `index` typed, "" if nothing.
const char* CoopNames_Typed(int32_t index);
// Player `index`'s name as the others read it: the typed name, else the character's name ("FALCO", "FOX").
const char* CoopNames_Label(int32_t index);

#ifdef __cplusplus
}
#endif

#endif
