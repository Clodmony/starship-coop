// The real backend of the Steam transport: Steam's flat C API, loaded at run time. No Steamworks header or library is
// needed to build this, and none is in this repository; the player's own copy of the library is loaded when asked for.
// As the hub's Steam page describes:
// https://github.com/cranebeaker/coop-anything/blob/main/playbook/networking-steam-spacewar.md
//
// Everything Steam-specific that a compiler cannot check is here, and none of it has run on Steam yet:
// the function names were compared with the export table of a shipped library; the numbers and the field order come
// from Valve's public headers and the hub's page.

#ifdef USE_STEAM_RELAY

#include "transport_steam.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#define APP_ID "480" // Spacewar, Valve's sample app: every Steam user has it
#define APP_ID_FILE "steam_appid.txt"

#if defined(_WIN32)
#define LIBRARY_NAME "steam_api64.dll"
#elif defined(__APPLE__)
#define LIBRARY_NAME "libsteam_api.dylib"
#else
#define LIBRARY_NAME "libsteam_api.so"
#endif

#define INIT_OK 0
#define INIT_NO_STEAM_CLIENT 2
#define INIT_VERSION_MISMATCH 3

#define RESULT_OK 1
#define RESULT_NO_CONNECTION 3 // the session failed or the peer closed it

#define SEND_NO_NAGLE 1
#define SEND_RELIABLE 8

#define CALLBACK_SESSION_REQUEST 1251
#define CALLBACK_SESSION_FAILED 1252

#define CHANNEL 0 // one channel: order only holds within a channel, and control messages must keep their place

// SteamNetworkingIdentity is 136 bytes, used only through Steam's own functions. This has room to spare.
#define IDENTITY_WORDS 20

// SteamNetworkingMessage_t on 64-bit: void* data, int size, a connection handle, then the sender's identity.
#define MESSAGE_SIZE_OFFSET 8
#define MESSAGE_SENDER_OFFSET 16

// CallbackMsg_t
typedef struct {
    int32_t user;
    int32_t id;
    uint8_t* param;
    int32_t paramSize;
} SteamCallback;

#define STEAM_EXPORTS(X)                                                                                               \
    X(int, SteamAPI_InitFlat, (char* error))                                                                           \
    X(void, SteamAPI_Shutdown, (void) )                                                                                \
    X(int32_t, SteamAPI_GetHSteamPipe, (void) )                                                                        \
    X(void, SteamAPI_ManualDispatch_Init, (void) )                                                                     \
    X(void, SteamAPI_ManualDispatch_RunFrame, (int32_t pipe))                                                          \
    X(bool, SteamAPI_ManualDispatch_GetNextCallback, (int32_t pipe, SteamCallback* out))                               \
    X(void, SteamAPI_ManualDispatch_FreeLastCallback, (int32_t pipe))                                                  \
    X(void*, SteamAPI_SteamUser_v023, (void) )                                                                         \
    X(uint64_t, SteamAPI_ISteamUser_GetSteamID, (void* self))                                                          \
    X(bool, SteamAPI_ISteamUser_BLoggedOn, (void* self))                                                               \
    X(void*, SteamAPI_SteamNetworkingMessages_SteamAPI_v002, (void) )                                                  \
    X(void*, SteamAPI_SteamNetworkingUtils_SteamAPI_v004, (void) )                                                     \
    X(void, SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess, (void* self))                                       \
    X(int, SteamAPI_ISteamNetworkingMessages_SendMessageToUser,                                                        \
      (void* self, const void* identity, const void* data, uint32_t size, int flags, int channel))                     \
    X(int, SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel, (void* self, int channel, void** out, int max)) \
    X(bool, SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser, (void* self, const void* identity))               \
    X(bool, SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser, (void* self, const void* identity))                \
    X(void, SteamAPI_SteamNetworkingMessage_t_Release, (void* message))                                                \
    X(void, SteamAPI_SteamNetworkingIdentity_SetSteamID64, (void* self, uint64_t id))                                  \
    X(uint64_t, SteamAPI_SteamNetworkingIdentity_GetSteamID64, (void* self))

typedef struct {
    CoopSteamBackend base;
    void* library;
    bool started;
#define X(ret, name, args) ret(*name) args;
    STEAM_EXPORTS(X)
#undef X
    int32_t pipe;
    void* messages;
    CoopSteamId self;
    bool dispatching; // callbacks of this frame are being read
} Flat;

