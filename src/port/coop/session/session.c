// Co-op session: handshake (WP-21), characters (WP-34), level start (WP-24) and the session's save (decision 0009 item
// 8). Messages and their layout: docs/coop/PROTOCOL.md, ids 0x0010-0x001A.
//
// Host, per peer: WAIT_HELLO from the transport's CONNECTED, then JOINED or REFUSED. A refused peer stays until the
// refusal is acknowledged, the client goes, or COOP_SESSION_REFUSE_LINGER_MS pass; the transport's disconnect drops
// unacknowledged messages, so leaving at once would lose the refusal. Client: CONNECTING, WAIT_ANSWER, ACTIVE, ENDED.
//
// The host holds the roster. Whatever a joined peer is owed goes out at the start of a poll, and stays owed while the
// transport's window is full: a client may miss a roster that way, but not the last one.
//
// A level start is taken with the host's values, its save and the roster of that moment, so that a client leaving
// afterwards cannot make two games come to different player counts. Picks and joins stop there. The save goes to each
// client just ahead of the level start, on the same reliable lane, so a client holds it when the level start comes.
//
// The session survives a lost player or a desync (session.h, "Recovery"): in a level the host keeps the slot of a
// client whose connection timed out (`away`, by player slot), tells every client that the level starts again
// (SESSION_RECOVER), and once nobody is waited for sends its save and the restart point as a level start with the next
// run number. What the host's owner hears of this comes through a small queue, so one poll can owe several events.

#ifdef USE_NETWORKING

#include "session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/coop/log/coop_log.h"
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
    bool saveDue;        // owed the session's save, which goes ahead of the level start
    bool levelDue;       // owed the level start
    bool recoverDue;     // owed SESSION_RECOVER, which goes ahead of the save
} Peer;

// Host: the slot of a player whose connection was lost in a level. Its character stays in the roster meanwhile.
typedef struct {
    bool on;
    uint32_t since;
} Away;

// Host: an event owed to the owner.
typedef struct {
    uint8_t type; // CoopSessionEventType
    int8_t peer;
    uint8_t end;
    uint8_t reason;
    uint8_t player;
    bool away;
    uint32_t diff;
} Queued;

#define QUEUE_SIZE 16

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
    bool saveHeld;   // client: the host's save came, into level.save
    ClientState client;
    uint32_t clientSince;
    uint8_t player;  // client: our slot, from the welcome
    bool endPending; // client: an ENDED event is owed after the REFUSED one
    CoopSessionEnd pendingEnd;
    uint32_t ignored;
    bool closedSaid; // `session closed` is in the log (CoopSession_SayClosed)
    // The session survives (session.h, "Recovery").
    uint64_t id;            // host: set by the owner; client: from the welcome
    CoopLevelStart restart; // host: what a recovery starts
    bool recovering;        // host: clients have been told, and the new level start has not gone out yet
    uint8_t recoverReason;
    uint8_t recoverPlayer;
    Away away[COOP_SESSION_PLAYERS];
    Queued queue[QUEUE_SIZE];
    uint8_t queueHead;
    uint8_t queueCount;
    bool rejoin;             // client: this session comes back to one, with id and rejoinCharacter
    uint8_t rejoinCharacter; //
    bool recoverHeard;       // client: SESSION_RECOVER came, so a save and a level start may come again
    bool awaiting;           // client: waiting for the host's new level start since awaitSince
    uint32_t awaitSince;
    bool recoverEvent; // client: a RECOVER event is owed for CoopSession_Recover
    // The co-op log (#172): when the transport's counters were last written, and what they were then.
    uint32_t nowMs;
    uint32_t statsMs;
    CoopNetStats lastStats[COOP_NET_MAX_PEERS];
    uint8_t buffer[COOP_NET_MAX_MESSAGE];
    char text[COOP_SESSION_TEXT];
};

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

// The co-op log: one line on a game's identity. Nothing personal in it (identity.h).
static void Log_Identity(const char* what, const CoopIdentity* id) {
    char line[256];

    CoopIdentity_Line(id, line, sizeof(line));
    CoopLog_Write(COOP_LOG_SESSION, false, "%s: %s", what, line);
}

CoopSession* CoopSession_Host(CoopTransport* transport, const CoopIdentity* identity) {
    CoopSession* s = Session_New(transport, identity, true);

    if (s != NULL) {
        s->roster.character[0] = COOP_CHARACTER_FOX;
        CoopLog_Begin(true, 0);
        Log_Identity("hosting a session; this game", identity);
    }
    return s;
}

CoopSession* CoopSession_Join(CoopTransport* transport, const CoopIdentity* identity, uint32_t nowMs) {
    CoopSession* s = Session_New(transport, identity, false);

    if (s != NULL) {
        s->client = CLIENT_CONNECTING;
        s->clientSince = nowMs;
        s->nowMs = nowMs;
        CoopLog_Begin(false, nowMs);
        Log_Identity("joining a host; this game", identity);
    }
    return s;
}

