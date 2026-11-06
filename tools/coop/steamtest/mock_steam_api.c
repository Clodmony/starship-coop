// A stand-in for the Steam library, for tests on one PC: one Steam account cannot talk to itself. It exports the flat
// API functions of steam_backend_flat.c under their real names and carries messages over loopback UDP, so
// coop_steam_test live runs exactly as it would on Steam, with no Steam and no SDK. Not for a lossy link: reliable
// messages get no resends. Linux and macOS only.
//
// The structs below follow the field order of Valve's public headers, on purpose not the offsets the backend uses, so a
// mismatch between the two shows up here. A mock cannot prove those offsets right: only a run on Steam can.
//
// Environment:
//   COOP_MOCK_STEAM_ID       this user's SteamID. Its UDP port is the id minus MOCK_ID_BASE.
//   COOP_MOCK_STEAM_INIT     the code SteamAPI_InitFlat returns, default 0
//   COOP_MOCK_STEAM_FAIL_MS  so long after the first message, the session with its sender fails
//   COOP_MOCK_STEAM_LOG      where to write the counters at shutdown, as JSON
//
// MOCK_OLD_LIBRARY leaves out SteamAPI_InitFlat, as Steam libraries before it did.

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define MOCK_ID_BASE 76561190000000000ull
#define MOCK_PIPE 7
#define MAX_PEERS 16
#define MAX_CALLBACKS 64
#define PACKET_HEADER 9
#define MAX_PACKET 2048

#define RESULT_OK 1
#define RESULT_NO_CONNECTION 3
#define IDENTITY_STEAM_ID 16
#define CALLBACK_SESSION_REQUEST 1251
#define CALLBACK_SESSION_FAILED 1252
#define SEND_NO_NAGLE 1
#define SEND_RELIABLE 8

typedef struct {
    int32_t type;
    int32_t size;
    union {
        uint64_t steamId;
        uint8_t raw[128];
    } value;
} Identity;

typedef struct Message {
    void* data;
    int size;
    uint32_t connection;
    Identity peer;
    int64_t connectionUserData;
    int64_t timeReceived;
    int64_t messageNumber;
    void (*freeData)(struct Message*);
    void (*release)(struct Message*);
    int channel;
    int flags;
    int64_t userData;
    uint16_t lane;
    uint16_t pad;
    struct Message* next; // past the end of the real struct
} Message;

typedef struct {
    int32_t user;
    int32_t id;
    uint8_t* param;
    int32_t paramSize;
} CallbackMsg;

typedef struct {
    int32_t id;
    Identity identity;
} Callback;

typedef struct {
    uint64_t id;
    bool accepted;
    bool requested;
    bool broken;
} Peer;

static struct {
    int socket;
    uint64_t self;
    bool initialized;
    bool manualDispatch;
    bool inCallback;
    Peer peers[MAX_PEERS];
    Callback callbacks[MAX_CALLBACKS];
    uint32_t callbackHead;
    uint32_t callbackCount;
    Callback current;
    Message* inboxHead;
    Message* inboxTail;
    Message* heldHead;
    Message* heldTail;
    uint64_t failAt;
    uint64_t failMs;
    uint64_t failPeer;
    uint32_t sent;
    uint32_t received;
    uint32_t released;
    uint32_t callbacksRead;
    uint32_t callbacksFreed;
    uint32_t accepted;
    uint32_t closed;
    uint32_t relayInits;
    uint32_t errors; // the backend used the API in a way the real one would not take
} sMock;

// Three interfaces, each with its own address, so that mixing them up is seen.
static char sUser;
static char sMessages;
static char sUtils;

static uint64_t Mock_NowMs(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000 + (uint64_t) ts.tv_nsec / 1000000;
}

static void Mock_Self(const void* self, const void* expected) {
    if (self != expected) {
        sMock.errors++;
    }
}

static Peer* Mock_Peer(uint64_t id) {
    int i;

    for (i = 0; i < MAX_PEERS; i++) {
        if (sMock.peers[i].id == id) {
            return &sMock.peers[i];
        }
    }
    for (i = 0; i < MAX_PEERS; i++) {
        if (sMock.peers[i].id == 0) {
            sMock.peers[i].id = id;
            return &sMock.peers[i];
        }
    }
    abort();
}

static void Mock_Post(int32_t id, uint64_t peer) {
    if (sMock.callbackCount < MAX_CALLBACKS) {
        Callback* c = &sMock.callbacks[(sMock.callbackHead + sMock.callbackCount) % MAX_CALLBACKS];

        memset(c, 0, sizeof(*c));
        c->id = id;
        c->identity.type = IDENTITY_STEAM_ID;
        c->identity.size = 8;
        c->identity.value.steamId = peer;
        sMock.callbackCount++;
    }
}