typedef struct {
    uint64_t words[IDENTITY_WORDS];
} Identity;

static void Flat_Identity(Flat* f, Identity* out, CoopSteamId id) {
    memset(out, 0, sizeof(*out));
    f->SteamAPI_SteamNetworkingIdentity_SetSteamID64(out, id);
}

static CoopSteamId Flat_Self(CoopSteamBackend* b) {
    return ((Flat*) b)->self;
}

static uint32_t Flat_NowMs(CoopSteamBackend* b) {
    (void) b;
    return SDL_GetTicks();
}

static uint32_t Flat_Nonce(CoopSteamBackend* b) {
    (void) b;
    // Only has to differ from the last connection of this user, so a clock is enough.
    return (uint32_t) SDL_GetPerformanceCounter() * 2654435761u + SDL_GetTicks();
}

static CoopSteamSendResult Flat_Send(CoopSteamBackend* b, CoopSteamId to, const void* data, uint32_t size,
                                     CoopNetLane lane) {
    Flat* f = (Flat*) b;
    Identity identity;
    int flags = SEND_NO_NAGLE | (lane == COOP_NET_RELIABLE ? SEND_RELIABLE : 0);
    int result;

    Flat_Identity(f, &identity, to);
    result = f->SteamAPI_ISteamNetworkingMessages_SendMessageToUser(f->messages, &identity, data, size, flags, CHANNEL);
    return result == RESULT_OK ? COOP_STEAM_SENT : result == RESULT_NO_CONNECTION ? COOP_STEAM_BROKEN : COOP_STEAM_BUSY;
}

static bool Flat_Receive(CoopSteamBackend* b, CoopSteamMessage* out) {
    Flat* f = (Flat*) b;
    void* message = NULL;
    const uint8_t* raw;
    int32_t size;

    if (f->SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel(f->messages, CHANNEL, &message, 1) < 1) {
        return false;
    }
    raw = message;
    memcpy(&size, raw + MESSAGE_SIZE_OFFSET, sizeof(size));
    memcpy(&out->data, raw, sizeof(out->data));
    out->size = size < 0 ? 0 : (uint32_t) size;
    out->from = f->SteamAPI_SteamNetworkingIdentity_GetSteamID64((void*) (raw + MESSAGE_SENDER_OFFSET));
    out->handle = message;
    return true;
}

static void Flat_Release(CoopSteamBackend* b, CoopSteamMessage* message) {
    ((Flat*) b)->SteamAPI_SteamNetworkingMessage_t_Release(message->handle);
    message->handle = NULL;
    message->data = NULL;
}

// Both callbacks start with the identity of the other side.
static bool Flat_ReadCallback(Flat* f, const SteamCallback* msg, CoopSteamEvent* out) {
    if (msg->id != CALLBACK_SESSION_REQUEST && msg->id != CALLBACK_SESSION_FAILED) {
        return false;
    }
    out->type = msg->id == CALLBACK_SESSION_REQUEST ? COOP_STEAM_SESSION_REQUEST : COOP_STEAM_SESSION_FAILED;
    out->peer = f->SteamAPI_SteamNetworkingIdentity_GetSteamID64(msg->param);
    return out->peer != 0;
}

static bool Flat_Event(CoopSteamBackend* b, CoopSteamEvent* out) {
    Flat* f = (Flat*) b;
    SteamCallback msg;

    if (!f->dispatching) {
        f->SteamAPI_ManualDispatch_RunFrame(f->pipe);
        f->dispatching = true;
    }
    while (f->SteamAPI_ManualDispatch_GetNextCallback(f->pipe, &msg)) {
        bool wanted = Flat_ReadCallback(f, &msg, out);

        f->SteamAPI_ManualDispatch_FreeLastCallback(f->pipe);
        if (wanted) {
            return true;
        }
    }
    f->dispatching = false;
    return false;
}

static void Flat_Accept(CoopSteamBackend* b, CoopSteamId peer) {
    Flat* f = (Flat*) b;
    Identity identity;

    Flat_Identity(f, &identity, peer);
    f->SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser(f->messages, &identity);
}

static void Flat_CloseSession(CoopSteamBackend* b, CoopSteamId peer) {
    Flat* f = (Flat*) b;
    Identity identity;

    Flat_Identity(f, &identity, peer);
    f->SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser(f->messages, &identity);
}