CoopSession* CoopSession_Rejoin(CoopTransport* transport, const CoopIdentity* identity, uint32_t nowMs, uint64_t id,
                                uint8_t character) {
    CoopSession* s = Session_New(transport, identity, false);

    if (s != NULL) {
        s->client = CLIENT_CONNECTING;
        s->clientSince = nowMs;
        s->nowMs = nowMs;
        s->rejoin = true;
        s->id = id;
        s->rejoinCharacter = character;
        CoopLog_Begin(false, nowMs);
        Log_Identity("coming back to a session; this game", identity);
    }
    return s;
}

void CoopSession_SetId(CoopSession* s, uint64_t id) {
    if (s->isHost) {
        s->id = id;
    }
}

uint64_t CoopSession_Id(const CoopSession* s) {
    return s->id;
}

static bool Log_Send(void* user, int peer, const uint8_t* data, uint16_t size);

void CoopSession_SayClosed(CoopSession* s) {
    if (!s->closedSaid) {
        s->closedSaid = true;
        CoopLog_Write(COOP_LOG_SESSION, false, "session closed");
    }
}

void CoopSession_Close(CoopSession* s) {
    if (s == NULL) {
        return;
    }
    CoopSession_SayClosed(s);
    CoopLog_End(s->nowMs, Log_Send, s);
    s->t->ops->close(s->t);
    free(s);
}

uint32_t CoopSession_Ignored(const CoopSession* s) {
    return s->ignored;
}

int CoopSession_Self(const CoopSession* s) {
    if (s->isHost) {
        return 0;
    }
    return (s->player != 0) ? s->player : -1;
}

int CoopSession_PeerPlayer(const CoopSession* s, int peer) {
    if (CoopSession_PeerIdentity(s, peer) == NULL) {
        return -1;
    }
    return s->isHost ? peer + 1 : 0;
}

bool CoopSession_LevelStarted(const CoopSession* s) {
    return s->started;
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
    CoopLog_Write(COOP_LOG_SESSION, false, "asking the host for %s", CoopSession_CharacterName(wire));
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
    s->level.run = 1;
    memcpy(s->level.roster, s->roster.character, sizeof(s->level.roster));
    s->restart = s->level;
    s->started = true;
    s->levelEvent = true;
    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        s->peers[i].saveDue = true;
        s->peers[i].levelDue = true;
    }
    return true;
}

void CoopSession_SetRestart(CoopSession* s, const CoopLevelStart* start) {
    if (s->isHost && s->started) {
        s->restart = *start;
    }
}

bool CoopSession_Recovering(const CoopSession* s) {
    return s->isHost ? s->recovering : s->awaiting;
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
        case COOP_SESSION_END_NO_RECOVERY:
            return "The host did not start the level again in time, so the session is over.";
        case COOP_SESSION_END_OTHER_SESSION:
            return "The host runs another session now, so this game cannot come back to its own.";
        default:
            return "";
    }
}

