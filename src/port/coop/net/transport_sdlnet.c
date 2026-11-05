// The co-op transport over UDP with SDL2_net (decision 0004). Sockets only; everything else is in net_core.c.

#ifdef USE_NETWORKING

#include "transport.h"

#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_net.h>

#include "net_core.h"

// Per poll, so that a flood on the port cannot hold up a game frame.
#define MAX_RECEIVES_PER_PUMP 256

typedef struct {
    CoopTransport base;
    CoopNetCore* core;
    UDPsocket socket;
    UDPpacket* rx;
    UDPpacket* tx;
    uint16_t port;
} SdlTransport;

static CoopNetAddr Addr_Pack(const IPaddress* ip) {
    return ((CoopNetAddr) ip->host << 16) | ip->port;
}

static void Sdl_Tx(void* user, CoopNetAddr to, const uint8_t* data, uint16_t size) {
    SdlTransport* s = user;

    memcpy(s->tx->data, data, size);
    s->tx->len = size;
    s->tx->address.host = (Uint32) (to >> 16);
    s->tx->address.port = (Uint16) to;
    // A failed send is the same as a lost packet, and handled the same way.
    SDLNet_UDP_Send(s->socket, -1, s->tx);
}

static void Sdl_Pump(SdlTransport* s) {
    int i;

    for (i = 0; i < MAX_RECEIVES_PER_PUMP && SDLNet_UDP_Recv(s->socket, s->rx) > 0; i++) {
        CoopNetCore_Receive(s->core, Addr_Pack(&s->rx->address), s->rx->data, (uint32_t) s->rx->len, SDL_GetTicks());
    }
    CoopNetCore_Update(s->core, SDL_GetTicks());
}

static bool Sdl_Send(CoopTransport* t, int peer, uint16_t msgId, const void* data, uint16_t size, CoopNetLane lane) {
    return CoopNetCore_Send(((SdlTransport*) t)->core, peer, msgId, data, size, lane, SDL_GetTicks());
}

static bool Sdl_Poll(CoopTransport* t, CoopNetEvent* ev) {
    SdlTransport* s = (SdlTransport*) t;

    if (CoopNetCore_Poll(s->core, ev)) {
        return true;
    }
    Sdl_Pump(s);
    return CoopNetCore_Poll(s->core, ev);
}

static void Sdl_Disconnect(CoopTransport* t, int peer) {
    CoopNetCore_Disconnect(((SdlTransport*) t)->core, peer);
}

static bool Sdl_Stats(CoopTransport* t, int peer, CoopNetStats* out) {
    return CoopNetCore_Stats(((SdlTransport*) t)->core, peer, out);
}

static void Sdl_Free(SdlTransport* s) {
    CoopNetCore_Destroy(s->core);
    if (s->rx != NULL) {
        SDLNet_FreePacket(s->rx);
    }
    if (s->tx != NULL) {
        SDLNet_FreePacket(s->tx);
    }
    if (s->socket != NULL) {
        SDLNet_UDP_Close(s->socket);
    }
    SDLNet_Quit();
    free(s);
}

static void Sdl_Close(CoopTransport* t) {
    Sdl_Free((SdlTransport*) t);
}

static const CoopTransportOps sSdlOps = {
    Sdl_Send, Sdl_Poll, Sdl_Disconnect, Sdl_Stats, Sdl_Close,
};

static SdlTransport* Sdl_Open(uint16_t port) {
    SdlTransport* s;
    IPaddress* local;

    if (SDLNet_Init() != 0) {
        return NULL;
    }
    s = calloc(1, sizeof(SdlTransport));
    if (s == NULL) {
        SDLNet_Quit();
        return NULL;
    }
    s->base.ops = &sSdlOps;
    s->socket = SDLNet_UDP_Open(port);
    // One byte more than a datagram may have, so that an oversized one fails the size check instead of being cut.
    s->rx = SDLNet_AllocPacket(COOP_NET_MAX_DATAGRAM + 1);
    s->tx = SDLNet_AllocPacket(COOP_NET_MAX_DATAGRAM);
    if (s->socket == NULL || s->rx == NULL || s->tx == NULL) {
        Sdl_Free(s);
        return NULL;
    }
    local = SDLNet_UDP_GetPeerAddress(s->socket, -1);
    s->port = local != NULL ? SDLNet_Read16(&local->port) : port;
    return s;
}

CoopTransport* CoopNetSdl_Host(uint16_t port, const CoopNetConfig* config) {
    SdlTransport* s = Sdl_Open(port);

    if (s == NULL) {
        return NULL;
    }
    s->core = CoopNetCore_CreateHost(config, Sdl_Tx, s);
    if (s->core == NULL) {
        Sdl_Free(s);
        return NULL;
    }
    return &s->base;
}

CoopTransport* CoopNetSdl_Connect(const char* host, uint16_t port, const CoopNetConfig* config) {
    SdlTransport* s = Sdl_Open(0);
    IPaddress address;
    uint32_t nonce;

    if (s == NULL) {
        return NULL;
    }
    if (SDLNet_ResolveHost(&address, host, port) != 0) {
        Sdl_Free(s);
        return NULL;
    }
    // Only has to differ from the last connection on this address, so a clock is enough.
    nonce = (uint32_t) SDL_GetPerformanceCounter() * 2654435761u + SDL_GetTicks();
    s->core = CoopNetCore_CreateClient(config, Sdl_Tx, s, Addr_Pack(&address), nonce, SDL_GetTicks());
    if (s->core == NULL) {
        Sdl_Free(s);
        return NULL;
    }
    return &s->base;
}

uint16_t CoopNetSdl_Port(CoopTransport* t) {
    return ((SdlTransport*) t)->port;
}

#endif
