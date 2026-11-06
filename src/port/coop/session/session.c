// Co-op session and handshake (WP-21). Messages and their layout: docs/coop/PROTOCOL.md, ids 0x0010-0x0012.
//
// Host, per peer: WAIT_HELLO from the transport's CONNECTED, then JOINED or REFUSED. A refused peer stays until the
// refusal is acknowledged, the client goes, or COOP_SESSION_REFUSE_LINGER_MS pass; the transport's disconnect drops
// unacknowledged messages, so leaving at once would lose the refusal. Client: CONNECTING, WAIT_ANSWER, ACTIVE, ENDED.

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
} Peer;

struct CoopSession {
    CoopTransport* t;
    bool isHost;
    CoopIdentity local;
    Peer peers[COOP_NET_MAX_PEERS];
    ClientState client;
    uint32_t clientSince;
    bool endPending; // client: an ENDED event is owed after the REFUSED one
    CoopSessionEnd pendingEnd;
    uint32_t ignored;
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

CoopSession* CoopSession_Host(CoopTransport* transport, const CoopIdentity* identity) {
    return Session_New(transport, identity, true);
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

bool CoopSession_Send(CoopSession* s, int peer, uint16_t msgId, const void* data, uint16_t size, CoopNetLane lane) {
    if (msgId < COOP_MSG_SESSION_END || CoopSession_PeerIdentity(s, peer) == NULL) {
        return false;
    }
    return s->t->ops->send(s->t, peer, msgId, data, size, lane);
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
            return "The other side sent a handshake this game cannot read. Both players need the same Starship "
                   "Co-op build.";
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

static void Event_Init(CoopSessionEvent* ev, CoopSessionEventType type, int peer) {
    memset(ev, 0, sizeof(*ev));
    ev->type = type;
    ev->peer = peer;
    ev->text = "";
}

// ---- Host ----

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

    if (CoopIdentity_Read(&p->identity, net->data, net->size) != net->size) {
        Host_Refuse(s, peer, COOP_SESSION_REFUSE_MALFORMED, nowMs, ev);
        return;
    }
    diff = CoopIdentity_Compare(&s->local, &p->identity);
    if (diff & COOP_ID_DIFF_REFUSE) {
        // The platform warning goes along, so both players see everything that differs.
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

static bool Host_Poll(CoopSession* s, uint32_t nowMs, CoopSessionEvent* ev) {
    CoopNetEvent net;

    if (Host_Tick(s, nowMs, ev)) {
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
                    Event_Init(ev, COOP_SESSION_EV_LEFT, net.peer);
                    ev->end = End_FromNet(net.end);
                    ev->text = CoopSession_EndText(ev->end);
                    return true;
                }
                break;
            }
            case COOP_NET_EVENT_MESSAGE:
                if (p->state == PEER_WAIT_HELLO && net.msgId == COOP_MSG_SESSION_HELLO) {
                    Host_Hello(s, net.peer, &net, nowMs, ev);
                    return true;
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

static void Client_Answer(CoopSession* s, const CoopNetEvent* net, CoopSessionEvent* ev) {
    CoopIdentity* host = &s->peers[0].identity;

    if (net->msgId == COOP_MSG_SESSION_WELCOME) {
        if (net->size < 1 || net->data[0] < 1 || net->data[0] > COOP_NET_MAX_PEERS ||
            CoopIdentity_Read(host, net->data + 1, (uint16_t) (net->size - 1)) != net->size - 1) {
            s->t->ops->disconnect(s->t, 0);
            Client_End(s, COOP_SESSION_END_BAD_MESSAGE, ev);
            return;
        }
        s->client = CLIENT_ACTIVE;
        Event_Init(ev, COOP_SESSION_EV_JOINED, 0);
        ev->playerIndex = net->data[0];
        ev->diff = CoopIdentity_Compare(host, &s->local);
        CoopIdentity_Describe(ev->diff, host, &s->local, s->text, sizeof(s->text));
        ev->text = s->text;
        return;
    }

    // SESSION_REFUSE. We leave at once: the host waits for our goodbye or for the ack.
    s->t->ops->disconnect(s->t, 0);
    if (net->size < 4 || CoopIdentity_Read(host, net->data + 4, (uint16_t) (net->size - 4)) != net->size - 4) {
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