void CoopSession_RecoverText(uint8_t reason, uint8_t player, char* out, size_t cap) {
    switch (reason) {
        case COOP_RECOVER_LOST:
            snprintf(out, cap,
                     "Player %d lost the connection. Waiting for it to come back (%d s), then the level starts again.",
                     (int) player + 1, COOP_SESSION_REJOIN_MS / 1000);
            break;
        case COOP_RECOVER_LEFT:
            snprintf(out, cap, "Player %d left. Starting the level again together.", (int) player + 1);
            break;
        default:
            snprintf(out, cap, "The games parted. Starting the level again together.");
            break;
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

// Level start on the wire: level, phase, run, roster, the values little-endian. The save goes in a message of its own.
#define LEVEL_START_HEAD 3
#define LEVEL_START_SIZE (LEVEL_START_HEAD + COOP_SESSION_PLAYERS + 4 * COOP_LEVEL_START_VALUES)

static void LevelStart_Write(const CoopLevelStart* start, uint8_t* out) {
    int i;

    *out++ = start->level;
    *out++ = start->phase;
    *out++ = start->run;
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
    start->run = *data++;
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

static void Event_Recover(CoopSession* s, uint8_t reason, uint8_t player, CoopSessionEvent* ev) {
    Event_Init(ev, COOP_SESSION_EV_RECOVER, 0);
    ev->reason = reason;
    ev->playerIndex = player;
    CoopSession_RecoverText(reason, player, s->text, sizeof(s->text));
    ev->text = s->text;
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
        if (p->recoverDue) {
            uint8_t recover[2] = { s->recoverReason, s->recoverPlayer };

            if (s->t->ops->send(s->t, i, COOP_MSG_SESSION_RECOVER, recover, sizeof(recover), COOP_NET_RELIABLE)) {
                p->recoverDue = false;
            }
        }
        if (p->saveDue && !p->recoverDue &&
            s->t->ops->send(s->t, i, COOP_MSG_SESSION_SAVE, s->level.save, COOP_SAVE_SIZE, COOP_NET_RELIABLE)) {
            p->saveDue = false;
        }
        if (p->levelDue && !p->saveDue) {
            LevelStart_Write(&s->level, s->buffer);
            if (s->t->ops->send(s->t, i, COOP_MSG_SESSION_LEVEL_START, s->buffer, LEVEL_START_SIZE,
                                COOP_NET_RELIABLE)) {
                p->levelDue = false;
            }
        }
    }
}

static int Host_Joined(const CoopSession* s) {
    int count = 0;
    int i;

    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        count += s->peers[i].state == PEER_JOINED;
    }
    return count;
}

static bool Host_AnyAway(const CoopSession* s) {
    int i;

    for (i = 1; i < COOP_SESSION_PLAYERS; i++) {
        if (s->away[i].on) {
            return true;
        }
    }
    return false;
}

static void Host_Queue(CoopSession* s, const Queued* q) {
    if (s->queueCount < QUEUE_SIZE) {
        s->queue[(s->queueHead + s->queueCount++) % QUEUE_SIZE] = *q;
    }
}

// The level cannot go on as it is: every client that is there is told, and so is the owner. The new level start goes
// out from Host_Recovery, once nobody is waited for.
static void Host_RecoverBegin(CoopSession* s, uint8_t reason, uint8_t player) {
    Queued q = { COOP_SESSION_EV_RECOVER, 0, 0, reason, player, false, 0 };
    int i;

    s->recovering = true;
    s->recoverReason = reason;
    s->recoverPlayer = player;
    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        s->peers[i].recoverDue = s->peers[i].state == PEER_JOINED;
    }
    Host_Queue(s, &q);
}

// A client that joined is gone. Before the level its slot and its character are free at once, and nothing is kept for
// its return. In a level a client whose connection timed out keeps both for COOP_SESSION_REJOIN_MS, if the session has
// an id to come back with; and whoever goes, the others start the level again. With nobody else there is nothing to
// recover, and the owner ends the session.
static void Host_Left(CoopSession* s, int peer, CoopSessionEnd end, CoopSessionEvent* ev) {
    bool away = s->started && end == COOP_SESSION_END_TIMEOUT && s->id != 0;

    if (away) {
        s->away[peer + 1].on = true;
        s->away[peer + 1].since = s->nowMs;
    } else {
        s->roster.character[peer + 1] = COOP_CHARACTER_NO_PLAYER;
        Host_RosterChanged(s);
    }
    if (s->started) {
        if (Host_Joined(s) > 0 || Host_AnyAway(s)) {
            Host_RecoverBegin(s, away ? COOP_RECOVER_LOST : COOP_RECOVER_LEFT, (uint8_t) (peer + 1));
        } else {
            s->recovering = false;
        }
    }
    Event_Init(ev, COOP_SESSION_EV_LEFT, peer);
    ev->end = end;
    ev->away = away;
    ev->text = CoopSession_EndText(ev->end);
}

// The level again, from the restart point, for everyone who is there now.
static void Host_Restart(CoopSession* s) {
    uint8_t run = (uint8_t) (s->level.run + 1);
    int i;

    s->level = s->restart;
    s->level.run = (run != 0) ? run : 1;
    memcpy(s->level.roster, s->roster.character, COOP_SESSION_PLAYERS);
    s->levelEvent = true;
    s->recovering = false;
    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        Peer* p = &s->peers[i];

        p->saveDue = true;
        p->levelDue = true;
    }
}

// While a recovery runs: a player who did not come back in time is gone for good, and once nobody is waited for the
// level starts again. A connection in its handshake is waited for, as it may be the player coming back; that ends
// within COOP_SESSION_HANDSHAKE_MS. True if ev was filled in.
static bool Host_Recovery(CoopSession* s, uint32_t nowMs, CoopSessionEvent* ev) {
    int i;

    if (!s->recovering) {
        return false;
    }
    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        if (s->peers[i].state == PEER_WAIT_HELLO) {
            return false;
        }
    }
    for (i = 1; i < COOP_SESSION_PLAYERS; i++) {
        if (s->away[i].on && nowMs - s->away[i].since >= COOP_SESSION_REJOIN_MS) {
            s->away[i].on = false;
            s->roster.character[i] = COOP_CHARACTER_NO_PLAYER;
            Host_RosterChanged(s);
            s->recovering = Host_Joined(s) > 0 || Host_AnyAway(s);
            Event_Init(ev, COOP_SESSION_EV_LEFT, i - 1);
            ev->end = COOP_SESSION_END_TIMEOUT;
            ev->text = CoopSession_EndText(COOP_SESSION_END_TIMEOUT);
            return true;
        }
    }
    if (Host_AnyAway(s)) {
        return false;
    }
    if (Host_Joined(s) == 0) {
        s->recovering = false;
        return false;
    }
    Host_Restart(s);
    return false;
}