static void Mock_Append(Message** head, Message** tail, Message* m) {
    m->next = NULL;
    if (*tail != NULL) {
        (*tail)->next = m;
    } else {
        *head = m;
    }
    *tail = m;
}

static void Mock_Release(Message* m) {
    sMock.released++;
    free(m->data);
    free(m);
}

// What arrived on the socket: it goes to the inbox, or waits for AcceptSessionWithUser.
static void Mock_Pump(void) {
    uint8_t packet[MAX_PACKET];
    ssize_t size;

    while ((size = recv(sMock.socket, packet, sizeof(packet), 0)) >= PACKET_HEADER) {
        Message* m = calloc(1, sizeof(Message));
        uint64_t from;
        Peer* peer;

        memcpy(&from, packet, 8);
        m->size = (int) size - PACKET_HEADER;
        m->data = malloc((size_t) m->size + 1);
        memcpy(m->data, packet + PACKET_HEADER, (size_t) m->size);
        m->peer.type = IDENTITY_STEAM_ID;
        m->peer.size = 8;
        m->peer.value.steamId = from;
        m->release = Mock_Release;
        peer = Mock_Peer(from);
        if (sMock.failMs != 0 && sMock.failAt == 0) {
            sMock.failAt = Mock_NowMs() + sMock.failMs;
            sMock.failPeer = from;
        }
        if (peer->accepted) {
            Mock_Append(&sMock.inboxHead, &sMock.inboxTail, m);
        } else {
            Mock_Append(&sMock.heldHead, &sMock.heldTail, m);
            if (!peer->requested) {
                peer->requested = true;
                Mock_Post(CALLBACK_SESSION_REQUEST, from);
            }
        }
    }
    if (sMock.failAt != 0 && Mock_NowMs() >= sMock.failAt) {
        sMock.failAt = 0;
        sMock.failMs = 0;
        Mock_Peer(sMock.failPeer)->broken = true;
        Mock_Post(CALLBACK_SESSION_FAILED, sMock.failPeer);
    }
}

static void Mock_Accept(Peer* peer) {
    Message* m = sMock.heldHead;

    peer->accepted = true;
    sMock.heldHead = NULL;
    sMock.heldTail = NULL;
    while (m != NULL) {
        Message* next = m->next;

        if (m->peer.value.steamId == peer->id) {
            Mock_Append(&sMock.inboxHead, &sMock.inboxTail, m);
        } else {
            Mock_Append(&sMock.heldHead, &sMock.heldTail, m);
        }
        m = next;
    }
}

#ifndef MOCK_OLD_LIBRARY
static int Mock_Start(char* error) {
    const char* id = getenv("COOP_MOCK_STEAM_ID");
    const char* code = getenv("COOP_MOCK_STEAM_INIT");
    const char* fail = getenv("COOP_MOCK_STEAM_FAIL_MS");
    struct sockaddr_in address;

    memset(&sMock, 0, sizeof(sMock));
    if (code != NULL && atoi(code) != 0) {
        snprintf(error, 1024, "mock: Steam is not running");
        return atoi(code);
    }
    if (id == NULL) {
        snprintf(error, 1024, "mock: COOP_MOCK_STEAM_ID is not set");
        return 1;
    }
    sMock.self = strtoull(id, NULL, 10);
    sMock.failMs = fail != NULL ? strtoull(fail, NULL, 10) : 0;
    sMock.socket = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons((uint16_t) (sMock.self - MOCK_ID_BASE));
    if (sMock.socket < 0 || bind(sMock.socket, (struct sockaddr*) &address, sizeof(address)) != 0) {
        snprintf(error, 1024, "mock: no socket for this id");
        return 1;
    }
    fcntl(sMock.socket, F_SETFL, O_NONBLOCK);
    sMock.initialized = true;
    return 0;
}

int SteamAPI_InitFlat(char* error) {
    return Mock_Start(error);
}
#endif

void SteamAPI_Shutdown(void) {
    const char* path = getenv("COOP_MOCK_STEAM_LOG");

    if (path != NULL) {
        FILE* f = fopen(path, "w");

        if (f != NULL) {
            fprintf(f,
                    "{\"sent\":%u,\"received\":%u,\"released\":%u,\"callbacks_read\":%u,\"callbacks_freed\":%u,"
                    "\"accepted\":%u,\"closed\":%u,\"relay_inits\":%u,\"errors\":%u}\n",
                    sMock.sent, sMock.received, sMock.released, sMock.callbacksRead, sMock.callbacksFreed,
                    sMock.accepted, sMock.closed, sMock.relayInits, sMock.errors);
            fclose(f);
        }
    }
    close(sMock.socket);
    sMock.initialized = false;
}

int32_t SteamAPI_GetHSteamPipe(void) {
    return MOCK_PIPE;
}

