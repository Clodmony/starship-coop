// The co-op menu without ImGui, see menu.h.

#ifdef USE_NETWORKING

#include "menu.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t sChoices[COOP_MENU_CHOICES] = { COOP_CHARACTER_FALCO, COOP_CHARACTER_SLIPPY,
                                                     COOP_CHARACTER_PEPPY };

static void Menu_Say(CoopMenu* m, const char* format, ...) {
    va_list args;

    va_start(args, format);
    vsnprintf(m->message, sizeof(m->message), format, args);
    va_end(args);
}

// strtol takes signs and spaces, a port field should not. A number too large for long comes back as LONG_MAX.
static bool Port_Read(const char* text, long lowest, uint16_t* out) {
    char* end;
    long value;

    if (text == NULL || !isdigit((unsigned char) text[0])) {
        return false;
    }
    value = strtol(text, &end, 10);
    if (*end != '\0' || value < lowest || value > 65535) {
        return false;
    }
    *out = (uint16_t) value;
    return true;
}

// The rest stays at the transport's defaults.
static const CoopNetConfig kNetConfig = { COOP_MENU_TIMEOUT_MS, 0, 0 };

static void Menu_EndSession(CoopMenu* m, bool leaving) {
    if (m->backend->handover != NULL) {
        m->backend->handover(m->session, leaving);
    }
    CoopSession_Close(m->session);
    m->session = NULL;
    m->phase = COOP_MENU_IDLE;
    m->pendingPick = 0;
    m->levelStarted = false;
    m->recovering = false;
    m->rejoining = false;
    m->backend->sessionEnded();
}

// The connection to the host went in a level, without the host saying that the session is over: go back to it on a new
// connection, as the player this game was (the session survives). True if that is under way; the session's events
// say how it ends. Once per loss: a session that came back and is lost again before its level start ends.
static bool Menu_Rejoin(CoopMenu* m, CoopSessionEnd end) {
    CoopIdentity identity;
    CoopTransport* t;
    uint64_t id;

    if (m->phase != COOP_MENU_JOINED || !m->levelStarted || m->rejoining || m->character == 0 ||
        (end != COOP_SESSION_END_TIMEOUT && end != COOP_SESSION_END_CLOSED)) {
        return false;
    }
    id = CoopSession_Id(m->session);
    if (id == 0) {
        return false;
    }
    t = m->backend->connect(m->address, m->port, &kNetConfig);
    if (t == NULL) {
        return false;
    }
    // The old connection is gone, so there is nobody to hand anything over to.
    CoopSession_Close(m->session);
    m->backend->identity(&identity);
    m->session = CoopSession_Rejoin(t, &identity, m->nowMs, id, m->character);
    if (m->session == NULL) {
        m->phase = COOP_MENU_IDLE;
        m->levelStarted = false;
        m->backend->sessionEnded();
        return true;
    }
    m->phase = COOP_MENU_CONNECTING;
    m->rejoining = true;
    m->lostEnd = end;
    m->recovering = true;
    m->eyes = true;
    if (m->backend->recover != NULL) {
        m->backend->recover();
    }
    Menu_Say(m, "The connection to the host was lost. Trying to come back to the session.");
    return true;
}

void CoopMenu_Init(CoopMenu* m, const CoopMenuBackend* backend) {
    memset(m, 0, sizeof(*m));
    m->backend = backend;
}

bool CoopMenu_Host(CoopMenu* m, const char* portText) {
    CoopTransport* t;
    CoopIdentity identity;
    uint16_t port;

    if (m->phase != COOP_MENU_IDLE) {
        return false;
    }
    m->message[0] = '\0';
    if (!Port_Read(portText, 0, &port)) {
        Menu_Say(m, "The port has to be a number from 0 to 65535. 0 picks a free one.");
        return false;
    }
    t = m->backend->host(port, &kNetConfig);
    if (t == NULL) {
        Menu_Say(m, "Could not open UDP port %u. Another program may be using it.", (unsigned) port);
        return false;
    }
    m->port = m->backend->port(t);
    m->backend->identity(&identity);
    m->session = CoopSession_Host(t, &identity);
    if (m->session == NULL) {
        Menu_Say(m, "Could not start the session.");
        return false;
    }
    if (m->backend->sessionId != NULL) {
        CoopSession_SetId(m->session, m->backend->sessionId());
    }
    m->phase = COOP_MENU_HOSTING;
    m->self = 0;
    return true;
}