// An event the owner is owed. True if ev was filled in.
static bool Host_Queued(CoopSession* s, CoopSessionEvent* ev) {
    const Queued* q;

    if (s->queueCount == 0) {
        return false;
    }
    q = &s->queue[s->queueHead];
    s->queueHead = (uint8_t) ((s->queueHead + 1) % QUEUE_SIZE);
    s->queueCount--;
    switch (q->type) {
        case COOP_SESSION_EV_RECOVER:
            Event_Recover(s, q->reason, q->player, ev);
            break;
        case COOP_SESSION_EV_JOINED:
            Event_Init(ev, COOP_SESSION_EV_JOINED, q->peer);
            ev->playerIndex = q->player;
            ev->diff = q->diff;
            ev->rejoined = true;
            CoopIdentity_Describe(q->diff, &s->local, &s->peers[q->peer].identity, s->text, sizeof(s->text));
            ev->text = s->text;
            break;
        default:
            Event_Init(ev, COOP_SESSION_EV_LEFT, q->peer);
            ev->end = (CoopSessionEnd) q->end;
            ev->away = q->away;
            ev->text = CoopSession_EndText((CoopSessionEnd) q->end);
            break;
    }
    return true;
}

#define STARTED_TEXT "The level has already started, so nobody can join this game now."
#define UNKNOWN_TEXT "The host is not running the session this game was in, so it cannot come back to it."

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
    } else if (reasons & COOP_SESSION_REFUSE_UNKNOWN) {
        snprintf(s->text, sizeof(s->text), "%s", UNKNOWN_TEXT);
    } else if (reasons & COOP_SESSION_REFUSE_STARTED) {
        snprintf(s->text, sizeof(s->text), "%s", STARTED_TEXT);
    } else if (reasons == 0) {
        snprintf(s->text, sizeof(s->text), "%s", CoopSession_EndText(COOP_SESSION_END_NO_HANDSHAKE));
    } else {
        CoopIdentity_Describe(reasons, &s->local, &p->identity, s->text, sizeof(s->text));
    }
    ev->text = s->text;
}

// True if the identity the peer at `peer` sent goes with ours, with what only warns in *out. Otherwise the peer is
// refused, and ev says so.
static bool Host_Fits(CoopSession* s, int peer, uint32_t nowMs, CoopSessionEvent* ev, uint32_t* out) {
    uint32_t diff = CoopIdentity_Compare(&s->local, &s->peers[peer].identity);

    *out = diff;
    if (diff & COOP_ID_DIFF_REFUSE) {
        // Every bit goes along, the platform too, so both players see everything that differs.
        Host_Refuse(s, peer, diff, nowMs, ev);
        return false;
    }
    return true;
}

// The welcome: the player index, the session id, this game's identity. False if it could not go.
#define WELCOME_HEAD 9

static bool Host_Welcome(CoopSession* s, int peer) {
    uint16_t size;
    int i;

    s->buffer[0] = (uint8_t) (peer + 1);
    for (i = 0; i < 8; i++) {
        s->buffer[1 + i] = (uint8_t) (s->id >> (8 * i));
    }
    size = CoopIdentity_Write(&s->local, s->buffer + WELCOME_HEAD, sizeof(s->buffer) - WELCOME_HEAD);
    return size != 0 && s->t->ops->send(s->t, peer, COOP_MSG_SESSION_WELCOME, s->buffer,
                                        (uint16_t) (size + WELCOME_HEAD), COOP_NET_RELIABLE);
}

static uint64_t Id_Read(const uint8_t* data) {
    uint64_t id = 0;
    int i;

    for (i = 0; i < 8; i++) {
        id |= (uint64_t) data[i] << (8 * i);
    }
    return id;
}

// SESSION_REJOIN: the session id, the character the player flew, its identity. A player of this session whose slot is
// kept (or whose old connection still stands: then that one is dropped, as the player says it is dead) gets its
// character back, in the slot of the connection it came on; if that slot is kept for another player who is away, the
// two change places. Anyone else is refused: another id as UNKNOWN, a player nothing is kept for as STARTED.
#define REJOIN_HEAD 9

