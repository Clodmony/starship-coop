// Co-op session: handshake (WP-21), characters (WP-34) and level start (WP-24). Messages and their layout:
// docs/coop/PROTOCOL.md, ids 0x0010-0x0016.
//
// Host, per peer: WAIT_HELLO from the transport's CONNECTED, then JOINED or REFUSED. A refused peer stays until the
// refusal is acknowledged, the client goes, or COOP_SESSION_REFUSE_LINGER_MS pass; the transport's disconnect drops
// unacknowledged messages, so leaving at once would lose the refusal. Client: CONNECTING, WAIT_ANSWER, ACTIVE, ENDED.
//
// The host holds the roster. Whatever a joined peer is owed goes out at the start of a poll, and stays owed while the
// transport's window is full: a client may miss a roster that way, but not the last one.
//
// A level start is taken once: the host's values and the roster of that moment, so that a client leaving afterwards
// cannot make two games come to different player counts. Picks and joins stop there.

#ifdef USE_NETWORKING

#include "session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/coop/net/protocol.h"

typedef enum {
    PEER_FREE,
    PEER_WAIT_HELLO,
    PEER_JOINED,
    PEER_REFUSED,
} PeerState;

typedef enum {
    CLIENT_CONNECTING,
    CLIENT_WAIT_ANSWER,
    CLIENT_ACTIVE,
    CLIENT_ENDED,
} ClientState;

typedef struct {
    PeerState state;
    uint32_t since;
    CoopIdentity identity;
    bool rosterDue;      // owed the current roster
    uint8_t refusedPick; // owed a refusal for this character, 0 if none
    bool levelDue;       // owed the level start
} Peer;

struct CoopSession {
    CoopTransport* t;
    bool isHost;
    CoopIdentity local;
    Peer peers[COOP_NET_MAX_PEERS];
    CoopRoster roster; // client: what the host last sent, all NO_PLAYER before that
    bool rosterEvent;  // host: a ROSTER event is owed after the JOINED or LEFT one
    CoopLevelStart level;
    bool started;    // a level started: level holds it, and picks and joins are closed
    bool levelEvent; // host: a LEVEL_START event is owed
    ClientState client;
    uint32_t clientSince;
    uint8_t player;  // client: our slot, from the welcome
    bool endPending; // client: an ENDED event is owed after the REFUSED one
    CoopSessionEnd pendingEnd;
    uint32_t ignored;
    uint8_t buffer[COOP_NET_MAX_MESSAGE];
    char text[COOP_SESSION_TEXT];
};

static bool sAnyPlatform;

void CoopSession_AnyPlatform(bool on) {
    sAnyPlatform = on;
}

static CoopSession* Session_New(CoopTransport* transport, const CoopIdentity* identity, bool isHost) {
    CoopSession* s;

    if (transport == NULL) {
        return NULL;
    }
    s = calloc(1, sizeof(*s));
    if (s == NULL) {
        transport->ops->close(transport);
        return NULL;
    }
    s->t = transport;
    s->isHost = isHost;
    s->local = *identity;
    return s;
}

CoopSession* CoopSession_Host(CoopTransport* transport, const CoopIdentity* identity) {
    CoopSession* s = Session_New(transport, identity, true);

    if (s != NULL) {
        s->roster.character[0] = COOP_CHARACTER_FOX;
    }
    return s;
}

CoopSession* CoopSession_Join(CoopTransport* transport, const CoopIdentity* identity, uint32_t nowMs) {
    CoopSession* s = Session_New(transport, identity, false);

    if (s != NULL) {
        s->client = CLIENT_CONNECTING;
        s->clientSince = nowMs;
    }
    return s;
}

void CoopSession_Close(CoopSession* s) {
    if (s == NULL) {
        return;
    }
    s->t->ops->close(s->t);
    free(s);
}

uint32_t CoopSession_Ignored(const CoopSession* s) {
    return s->ignored;
}

const CoopIdentity* CoopSession_PeerIdentity(const CoopSession* s, int peer) {
    if (peer < 0 || peer >= COOP_NET_MAX_PEERS) {
        return NULL;
    }
    if (s->isHost) {
        return s->peers[peer].state == PEER_JOINED ? &s->peers[peer].identity : NULL;
    }
    return (peer == 0 && s->client == CLIENT_ACTIVE) ? &s->peers[0].identity : NULL;
}