bool CoopMenu_Join(CoopMenu* m, const char* address, const char* portText, uint32_t nowMs) {
    CoopTransport* t;
    CoopIdentity identity;
    uint16_t port;

    if (m->phase != COOP_MENU_IDLE) {
        return false;
    }
    m->message[0] = '\0';
    if (address == NULL || address[0] == '\0') {
        Menu_Say(m, "Enter the host's address.");
        return false;
    }
    if (!Port_Read(portText, 1, &port)) {
        Menu_Say(m, "The port has to be a number from 1 to 65535, the one the host shows.");
        return false;
    }
    t = m->backend->connect(address, port, &kNetConfig);
    if (t == NULL) {
        Menu_Say(m, "Could not reach %s. Check the address.", address);
        return false;
    }
    m->backend->identity(&identity);
    m->session = CoopSession_Join(t, &identity, nowMs);
    if (m->session == NULL) {
        Menu_Say(m, "Could not start the session.");
        return false;
    }
    snprintf(m->address, sizeof(m->address), "%s", address);
    m->port = port;
    m->phase = COOP_MENU_CONNECTING;
    return true;
}

// The index of the player who holds character, -1 if nobody does.
static int Roster_Holder(const CoopRoster* roster, uint8_t character) {
    int i;

    for (i = 0; i < COOP_SESSION_PLAYERS; i++) {
        if (roster->character[i] == character) {
            return i;
        }
    }
    return -1;
}

static bool Choice_Is(uint8_t character) {
    int i;

    for (i = 0; i < COOP_MENU_CHOICES; i++) {
        if (sChoices[i] == character) {
            return true;
        }
    }
    return false;
}

// This is the one rule for what can be chosen: the view shows it, and CoopMenu_Pick holds to it.
static bool Menu_CanPick(const CoopMenu* m, const CoopRoster* roster, uint8_t character) {
    return m->phase == COOP_MENU_JOINED && !m->levelStarted && roster != NULL && m->pendingPick == 0 &&
           Choice_Is(character) && Roster_Holder(roster, character) < 0;
}

bool CoopMenu_Pick(CoopMenu* m, uint8_t character) {
    if (m->session == NULL || !Menu_CanPick(m, CoopSession_Roster(m->session), character)) {
        return false;
    }
    m->message[0] = '\0';
    if (!CoopSession_Pick(m->session, (CoopCharacter) character)) {
        Menu_Say(m, "Could not ask the host. Try again.");
        return false;
    }
    m->pendingPick = character;
    return true;
}

// This is the one rule for the start button: the view shows it, and CoopMenu_Start holds to it. The session decides.
static bool Menu_CanStart(const CoopMenu* m) {
    return m->phase == COOP_MENU_HOSTING && CoopSession_CanStart(m->session);
}

bool CoopMenu_Start(CoopMenu* m) {
    CoopLevelStart start;

    if (!Menu_CanStart(m)) {
        return false;
    }
    m->message[0] = '\0';
    m->backend->levelStart(&start);
    if (!CoopSession_StartLevel(m->session, &start)) {
        Menu_Say(m, "Could not start the level.");
        return false;
    }
    return true;
}

void CoopMenu_Leave(CoopMenu* m) {
    if (m->session != NULL) {
        Menu_EndSession(m, true);
        m->message[0] = '\0';
    }
}

void CoopMenu_Fail(CoopMenu* m, const char* text) {
    if (m->session != NULL) {
        Menu_Say(m, "%s", text);
        m->lost = m->levelStarted;
        Menu_EndSession(m, false);
    }
}

bool CoopMenu_WantsEyes(CoopMenu* m) {
    int32_t player;
    bool lost = m->lost || m->eyes;

    m->lost = false;
    m->eyes = false;
    return lost || (m->session != NULL && m->backend->waiting(&player) != 0);
}