static void Host_Rejoin(CoopSession* s, int peer, const CoopNetEvent* net, uint32_t nowMs, CoopSessionEvent* ev) {
    Queued q = { COOP_SESSION_EV_JOINED, (int8_t) peer, 0, 0, (uint8_t) (peer + 1), false, 0 };
    uint32_t refuse = 0;
    uint8_t character = 0;
    int target = peer + 1;
    int slot = 0;
    int i;

    if (net->size < REJOIN_HEAD || !Identity_ReadAll(&s->peers[peer].identity, net->data + REJOIN_HEAD,
                                                     (uint16_t) (net->size - REJOIN_HEAD))) {
        refuse = COOP_SESSION_REFUSE_MALFORMED;
    } else if (!Host_Fits(s, peer, nowMs, ev, &q.diff)) {
        return;
    } else if (!s->started || s->id == 0 || Id_Read(net->data) != s->id) {
        refuse = COOP_SESSION_REFUSE_UNKNOWN;
    } else {
        character = net->data[8];
        for (i = 1; i < COOP_SESSION_PLAYERS; i++) {
            // A byte that is no wingman finds nobody: a free slot, which holds 0, is no player's and is refused below.
            if (s->roster.character[i] == character) {
                slot = i;
            }
        }
        if (slot != 0 && !s->away[slot].on && s->peers[slot - 1].state == PEER_JOINED) {
            Queued left = { COOP_SESSION_EV_LEFT, (int8_t) (slot - 1), COOP_SESSION_END_TIMEOUT, 0, 0, true, 0 };

            s->t->ops->disconnect(s->t, slot - 1);
            s->peers[slot - 1].state = PEER_FREE;
            s->away[slot].on = true;
            s->away[slot].since = nowMs;
            Host_Queue(s, &left);
            Host_RecoverBegin(s, COOP_RECOVER_LOST, (uint8_t) slot);
        }
        if (slot == 0 || !s->away[slot].on) {
            refuse = COOP_SESSION_REFUSE_STARTED;
        }
    }
    if (refuse != 0 || !Host_Welcome(s, peer)) {
        Host_Refuse(s, peer, refuse, nowMs, ev);
        return;
    }
    if (slot != target) {
        uint8_t other = s->roster.character[target];
        Away kept = s->away[target];

        s->roster.character[target] = character;
        s->roster.character[slot] = other;
        s->away[slot] = kept;
    }
    s->away[target].on = false;
    s->peers[peer].state = PEER_JOINED;
    Host_RosterChanged(s);
    Host_Queue(s, &q);
    Host_Queued(s, ev);
}

