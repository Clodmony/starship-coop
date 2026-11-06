#pragma once

// The co-op menu without ImGui (WP-25): what a player can do (host, join, pick a character, leave) and what the window
// shows (status, players, ping). menu_window.cpp draws a CoopMenuView and calls these functions; tools/coop/menutest
// runs them with no game and no window.
//
// Without a session nothing here does anything: CoopMenu_Tick returns at once, and no socket is open.
//
// Level start (WP-24, #22) is not here. Two places wait for it: Menu_OnEvent in menu.c, the one reader of the session's
// events, and DrawLevelStart in menu_window.cpp, where its start button goes.

#include <stdbool.h>
#include <stdint.h>

#include "port/coop/net/transport.h"
#include "port/coop/session/identity.h"
#include "port/coop/session/session.h"

#ifdef __cplusplus
extern "C" {
#endif

// What the port field starts with. Nothing depends on it: any free port works, on both sides.
#define COOP_MENU_DEFAULT_PORT 46464
#define COOP_MENU_ADDRESS_SIZE 128
#define COOP_MENU_LINE_SIZE 192
// Falco, Slippy, Peppy: what players 2 to 4 pick from (decision 0005).
#define COOP_MENU_CHOICES 3

typedef enum {
    COOP_MENU_IDLE,       // no session
    COOP_MENU_HOSTING,    // we are the host
    COOP_MENU_CONNECTING, // client: on the way in, no answer from the host yet
    COOP_MENU_JOINED,     // client: the host let us in
} CoopMenuPhase;

// What the menu needs from outside. The game passes the SDL2_net transport and CoopIdentity_FromGame; a test can pass
// anything else.
typedef struct {
    CoopTransport* (*host)(uint16_t port, const CoopNetConfig* config);
    CoopTransport* (*connect)(const char* host, uint16_t port, const CoopNetConfig* config);
    uint16_t (*port)(CoopTransport* t); // the port a host transport listens on
    void (*identity)(CoopIdentity* out);
} CoopMenuBackend;

typedef struct {
    const CoopMenuBackend* backend;
    CoopSession* session;
    CoopMenuPhase phase;
    uint16_t port; // the port we listen on, or the one we dialled
    char address[COOP_MENU_ADDRESS_SIZE];
    uint8_t self;        // our player index
    uint8_t pendingPick; // the CoopCharacter we asked for and got no answer on, 0 if none
    char message[COOP_SESSION_TEXT];
} CoopMenu;

typedef struct {
    bool present;      // somebody is in this slot
    uint8_t character; // a CoopCharacter
    bool hasPing;      // the round trip to this player is known
    uint32_t pingMs;
    char text[COOP_MENU_LINE_SIZE]; // the whole line for the player to read
} CoopMenuPlayer;

typedef struct {
    uint8_t character; // a CoopCharacter
    bool enabled;      // the player can choose it right now
    char label[COOP_MENU_LINE_SIZE];
} CoopMenuChoice;

typedef struct {
    CoopMenuPhase phase;
    char status[COOP_MENU_LINE_SIZE];
    char message[COOP_SESSION_TEXT]; // why the last thing went wrong, or what just happened; "" if nothing
    CoopMenuPlayer players[COOP_SESSION_PLAYERS];
    bool showChoices; // false on a host: player 1 is always Fox
    CoopMenuChoice choices[COOP_MENU_CHOICES];
} CoopMenuView;

void CoopMenu_Init(CoopMenu* m, const CoopMenuBackend* backend);
// Start hosting on portText, 0 for a free port. False if nothing started; the message says why.
bool CoopMenu_Host(CoopMenu* m, const char* portText);
// Start joining. A host name is resolved here, which can block on a DNS lookup (transport.h). False if nothing started;
// the message says why.
bool CoopMenu_Join(CoopMenu* m, const char* address, const char* portText, uint32_t nowMs);
// Ask the host for a character. False, with nothing sent, unless it is one this player can choose right now, so a
// character someone holds cannot be chosen. Two players can still ask for one at the same moment: the host then
// refuses the slower one, which shows up in the message.
bool CoopMenu_Pick(CoopMenu* m, uint8_t character);
// Close the session, if there is one.
void CoopMenu_Leave(CoopMenu* m);
// Once per frame, whatever the window shows.
void CoopMenu_Tick(CoopMenu* m, uint32_t nowMs);
void CoopMenu_View(const CoopMenu* m, CoopMenuView* out);

#ifdef __cplusplus
}
#endif
