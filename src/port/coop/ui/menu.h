#pragma once

// The co-op menu without ImGui (WP-25): what a player can do (host, join, pick a character, start the level, leave) and
// what the window shows (status, players, ping). menu_window.cpp draws a CoopMenuView and calls these functions;
// tools/coop/menutest runs them with no game and no window.
//
// Without a session nothing here does anything: CoopMenu_Tick returns at once, and no socket is open.
//
// The host's Start asks the session, and the session tells every game, the host's too. So nobody enters a level from a
// click: every game enters it from the event, in CoopMenu_Tick.

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
// How long a game hears nothing from another before it drops it. A game that loads a level hears nothing while it
// loads: the longest such stall measured was 167 ms (three games on one PC, #132, docs/coop/NOTES.md). Ten seconds
// leaves room for slower PCs and disks; a player who is really gone shows as gone after that long.
#define COOP_MENU_TIMEOUT_MS 10000
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
    void (*levelStart)(CoopLevelStart* out);         // host: what to start, from this game as it is now
    void (*enterLevel)(const CoopLevelStart* start); // every game: go there; comes from inside CoopMenu_Tick
    void (*sessionEnded)(void);                      // the session is over, however it went
} CoopMenuBackend;

typedef struct {
    const CoopMenuBackend* backend;
    CoopSession* session;
    CoopMenuPhase phase;
    uint16_t port; // the port we listen on, or the one we dialled
    char address[COOP_MENU_ADDRESS_SIZE];
    uint8_t self;        // our player index
    uint8_t pendingPick; // the CoopCharacter we asked for and got no answer on, 0 if none
    bool levelStarted;   // the session said so: nothing left to pick or start
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
    bool showStart; // a host until the level has started
    bool canStart;  // the host may start now
    char startLabel[COOP_MENU_LINE_SIZE];
    char level[COOP_MENU_LINE_SIZE]; // where the level stands for this player; "" for a host that can still start
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
// Host: start the level for everyone. False, with the game not even asked, unless the view says canStart.
bool CoopMenu_Start(CoopMenu* m);
// Close the session, if there is one.
void CoopMenu_Leave(CoopMenu* m);
// Once per frame, whatever the window shows. The level start comes out of here, so call it where a game can enter one.
void CoopMenu_Tick(CoopMenu* m, uint32_t nowMs);
void CoopMenu_View(const CoopMenu* m, CoopMenuView* out);

#ifdef __cplusplus
}
#endif