static void Host_Hello(CoopSession* s, int peer, const CoopNetEvent* net, uint32_t nowMs, CoopSessionEvent* ev) {
    Peer* p = &s->peers[peer];
    uint32_t diff;

    if (!Identity_ReadAll(&p->identity, net->data, net->size)) {
        Host_Refuse(s, peer, COOP_SESSION_REFUSE_MALFORMED, nowMs, ev);
        return;
    }
    if (s->started) {
        Host_Refuse(s, peer, COOP_SESSION_REFUSE_STARTED, nowMs, ev);
        return;
    }
    if (!Host_Fits(s, peer, nowMs, ev, &diff)) {
        return;
    }

    if (!Host_Welcome(s, peer)) {
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

// As a connection that timed out: the slot is kept if the session has an id, because a client whose connection broke
// one way stops sending input before either side's time-out says so, and it comes back the same way.
bool CoopSession_Kick(CoopSession* s, int peer, uint32_t nowMs) {
    Queued left = { COOP_SESSION_EV_LEFT, (int8_t) peer, COOP_SESSION_END_TIMEOUT, 0, 0, false, 0 };
    bool away;
    int slot;

    if (!s->isHost || !s->started || peer < 0 || peer >= COOP_NET_MAX_PEERS || s->peers[peer].state != PEER_JOINED ||
        (s->id == 0 && Host_Joined(s) < 2 && !Host_AnyAway(s))) {
        return false;
    }
    away = s->id != 0;
    slot = peer + 1;
    s->t->ops->disconnect(s->t, peer);
    s->peers[peer].state = PEER_FREE;
    if (away) {
        s->away[slot].on = true;
        s->away[slot].since = nowMs;
    } else {
        s->roster.character[slot] = COOP_CHARACTER_NO_PLAYER;
        Host_RosterChanged(s);
    }
    left.away = away;
    Host_Queue(s, &left);
    Host_RecoverBegin(s, away ? COOP_RECOVER_LOST : COOP_RECOVER_LEFT, (uint8_t) slot);
    return true;
}

bool CoopSession_Recover(CoopSession* s, uint8_t reason, uint32_t nowMs) {
    if (!s->started || reason < COOP_RECOVER_LOST || reason >= COOP_RECOVER_REASONS) {
        return false;
    }
    if (s->isHost) {
        if (Host_Joined(s) == 0 && !Host_AnyAway(s)) {
            return false;
        }
        Host_RecoverBegin(s, reason, 0xFF);
        return true;
    }
    if (s->client != CLIENT_ACTIVE) {
        return false;
    }
    if (!s->awaiting) {
        s->awaiting = true;
        s->awaitSince = nowMs;
    }
    s->recoverReason = reason;
    s->recoverEvent = true;
    return true;
}

static bool Host_Poll(CoopSession* s, uint32_t nowMs, CoopSessionEvent* ev) {
    CoopNetEvent net;

    Host_SendOwed(s);
    if (Host_Queued(s, ev) || Host_RosterEvent(s, ev) || Host_LevelEvent(s, ev) || Host_Tick(s, nowMs, ev) ||
        Host_Recovery(s, nowMs, ev)) {
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
                if (p->state == PEER_WAIT_HELLO && net.msgId == COOP_MSG_SESSION_REJOIN) {
                    Host_Rejoin(s, net.peer, &net, nowMs, ev);
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

// True if data is a level start this client can enter: a level there is, and a roster as above in which everyone
// holds a character, as the host starts no other way.
static bool LevelStart_Valid(const uint8_t* data, uint16_t size, uint8_t self) {
    const uint8_t* roster = data + LEVEL_START_HEAD;
    int i;

    if (size != LEVEL_START_SIZE || data[0] >= COOP_LEVEL_START_LEVELS || data[2] == 0 ||
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

// The host's save, ahead of the level start. To the session it is bytes; the game takes it at the level start
// (src/port/coop/game/save.h). True if ev was filled in: a save of another size ends the session.
static bool Client_Save(CoopSession* s, const CoopNetEvent* net, CoopSessionEvent* ev) {
    if (net->size != COOP_SAVE_SIZE) {
        Client_Broken(s, ev);
        return true;
    }
    memcpy(s->level.save, net->data, COOP_SAVE_SIZE);
    s->saveHeld = true;
    return false;
}

// A level start the host sent without its save first is from a broken host: the level would start on this game's own.
// The run number counts the level starts of a session: the first one this session object takes may be any but 0 (a
// client that came back has missed some), every later one is the next.
static void Client_LevelStart(CoopSession* s, const CoopNetEvent* net, CoopSessionEvent* ev) {
    if (!s->saveHeld || !LevelStart_Valid(net->data, net->size, s->player) ||
        (s->started && net->data[2] != (uint8_t) (s->level.run + 1) && !(s->level.run == 255 && net->data[2] == 1))) {
        Client_Broken(s, ev);
        return;
    }
    LevelStart_Read(&s->level, net->data);
    s->started = true;
    s->saveHeld = false;
    s->recoverHeard = false;
    s->awaiting = false;
    Event_LevelStart(s, ev);
}

// SESSION_RECOVER: the host is going to start the level again. From here on a save and a level start may come again.
static void Client_Recover(CoopSession* s, const CoopNetEvent* net, uint32_t nowMs, CoopSessionEvent* ev) {
    if (net->size != 2 || net->data[0] < COOP_RECOVER_LOST || net->data[0] >= COOP_RECOVER_REASONS ||
        (net->data[1] >= COOP_SESSION_PLAYERS && net->data[1] != 0xFF)) {
        Client_Broken(s, ev);
        return;
    }
    s->recoverHeard = true;
    s->awaiting = true;
    s->awaitSince = nowMs;
    Event_Recover(s, net->data[0], net->data[1], ev);
}

// True while a save and a level start are due: before the first level, and after SESSION_RECOVER.
static bool Client_Open(const CoopSession* s) {
    return !s->started || s->recoverHeard;
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

// What goes ahead of the identity: nothing in a hello, the session id and the character in a SESSION_REJOIN. Returns
// its size, written to the session's buffer.
static uint16_t Client_HelloHead(CoopSession* s) {
    int i;

    if (!s->rejoin) {
        return 0;
    }
    for (i = 0; i < 8; i++) {
        s->buffer[i] = (uint8_t) (s->id >> (8 * i));
    }
    s->buffer[8] = s->rejoinCharacter;
    return REJOIN_HEAD;
}

static void Client_Answer(CoopSession* s, const CoopNetEvent* net, CoopSessionEvent* ev) {
    CoopIdentity* host = &s->peers[0].identity;

    if (net->msgId == COOP_MSG_SESSION_WELCOME) {
        if (net->size < WELCOME_HEAD || net->data[0] < 1 || net->data[0] > COOP_NET_MAX_PEERS ||
            !Identity_ReadAll(host, net->data + WELCOME_HEAD, (uint16_t) (net->size - WELCOME_HEAD))) {
            s->t->ops->disconnect(s->t, 0);
            Client_End(s, COOP_SESSION_END_BAD_MESSAGE, ev);
            return;
        }
        // A client that comes back takes no other session than its own: a host that restarted has a new id.
        if (s->rejoin && Id_Read(net->data + 1) != s->id) {
            s->t->ops->disconnect(s->t, 0);
            Client_End(s, COOP_SESSION_END_OTHER_SESSION, ev);
            return;
        }
        s->id = Id_Read(net->data + 1);
        s->client = CLIENT_ACTIVE;
        s->player = net->data[0];
        Event_Init(ev, COOP_SESSION_EV_JOINED, 0);
        ev->playerIndex = net->data[0];
        ev->rejoined = s->rejoin;
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
    } else if (ev->diff & COOP_SESSION_REFUSE_UNKNOWN) {
        snprintf(s->text, sizeof(s->text), "%s", UNKNOWN_TEXT);
    } else if (ev->diff & COOP_SESSION_REFUSE_STARTED) {
        snprintf(s->text, sizeof(s->text), "%s", STARTED_TEXT);
    } else if ((ev->diff & COOP_ID_DIFF_ALL) == 0) {
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
    if (s->recoverEvent) {
        s->recoverEvent = false;
        Event_Recover(s, s->recoverReason, 0xFF, ev);
        return true;
    }
    // The host said, or this game saw, that the level cannot go on, and no new level start came.
    if (s->awaiting && nowMs - s->awaitSince >= COOP_SESSION_RECOVER_MS) {
        s->t->ops->disconnect(s->t, 0);
        Client_End(s, COOP_SESSION_END_NO_RECOVERY, ev);
        return true;
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
                uint16_t head = Client_HelloHead(s);
                uint16_t size = CoopIdentity_Write(&s->local, s->buffer + head, (uint16_t) (sizeof(s->buffer) - head));

                if (size == 0 || !s->t->ops->send(s->t, 0, s->rejoin ? COOP_MSG_SESSION_REJOIN : COOP_MSG_SESSION_HELLO,
                                                  s->buffer, (uint16_t) (size + head), COOP_NET_RELIABLE)) {
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
                if (s->client == CLIENT_ACTIVE && s->started && net.msgId == COOP_MSG_SESSION_RECOVER) {
                    Client_Recover(s, &net, nowMs, ev);
                    return true;
                }
                if (s->client == CLIENT_ACTIVE && Client_Open(s) && !s->saveHeld && net.msgId == COOP_MSG_SESSION_SAVE) {
                    if (Client_Save(s, &net, ev)) {
                        return true;
                    }
                    break;
                }
                if (s->client == CLIENT_ACTIVE && Client_Open(s) && net.msgId == COOP_MSG_SESSION_LEVEL_START) {
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

// ---- The co-op log (#172) ----

// How often the transport's counters go into the log, per joined peer.
#define LOG_NET_MS 5000

static bool Log_Send(void* user, int peer, const uint8_t* data, uint16_t size) {
    CoopSession* s = user;

    return CoopSession_Send(s, peer, COOP_MSG_DIAG_LOG, data, size, COOP_NET_RELIABLE);
}

static bool Log_Pending(void* user, int peer, uint32_t* pending) {
    CoopNetStats stats;

    if (!CoopSession_Stats((CoopSession*) user, peer, &stats)) {
        return false;
    }
    *pending = stats.pending;
    return true;
}

static int Log_Player(const CoopSession* s, int peer) {
    return s->isHost ? peer + 1 : 0;
}

// The transport's counters since the last line: ping, resends, duplicates, what was dropped unread.
static void Log_Net(CoopSession* s, uint32_t nowMs) {
    int i;

    if (nowMs - s->statsMs < LOG_NET_MS) {
        return;
    }
    s->statsMs = nowMs;
    for (i = 0; i < COOP_NET_MAX_PEERS; i++) {
        CoopNetStats now;
        CoopNetStats* last = &s->lastStats[i];

        if (!CoopSession_Stats(s, i, &now)) {
            memset(last, 0, sizeof(*last));
            continue;
        }
        CoopLog_Write(COOP_LOG_NET, false,
                      "player %d: ping %u ms (%u to %u), in %us: sent %u, received %u, resent %u, duplicates %u, "
                      "dropped unread %u, pending %u, bad datagrams %u, stray %u",
                      Log_Player(s, i) + 1, (unsigned) now.rttMs, (unsigned) now.rttMinMs, (unsigned) now.rttMaxMs,
                      (unsigned) (LOG_NET_MS / 1000), (unsigned) (now.sent - last->sent),
                      (unsigned) (now.received - last->received), (unsigned) (now.resent - last->resent),
                      (unsigned) (now.duplicates - last->duplicates), (unsigned) (now.overflow - last->overflow),
                      (unsigned) now.pending, (unsigned) (now.badDatagrams - last->badDatagrams),
                      (unsigned) (now.strayDatagrams - last->strayDatagrams));
        *last = now;
    }
}

static const char* Log_Character(uint8_t character) {
    const char* name = CoopSession_CharacterName(character);

    return (name[0] != '\0') ? name : (character == COOP_CHARACTER_UNPICKED) ? "unpicked" : "-";
}

// What the owner of the session is about to hear, in the log too.
static void Log_Event(CoopSession* s, const CoopSessionEvent* ev) {
    const CoopRoster* roster;

    switch (ev->type) {
        case COOP_SESSION_EV_JOINED:
            if (s->isHost) {
                CoopLog_PeerOpen(ev->peer);
                CoopLog_Write(COOP_LOG_SESSION, false, "player %d %s%s%s", ev->playerIndex + 1,
                              ev->rejoined ? "came back" : "joined", (ev->text[0] != '\0') ? "; " : "", ev->text);
                Log_Identity("its game", &s->peers[ev->peer].identity);
            } else {
                CoopLog_SetPlayer(ev->playerIndex);
                CoopLog_PeerOpen(0);
                CoopLog_Write(COOP_LOG_SESSION, false, "%s as player %d%s%s", ev->rejoined ? "came back" : "joined",
                              ev->playerIndex + 1, (ev->text[0] != '\0') ? "; " : "", ev->text);
                Log_Identity("the host's game", &s->peers[0].identity);
            }
            break;
        case COOP_SESSION_EV_REFUSED:
            if (s->isHost) {
                CoopLog_Write(COOP_LOG_SESSION, false, "refused a joining game (reasons 0x%x): %s", (unsigned) ev->diff,
                              ev->text);
                if ((ev->diff & ~(uint32_t) COOP_SESSION_REFUSE_MALFORMED) != 0) {
                    Log_Identity("its game", &s->peers[ev->peer].identity);
                }
            } else {
                CoopLog_Write(COOP_LOG_SESSION, false, "the host refused this game (reasons 0x%x): %s",
                              (unsigned) ev->diff, ev->text);
            }
            break;
        case COOP_SESSION_EV_LEFT:
            CoopLog_Write(COOP_LOG_SESSION, false, "player %d left: %s%s", ev->peer + 2, ev->text,
                          ev->away ? " Its slot is kept for a while." : "");
            CoopLog_PeerClose(ev->peer);
            break;
        case COOP_SESSION_EV_RECOVER:
            CoopLog_Write(COOP_LOG_SESSION, false, "recover (reason %d): %s", (int) ev->reason, ev->text);
            break;
        case COOP_SESSION_EV_ENDED:
            CoopLog_Write(COOP_LOG_SESSION, false, "session ended: %s", ev->text);
            CoopLog_PeerClose(0);
            break;
        case COOP_SESSION_EV_ROSTER:
            roster = CoopSession_Roster(s);
            if (roster != NULL) {
                CoopLog_Write(COOP_LOG_SESSION, false, "roster: %s, %s, %s, %s", Log_Character(roster->character[0]),
                              Log_Character(roster->character[1]), Log_Character(roster->character[2]),
                              Log_Character(roster->character[3]));
            }
            break;
        case COOP_SESSION_EV_PICK_REFUSED:
            if (s->isHost) {
                CoopLog_Write(COOP_LOG_SESSION, false, "player %d asked for %s, which is taken", ev->peer + 2,
                              Log_Character(ev->character));
            } else {
                CoopLog_Write(COOP_LOG_SESSION, false, "the host refused %s: %s", Log_Character(ev->character),
                              ev->text);
            }
            break;
        case COOP_SESSION_EV_LEVEL_START:
            CoopLog_Write(COOP_LOG_SESSION, false, "level start: level %d, phase %d, roster %s, %s, %s, %s, run %d",
                          (int) ev->levelStart->level, (int) ev->levelStart->phase,
                          Log_Character(ev->levelStart->roster[0]), Log_Character(ev->levelStart->roster[1]),
                          Log_Character(ev->levelStart->roster[2]), Log_Character(ev->levelStart->roster[3]),
                          (int) ev->levelStart->run);
            break;
        case COOP_SESSION_EV_MESSAGE:
            break;
    }
}

bool CoopSession_Poll(CoopSession* s, uint32_t nowMs, CoopSessionEvent* ev) {
    s->nowMs = nowMs;
    CoopLog_Pump(nowMs, Log_Send, Log_Pending, s);
    Log_Net(s, nowMs);
    while (s->isHost ? Host_Poll(s, nowMs, ev) : Client_Poll(s, nowMs, ev)) {
        // A log batch is the log's and nobody else's: it never reaches the owner, and one that cannot be read is
        // dropped and counted, never the end of a session.
        if (ev->type == COOP_SESSION_EV_MESSAGE && ev->msgId == COOP_MSG_DIAG_LOG) {
            CoopLog_Receive(ev->peer, s->isHost, ev->data, ev->size);
            continue;
        }
        Log_Event(s, ev);
        return true;
    }
    return false;
}

#endif