static void Menu_OnEvent(CoopMenu* m, const CoopSessionEvent* ev) {
    const CoopRoster* roster;

    switch (ev->type) {
        case COOP_SESSION_EV_JOINED:
            if (m->phase == COOP_MENU_CONNECTING) {
                m->phase = COOP_MENU_JOINED;
                m->self = ev->playerIndex;
                Menu_Say(m, "%s", ev->text);
            } else {
                Menu_Say(m, "Player %d joined.\n%s", ev->playerIndex + 1, ev->text);
            }
            break;
        case COOP_SESSION_EV_REFUSED:
            if (m->phase == COOP_MENU_HOSTING) {
                Menu_Say(m, "A player was refused.\n%s", ev->text);
            } else {
                Menu_Say(m, "%s", ev->text);
            }
            break;
        case COOP_SESSION_EV_LEFT:
            if (m->levelStarted && CoopSession_Recovering(m->session)) {
                // The level starts again for whoever is there: the RECOVER event says so, and what the player reads.
            } else if (m->levelStarted) {
                char text[COOP_SESSION_TEXT];

                // Every frame needs every player's input, so nobody can carry on without this one.
                snprintf(text, sizeof(text), "Player %d left, so the session is over. %s", ev->peer + 2, ev->text);
                CoopMenu_Fail(m, text);
            } else {
                Menu_Say(m, "Player %d left. %s", ev->peer + 2, ev->text);
            }
            break;
        case COOP_SESSION_EV_ENDED:
            if (Menu_Rejoin(m, ev->end)) {
                break;
            }
            // After a refusal the REFUSED event has already said more than this one can. A game that tried to come
            // back and reached nobody says how it lost its host, as it would have without trying.
            if (m->rejoining && (ev->end == COOP_SESSION_END_TIMEOUT || ev->end == COOP_SESSION_END_CLOSED)) {
                Menu_Say(m, "%s", CoopSession_EndText(m->lostEnd));
            } else if (ev->end != COOP_SESSION_END_REFUSED) {
                Menu_Say(m, "%s", ev->text);
            }
            m->lost = m->levelStarted;
            Menu_EndSession(m, false);
            break;
        case COOP_SESSION_EV_ROSTER:
            roster = CoopSession_Roster(m->session);
            if (roster != NULL && roster->character[m->self] == m->pendingPick) {
                m->pendingPick = 0;
            }
            break;
        case COOP_SESSION_EV_PICK_REFUSED:
            if (m->phase == COOP_MENU_HOSTING) {
                Menu_Say(m, "Player %d: %s", ev->peer + 2, ev->text);
            } else {
                m->pendingPick = 0;
                Menu_Say(m, "%s", ev->text);
            }
            break;
        case COOP_SESSION_EV_LEVEL_START:
            // A pick still on its way is dropped, and no answer comes. What this player holds is in the level start.
            m->pendingPick = 0;
            m->levelStarted = true;
            m->recovering = false;
            m->rejoining = false;
            m->character = ev->levelStart->roster[m->self];
            m->backend->enterLevel(ev->levelStart);
            break;
        case COOP_SESSION_EV_RECOVER:
            // One line, and the window comes up by itself to show it (CoopMenu_WantsEyes).
            Menu_Say(m, "%s", ev->text);
            m->recovering = true;
            m->eyes = true;
            if (m->backend->recover != NULL) {
                m->backend->recover();
            }
            break;
        case COOP_SESSION_EV_MESSAGE:
            // Nothing above the session range is for the menu. This switch has no default, so the compiler names a new
            // event.
            m->backend->message(ev->peer, ev->msgId, ev->data, ev->size);
            break;
    }
}

void CoopMenu_Tick(CoopMenu* m, uint32_t nowMs) {
    CoopSessionEvent ev;

    m->nowMs = nowMs;
    while (m->session != NULL && CoopSession_Poll(m->session, nowMs, &ev)) {
        Menu_OnEvent(m, &ev);
    }
}

// A host measures the round trip to each client, a client only the one to the host.
static int View_PingPeer(const CoopMenu* m, int player) {
    if (m->phase == COOP_MENU_HOSTING) {
        return player > 0 ? player - 1 : -1;
    }
    return player == 0 ? 0 : -1;
}