bool CoopSession_Stats(const CoopSession* s, int peer, CoopNetStats* out) {
    if (CoopSession_PeerIdentity(s, peer) == NULL) {
        return false;
    }
    return s->t->ops->stats(s->t, peer, out);
}

bool CoopSession_Send(CoopSession* s, int peer, uint16_t msgId, const void* data, uint16_t size, CoopNetLane lane) {
    if (msgId < COOP_MSG_SESSION_END || CoopSession_PeerIdentity(s, peer) == NULL) {
        return false;
    }
    return s->t->ops->send(s->t, peer, msgId, data, size, lane);
}

// A host's client state never leaves CLIENT_CONNECTING, so a host cannot pick.
bool CoopSession_Pick(CoopSession* s, CoopCharacter character) {
    uint8_t wire = (uint8_t) character;

    if (s->client != CLIENT_ACTIVE || s->started || character <= COOP_CHARACTER_FOX ||
        character >= COOP_CHARACTER_COUNT) {
        return false;
    }
    return s->t->ops->send(s->t, 0, COOP_MSG_SESSION_PICK, &wire, 1, COOP_NET_RELIABLE);
}

bool CoopSession_CanStart(const CoopSession* s) {
    int i;

    if (!s->isHost || s->started) {
        return false;
    }
    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        if (s->peers[i].state == PEER_WAIT_HELLO || s->roster.character[i + 1] == COOP_CHARACTER_UNPICKED) {
            return false;
        }
    }
    return true;
}

bool CoopSession_StartLevel(CoopSession* s, const CoopLevelStart* start) {
    int i;

    if (!CoopSession_CanStart(s) || start->level >= COOP_LEVEL_START_LEVELS) {
        return false;
    }
    s->level = *start;
    memcpy(s->level.roster, s->roster.character, sizeof(s->level.roster));
    s->started = true;
    s->levelEvent = true;
    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        s->peers[i].levelDue = true;
    }
    return true;
}

const CoopRoster* CoopSession_Roster(const CoopSession* s) {
    if (s->isHost) {
        return &s->roster;
    }
    return (s->client == CLIENT_ACTIVE && s->roster.character[0] == COOP_CHARACTER_FOX) ? &s->roster : NULL;
}

const char* CoopSession_CharacterName(uint8_t character) {
    switch (character) {
        case COOP_CHARACTER_FOX:
            return "Fox";
        case COOP_CHARACTER_FALCO:
            return "Falco";
        case COOP_CHARACTER_SLIPPY:
            return "Slippy";
        case COOP_CHARACTER_PEPPY:
            return "Peppy";
        default:
            return "";
    }
}

const char* CoopSession_EndText(CoopSessionEnd end) {
    switch (end) {
        case COOP_SESSION_END_REFUSED:
            return "The host refused this game.";
        case COOP_SESSION_END_CLOSED:
            return "The other player closed the connection.";
        case COOP_SESSION_END_TIMEOUT:
            return "The connection timed out: nothing was heard from the other side.";
        case COOP_SESSION_END_HOST_FULL:
            return "The host is full.";
        case COOP_SESSION_END_PROTOCOL:
            return "The host runs another version of the co-op protocol. Both players need the same Starship Co-op "
                   "build.";
        case COOP_SESSION_END_NO_HANDSHAKE:
            return "Connected, but the handshake did not finish in time. The other side may run an older build.";
        case COOP_SESSION_END_BAD_MESSAGE:
            return "The other side sent a session message this game cannot read. Both players need the same "
                   "Starship Co-op build.";
        default:
            return "";
    }
}

static CoopSessionEnd End_FromNet(CoopNetEnd end) {
    switch (end) {
        case COOP_NET_END_TIMEOUT:
            return COOP_SESSION_END_TIMEOUT;
        case COOP_NET_END_REFUSED_FULL:
            return COOP_SESSION_END_HOST_FULL;
        case COOP_NET_END_REFUSED_VERSION:
            return COOP_SESSION_END_PROTOCOL;
        default:
            return COOP_SESSION_END_CLOSED;
    }
}

// True if data is one identity and nothing else. CoopIdentity_Read returns 0 on failure, so an empty payload would
// pass a plain comparison with its size.
static bool Identity_ReadAll(CoopIdentity* id, const uint8_t* data, uint16_t size) {
    uint16_t read = CoopIdentity_Read(id, data, size);

    return read != 0 && read == size;
}