void SteamAPI_ManualDispatch_Init(void) {
    sMock.errors += !sMock.initialized;
    sMock.manualDispatch = true;
}

void SteamAPI_ManualDispatch_RunFrame(int32_t pipe) {
    sMock.errors += pipe != MOCK_PIPE || !sMock.manualDispatch;
    Mock_Pump();
}

bool SteamAPI_ManualDispatch_GetNextCallback(int32_t pipe, CallbackMsg* out) {
    sMock.errors += pipe != MOCK_PIPE || !sMock.manualDispatch || sMock.inCallback;
    if (sMock.callbackCount == 0) {
        return false;
    }
    sMock.current = sMock.callbacks[sMock.callbackHead];
    sMock.callbackHead = (sMock.callbackHead + 1) % MAX_CALLBACKS;
    sMock.callbackCount--;
    sMock.inCallback = true;
    sMock.callbacksRead++;
    out->user = 1;
    out->id = sMock.current.id;
    out->param = (uint8_t*) &sMock.current.identity;
    out->paramSize = (int32_t) sizeof(Identity);
    return true;
}

void SteamAPI_ManualDispatch_FreeLastCallback(int32_t pipe) {
    sMock.errors += pipe != MOCK_PIPE || !sMock.inCallback;
    sMock.inCallback = false;
    sMock.callbacksFreed++;
}

void* SteamAPI_SteamUser_v023(void) {
    return &sUser;
}

uint64_t SteamAPI_ISteamUser_GetSteamID(void* self) {
    Mock_Self(self, &sUser);
    return sMock.self;
}

bool SteamAPI_ISteamUser_BLoggedOn(void* self) {
    Mock_Self(self, &sUser);
    return true;
}

void* SteamAPI_SteamNetworkingMessages_SteamAPI_v002(void) {
    return &sMessages;
}

void* SteamAPI_SteamNetworkingUtils_SteamAPI_v004(void) {
    return &sUtils;
}

void SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess(void* self) {
    Mock_Self(self, &sUtils);
    sMock.relayInits++;
}

int SteamAPI_ISteamNetworkingMessages_SendMessageToUser(void* self, const Identity* to, const void* data, uint32_t size,
                                                        int flags, int channel) {
    uint8_t packet[MAX_PACKET];
    struct sockaddr_in address;
    Peer* peer;

    Mock_Self(self, &sMessages);
    sMock.errors += channel != 0 || (flags != SEND_NO_NAGLE && flags != (SEND_RELIABLE | SEND_NO_NAGLE)) ||
                    size + PACKET_HEADER > sizeof(packet) || to->type != IDENTITY_STEAM_ID;
    peer = Mock_Peer(to->value.steamId);
    if (peer->broken) {
        return RESULT_NO_CONNECTION;
    }
    Mock_Accept(
        peer); // "Sending a message to a host will also implicitly accept any incoming connection from that host."
    memcpy(packet, &sMock.self, 8);
    packet[8] = (uint8_t) flags;
    memcpy(packet + PACKET_HEADER, data, size);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons((uint16_t) (to->value.steamId - MOCK_ID_BASE));
    sendto(sMock.socket, packet, size + PACKET_HEADER, 0, (struct sockaddr*) &address, sizeof(address));
    sMock.sent++;
    return RESULT_OK;
}

int SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel(void* self, int channel, Message** out, int max) {
    Mock_Self(self, &sMessages);
    sMock.errors += channel != 0 || max < 1;
    Mock_Pump();
    if (sMock.inboxHead == NULL) {
        return 0;
    }
    *out = sMock.inboxHead;
    sMock.inboxHead = sMock.inboxHead->next;
    if (sMock.inboxHead == NULL) {
        sMock.inboxTail = NULL;
    }
    sMock.received++;
    return 1;
}

bool SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser(void* self, const Identity* who) {
    Mock_Self(self, &sMessages);
    sMock.accepted++;
    Mock_Accept(Mock_Peer(who->value.steamId));
    return true;
}

bool SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser(void* self, const Identity* who) {
    Peer* peer;

    Mock_Self(self, &sMessages);
    sMock.closed++;
    peer = Mock_Peer(who->value.steamId);
    peer->broken = false;
    peer->accepted = false;
    peer->requested = false;
    return true;
}

void SteamAPI_SteamNetworkingMessage_t_Release(Message* message) {
    message->release(message);
}

void SteamAPI_SteamNetworkingIdentity_SetSteamID64(Identity* self, uint64_t id) {
    memset(self, 0, sizeof(*self));
    self->type = IDENTITY_STEAM_ID;
    self->size = 8;
    self->value.steamId = id;
}

uint64_t SteamAPI_SteamNetworkingIdentity_GetSteamID64(Identity* self) {
    return self->type == IDENTITY_STEAM_ID ? self->value.steamId : 0;
}