static void View_Player(const CoopMenu* m, const CoopRoster* roster, int player, CoopMenuPlayer* out) {
    const char* name;
    const char* you = player == m->self ? " (you)" : "";
    char ping[32] = "";
    int peer = View_PingPeer(m, player);

    out->character = roster->character[player];
    out->present = out->character != COOP_CHARACTER_NO_PLAYER;
    if (!out->present) {
        return;
    }
    name = out->character == COOP_CHARACTER_UNPICKED ? "no character yet" : CoopSession_CharacterName(out->character);
    if (peer >= 0) {
        CoopNetStats stats;

        // The transport gives 0 until the first pong.
        out->hasPing = CoopSession_Stats(m->session, peer, &stats) && stats.rttMs != 0;
        if (out->hasPing) {
            out->pingMs = stats.rttMs;
            snprintf(ping, sizeof(ping), ", ping %u ms", (unsigned) stats.rttMs);
        } else {
            snprintf(ping, sizeof(ping), ", ping not measured yet");
        }
    }
    snprintf(out->text, sizeof(out->text), "Player %d%s: %s%s", player + 1, you, name, ping);
}

static void View_Choice(const CoopMenu* m, const CoopRoster* roster, uint8_t character, CoopMenuChoice* out) {
    const char* name = CoopSession_CharacterName(character);
    int holder = Roster_Holder(roster, character);

    out->character = character;
    out->enabled = Menu_CanPick(m, roster, character);
    if (character == m->pendingPick) {
        snprintf(out->label, sizeof(out->label), "%s (asking the host)", name);
    } else if (holder == m->self) {
        snprintf(out->label, sizeof(out->label), "%s (you)", name);
    } else if (holder >= 0) {
        snprintf(out->label, sizeof(out->label), "%s (player %d)", name, holder + 1);
    } else {
        snprintf(out->label, sizeof(out->label), "%s", name);
    }
}

void CoopMenu_View(const CoopMenu* m, CoopMenuView* out) {
    const CoopRoster* roster = m->session != NULL ? CoopSession_Roster(m->session) : NULL;
    int players = 0;
    int i;

    memset(out, 0, sizeof(*out));
    out->phase = m->phase;
    snprintf(out->message, sizeof(out->message), "%s", m->message);
    if (roster != NULL) {
        for (i = 0; i < COOP_SESSION_PLAYERS; i++) {
            View_Player(m, roster, i, &out->players[i]);
            players += out->players[i].present;
        }
    }
    if (m->phase == COOP_MENU_JOINED && !m->levelStarted && roster != NULL) {
        out->showChoices = true;
        for (i = 0; i < COOP_MENU_CHOICES; i++) {
            View_Choice(m, roster, sChoices[i], &out->choices[i]);
        }
    }
    out->showStart = m->phase == COOP_MENU_HOSTING && !m->levelStarted;
    out->canStart = Menu_CanStart(m);
    snprintf(out->startLabel, sizeof(out->startLabel), "%s",
             out->canStart ? "Start the level" : "Start the level (every player has to choose first)");
    if (m->levelStarted) {
        int32_t player = 0;
        uint32_t waited = m->backend->waiting(&player);

        if (m->recovering) {
            snprintf(out->level, sizeof(out->level), "Waiting for the level to start again.");
        } else if (waited != 0) {
            snprintf(out->level, sizeof(out->level), "Waiting for player %d (%u s).", (int) player + 1,
                     (unsigned) (waited / 1000));
        } else {
            snprintf(out->level, sizeof(out->level), "The level has started.");
        }
    } else if (m->phase == COOP_MENU_JOINED) {
        snprintf(out->level, sizeof(out->level), "Waiting for the host to start the level.");
    }

    switch (m->phase) {
        case COOP_MENU_IDLE:
            snprintf(out->status, sizeof(out->status), "Not in a session.");
            break;
        case COOP_MENU_HOSTING:
            snprintf(out->status, sizeof(out->status), "Hosting on port %u, %d of %d players.", (unsigned) m->port,
                     players, COOP_SESSION_PLAYERS);
            break;
        case COOP_MENU_CONNECTING:
            snprintf(out->status, sizeof(out->status), "%s %s:%u...", m->rejoining ? "Coming back to" : "Connecting to",
                     m->address, (unsigned) m->port);
            break;
        case COOP_MENU_JOINED:
            snprintf(out->status, sizeof(out->status), "In the session at %s:%u as player %d.", m->address,
                     (unsigned) m->port, m->self + 1);
            break;
    }
}

#endif