// Level start on the wire: level, phase, roster, then the values little-endian.
#define LEVEL_START_SIZE (2 + COOP_SESSION_PLAYERS + 4 * COOP_LEVEL_START_VALUES)

static void LevelStart_Write(const CoopLevelStart* start, uint8_t* out) {
    int i;

    *out++ = start->level;
    *out++ = start->phase;
    memcpy(out, start->roster, COOP_SESSION_PLAYERS);
    out += COOP_SESSION_PLAYERS;
    for (i = 0; i < COOP_LEVEL_START_VALUES; i++) {
        uint32_t value = (uint32_t) start->values[i];

        *out++ = (uint8_t) value;
        *out++ = (uint8_t) (value >> 8);
        *out++ = (uint8_t) (value >> 16);
        *out++ = (uint8_t) (value >> 24);
    }
}

static void LevelStart_Read(CoopLevelStart* start, const uint8_t* data) {
    int i;

    start->level = *data++;
    start->phase = *data++;
    memcpy(start->roster, data, COOP_SESSION_PLAYERS);
    data += COOP_SESSION_PLAYERS;
    for (i = 0; i < COOP_LEVEL_START_VALUES; i++) {
        uint32_t value =
            (uint32_t) data[0] | ((uint32_t) data[1] << 8) | ((uint32_t) data[2] << 16) | ((uint32_t) data[3] << 24);

        start->values[i] = (int32_t) value;
        data += 4;
    }
}

static void Event_Init(CoopSessionEvent* ev, CoopSessionEventType type, int peer) {
    memset(ev, 0, sizeof(*ev));
    ev->type = type;
    ev->peer = peer;
    ev->text = "";
}

// The same event and text on the host and on the client that asked.
static void Event_LevelStart(CoopSession* s, CoopSessionEvent* ev) {
    Event_Init(ev, COOP_SESSION_EV_LEVEL_START, 0);
    ev->levelStart = &s->level;
}

static void Event_PickRefused(CoopSession* s, int peer, uint8_t character, CoopSessionEvent* ev) {
    Event_Init(ev, COOP_SESSION_EV_PICK_REFUSED, peer);
    ev->character = character;
    snprintf(s->text, sizeof(s->text), "%s is already taken.", CoopSession_CharacterName(character));
    ev->text = s->text;
}

// True if a SESSION_PICK or SESSION_PICK_REFUSE holds one character and nothing else.
static bool Character_Read(const CoopNetEvent* net) {
    return net->size == 1 && net->data[0] >= COOP_CHARACTER_FOX && net->data[0] < COOP_CHARACTER_COUNT;
}

// ---- Host ----

static void Host_RosterChanged(CoopSession* s) {
    int i;

    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        s->peers[i].rosterDue = true;
    }
    s->rosterEvent = true;
}

static bool Host_RosterEvent(CoopSession* s, CoopSessionEvent* ev) {
    if (!s->rosterEvent) {
        return false;
    }
    s->rosterEvent = false;
    Event_Init(ev, COOP_SESSION_EV_ROSTER, 0);
    return true;
}

static bool Host_LevelEvent(CoopSession* s, CoopSessionEvent* ev) {
    if (!s->levelEvent) {
        return false;
    }
    s->levelEvent = false;
    Event_LevelStart(s, ev);
    return true;
}

// What a full window held back stays owed. A later change needs no queue: the roster that goes out is the current one.
static void Host_SendOwed(CoopSession* s) {
    int i;

    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        Peer* p = &s->peers[i];

        if (p->state != PEER_JOINED) {
            continue;
        }
        if (p->refusedPick != 0 &&
            s->t->ops->send(s->t, i, COOP_MSG_SESSION_PICK_REFUSE, &p->refusedPick, 1, COOP_NET_RELIABLE)) {
            p->refusedPick = 0;
        }
        if (p->rosterDue && s->t->ops->send(s->t, i, COOP_MSG_SESSION_ROSTER, s->roster.character,
                                            sizeof(s->roster.character), COOP_NET_RELIABLE)) {
            p->rosterDue = false;
        }
        if (p->levelDue) {
            LevelStart_Write(&s->level, s->buffer);
            if (s->t->ops->send(s->t, i, COOP_MSG_SESSION_LEVEL_START, s->buffer, LEVEL_START_SIZE,
                                COOP_NET_RELIABLE)) {
                p->levelDue = false;
            }
        }
    }
}