static void Flat_Destroy(CoopSteamBackend* b) {
    Flat* f = (Flat*) b;

    if (f->started) {
        f->SteamAPI_Shutdown();
    }
    if (f->library != NULL) {
        SDL_UnloadObject(f->library);
    }
    free(f);
}

static const CoopSteamBackendOps sFlatOps = {
    Flat_Self,    Flat_NowMs, Flat_Nonce,  Flat_Send,         Flat_Receive,
    Flat_Release, Flat_Event, Flat_Accept, Flat_CloseSession, Flat_Destroy,
};

// Steam takes the app id from this file in the working directory.
static void Flat_WriteAppId(void) {
    FILE* file = fopen(APP_ID_FILE, "r");

    if (file == NULL) {
        file = fopen(APP_ID_FILE, "w");
        if (file != NULL) {
            fputs(APP_ID, file);
        }
    }
    if (file != NULL) {
        fclose(file);
    }
}

static void* Flat_LoadLibrary(const char* library) {
    char* base;
    void* loaded = NULL;

    if (library != NULL) {
        return SDL_LoadObject(library);
    }
    base = SDL_GetBasePath();
    if (base != NULL) {
        char path[1024];

        snprintf(path, sizeof(path), "%s%s", base, LIBRARY_NAME);
        SDL_free(base);
        loaded = SDL_LoadObject(path);
    }
    return loaded != NULL ? loaded : SDL_LoadObject(LIBRARY_NAME);
}

// The name of the first export the library does not have, or NULL.
static const char* Flat_Bind(Flat* f) {
#define X(ret, name, args)                                    \
    *(void**) &f->name = SDL_LoadFunction(f->library, #name); \
    if (f->name == NULL) {                                    \
        return #name;                                         \
    }
    STEAM_EXPORTS(X)
#undef X
    return NULL;
}

static const char* Flat_InitText(int code) {
    switch (code) {
        case INIT_NO_STEAM_CLIENT:
            return "Steam is not running";
        case INIT_VERSION_MISMATCH:
            return "the Steam client and this library do not fit";
        default:
            return "Steam did not start";
    }
}

CoopSteamBackend* CoopSteamFlat_Open(const char* library, char* error, size_t errorSize) {
    char steamError[1024] = { 0 };
    const char* missing;
    Flat* f;
    void* user;
    void* utils;
    int code;

    if (sizeof(void*) != 8) {
        snprintf(error, errorSize, "the Steam transport needs a 64-bit build");
        return NULL;
    }
    f = calloc(1, sizeof(Flat));
    if (f == NULL) {
        snprintf(error, errorSize, "out of memory");
        return NULL;
    }
    f->base.ops = &sFlatOps;
    f->library = Flat_LoadLibrary(library);
    if (f->library == NULL) {
        snprintf(error, errorSize, "cannot load the Steam library %s: %s", library ? library : LIBRARY_NAME,
                 SDL_GetError());
        Flat_Destroy(&f->base);
        return NULL;
    }
    missing = Flat_Bind(f);
    if (missing != NULL) {
        snprintf(error, errorSize, "the Steam library is too old: it has no %s", missing);
        Flat_Destroy(&f->base);
        return NULL;
    }

    Flat_WriteAppId();
    code = f->SteamAPI_InitFlat(steamError);
    if (code != INIT_OK) {
        snprintf(error, errorSize, "%s (code %d): %s", Flat_InitText(code), code, steamError);
        Flat_Destroy(&f->base);
        return NULL;
    }
    f->started = true;
    f->SteamAPI_ManualDispatch_Init();
    f->pipe = f->SteamAPI_GetHSteamPipe();
    user = f->SteamAPI_SteamUser_v023();
    f->messages = f->SteamAPI_SteamNetworkingMessages_SteamAPI_v002();
    utils = f->SteamAPI_SteamNetworkingUtils_SteamAPI_v004();
    if (user == NULL || f->messages == NULL || utils == NULL) {
        snprintf(error, errorSize, "the Steam library gave no interface");
        Flat_Destroy(&f->base);
        return NULL;
    }
    if (!f->SteamAPI_ISteamUser_BLoggedOn(user)) {
        snprintf(error, errorSize, "Steam is not logged in");
        Flat_Destroy(&f->base);
        return NULL;
    }
    f->self = f->SteamAPI_ISteamUser_GetSteamID(user);
    // Finding a route takes seconds. Start now, not at the first message.
    f->SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess(utils);
    return &f->base;
}

#endif