// A client that joined is gone. Its slot and its character are free at once; nothing is kept for its return.
static void Host_Left(CoopSession* s, int peer, CoopSessionEnd end, CoopSessionEvent* ev) {
    s->roster.character[peer + 1] = COOP_CHARACTER_NO_PLAYER;
    Host_RosterChanged(s);
    Event_Init(ev, COOP_SESSION_EV_LEFT, peer);
    ev->end = end;
    ev->text = CoopSession_EndText(ev->end);
}

#define STARTED_TEXT "The level has already started, so nobody can join this game now."

static void Host_Refuse(CoopSession* s, int peer, uint32_t reasons, uint32_t nowMs, CoopSessionEvent* ev) {
    Peer* p = &s->peers[peer];
    uint16_t size;

    s->buffer[0] = (uint8_t) reasons;
    s->buffer[1] = (uint8_t) (reasons >> 8);
    s->buffer[2] = (uint8_t) (reasons >> 16);
    s->buffer[3] = (uint8_t) (reasons >> 24);
    size = CoopIdentity_Write(&s->local, s->buffer + 4, sizeof(s->buffer) - 4);
    if (size == 0 ||
        !s->t->ops->send(s->t, peer, COOP_MSG_SESSION_REFUSE, s->buffer, (uint16_t) (size + 4), COOP_NET_RELIABLE)) {
        // Nothing would reach the client; it ends with a timeout of its own.
        s->t->ops->disconnect(s->t, peer);
        p->state = PEER_FREE;
    } else {
        p->state = PEER_REFUSED;
        p->since = nowMs;
    }

    Event_Init(ev, COOP_SESSION_EV_REFUSED, peer);
    ev->diff = reasons;
    ev->end = COOP_SESSION_END_REFUSED;
    if (reasons & COOP_SESSION_REFUSE_MALFORMED) {
        snprintf(s->text, sizeof(s->text), "%s", CoopSession_EndText(COOP_SESSION_END_BAD_MESSAGE));
    } else if (reasons & COOP_SESSION_REFUSE_STARTED) {
        snprintf(s->text, sizeof(s->text), "%s", STARTED_TEXT);
    } else if (reasons == 0) {
        snprintf(s->text, sizeof(s->text), "%s", CoopSession_EndText(COOP_SESSION_END_NO_HANDSHAKE));
    } else {
        CoopIdentity_Describe(reasons, &s->local, &p->identity, s->text, sizeof(s->text));
    }
    ev->text = s->text;
}

static void Host_Hello(CoopSession* s, int peer, const CoopNetEvent* net, uint32_t nowMs, CoopSessionEvent* ev) {
    Peer* p = &s->peers[peer];
    uint32_t diff;
    uint16_t size;

    if (!Identity_ReadAll(&p->identity, net->data, net->size)) {
        Host_Refuse(s, peer, COOP_SESSION_REFUSE_MALFORMED, nowMs, ev);
        return;
    }
    if (s->started) {
        Host_Refuse(s, peer, COOP_SESSION_REFUSE_STARTED, nowMs, ev);
        return;
    }
    diff = CoopIdentity_Compare(&s->local, &p->identity);
    if (diff & (sAnyPlatform ? COOP_ID_DIFF_REFUSE & ~COOP_ID_DIFF_PLATFORM : COOP_ID_DIFF_REFUSE)) {
        // With the test switch on the platform goes along, so both players see everything that differs.
        Host_Refuse(s, peer, diff, nowMs, ev);
        return;
    }

    s->buffer[0] = (uint8_t) (peer + 1);
    size = CoopIdentity_Write(&s->local, s->buffer + 1, sizeof(s->buffer) - 1);
    if (size == 0 ||
        !s->t->ops->send(s->t, peer, COOP_MSG_SESSION_WELCOME, s->buffer, (uint16_t) (size + 1), COOP_NET_RELIABLE)) {
        Host_Refuse(s, peer, 0, nowMs, ev);
        return;
    }
    p->state = PEER_JOINED;
    s->roster.character[peer + 1] = COOP_CHARACTER_UNPICKED;
    Host_RosterChanged(s);
    Event_Init(ev, COOP_SESSION_EV_JOINED, peer);
    ev->playerIndex = (uint8_t) (peer + 1);
    ev->diff = diff;
    CoopIdentity_Describe(diff, &s->local, &p->identity, s->text, sizeof(s->text));
    ev->text = s->text;
}

// Time-outs and refused peers whose refusal has arrived. True if ev was filled in.
static bool Host_Tick(CoopSession* s, uint32_t nowMs, CoopSessionEvent* ev) {
    int i;

    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        Peer* p = &s->peers[i];

        if (p->state == PEER_REFUSED) {
            CoopNetStats stats;
            bool delivered = s->t->ops->stats(s->t, i, &stats) && stats.pending == 0;

            if (delivered || nowMs - p->since >= COOP_SESSION_REFUSE_LINGER_MS) {
                s->t->ops->disconnect(s->t, i);
                p->state = PEER_FREE;
            }
        } else if (p->state == PEER_WAIT_HELLO && nowMs - p->since >= COOP_SESSION_HANDSHAKE_MS) {
            memset(&p->identity, 0, sizeof(p->identity));
            Host_Refuse(s, i, 0, nowMs, ev);
            return true;
        }
    }
    return false;
}

// A character someone else holds, Fox included, is refused and the client stays; one it cannot have sent means a broken
// peer, as with a hello that cannot be read. True if ev was filled in.
static bool Host_Pick(CoopSession* s, int peer, const CoopNetEvent* net, CoopSessionEvent* ev) {
    Peer* p = &s->peers[peer];
    int i;

    if (!Character_Read(net)) {
        s->t->ops->disconnect(s->t, peer);
        p->state = PEER_FREE;
        Host_Left(s, peer, COOP_SESSION_END_BAD_MESSAGE, ev);
        return true;
    }
    if (s->roster.character[peer + 1] == net->data[0]) {
        return false;
    }
    for (i = 0; i < COOP_SESSION_PLAYERS; i++) {
        if (s->roster.character[i] == net->data[0]) {
            p->refusedPick = net->data[0];
            Event_PickRefused(s, peer, net->data[0], ev);
            return true;
        }
    }
    s->roster.character[peer + 1] = net->data[0];
    Host_RosterChanged(s);
    return Host_RosterEvent(s, ev);
}

static bool Host_Poll(CoopSession* s, uint32_t nowMs, CoopSessionEvent* ev) {
    CoopNetEvent net;

    Host_SendOwed(s);
    if (Host_RosterEvent(s, ev) || Host_LevelEvent(s, ev) || Host_Tick(s, nowMs, ev)) {
        return true;
    }
    while (s->t->ops->poll(s->t, &net)) {
        Peer* p;

        if (net.peer < 0 || net.peer >= COOP_NET_MAX_PEERS) {
            continue;
        }
        p = &s->peers[net.peer];
        switch (net.type) {
            case COOP_NET_EVENT_CONNECTED:
                memset(p, 0, sizeof(*p));
                p->state = PEER_WAIT_HELLO;
                p->since = nowMs;
                break;
            case COOP_NET_EVENT_DISCONNECTED: {
                bool joined = p->state == PEER_JOINED;

                p->state = PEER_FREE;
                if (joined) {
                    Host_Left(s, net.peer, End_FromNet(net.end), ev);
                    return true;
                }
                break;
            }
            case COOP_NET_EVENT_MESSAGE:
                if (p->state == PEER_WAIT_HELLO && net.msgId == COOP_MSG_SESSION_HELLO) {
                    Host_Hello(s, net.peer, &net, nowMs, ev);
                    return true;
                }
                if (p->state == PEER_JOINED && !s->started && net.msgId == COOP_MSG_SESSION_PICK) {
                    if (Host_Pick(s, net.peer, &net, ev)) {
                        return true;
                    }
                    break;
                }
                if (p->state == PEER_JOINED && net.msgId >= COOP_MSG_SESSION_END) {
                    Event_Init(ev, COOP_SESSION_EV_MESSAGE, net.peer);
                    ev->msgId = net.msgId;
                    ev->size = net.size;
                    ev->data = net.data;
                    return true;
                }
                s->ignored++;
                break;
            default:
                break;
        }
    }
    return false;
}

// ---- Client ----

static void Client_End(CoopSession* s, CoopSessionEnd end, CoopSessionEvent* ev) {
    s->client = CLIENT_ENDED;
    Event_Init(ev, COOP_SESSION_EV_ENDED, 0);
    ev->end = end;
    ev->text = CoopSession_EndText(end);
}

// After the welcome, a message from the host that cannot be read ends the session as a broken welcome does.
static void Client_Broken(CoopSession* s, CoopSessionEvent* ev) {
    s->t->ops->disconnect(s->t, 0);
    Client_End(s, COOP_SESSION_END_BAD_MESSAGE, ev);
}

// True if data is a roster this client can be part of: Fox at player 0, everyone else free, without a character or a
// wingman, no wingman twice, and our own slot taken.
static bool Roster_Valid(const uint8_t* data, uint16_t size, uint8_t self) {
    int i, j;

    if (size != COOP_SESSION_PLAYERS || data[0] != COOP_CHARACTER_FOX || data[self] == COOP_CHARACTER_NO_PLAYER) {
        return false;
    }
    for (i = 1; i < COOP_SESSION_PLAYERS; i++) {
        if (data[i] == COOP_CHARACTER_FOX || data[i] >= COOP_CHARACTER_COUNT) {
            return false;
        }
        for (j = 1; j < i; j++) {
            if (data[i] > COOP_CHARACTER_FOX && data[i] == data[j]) {
                return false;
            }
        }
    }
    return true;
}

// True if data is a level start this client can enter: a level there is, and a roster as above in which everyone holds
// a character, as the host starts no other way.
static bool LevelStart_Valid(const uint8_t* data, uint16_t size, uint8_t self) {
    const uint8_t* roster = data + 2;
    int i;

    if (size != LEVEL_START_SIZE || data[0] >= COOP_LEVEL_START_LEVELS ||
        !Roster_Valid(roster, COOP_SESSION_PLAYERS, self)) {
        return false;
    }
    for (i = 1; i < COOP_SESSION_PLAYERS; i++) {
        if (roster[i] == COOP_CHARACTER_UNPICKED) {
            return false;
        }
    }
    return true;
}

static void Client_LevelStart(CoopSession* s, const CoopNetEvent* net, CoopSessionEvent* ev) {
    if (!LevelStart_Valid(net->data, net->size, s->player)) {
        Client_Broken(s, ev);
        return;
    }
    LevelStart_Read(&s->level, net->data);
    s->started = true;
    Event_LevelStart(s, ev);
}

static void Client_Roster(CoopSession* s, const CoopNetEvent* net, CoopSessionEvent* ev) {
    if (!Roster_Valid(net->data, net->size, s->player)) {
        Client_Broken(s, ev);
        return;
    }
    memcpy(s->roster.character, net->data, sizeof(s->roster.character));
    Event_Init(ev, COOP_SESSION_EV_ROSTER, 0);
}

static void Client_PickRefused(CoopSession* s, const CoopNetEvent* net, CoopSessionEvent* ev) {
    if (!Character_Read(net)) {
        Client_Broken(s, ev);
        return;
    }
    Event_PickRefused(s, 0, net->data[0], ev);
}

static void Client_Answer(CoopSession* s, const CoopNetEvent* net, CoopSessionEvent* ev) {
    CoopIdentity* host = &s->peers[0].identity;

    if (net->msgId == COOP_MSG_SESSION_WELCOME) {
        if (net->size < 1 || net->data[0] < 1 || net->data[0] > COOP_NET_MAX_PEERS ||
            !Identity_ReadAll(host, net->data + 1, (uint16_t) (net->size - 1))) {
            s->t->ops->disconnect(s->t, 0);
            Client_End(s, COOP_SESSION_END_BAD_MESSAGE, ev);
            return;
        }
        s->client = CLIENT_ACTIVE;
        s->player = net->data[0];
        Event_Init(ev, COOP_SESSION_EV_JOINED, 0);
        ev->playerIndex = net->data[0];
        ev->diff = CoopIdentity_Compare(host, &s->local);
        CoopIdentity_Describe(ev->diff, host, &s->local, s->text, sizeof(s->text));
        ev->text = s->text;
        return;
    }

    // SESSION_REFUSE. We leave at once: the host waits for our goodbye or for the ack.
    s->t->ops->disconnect(s->t, 0);
    if (net->size < 4 || !Identity_ReadAll(host, net->data + 4, (uint16_t) (net->size - 4))) {
        Client_End(s, COOP_SESSION_END_BAD_MESSAGE, ev);
        return;
    }
    s->client = CLIENT_ENDED;
    s->endPending = true;
    s->pendingEnd = COOP_SESSION_END_REFUSED;
    Event_Init(ev, COOP_SESSION_EV_REFUSED, 0);
    ev->end = COOP_SESSION_END_REFUSED;
    ev->diff = (uint32_t) net->data[0] | ((uint32_t) net->data[1] << 8) | ((uint32_t) net->data[2] << 16) |
               ((uint32_t) net->data[3] << 24);
    if (ev->diff & COOP_SESSION_REFUSE_MALFORMED) {
        snprintf(s->text, sizeof(s->text),
                 "The host could not read this game's handshake. Both players need the "
                 "same Starship Co-op build.");
    } else if (ev->diff & COOP_SESSION_REFUSE_STARTED) {
        snprintf(s->text, sizeof(s->text), "%s", STARTED_TEXT);
    } else if ((ev->diff & COOP_ID_DIFF_REFUSE) == 0) {
        snprintf(s->text, sizeof(s->text), "%s", CoopSession_EndText(COOP_SESSION_END_NO_HANDSHAKE));
    } else {
        CoopIdentity_Describe(ev->diff, host, &s->local, s->text, sizeof(s->text));
    }
    ev->text = s->text;
}

static bool Client_Poll(CoopSession* s, uint32_t nowMs, CoopSessionEvent* ev) {
    CoopNetEvent net;

    if (s->endPending) {
        s->endPending = false;
        Client_End(s, s->pendingEnd, ev);
        return true;
    }
    if (s->client == CLIENT_ENDED) {
        return false;
    }
    // While connecting, the transport's own timeout applies.
    if (s->client == CLIENT_WAIT_ANSWER && nowMs - s->clientSince >= COOP_SESSION_HANDSHAKE_MS) {
        s->t->ops->disconnect(s->t, 0);
        Client_End(s, COOP_SESSION_END_NO_HANDSHAKE, ev);
        return true;
    }
    while (s->t->ops->poll(s->t, &net)) {
        switch (net.type) {
            case COOP_NET_EVENT_CONNECTED: {
                uint16_t size = CoopIdentity_Write(&s->local, s->buffer, sizeof(s->buffer));

                if (size == 0 ||
                    !s->t->ops->send(s->t, 0, COOP_MSG_SESSION_HELLO, s->buffer, size, COOP_NET_RELIABLE)) {
                    s->t->ops->disconnect(s->t, 0);
                    Client_End(s, COOP_SESSION_END_BAD_MESSAGE, ev);
                    return true;
                }
                s->client = CLIENT_WAIT_ANSWER;
                s->clientSince = nowMs;
                break;
            }
            case COOP_NET_EVENT_DISCONNECTED:
                Client_End(s, End_FromNet(net.end), ev);
                return true;
            case COOP_NET_EVENT_MESSAGE:
                if (s->client == CLIENT_WAIT_ANSWER &&
                    (net.msgId == COOP_MSG_SESSION_WELCOME || net.msgId == COOP_MSG_SESSION_REFUSE)) {
                    Client_Answer(s, &net, ev);
                    return true;
                }
                if (s->client == CLIENT_ACTIVE && net.msgId == COOP_MSG_SESSION_ROSTER) {
                    Client_Roster(s, &net, ev);
                    return true;
                }
                if (s->client == CLIENT_ACTIVE && !s->started && net.msgId == COOP_MSG_SESSION_LEVEL_START) {
                    Client_LevelStart(s, &net, ev);
                    return true;
                }
                if (s->client == CLIENT_ACTIVE && net.msgId == COOP_MSG_SESSION_PICK_REFUSE) {
                    Client_PickRefused(s, &net, ev);
                    return true;
                }
                if (s->client == CLIENT_ACTIVE && net.msgId >= COOP_MSG_SESSION_END) {
                    Event_Init(ev, COOP_SESSION_EV_MESSAGE, 0);
                    ev->msgId = net.msgId;
                    ev->size = net.size;
                    ev->data = net.data;
                    return true;
                }
                s->ignored++;
                break;
            default:
                break;
        }
    }
    return false;
}

bool CoopSession_Poll(CoopSession* s, uint32_t nowMs, CoopSessionEvent* ev) {
    return s->isHost ? Host_Poll(s, nowMs, ev) : Client_Poll(s, nowMs, ev);
}

#endif
